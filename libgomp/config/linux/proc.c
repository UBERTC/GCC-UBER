/* Copyright (C) 2005-2013 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* This file contains system specific routines related to counting
   online processors and dynamic load balancing.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "libgomp.h"
#include "proc.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_GETLOADAVG
# ifdef HAVE_SYS_LOADAVG_H
#  include <sys/loadavg.h>
# endif
#endif

#ifdef HAVE_PTHREAD_AFFINITY_NP
unsigned long
gomp_cpuset_popcount (cpu_set_t *cpusetp)
{
#ifdef CPU_COUNT
  /* glibc 2.6 and above provide a macro for this.  */
  return CPU_COUNT (cpusetp);
#else
  size_t i;
  unsigned long ret = 0;
  extern int check[sizeof (cpusetp->__bits[0]) == sizeof (unsigned long int)];

  (void) check;
  for (i = 0; i < sizeof (*cpusetp) / sizeof (cpusetp->__bits[0]); i++)
    {
      unsigned long int mask = cpusetp->__bits[i];
      if (mask == 0)
	continue;
      ret += __builtin_popcountl (mask);
    }
  return ret;
#endif
}
#endif

/* Read the content of a file.
 * Return the length of the data, or -1 on error. Does *not*
 * zero-terminate the content. Will not read more
 * than 'buffsize' bytes.
 */
static int
read_file(const char*  pathname, char*  buffer, size_t  buffsize)
{
    int  fd, len;

    fd = open(pathname, O_RDONLY);
    if (fd < 0)
        return -1;

    do {
        len = read(fd, buffer, buffsize);
    } while (len < 0 && errno == EINTR);

    close(fd);

    return len;
}


/* Parse a decimal integer starting from 'input', but not going further
 * than 'limit'. Return the value into '*result'.
 *
 * NOTE: Does not skip over leading spaces, or deal with sign characters.
 * NOTE: Ignores overflows.
 *
 * The function returns NULL in case of error (bad format), or the new
 * position after the decimal number in case of success (which will always
 * be <= 'limit').
 */
static const char*
parse_decimal(const char* input, const char* limit, int* result)
{
    const char* p = input;
    int val = 0;
    while (p < limit) {
        int d = (*p - '0');
        if ((unsigned)d >= 10U)
            break;
        val = val*10 + d;
        p++;
    }
    if (p == input)
        return NULL;

    *result = val;
    return p;
}


/* This data type is used to represent a CPU list / mask, as read
 * from sysfs on Linux. See http://www.kernel.org/doc/Documentation/cputopology.txt
 */
typedef struct {
    int mask;
} cpuList;

/* Returns Actual CPUs (total installed CPUs) */
static int
cpuList_count (cpuList* list) {
	return list->mask ;
}

/* Parse a textual list of cpus and store the result inside a cpuList object.
 * Input format is the following:
 * - comma-separated list of items (no spaces)
 * - each item is either a single decimal number (cpu index), or a range made
 *   of two numbers separated by a single dash (-). Ranges are inclusive.
 * Examples:
 *           0
 *           2,4-127,128-143
 *           0-1
 */
static void
cpuList_parse (cpuList* list, const char* line, int line_len)
{
    const char* p = line;
    const char* end = p + line_len;
    const char* q;

    /* NOTE: the input line coming from sysfs typically contains a
    * trailing newline, so take care of it in the code below
     */
    while (p < end && *p != '\n')
    {
        int val, start_value, end_value;

        /* Find the end of current item, and put it into 'q' */
        q = memchr(p, ',', end-p);
        if (q == NULL) {
            q = end;
        }

        /* Get first value */
        p = parse_decimal(p, q, &start_value);
        if (p == NULL)
            goto BAD_FORMAT;

        end_value = start_value;

        /* If we're not at the end of the item, expect a dash and
         * and integer; extract end value.
         */
        if (p < q && *p == '-') {
            p = parse_decimal(p+1, q, &end_value);
            if (p == NULL)
                goto BAD_FORMAT;
        }

        /* Set CPU list */
        for (val = start_value; val <= end_value; val++) {
            list->mask++;
        }

        /* Jump to next item */
        p = q;
        if (p < end)
            p++;
    }

BAD_FORMAT:
    ;
}


/* Read a CPU list from one sysfs file
 * The below is CPU related sys interface by CPU topologoy.
 */
static void
cpuList_read_from (cpuList* list, const char* filename)
{
    char file[64];
    int filelen;

    list->mask = 0;

    filelen = read_file(filename, file, sizeof file);
    if (filelen < 0) {
        fprintf(stderr,"Could not read %s: %s\n", filename, strerror(errno));
        return;
    }

    cpuList_parse(list, file, filelen);
}


/* Probe the number of actual CPUs on devices (e.g. Android devices) using
 * CPU-Hotplug and CPU-DVFS to optimize battery life.
 * Return the number of CPUs present on a given device.
 * See http://www.kernel.org/doc/Documentation/cputopology.txt
 */
static int
sc_nprocessors_actu ()
{
    char buffer[256];
    int buffer_len;
    int cpuCount = 1;
    cpuList cpus_present[1];
    char file_name[64] = "/sys/devices/system/cpu/present";

    buffer_len = read_file(file_name, buffer, sizeof buffer);

    if (buffer_len < 0)  /* should not happen */ {
        fprintf(stderr,"Could not find %s: %s\n", file_name, strerror(errno));
        return 1;
    }

    /* Count the CPU cores, the value may be 0 for single-core CPUs */
    cpuList_read_from(cpus_present, file_name);
    cpuCount = cpuList_count(cpus_present);
    if (cpuCount == 0) {
        cpuCount = 1;
    }
    return cpuCount;
}

/* At startup, determine the default number of threads.  It would seem
   this should be related to the number of cpus online.  */

void
gomp_init_num_threads (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
  cpu_set_t cpuset;

  if (pthread_getaffinity_np (pthread_self (), sizeof (cpuset), &cpuset) == 0)
    {
      /* Count only the CPUs this process can use.  */
      gomp_global_icv.nthreads_var = gomp_cpuset_popcount (&cpuset);
      if (gomp_global_icv.nthreads_var == 0)
	gomp_global_icv.nthreads_var = 1;
      return;
    }
#endif
#if defined(__ANDROID__)
  gomp_global_icv.nthreads_var = sc_nprocessors_actu ();
#elif defined(_SC_NPROCESSORS_ONLN)
  gomp_global_icv.nthreads_var = sysconf (_SC_NPROCESSORS_ONLN);
#endif
}

static int
get_num_procs (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
  cpu_set_t cpuset;

  if (gomp_cpu_affinity == NULL)
    {
      /* Count only the CPUs this process can use.  */
      if (pthread_getaffinity_np (pthread_self (), sizeof (cpuset),
				  &cpuset) == 0)
	{
	  int ret = gomp_cpuset_popcount (&cpuset);
	  return ret != 0 ? ret : 1;
	}
    }
  else
    {
      /* We can't use pthread_getaffinity_np in this case
	 (we have changed it ourselves, it binds to just one CPU).
	 Count instead the number of different CPUs we are
	 using.  gomp_init_affinity updated gomp_available_cpus to
	 the number of CPUs in the GOMP_AFFINITY mask that we are
	 allowed to use though.  */
      return gomp_available_cpus;
    }
#endif
#if defined(__ANDROID__)
  return sc_nprocessors_actu ();
#elif defined(_SC_NPROCESSORS_ONLN)
  return sysconf (_SC_NPROCESSORS_ONLN);
#else
  return gomp_icv (false)->nthreads_var;
#endif
}

/* When OMP_DYNAMIC is set, at thread launch determine the number of
   threads we should spawn for this team.  */
/* ??? I have no idea what best practice for this is.  Surely some
   function of the number of processors that are *still* online and
   the load average.  Here I use the number of processors online
   minus the 15 minute load average.  */

unsigned
gomp_dynamic_max_threads (void)
{
  unsigned n_onln, loadavg, nthreads_var = gomp_icv (false)->nthreads_var;

  n_onln = get_num_procs ();
  if (n_onln > nthreads_var)
    n_onln = nthreads_var;

  loadavg = 0;
#ifdef HAVE_GETLOADAVG
  {
    double dloadavg[3];
    if (getloadavg (dloadavg, 3) == 3)
      {
	/* Add 0.1 to get a kind of biased rounding.  */
	loadavg = dloadavg[2] + 0.1;
      }
  }
#endif

  if (loadavg >= n_onln)
    return 1;
  else
    return n_onln - loadavg;
}

int
omp_get_num_procs (void)
{
  return get_num_procs ();
}

ialias (omp_get_num_procs)
