/*
 * StarPU
 * Copyright (C) INRIA 2008-2010 (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#ifndef __PI_H__
#define __PI_H__

#include <starpu.h>
#include <stdio.h>
#include <sys/time.h>

#define NSHOT_PER_TASK	(16*1024*1024ULL)

#define TYPE	float

//extern "C" void cuda_kernel(void *descr[], void *cl_arg);

static int n_dimensions = 100;

#endif // __PI_H__
