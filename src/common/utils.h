/*
 * StarPU
 * Copyright (C) Université Bordeaux 1, CNRS 2008-2010 (see AUTHORS file)
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

#ifndef __COMMON_UTILS_H__
#define __COMMON_UTILS_H__

#include <starpu.h>
#include <common/config.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>

#ifdef STARPU_VERBOSE
#  define _STARPU_DEBUG(fmt, args ...) fprintf(stderr, "[starpu][%s] " fmt ,__func__ ,##args)
#else
#  define _STARPU_DEBUG(fmt, args ...)
#endif

#ifdef STARPU_VERBOSE0
#  define _STARPU_LOG_IN()             fprintf(stderr, "[starpu][%ld][%s] -->\n", pthread_self(), __func__ );
#  define _STARPU_LOG_OUT()            fprintf(stderr, "[starpu][%ld][%s] <--\n", pthread_self(), __func__ );
#  define _STARPU_LOG_OUT_TAG(outtag)  fprintf(stderr, "[starpu][%ld][%s] <-- (%s)\n", pthread_self(), __func__, outtag);
#else
#  define _STARPU_LOG_IN()
#  define _STARPU_LOG_OUT()
#  define _STARPU_LOG_OUT_TAG(outtag)
#endif

#define _STARPU_DISP(fmt, args ...) fprintf(stderr, "[starpu][%s] " fmt ,__func__ ,##args)
#define _STARPU_ERROR(fmt, args ...)                                                  \
	do {                                                                          \
                fprintf(stderr, "[starpu][%s] Error: " fmt ,__func__ ,##args);        \
		STARPU_ABORT();                                                            \
	} while (0)


int _starpu_mkpath(const char *s, mode_t mode);
int _starpu_check_mutex_deadlock(pthread_mutex_t *mutex);

/* If FILE is currently on a comment line, eat it.  */
void _starpu_drop_comments(FILE *f);

#define PTHREAD_MUTEX_INIT(mutex, attr) { int p_ret = pthread_mutex_init((mutex), (attr)); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_mutex_init: %s\n", strerror(p_ret)); STARPU_ABORT(); }}
#define PTHREAD_MUTEX_DESTROY(mutex) { int p_ret = pthread_mutex_destroy(mutex); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_mutex_destroy: %s\n", strerror(p_ret)); STARPU_ABORT(); }}
#define PTHREAD_MUTEX_LOCK(mutex) { int p_ret = pthread_mutex_lock(mutex); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_mutex_lock : %s\n", strerror(p_ret)); STARPU_ABORT(); }}
#define PTHREAD_MUTEX_UNLOCK(mutex) { int p_ret = pthread_mutex_unlock(mutex); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_mutex_unlock : %s\n", strerror(p_ret)); STARPU_ABORT(); }}

#define PTHREAD_RWLOCK_INIT(rwlock, attr) { int p_ret = pthread_rwlock_init((rwlock), (attr)); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_rwlock_init : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_RWLOCK_RDLOCK(rwlock) { int p_ret = pthread_rwlock_rdlock(rwlock); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_rwlock_rdlock : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_RWLOCK_WRLOCK(rwlock) { int p_ret = pthread_rwlock_wrlock(rwlock); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_rwlock_wrlock : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_RWLOCK_UNLOCK(rwlock) { int p_ret = pthread_rwlock_unlock(rwlock); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_rwlock_unlock : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_RWLOCK_DESTROY(rwlock) { int p_ret = pthread_rwlock_destroy(rwlock); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_rwlock_destroy : %s\n", strerror(p_ret)); STARPU_ABORT();}}

#define PTHREAD_COND_INIT(cond, attr) { int p_ret = pthread_cond_init((cond), (attr)); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_cond_init : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_COND_DESTROY(cond) { int p_ret = pthread_cond_destroy(cond); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_cond_destroy : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_COND_SIGNAL(cond) { int p_ret = pthread_cond_signal(cond); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_cond_signal : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_COND_BROADCAST(cond) { int p_ret = pthread_cond_broadcast(cond); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_cond_broadcast : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_COND_WAIT(cond, mutex) { int p_ret = pthread_cond_wait((cond), (mutex)); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_cond_wait : %s\n", strerror(p_ret)); STARPU_ABORT();}}

#define PTHREAD_BARRIER_INIT(barrier, attr, count) { int p_ret = pthread_barrier_init((barrier), (attr), (count)); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_barrier_init : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_BARRIER_DESTROY(barrier) { int p_ret = pthread_barrier_destroy((barrier)); if (STARPU_UNLIKELY(p_ret)) { fprintf(stderr, "pthread_barrier_destroy : %s\n", strerror(p_ret)); STARPU_ABORT();}}
#define PTHREAD_BARRIER_WAIT(barrier) { int p_ret = pthread_barrier_wait(barrier); if (STARPU_UNLIKELY(!((p_ret == 0) || (p_ret == PTHREAD_BARRIER_SERIAL_THREAD)))) { fprintf(stderr, "pthread_barrier_wait : %s\n", strerror(p_ret)); STARPU_ABORT();}}

#endif // __COMMON_UTILS_H__
