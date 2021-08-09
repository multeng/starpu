/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2021  Université de Bordeaux, CNRS (LaBRI UMR 5800), Inria
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

/*
 * This examplifies how to use partitioning filters.  We here just split a 4D
 * matrix into 4D slices (along the X axis), and run a dumb kernel on them.
 */

#include <starpu.h>

#define NX    6
#define NY    5
#define NZ    4
#define NT    3
#define PARTS 2

#define FPRINTF(ofile, fmt, ...) do { if (!getenv("STARPU_SSILENT")) {fprintf(ofile, fmt, ## __VA_ARGS__); }} while(0)

extern void f4d_cpu_func(void *buffers[], void *cl_arg);

extern void print_tensor(int *tensor, int nx, int ny, int nz, int nt, unsigned ldy, unsigned ldz, unsigned ldt);
extern void print_4dim_data(starpu_data_handle_t ndim_handle);

int main(void)
{
    int *ndim_arr,n=0;
    int i, j, k, l;
    int ret;

    ndim_arr = (int*)malloc(NX*NY*NZ*NT*sizeof(ndim_arr[0]));
    assert(ndim_arr);
    for(l=0 ; l<NT ; l++)
    {
        for(k=0 ; k<NZ ; k++)
        {
            for(j=0 ; j<NY ; j++)
            {
                for(i=0 ; i<NX ; i++)
                {
                    ndim_arr[(l*NX*NY*NZ)+(k*NX*NY)+(j*NX)+i] = n++;
                }
            }
        }
    }

    starpu_data_handle_t handle;
    struct starpu_codelet cl =
    {
        .cpu_funcs = {f4d_cpu_func},
        .cpu_funcs_name = {"f4d_cpu_func"},
        .nbuffers = 1,
        .modes = {STARPU_RW},
        .name = "ndim_scal"
    };

    ret = starpu_init(NULL);
    if (ret == -ENODEV)
        exit(77);
    STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");
        
    unsigned nn[4] = {NX, NY, NZ, NT};
    unsigned ldn[4] = {1, NX, NX*NY, NX*NY*NZ};

    /* Declare data to StarPU */
    starpu_ndim_data_register(&handle, STARPU_MAIN_RAM, (uintptr_t)ndim_arr, ldn, nn, 4, sizeof(int));
    FPRINTF(stderr, "IN  Ndim Array\n");
    print_4dim_data(handle);

    /* Partition the ndim array in PARTS sub-ndimarrays */
    struct starpu_data_filter f =
    {
        .filter_func = starpu_ndim_filter_block,
        .filter_arg = 0, //Partition the array along X dimension
        .nchildren = PARTS
    };
    starpu_data_partition(handle, &f);

    FPRINTF(stderr,"Nb of partitions : %d\n",starpu_data_get_nb_children(handle));

    for(i=0 ; i<starpu_data_get_nb_children(handle) ; i++)
    {
        starpu_data_handle_t sndim = starpu_data_get_sub_data(handle, 1, i);
        FPRINTF(stderr, "Sub Ndim Array %d\n", i);
        print_4dim_data(sndim);
    }

    /* Submit a task on each sub-ndimarray */
    for(i=0 ; i<starpu_data_get_nb_children(handle) ; i++)
    {
        int multiplier=i;
        struct starpu_task *task = starpu_task_create();

        FPRINTF(stderr,"Dealing with sub-ndimarray %d\n", i);
        task->cl = &cl;
        task->synchronous = 1;
        task->callback_func = NULL;
        task->handles[0] = starpu_data_get_sub_data(handle, 1, i);
        task->cl_arg = &multiplier;
        task->cl_arg_size = sizeof(multiplier);

        ret = starpu_task_submit(task);
        if (ret == -ENODEV) goto enodev;
        STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
    }

    /* Unpartition the data, unregister it from StarPU and shutdown */
    starpu_data_unpartition(handle, STARPU_MAIN_RAM);
    print_4dim_data(handle);
    starpu_data_unregister(handle);

    /* Print result ndim array*/
    FPRINTF(stderr, "OUT Ndim Array\n");
    print_tensor(ndim_arr, NX, NY, NZ, NT, NX, NX*NY, NX*NY*NZ);

    free(ndim_arr);

    starpu_shutdown();
    return 0;

enodev:
    FPRINTF(stderr, "WARNING: No one can execute this task\n");
    starpu_shutdown();
    return 77;
}
