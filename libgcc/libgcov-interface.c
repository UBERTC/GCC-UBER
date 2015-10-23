/* Routines required for instrumenting a program.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1989-2014 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

#include "libgcov.h"
#include "gthr.h"

#if defined(inhibit_libc)

#ifdef L_gcov_flush
void __gcov_flush (void) {}
#endif

#ifdef L_gcov_reset
void __gcov_reset (void) {}
#endif

#ifdef L_gcov_dump
void __gcov_dump (void) {}
#endif

#else

extern void gcov_clear (void) ATTRIBUTE_HIDDEN;
extern void gcov_exit (void) ATTRIBUTE_HIDDEN;
extern void set_gcov_dump_complete (void) ATTRIBUTE_HIDDEN;
extern void reset_gcov_dump_complete (void) ATTRIBUTE_HIDDEN;

#ifdef L_gcov_flush

#ifdef __GTHREAD_MUTEX_INIT
ATTRIBUTE_HIDDEN __gthread_mutex_t __gcov_flush_mx = __GTHREAD_MUTEX_INIT;
#define init_mx_once()
#else
__gthread_mutex_t __gcov_flush_mx ATTRIBUTE_HIDDEN;

static void
init_mx (void)
{
  __GTHREAD_MUTEX_INIT_FUNCTION (&__gcov_flush_mx);
}
static void
init_mx_once (void)
{
  static __gthread_once_t once = __GTHREAD_ONCE_INIT;
  __gthread_once (&once, init_mx);
}
#endif

/* Called before fork or exec - write out profile information gathered so
   far and reset it to zero.  This avoids duplication or loss of the
   profile information gathered so far.  */

void
__gcov_flush (void)
{
  init_mx_once ();
  __gthread_mutex_lock (&__gcov_flush_mx);

  gcov_exit ();
  gcov_clear ();

  __gthread_mutex_unlock (&__gcov_flush_mx);
}

#endif /* L_gcov_flush */

#ifdef L_gcov_reset

/* Function that can be called from application to reset counters to zero,
   in order to collect profile in region of interest.  */

void
__gcov_reset (void)
{
  gcov_clear ();
  /* Re-enable dumping to support collecting profile in multiple regions
     of interest.  */
  reset_gcov_dump_complete ();
}

#endif /* L_gcov_reset */

#ifdef L_gcov_dump

/* Function that can be called from application to write profile collected
   so far, in order to collect profile in region of interest.  */

void
__gcov_dump (void)
{
  gcov_exit ();
  /* Prevent profile from being dumped a second time on application exit.  */
  set_gcov_dump_complete ();
}

/* Emitted in coverage.c.  */
extern gcov_unsigned_t __gcov_test_coverage;

unsigned int __gcov_profiling_for_test_coverage (void);

/* Function that can be called from application to distinguish binaries
   instrumented for coverage from those instrumented for profile
   optimization (e.g. -fprofile-generate).  */

unsigned int __gcov_profiling_for_test_coverage (void)
{
  return __gcov_test_coverage;
}

typedef void (*gcov_dumper_type) (void);
struct dumper_entry
{
  gcov_dumper_type dumper;
  struct dumper_entry *next_dumper;
};

static struct dumper_entry this_dumper = {&__gcov_dump, 0};

/* global dumper list with default visibilty. */
struct dumper_entry *__gcov_dumper_list;

#ifdef __GTHREAD_MUTEX_INIT
__gthread_mutex_t __gcov_dump_mx = __GTHREAD_MUTEX_INIT;
#define init_mx_once()
#else
__gthread_mutex_t __gcov_dump_mx;

static void
init_mx (void)
{
  __GTHREAD_MUTEX_INIT_FUNCTION (&__gcov_dump_mx);
}
static void
init_mx_once (void)
{
  static __gthread_once_t once = __GTHREAD_ONCE_INIT;
  __gthread_once (&once, init_mx);
}
#endif

/* Register the library private __gcov_dump method
   to the global list.  */

__attribute__((constructor))
static void
register_dumper (void)
{
  init_mx_once ();
  __gthread_mutex_lock (&__gcov_dump_mx);
  this_dumper.next_dumper = __gcov_dumper_list;
  __gcov_dumper_list = &this_dumper;
  __gthread_mutex_unlock (&__gcov_dump_mx);
}

__attribute__((destructor))
static void
unregister_dumper (void)
{
  struct dumper_entry *dumper;
  struct dumper_entry *prev_dumper = 0;

  init_mx_once ();
  __gthread_mutex_lock (&__gcov_dump_mx);
  dumper = __gcov_dumper_list;

  while (dumper)
    {
      if (dumper->dumper == &__gcov_dump)
        {
	  if (prev_dumper)
	    prev_dumper->next_dumper = dumper->next_dumper;
 	  else
	    __gcov_dumper_list = dumper->next_dumper;
          break;
        }
      prev_dumper = dumper;
      dumper = dumper->next_dumper;
    }
  __gthread_mutex_unlock (&__gcov_dump_mx);
}

/* Public interface to dump profile data for all shared libraries
   via registered dumpers from the libraries. This interface
   has default visibility (unlike gcov_dump which has hidden
   visbility.  */

void
__gcov_dump_all (void)
{
  struct dumper_entry *dumper;

  init_mx_once ();
  __gthread_mutex_lock (&__gcov_dump_mx);

  dumper = __gcov_dumper_list;
  while (dumper)
   {
     dumper->dumper ();
     dumper = dumper->next_dumper;
   }
  __gthread_mutex_unlock (&__gcov_dump_mx);
}

#endif /* L_gcov_dump */

#ifdef L_gcov_sampling

/* Emitted in coverage.c.  */

/* Sampling period.  */
extern gcov_unsigned_t __gcov_sampling_period;
extern gcov_unsigned_t __gcov_has_sampling;
void __gcov_set_sampling_period (unsigned int period);
unsigned int __gcov_sampling_enabled ();
/* Per thread sample counter.  */
__thread gcov_unsigned_t __gcov_sample_counter = 0;

/* Set sampling period to PERIOD.  */

void __gcov_set_sampling_period (unsigned int period)
{
  gcc_assert (__gcov_has_sampling);
  __gcov_sampling_period = period;
}

unsigned int __gcov_sampling_enabled ()
{
  return __gcov_has_sampling;
}

#endif

#ifdef L_gcov_prefix

/* Profile directory prefix specified to -fprofile-generate=.  */
extern char * __gcov_profile_prefix;

char *__gcov_get_profile_prefix ()
{
  return __gcov_profile_prefix;
}

#endif


#ifdef L_gcov_fork
/* A wrapper for the fork function.  Flushes the accumulated profiling data, so
   that they are not counted twice.  */

pid_t
__gcov_fork (void)
{
  pid_t pid;
  extern __gthread_mutex_t __gcov_flush_mx;
  __gcov_flush ();
  pid = fork ();
  if (pid == 0)
    __GTHREAD_MUTEX_INIT_FUNCTION (&__gcov_flush_mx);
  return pid;
}
#endif

#ifdef L_gcov_execl
/* A wrapper for the execl function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execl (const char *path, char *arg, ...)
{
  va_list ap, aq;
  unsigned i, length;
  char **args;

  __gcov_flush ();

  va_start (ap, arg);
  va_copy (aq, ap);

  length = 2;
  while (va_arg (ap, char *))
    length++;
  va_end (ap);

  args = (char **) alloca (length * sizeof (void *));
  args[0] = arg;
  for (i = 1; i < length; i++)
    args[i] = va_arg (aq, char *);
  va_end (aq);

  return execv (path, args);
}
#endif

#ifdef L_gcov_execlp
/* A wrapper for the execlp function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execlp (const char *path, char *arg, ...)
{
  va_list ap, aq;
  unsigned i, length;
  char **args;

  __gcov_flush ();

  va_start (ap, arg);
  va_copy (aq, ap);

  length = 2;
  while (va_arg (ap, char *))
    length++;
  va_end (ap);

  args = (char **) alloca (length * sizeof (void *));
  args[0] = arg;
  for (i = 1; i < length; i++)
    args[i] = va_arg (aq, char *);
  va_end (aq);

  return execvp (path, args);
}
#endif

#ifdef L_gcov_execle
/* A wrapper for the execle function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execle (const char *path, char *arg, ...)
{
  va_list ap, aq;
  unsigned i, length;
  char **args;
  char **envp;

  __gcov_flush ();

  va_start (ap, arg);
  va_copy (aq, ap);

  length = 2;
  while (va_arg (ap, char *))
    length++;
  va_end (ap);

  args = (char **) alloca (length * sizeof (void *));
  args[0] = arg;
  for (i = 1; i < length; i++)
    args[i] = va_arg (aq, char *);
  envp = va_arg (aq, char **);
  va_end (aq);

  return execve (path, args, envp);
}
#endif

#ifdef L_gcov_execv
/* A wrapper for the execv function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execv (const char *path, char *const argv[])
{
  __gcov_flush ();
  return execv (path, argv);
}
#endif

#ifdef L_gcov_execvp
/* A wrapper for the execvp function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execvp (const char *path, char *const argv[])
{
  __gcov_flush ();
  return execvp (path, argv);
}
#endif

#ifdef L_gcov_execve
/* A wrapper for the execve function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execve (const char *path, char *const argv[], char *const envp[])
{
  __gcov_flush ();
  return execve (path, argv, envp);
}
#endif
#endif /* inhibit_libc */
