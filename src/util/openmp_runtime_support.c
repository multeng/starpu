/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2014  Inria
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <starpu.h>
#ifdef STARPU_OPENMP
/*
 * locally disable -Wdeprecated-declarations to avoid
 * lots of deprecated warnings for ucontext related functions
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <util/openmp_runtime_support.h>
#include <core/task.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

#define _STARPU_STACKSIZE 2097152

static struct starpu_omp_global _global_state;
static starpu_pthread_key_t omp_thread_key;
static starpu_pthread_key_t omp_task_key;

struct starpu_omp_global *_starpu_omp_global_state = NULL;
double _starpu_omp_clock_ref = 0.0; /* clock reference for starpu_omp_get_wtick */


static struct starpu_omp_device *create_omp_device_struct(void)
{
	struct starpu_omp_device *dev = malloc(sizeof(*dev));
	if (dev == NULL)
		_STARPU_ERROR("memory allocation failed");

	/* TODO: initialize dev->icvs with proper values */ 
	memset(&dev->icvs, 0, sizeof(dev->icvs));

	return dev;
}

static struct starpu_omp_region *create_omp_region_struct(struct starpu_omp_region *parent_region, struct starpu_omp_device *owner_device, int nb_threads)
{
	struct starpu_omp_region *region = malloc(sizeof(*region));
	if (region == NULL)
		_STARPU_ERROR("memory allocation failed");

	region->parent_region = parent_region;
	region->initial_nested_region = NULL;
	region->owner_device = owner_device;
	region->thread_list = starpu_omp_thread_list_new();
	region->implicit_task_list = starpu_omp_task_list_new();

	region->nb_threads = nb_threads;
	region->level = (parent_region != NULL)?parent_region->level+1:0;
	return region;
}

static void omp_initial_thread_func(void)
{
	struct starpu_omp_region *init_region = _global_state.initial_region;
	struct starpu_omp_thread *init_thread = _global_state.initial_thread;
	struct starpu_omp_task *init_task = _global_state.initial_task;
	struct starpu_task *continuation_task = init_region->initial_nested_region->continuation_starpu_task;
	while (1)
	{
		starpu_driver_run_once(&init_thread->starpu_driver);

		/*
		 * if we are leaving the first nested region we give control back to initial task
		 * otherwise, we should continue to execute work
		 */
		if (_starpu_task_test_termination(continuation_task))
		{
			swapcontext(&init_thread->ctx, &init_task->ctx);
		}
	}
}

/*
 * setup the main application thread to handle the possible preemption of the initial task
 */
static void omp_initial_thread_setup(void)
{
	struct starpu_omp_thread *initial_thread = _global_state.initial_thread;
	struct starpu_omp_task *initial_task = _global_state.initial_task;
	/* .current_task */
	initial_thread->current_task = initial_task;
	/* .owner_region already set in create_omp_thread_struct */
	/* .initial_thread_stack */
	initial_thread->initial_thread_stack = malloc(_STARPU_STACKSIZE);
	if (initial_thread->initial_thread_stack == NULL)
		_STARPU_ERROR("memory allocation failed");
	/* .ctx */
	getcontext(&initial_thread->ctx);
	/*
	 * we do not use uc_link, the initial thread always should give hand back to the initial task
	 */
	initial_thread->ctx.uc_link          = NULL;
	initial_thread->ctx.uc_stack.ss_sp   = initial_thread->initial_thread_stack;
	initial_thread->ctx.uc_stack.ss_size = _STARPU_STACKSIZE;
	makecontext(&initial_thread->ctx, omp_initial_thread_func, 0);
	/* .starpu_driver */
	/*
	 * we configure starpu to not launch CPU worker 0
	 * because we will use the main thread to play the role of worker 0
	 */
	struct starpu_conf conf;
	int ret = starpu_conf_init(&conf);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_conf_init");
	initial_thread->starpu_driver.type = STARPU_CPU_WORKER;
	initial_thread->starpu_driver.id.cpu_id = 0;
	conf.not_launched_drivers = &initial_thread->starpu_driver;
	conf.n_not_launched_drivers = 1;
	ret = starpu_init(&conf);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");
	ret = starpu_driver_init(&initial_thread->starpu_driver);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_driver_init");
}

static void omp_initial_thread_exit()
{
	struct starpu_omp_thread *initial_thread = _global_state.initial_thread;
	int ret = starpu_driver_deinit(&initial_thread->starpu_driver);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_driver_deinit");
	starpu_shutdown();

	/* TODO: free initial_thread data structures */
}

static struct starpu_omp_thread *create_omp_thread_struct(struct starpu_omp_region *owner_region)
{
	struct starpu_omp_thread *thread = malloc(sizeof(*thread));
	if (thread == NULL)
		_STARPU_ERROR("memory allocation failed");
	/* .current_task */
	thread->current_task = NULL;
	/* .owner_region */
	thread->owner_region = owner_region;
	/* .primary_task */
	thread->primary_task = NULL;
	/* .init_thread_stack */
	thread->initial_thread_stack = NULL;
	/* .ctx */
	memset(&thread->ctx, 0, sizeof(thread->ctx));
	/* .starpu_driver will be initialized later on */
	return thread;
}

static void starpu_omp_task_entry(struct starpu_omp_task *task)
{
	task->f(task->starpu_buffers, task->starpu_cl_arg);
	task->state = starpu_omp_task_state_terminated;
	struct starpu_omp_thread *thread = STARPU_PTHREAD_GETSPECIFIC(omp_thread_key);
	/* 
	 * the task reached the terminated state, definitively give hand back to the worker code.
	 *
	 * about to run on the worker stack...
	 */
	setcontext(&thread->ctx);
	STARPU_ASSERT(0); /* unreachable code */
}

/*
 * stop executing a task that is about to block
 * and give hand back to the thread
 */
static void starpu_omp_task_preempt(void)
{
	struct starpu_omp_task *task = STARPU_PTHREAD_GETSPECIFIC(omp_task_key);
	struct starpu_omp_thread *thread = STARPU_PTHREAD_GETSPECIFIC(omp_thread_key);
	task->state = starpu_omp_task_state_preempted;

	/* 
	 * the task reached a blocked state, give hand back to the worker code.
	 *
	 * about to run on the worker stack...
	 */
	swapcontext(&task->ctx, &thread->ctx);
	/* now running on the task stack again */
}

/*
 * wrap a task function to allow the task to be preempted
 */
static void starpu_omp_task_exec(void *buffers[], void *cl_arg)
{
	struct starpu_omp_task *task = starpu_task_get_current()->omp_task;
	STARPU_PTHREAD_SETSPECIFIC(omp_task_key, task);
	struct starpu_omp_thread *thread = STARPU_PTHREAD_GETSPECIFIC(omp_thread_key);
	if (thread == NULL)
	{
		/*
		 * this is the first time an omp task is launched on the current worker.
		 * this first task should be an implicit parallel region task.
		 */
		if (!task->is_implicit)
			_STARPU_ERROR("unexpected omp task\n");

		thread = task->owner_thread;
		STARPU_ASSERT(thread->owner_region != NULL);
		STARPU_ASSERT(thread->owner_region == task->owner_region);
		thread->primary_task = task;

		/*
		 * make this worker an omp-enabled worker
		 */
		STARPU_PTHREAD_SETSPECIFIC(omp_thread_key, thread);
	}
	if (task->state != starpu_omp_task_state_preempted)
	{
		task->starpu_buffers = buffers;
		task->starpu_cl_arg = cl_arg;
	}
	task->state = starpu_omp_task_state_clear;

	/* 
	 * start the task execution, or restore a previously preempted task.
	 * about to run on the task stack...
	 * */
	swapcontext(&thread->ctx, &task->ctx);
	/* now running on the worker stack again */

	STARPU_ASSERT(task->state == starpu_omp_task_state_preempted
			|| task->state == starpu_omp_task_state_terminated);
	STARPU_PTHREAD_SETSPECIFIC(omp_task_key, NULL);
	if (task->state == starpu_omp_task_state_terminated && task == thread->primary_task)
	{
		/*
		 * make this worker an omp-disabled worker
		 */
		STARPU_PTHREAD_SETSPECIFIC(omp_thread_key, NULL);
		thread->primary_task = NULL;

		/*
		 * make sure this worker wont be used for running omp tasks
		 * until a new region is created
		 */
		thread->owner_region = NULL;
	}

	/* TODO: analyse the cause of the return and take appropriate steps */
}

/*
 * prepare the starpu_task fields of a currently running task
 * for accepting a new set of dependencies in anticipation of a preemption
 *
 * when the task becomes preempted, it will only be queued again when the new
 * set of dependencies is fulfilled
 */
static void _starpu_task_prepare_for_preemption(struct starpu_task *starpu_task)
{
	/* TODO: implement funciton */
	(void)starpu_task;
}

static struct starpu_omp_task *create_omp_task_struct(struct starpu_omp_task *parent_task,
		struct starpu_omp_thread *owner_thread, struct starpu_omp_region *owner_region, int is_implicit)
{
	struct starpu_omp_task *task = malloc(sizeof(*task));
	if (task == NULL)
		_STARPU_ERROR("memory allocation failed");
	task->parent_task = parent_task;
	task->owner_thread = owner_thread;
	task->owner_region = owner_region;
	task->is_implicit = is_implicit;
	/* TODO: initialize task->data_env_icvs with proper values */ 
	memset(&task->data_env_icvs, 0, sizeof(task->data_env_icvs));
	if (is_implicit)
	{
	  /* TODO: initialize task->implicit_task_icvs with proper values */ 
		memset(&task->implicit_task_icvs, 0, sizeof(task->implicit_task_icvs));
	}
	task->starpu_task = NULL;
	task->starpu_buffers = NULL;
	task->starpu_cl_arg = NULL;
	task->f = NULL;
	task->state = starpu_omp_task_state_clear;

	if (parent_task == NULL)
	{
		/* do not allocate a stack for the initial task */
		task->stack = NULL;
		memset(&task->ctx, 0, sizeof(task->ctx));
	}
	else
	{
		/* TODO: use ICV stack size info instead */
		task->stack = malloc(_STARPU_STACKSIZE);
		if (task->stack == NULL)
			_STARPU_ERROR("memory allocation failed");
		getcontext(&task->ctx);
		/*
		 * we do not use uc_link, starpu_omp_task_entry will handle
		 * the end of the task
		 */
		task->ctx.uc_link                 = NULL;
		task->ctx.uc_stack.ss_sp          = task->stack;
		task->ctx.uc_stack.ss_size        = _STARPU_STACKSIZE;
		makecontext(&task->ctx, (void (*) ()) starpu_omp_task_entry, 1, task);
	}

	return task;
}

/*
 * Entry point to be called by the OpenMP runtime constructor
 */
int starpu_omp_init(void)
{
	_starpu_omp_environment_init();
	_global_state.icvs.cancel_var = _starpu_omp_initial_icv_values->cancel_var;
	_global_state.initial_device = create_omp_device_struct();
	_global_state.initial_region = create_omp_region_struct(NULL, _global_state.initial_device, 1);
	_global_state.initial_thread = create_omp_thread_struct(_global_state.initial_region);
	starpu_omp_thread_list_push_back(_global_state.initial_region->thread_list,
			_global_state.initial_thread);
	_global_state.initial_task = create_omp_task_struct(NULL,
			_global_state.initial_thread, _global_state.initial_region, 1);
	_starpu_omp_global_state = &_global_state;

	STARPU_PTHREAD_KEY_CREATE(&omp_thread_key, NULL);
	STARPU_PTHREAD_KEY_CREATE(&omp_task_key, NULL);

	omp_initial_thread_setup();

	/* init clock reference for starpu_omp_get_wtick */
	_starpu_omp_clock_ref = starpu_timing_now();

	return 0;
}

void starpu_omp_shutdown(void)
{
	omp_initial_thread_exit();
	STARPU_PTHREAD_KEY_DELETE(omp_task_key);
	STARPU_PTHREAD_KEY_DELETE(omp_thread_key);
	/* TODO: free ICV variables */
	/* TODO: free task/thread/region/device structures */
}

void starpu_parallel_region(struct starpu_codelet *parallel_region_cl, void *parallel_region_cl_arg)
{
	struct starpu_omp_thread *master_thread = STARPU_PTHREAD_GETSPECIFIC(omp_thread_key);
	struct starpu_omp_task *parent_task = STARPU_PTHREAD_GETSPECIFIC(omp_task_key);
	struct starpu_omp_region *parent_region = parent_task->owner_region;
	int ret;

	/* TODO: compute the proper nb_threads and launch additional workers as needed.
	 * for now, the level 1 parallel region spans all the threads
	 * and level >= 2 parallel regions have only one thread */
	int nb_threads = (parent_region->level == 0)?starpu_cpu_worker_get_count():1;

	struct starpu_omp_region *new_region = 
		create_omp_region_struct(parent_region, _global_state.initial_device, 1);

	int i;
	for (i = 0; i < nb_threads; i++)
	{
		struct starpu_omp_thread *new_thread =
			(i == 0) ? master_thread : create_omp_thread_struct(new_region);
		/* TODO: specify actual starpu worker */

		starpu_omp_thread_list_push_back(new_region->thread_list, new_thread);
		struct starpu_omp_task *new_task = create_omp_task_struct(parent_task, new_thread, new_region, 1);
		starpu_omp_task_list_push_back(new_region->implicit_task_list, new_task);
	}

	/* 
	 * if parent_task == initial_task, create a starpu task as a continuation to all the implicit
	 * tasks of the new region, else prepare the parent_task for preemption,
	 * to become itself a continuation to the implicit tasks of the new region
	 */
	if (parent_task == _global_state.initial_task)
	{
		new_region->continuation_starpu_task = starpu_task_create();

		/* in that case, the continuation starpu task is only used for synchronisation */
		new_region->continuation_starpu_task->cl = NULL;
		parent_region->initial_nested_region = new_region;

	}
	else
	{
		/* through the preemption, the parent starpu task becomes the continuation task */
		_starpu_task_prepare_for_preemption(parent_task->starpu_task);
		new_region->continuation_starpu_task = parent_task->starpu_task;
	}

	/*
	 * save pointer to the regions user function from the parallel region codelet
	 *
	 * TODO: add support for multiple/heterogeneous implementations
	 */
	void (*parallel_region_f)(void **starpu_buffers, void *starpu_cl_arg) = parallel_region_cl->cpu_funcs[0];

	/*
	 * plug the task wrapper into the parallel region codelet instead, to support task preemption
	 */
	parallel_region_cl->cpu_funcs[0] = starpu_omp_task_exec;

	/*
	 * create the starpu tasks for the implicit omp tasks,
	 * create explicit dependencies between these starpu tasks and the continuation starpu task
	 */
	struct starpu_omp_task * implicit_task;
	for (implicit_task  = starpu_omp_task_list_begin(new_region->implicit_task_list);
			implicit_task != starpu_omp_task_list_end(new_region->implicit_task_list);
			implicit_task  = starpu_omp_task_list_next(implicit_task))
	{
		implicit_task->f = parallel_region_f;

		implicit_task->starpu_task = starpu_task_create();
		implicit_task->starpu_task->cl = parallel_region_cl;
		implicit_task->starpu_task->cl_arg = parallel_region_cl_arg;
		starpu_task_declare_deps_array(new_region->continuation_starpu_task, 1, &implicit_task->starpu_task);
	}

	/*
	 * submit all the region implicit starpu tasks
	 */
	for (implicit_task  = starpu_omp_task_list_begin(new_region->implicit_task_list);
			implicit_task != starpu_omp_task_list_end(new_region->implicit_task_list);
			implicit_task  = starpu_omp_task_list_next(implicit_task))
	{
		ret = starpu_task_submit(implicit_task->starpu_task);
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
	}

	/*
	 * submit the region continuation starpu task if parent_task == initial_task
	 */
	if (parent_task == _global_state.initial_task)
	{
		ret = _starpu_task_submit_internally(new_region->continuation_starpu_task);
		STARPU_CHECK_RETURN_VALUE(ret, "_starpu_task_submit_internally");
	}

	/*
	 * preempt for completion of the region
	 */
	starpu_omp_task_preempt();

	/*
	 * TODO: free region resources
	 */
}

/*
 * restore deprecated diagnostics (-Wdeprecated-declarations)
 */
#pragma GCC diagnostic pop
#endif /* STARPU_OPENMP */
