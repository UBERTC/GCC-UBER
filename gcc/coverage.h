/* coverage.h - Defines data exported from coverage.c
   Copyright (C) 1998-2014 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_COVERAGE_H
#define GCC_COVERAGE_H

#include "gcov-io.h"

extern void coverage_init (const char *, const char*);
extern void coverage_finish (void);

/* Start outputting coverage information for the current
   function.  */
extern int coverage_begin_function (unsigned, unsigned);

/* Complete the coverage information for the current function.  */
extern void coverage_end_function (unsigned, unsigned);

/* Compute the control flow checksum for the current function.  */
extern unsigned coverage_compute_cfg_checksum (void);

/* Compute the profile id of function N.  */
extern unsigned coverage_compute_profile_id (struct cgraph_node *n);

/* Compute the line number checksum for the current function.  */
extern unsigned coverage_compute_lineno_checksum (void);

/* Allocate some counters. Repeatable per function.  */
extern int coverage_counter_alloc (unsigned /*counter*/, unsigned/*num*/);
/* Use a counter from the most recent allocation.  */
extern tree tree_coverage_counter_ref (unsigned /*counter*/, unsigned/*num*/);
/* Use a counter address from the most recent allocation.  */
extern tree tree_coverage_counter_addr (unsigned /*counter*/, unsigned/*num*/);

/* Get all the counters for the current function.  */
extern gcov_type *get_coverage_counts (unsigned /*counter*/,
				       unsigned /*expected*/,
				       unsigned /*cfg_checksum*/,
				       unsigned /*lineno_checksum*/,
				       const struct gcov_ctr_summary **);
/* Get all the counters for the current function without warning.  */
extern gcov_type *get_coverage_counts_no_warn (struct function *, 
                                               unsigned /*counter*/, unsigned *);

extern struct cgraph_node * find_func_by_global_id (unsigned HOST_WIDE_INT gid,
						    bool);

extern bool check_ic_target (gimple call_stmt, struct cgraph_node *target);

/* All the coverage counters are supposed to be allocated by the time
   coverage_end_function is called. However, direct-call counters are
   allocated after coverage_end_function has been called. This function
   fixes up the various internal structures to reflect these counter
   allocations. This function is called after coverage_end_function and
   before coverage_finish.  */
extern void coverage_dc_end_function (void);

/* True if a function entry corresponding to the given function identifier
   is present in the coverage internal data structures.  */
extern bool coverage_function_present (unsigned fn_ident);

extern bool coverage_node_map_initialized_p (void);

extern void emit_function_name (void);

extern tree get_gcov_type (void);
extern tree get_gcov_unsigned_t (void);
extern tree get_const_string_type (void);

/* Mark this module as containing asm statements.  */
extern void coverage_has_asm_stmt (void);

extern bool incompatible_cl_args (struct gcov_module_info *,
				  struct gcov_module_info *);

/* Defined in tree-profile.c.  */
extern void tree_init_instrumentation_sampling (void);
extern void tree_init_dyn_ipa_parameters (void);
extern void tree_init_instrumentation (void);

#endif
