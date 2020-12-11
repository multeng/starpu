# StarPU --- Runtime system for heterogeneous multicore architectures.
#
# Copyright (C) 2020       Université de Bordeaux, CNRS (LaBRI UMR 5800), Inria
#
# StarPU is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or (at
# your option) any later version.
#
# StarPU is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See the GNU Lesser General Public License in COPYING.LGPL for more details.
#

import sys
import types
import joblib as jl
from joblib import logger
from starpu import starpupy
import starpu
import asyncio
import math
import functools
import numpy as np
import inspect
import threading

BACKENDS={}
_backend = threading.local()

# get the number of CPUs controlled by StarPU
def cpu_count():
	n_cpus=starpupy.cpu_worker_get_count()
	return n_cpus

# split a list ls into n_block numbers of sub-lists 
def partition(ls, n_block):
	if len(ls)>=n_block:
		# there are n1 sub-lists which contain q1 elements, and (n_block-n1) sublists which contain q2 elements (n1 can be 0)
		q1=math.ceil(len(ls)/n_block)
		q2=math.floor(len(ls)/n_block)
		n1=len(ls)%n_block
		#n2=n_block-n1
		# generate n1 sub-lists in L1, and (n_block-n1) sub-lists in L2
		L1=[ls[i:i+q1] for i in range(0, n1*q1, q1)]
		L2=[ls[i:i+q2] for i in range(n1*q1, len(ls), q2)]

		L=L1+L2
	else:
		# if the block number is larger than the length of list, each element in the list is a sub-list
		L=[ls[i:i+1] for i in range (len(ls))]
	return L

def future_generator(iterable, n_jobs, dict_task):
	# iterable is generated by delayed function, after converting to a list, the format is [function, (arg1, arg2, ... ,)]
	#print("iterable type is ", type(iterable))
	#print("iterable is", iterable)
	# get the number of block
	if n_jobs<-cpu_count()-1 or n_jobs>cpu_count():
		raise SystemExit('Error: n_jobs is out of range')
		#print("Error: n_jobs is out of range, number of CPUs is", cpu_count())
	elif n_jobs<0:
		n_block=cpu_count()+1+n_jobs
	else:
		n_block=n_jobs

	# if arguments is tuple format
	if type(iterable) is tuple:
		# the function is always the first element
		f=iterable[0]
		# get the name of formal arguments of f
		formal_args=inspect.getargspec(f).args
		# get the arguments list
		args=[]
		# argument is arbitrary in iterable[1]
		args=list(iterable[1])
		# argument is keyword argument in iterable[2]
		for i in range(len(formal_args)):
			for j in iterable[2].keys():
				if j==formal_args[i]:
					args.append(iterable[2][j])
		# check whether all arrays have the same size
		l_arr=[]
		# list of Future result
		L_fut=[]
		# split the vector
		args_split=[]
		for i in range(len(args)):
			args_split.append([])
			# if the array is an numpy array
			if type(args[i]) is np.ndarray:
				# split numpy array
				args_split[i]=np.array_split(args[i],n_block)
				# get the length of numpy array
				l_arr.append(args[i].size)
			# if the array is a generator
			elif isinstance(args[i],types.GeneratorType):
				# split generator
				args_split[i]=partition(list(args[i]),n_block)
				# get the length of generator
				l_arr.append(sum(len(args_split[i][j]) for j in range(len(args_split[i]))))
		if len(set(l_arr))>1:
			raise SystemExit('Error: all arrays should have the same size')
		#print("args list is", args_split)
		for i in range(n_block):
			# generate the argument list
			L_args=[]
			for j in range(len(args)):
				if type(args[j]) is np.ndarray or isinstance(args[j],types.GeneratorType):
					L_args.append(args_split[j][i])
				else:
					L_args.append(args[j])
			#print("L_args is", L_args)
			fut=starpu.task_submit(name=dict_task['name'], synchronous=dict_task['synchronous'], priority=dict_task['priority'],\
								   color=dict_task['color'], flops=dict_task['flops'], perfmodel=dict_task['perfmodel'])\
				                  (f, *L_args)
			L_fut.append(fut)
		return L_fut

	# if iterable is a generator or a list of function
	else:
		L=list(iterable)
		#print(L)
		# generate a list of function according to iterable
		def lf(ls):
			L_func=[]
			for i in range(len(ls)):
				# the first element is the function
				f=ls[i][0]
				# the second element is the args list of a type tuple
				L_args=list(ls[i][1])
				# generate a list of function
				L_func.append(f(*L_args))
			return L_func

		# generate the split function list
		L_split=partition(L,n_block)
		# operation in each split list
		L_fut=[]
		for i in range(len(L_split)):
			fut=starpu.task_submit(name=dict_task['name'], synchronous=dict_task['synchronous'], priority=dict_task['priority'],\
								   color=dict_task['color'], flops=dict_task['flops'], perfmodel=dict_task['perfmodel'])\
				                  (lf, L_split[i])
			L_fut.append(fut)
		return L_fut

class Parallel(object):
	def __init__(self, mode="normal", perfmodel=None, end_msg=None,\
			 name=None, synchronous=0, priority=0, color=None, flops=None,\
	         n_jobs=None, backend=None, verbose=0, timeout=None, pre_dispatch='2 * n_jobs',\
	         batch_size='auto', temp_folder=None, max_nbytes='1M',\
	         mmap_mode='r', prefer=None, require=None):
		active_backend, context_n_jobs = get_active_backend(prefer=prefer, require=require, verbose=verbose)
		nesting_level = active_backend.nesting_level

		if backend is None:
			backend = active_backend

		else:
			try:
				backend_factory = BACKENDS[backend]
			except KeyError as e:
				raise ValueError("Invalid backend: %s, expected one of %r"
                                 % (backend, sorted(BACKENDS.keys()))) from e
			backend = backend_factory(nesting_level=nesting_level)

		if n_jobs is None:
			n_jobs = 1

		self.mode=mode
		self.perfmodel=perfmodel
		self.end_msg=end_msg
		self.name=name
		self.synchronous=synchronous
		self.priority=priority
		self.color=color
		self.flops=flops
		self.n_jobs=n_jobs
		self._backend=backend

	def print_progress(self):
		#pass
		print("", starpupy.task_nsubmitted())

	def __call__(self,iterable):
		#generate the dictionary of task_submit
		dict_task={'name': self.name, 'synchronous': self.synchronous, 'priority': self.priority, 'color': self.color, 'flops': self.flops, 'perfmodel': self.perfmodel}
		if hasattr(self._backend, 'start_call'):
			self._backend.start_call()
		# the mode normal, user can call the function directly without using async
		if self.mode=="normal":
			async def asy_main():
				L_fut=future_generator(iterable, self.n_jobs, dict_task)
				res=[]
				for i in range(len(L_fut)):
					L_res=await L_fut[i]
					res.extend(L_res)
				#print(res)
				#print("type of result is", type(res))
				return res
			#asyncio.run(asy_main())
			#retVal=asy_main
			loop = asyncio.get_event_loop()
			results = loop.run_until_complete(asy_main())
			retVal = results
		# the mode future, user needs to use asyncio module and await the Future result in main function
		elif self.mode=="future":
			L_fut=future_generator(iterable, self.n_jobs, dict_task)
			fut=asyncio.gather(*L_fut)
			if self.end_msg!=None:
				fut.add_done_callback(functools.partial(print, self.end_msg))
			retVal=fut
		if hasattr(self._backend, 'stop_call'):
			self._backend.stop_call()
		return retVal

def delayed(function):
	def delayed_function(*args, **kwargs):
		return function, args, kwargs
	return delayed_function


######################################################################
__version__ = jl.__version__

class Memory(jl.Memory):
	def __init__(self,location=None, backend='local', cachedir=None,
                 mmap_mode=None, compress=False, verbose=1, bytes_limit=None,
                 backend_options=None):
		super(Memory, self).__init__(location=None, backend='local', cachedir=None,
                 mmap_mode=None, compress=False, verbose=1, bytes_limit=None,
                 backend_options=None)


def dump(value, filename, compress=0, protocol=None, cache_size=None):
	return jl.dump(value, filename, compress, protocol, cache_size)

def load(filename, mmap_mode=None):
	return jl.load(filename, mmap_mode)

def hash(obj, hash_name='md5', coerce_mmap=False):
	return jl.hash(obj, hash_name, coerce_mmap)

def register_compressor(compressor_name, compressor, force=False):
	return jl.register_compressor(compressor_name, compressor, force)

def effective_n_jobs(n_jobs=-1):
	return cpu_count()

def get_active_backend(prefer=None, require=None, verbose=0):
	return jl.parallel.get_active_backend(prefer, require, verbose)

class parallel_backend(object):
	def __init__(self, backend, n_jobs=-1, inner_max_num_threads=None,
                 **backend_params):
		if isinstance(backend, str):
			backend = BACKENDS[backend](**backend_params)

		current_backend_and_jobs = getattr(_backend, 'backend_and_jobs', None)
		if backend.nesting_level is None:
			if current_backend_and_jobs is None:
				nesting_level = 0
			else:
				nesting_level = current_backend_and_jobs[0].nesting_level

			backend.nesting_level = nesting_level

		# Save the backends info and set the active backend
		self.old_backend_and_jobs = current_backend_and_jobs
		self.new_backend_and_jobs = (backend, n_jobs)

		_backend.backend_and_jobs = (backend, n_jobs)

	def __enter__(self):
		return self.new_backend_and_jobs

	def __exit__(self, type, value, traceback):
		self.unregister()

	def unregister(self):
		if self.old_backend_and_jobs is None:
			if getattr(_backend, 'backend_and_jobs', None) is not None:
				del _backend.backend_and_jobs
		else:
			_backend.backend_and_jobs = self.old_backend_and_jobs

def register_parallel_backend(name, factory):
	BACKENDS[name] = factory
