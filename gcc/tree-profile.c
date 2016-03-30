/* Calculate branch probabilities, and basic block execution counts.
   Copyright (C) 1990-2014 Free Software Foundation, Inc.
   Contributed by James E. Wilson, UC Berkeley/Cygnus Support;
   based on some ideas from Dain Samples of UC Berkeley.
   Further mangling by Bob Manson, Cygnus Support.
   Converted to use trees by Dale Johannesen, Apple Computer.

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

/* Generate basic block profile instrumentation and auxiliary files.
   Tree-based version.  See profile.c for overview.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "flags.h"
#include "target.h"
#include "output.h"
#include "regs.h"
#include "function.h"
#include "basic-block.h"
#include "diagnostic-core.h"
#include "coverage.h"
#include "tree.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "varasm.h"
#include "tree-nested.h"
#include "gimplify.h"
#include "gimple-iterator.h"
#include "gimplify-me.h"
#include "gimple-ssa.h"
#include "cgraph.h"
#include "tree-cfg.h"
#include "stringpool.h"
#include "tree-ssanames.h"
#include "tree-into-ssa.h"
#include "tree-pass.h"
#include "value-prof.h"
#include "output.h"
#include "params.h"
#include "profile.h"
#include "l-ipo.h"
#include "profile.h"
#include "target.h"
#include "tree-cfgcleanup.h"
#include "tree-nested.h"
#include "pointer-set.h"

/* Default name for coverage callback function.  */
#define COVERAGE_CALLBACK_FUNC_NAME "__coverage_callback"

/* True if we insert a callback to edge instrumentation code. Avoid this
   for the callback function itself.  */
#define COVERAGE_INSERT_CALL ((PARAM_VALUE (PARAM_COVERAGE_CALLBACK) == 1) \
                              && strcmp (get_name (current_function_decl), \
                                         COVERAGE_CALLBACK_FUNC_NAME))

/* Number of statements inserted for each edge counter increment.  */
#define EDGE_COUNTER_STMT_COUNT 3

static GTY(()) tree gcov_type_node;
static GTY(()) tree tree_interval_profiler_fn;
static GTY(()) tree tree_pow2_profiler_fn;
static GTY(()) tree tree_one_value_profiler_fn;
static GTY(()) tree tree_indirect_call_profiler_fn;
static GTY(()) tree tree_indirect_call_topn_profiler_fn;
static GTY(()) tree tree_direct_call_profiler_fn;
static GTY(()) tree tree_time_profiler_fn;
static GTY(()) tree tree_average_profiler_fn;
static GTY(()) tree tree_ior_profiler_fn;


static GTY(()) tree ic_void_ptr_var;
static GTY(()) tree ic_gcov_type_ptr_var;
static GTY(()) tree dc_void_ptr_var;
static GTY(()) tree dc_gcov_type_ptr_var;
static GTY(()) tree ptr_void;
static GTY(()) tree gcov_info_decl;

/* When -D__KERNEL__ is in the option list, we assume this is a
   compilation for Linux Kernel. This is checked and set in
   coverage.c.  */ 
bool is_kernel_build;

/* Do initialization work for the edge profiler.  */

/* Add code:
   // if flag_dyn_ipa
   extern gcov*	__gcov_indirect_call_topn_counters; // pointer to actual counter
   extern void*	__gcov_indirect_call_topn_callee; // actual callee address

   // else
   __thread gcov*	__gcov_indirect_call_counters; // pointer to actual counter
   __thread void*	__gcov_indirect_call_callee; // actual callee address
   __thread int __gcov_function_counter; // time profiler function counter
*/
static void
init_ic_make_global_vars (void)
{
  tree  gcov_type_ptr;

  ptr_void = build_pointer_type (void_type_node);

  if (flag_dyn_ipa)
    {
      ic_void_ptr_var 
	= build_decl (UNKNOWN_LOCATION, VAR_DECL, 
		      get_identifier ("__gcov_indirect_call_topn_callee"), 
		      ptr_void);
      TREE_PUBLIC (ic_void_ptr_var) = 1;
      DECL_EXTERNAL (ic_void_ptr_var) = 1;
      if (targetm.have_tls && !is_kernel_build)
        DECL_TLS_MODEL (ic_void_ptr_var) =
          decl_default_tls_model (ic_void_ptr_var);
      gcov_type_ptr = build_pointer_type (get_gcov_type ());
      ic_gcov_type_ptr_var 
	= build_decl (UNKNOWN_LOCATION, VAR_DECL, 
		      get_identifier ("__gcov_indirect_call_topn_counters"), 
		      gcov_type_ptr);
      TREE_PUBLIC (ic_gcov_type_ptr_var) = 1;
      DECL_EXTERNAL (ic_gcov_type_ptr_var) = 1;
      if (targetm.have_tls && !is_kernel_build)
        DECL_TLS_MODEL (ic_gcov_type_ptr_var) =
          decl_default_tls_model (ic_gcov_type_ptr_var);
    }
  else 
    {
  /* Do not fix indentation to avoid merge conflicts.  */
  /* Workaround for binutils bug 14342.  Once it is fixed, remove lto path.  */
  if (flag_lto)
    {
      ic_void_ptr_var
	= build_decl (UNKNOWN_LOCATION, VAR_DECL,
		      get_identifier ("__gcov_indirect_call_callee_ltopriv"),
		      ptr_void);
      TREE_PUBLIC (ic_void_ptr_var) = 1;
      DECL_COMMON (ic_void_ptr_var) = 1;
      DECL_VISIBILITY (ic_void_ptr_var) = VISIBILITY_HIDDEN;
      DECL_VISIBILITY_SPECIFIED (ic_void_ptr_var) = true;
    }
  else
    {
      ic_void_ptr_var
	= build_decl (UNKNOWN_LOCATION, VAR_DECL,
		      get_identifier ("__gcov_indirect_call_callee"),
		      ptr_void);
      TREE_PUBLIC (ic_void_ptr_var) = 1;
      DECL_EXTERNAL (ic_void_ptr_var) = 1;
    }
  TREE_STATIC (ic_void_ptr_var) = 1;
  DECL_ARTIFICIAL (ic_void_ptr_var) = 1;
  DECL_INITIAL (ic_void_ptr_var) = NULL;
  if (targetm.have_tls && !is_kernel_build)
    DECL_TLS_MODEL (ic_void_ptr_var) =
      decl_default_tls_model (ic_void_ptr_var);

  varpool_finalize_decl (ic_void_ptr_var);

  gcov_type_ptr = build_pointer_type (get_gcov_type ());
  /* Workaround for binutils bug 14342.  Once it is fixed, remove lto path.  */
  if (flag_lto)
    {
      ic_gcov_type_ptr_var
	= build_decl (UNKNOWN_LOCATION, VAR_DECL,
		      get_identifier ("__gcov_indirect_call_counters_ltopriv"),
		      gcov_type_ptr);
      TREE_PUBLIC (ic_gcov_type_ptr_var) = 1;
      DECL_COMMON (ic_gcov_type_ptr_var) = 1;
      DECL_VISIBILITY (ic_gcov_type_ptr_var) = VISIBILITY_HIDDEN;
      DECL_VISIBILITY_SPECIFIED (ic_gcov_type_ptr_var) = true;
    }
  else
    {
      ic_gcov_type_ptr_var
	= build_decl (UNKNOWN_LOCATION, VAR_DECL,
		      get_identifier ("__gcov_indirect_call_counters"),
		      gcov_type_ptr);
      TREE_PUBLIC (ic_gcov_type_ptr_var) = 1;
      DECL_EXTERNAL (ic_gcov_type_ptr_var) = 1;
    }
  TREE_STATIC (ic_gcov_type_ptr_var) = 1;
  DECL_ARTIFICIAL (ic_gcov_type_ptr_var) = 1;
  DECL_INITIAL (ic_gcov_type_ptr_var) = NULL;
  if (targetm.have_tls && !is_kernel_build)
    DECL_TLS_MODEL (ic_gcov_type_ptr_var) =
      decl_default_tls_model (ic_gcov_type_ptr_var);

  varpool_finalize_decl (ic_gcov_type_ptr_var);

   } /* Indentation not fixed intentionally.  */

  if (!flag_dyn_ipa)
    {
      varpool_finalize_decl (ic_void_ptr_var);
      varpool_finalize_decl (ic_gcov_type_ptr_var);
    }
}

/* A pointer-set of the first statement in each block of statements that need to
   be applied a sampling wrapper.  */
static struct pointer_set_t *instrumentation_to_be_sampled = NULL;

/* extern __thread gcov_unsigned_t __gcov_sample_counter  */
static GTY(()) tree gcov_sample_counter_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_profile_prefix  */
static tree GTY(()) gcov_profile_prefix_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_test_coverage  */
static tree GTY(()) gcov_test_coverage_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_sampling_period  */
static GTY(()) tree gcov_sampling_period_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_has_sampling  */
static tree gcov_has_sampling_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_cutoff  */
static tree GTY(()) gcov_lipo_cutoff_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_random_seed  */
static tree GTY(()) gcov_lipo_random_seed_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_random_group_size  */
static tree GTY(()) gcov_lipo_random_group_size_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_propagate_scale  */
static tree GTY(()) gcov_lipo_propagate_scale_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_dump_cgraph  */
static tree GTY(()) gcov_lipo_dump_cgraph_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_max_mem  */
static tree GTY(()) gcov_lipo_max_mem_decl = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_grouping_algorithm  */
static tree GTY(()) gcov_lipo_grouping_algorithm = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_merge_modu_edges  */
static tree GTY(()) gcov_lipo_merge_modu_edges = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_strict_inclusion  */
static tree GTY(()) gcov_lipo_strict_inclusion = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_comdat_algorithm  */
static tree GTY(()) gcov_lipo_comdat_algorithm = NULL_TREE;

/* extern gcov_unsigned_t __gcov_lipo_sampling_period  */
static tree GTY(()) gcov_lipo_sampling_period = NULL_TREE;

/* Insert STMT_IF around given sequence of consecutive statements in the
   same basic block starting with STMT_START, ending with STMT_END.
   PROB is the probability of the taken branch.  */

static void
insert_if_then (gimple stmt_start, gimple stmt_end, gimple stmt_if, int prob)
{
  gimple_stmt_iterator gsi;
  basic_block bb_original, bb_before_if, bb_after_if;
  edge e_if_taken, e_then_join, e_else;
  int orig_frequency;

  gsi = gsi_for_stmt (stmt_start);
  gsi_insert_before (&gsi, stmt_if, GSI_SAME_STMT);
  bb_original = gsi_bb (gsi);
  e_if_taken = split_block (bb_original, stmt_if);
  e_if_taken->flags &= ~EDGE_FALLTHRU;
  e_if_taken->flags |= EDGE_TRUE_VALUE;
  e_then_join = split_block (e_if_taken->dest, stmt_end);
  bb_before_if = e_if_taken->src;
  bb_after_if = e_then_join->dest;
  e_else = make_edge (bb_before_if, bb_after_if, EDGE_FALSE_VALUE);
  orig_frequency = bb_original->frequency;
  e_if_taken->probability = prob;
  e_else->probability = REG_BR_PROB_BASE - prob;
  e_if_taken->dest->frequency = orig_frequency * (prob / REG_BR_PROB_BASE);
}

/* Transform:

   ORIGINAL CODE

   Into:

   __gcov_sample_counter++;
   if (__gcov_sample_counter >= __gcov_sampling_period)
     {
       __gcov_sample_counter = 0;
       ORIGINAL CODE
     }

   The original code block starts with STMT_START, is made of STMT_COUNT
   consecutive statements in the same basic block.  */

static void
add_sampling_wrapper (gimple stmt_start, gimple stmt_end)
{
  tree zero, one, tmp_var, tmp1, tmp2, tmp3;
  gimple stmt_inc_counter1, stmt_inc_counter2, stmt_inc_counter3;
  gimple stmt_reset_counter, stmt_assign_period, stmt_if;
  gimple_stmt_iterator gsi;

  tmp_var = create_tmp_reg (get_gcov_unsigned_t (), "PROF_sample");
  tmp1 = make_ssa_name (tmp_var, NULL);
  tmp2 = make_ssa_name (tmp_var, NULL);

  /* Create all the new statements needed.  */
  stmt_inc_counter1 = gimple_build_assign (tmp1, gcov_sample_counter_decl);
  one = build_int_cst (get_gcov_unsigned_t (), 1);
  stmt_inc_counter2 = gimple_build_assign_with_ops (
      PLUS_EXPR, tmp2, tmp1, one);
  stmt_inc_counter3 = gimple_build_assign (gcov_sample_counter_decl, tmp2);
  zero = build_int_cst (get_gcov_unsigned_t (), 0);
  stmt_reset_counter = gimple_build_assign (gcov_sample_counter_decl, zero);
  tmp3 = make_ssa_name (tmp_var, NULL);
  stmt_assign_period = gimple_build_assign (tmp3, gcov_sampling_period_decl);
  stmt_if = gimple_build_cond (GE_EXPR, tmp2, tmp3, NULL_TREE, NULL_TREE);

  /* Insert them for now in the original basic block.  */
  gsi = gsi_for_stmt (stmt_start);
  gsi_insert_before (&gsi, stmt_inc_counter1, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt_inc_counter2, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt_inc_counter3, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt_assign_period, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt_reset_counter, GSI_SAME_STMT);

  /* Insert IF block.  */
  /* Sampling rate can be changed at runtime: hard to guess the branch prob,
     so make it 1.  */
  insert_if_then (stmt_reset_counter, stmt_end, stmt_if, REG_BR_PROB_BASE);
}

/* Add a conditional stmt so that counter update will only exec one time.  */
 
static void
add_execonce_wrapper (gimple stmt_start, gimple stmt_end)
{
  tree zero, tmp_var, tmp1;
  gimple stmt_if, stmt_assign;
  gimple_stmt_iterator gsi;

  /* Create all the new statements needed.  */
  tmp_var = create_tmp_reg (get_gcov_type (), "PROF_temp");
  tmp1 = make_ssa_name (tmp_var, NULL);
  stmt_assign = gimple_build_assign (tmp1, gimple_assign_lhs (stmt_end));

  zero = build_int_cst (get_gcov_type (), 0);
  stmt_if = gimple_build_cond (EQ_EXPR, tmp1, zero, NULL_TREE, NULL_TREE);

  gsi = gsi_for_stmt (stmt_start);
  gsi_insert_before (&gsi, stmt_assign, GSI_SAME_STMT);

  /* Insert IF block.  */
  insert_if_then (stmt_start, stmt_end, stmt_if, 1);
}

/* Return whether STMT is the beginning of an instrumentation block to be
   applied sampling.  */

static bool
is_instrumentation_to_be_sampled (gimple stmt)
{
  return pointer_set_contains (instrumentation_to_be_sampled, stmt);
}

/* Add sampling wrappers around edge counter code in current function.  */

void
add_sampling_to_edge_counters (void)
{
  gimple_stmt_iterator gsi;
  basic_block bb;

  FOR_EACH_BB_REVERSE_FN (bb, cfun)
    for (gsi = gsi_last_bb (bb); !gsi_end_p (gsi); gsi_prev (&gsi))
      {
        gimple stmt_end = gsi_stmt (gsi);
        if (is_instrumentation_to_be_sampled (stmt_end))
          {
            gimple stmt_beg;
            int i;
            int edge_counter_stmt_count = EDGE_COUNTER_STMT_COUNT;

            /* The code for edge counter increment has EDGE_COUNTER_STMT_COUNT
               gimple statements. Advance that many statements to find the
               beginning statement.  */
            if (COVERAGE_INSERT_CALL)
              edge_counter_stmt_count++;

            for (i = 0; i < edge_counter_stmt_count - 1; i++)
              gsi_prev (&gsi);
            stmt_beg = gsi_stmt (gsi);
            gcc_assert (stmt_beg);


            if (flag_profile_generate_sampling)
              add_sampling_wrapper (stmt_beg, stmt_end);
            if (PARAM_VALUE (PARAM_COVERAGE_EXEC_ONCE))
              add_execonce_wrapper (stmt_beg, stmt_end);

            /* reset the iterator and continue.  */
            gsi = gsi_last_bb (bb);
          }
      }
}

/* Helper function to define a variable in comdat with initialization.
   DECL is the variable, PARAM is the parameter to set init value.  */

static void
init_comdat_decl (tree decl, int param)
{
  TREE_PUBLIC (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  DECL_COMDAT_GROUP (decl)
      = DECL_ASSEMBLER_NAME (decl);
  TREE_STATIC (decl) = 1;
  DECL_INITIAL (decl) = build_int_cst (
      get_gcov_unsigned_t (),
      PARAM_VALUE (param));
  varpool_finalize_decl (decl);
}

/* Initialization function for LIPO runtime parameters.  */

void
tree_init_dyn_ipa_parameters (void)
{
  if (!gcov_lipo_cutoff_decl)
    {
      gcov_lipo_cutoff_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_cutoff"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_cutoff_decl, PARAM_LIPO_CUTOFF);
      gcov_lipo_random_seed_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_random_seed"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_random_seed_decl, PARAM_LIPO_RANDOM_SEED);
      gcov_lipo_random_group_size_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_random_group_size"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_random_group_size_decl, PARAM_LIPO_RANDOM_GROUP_SIZE);
      gcov_lipo_propagate_scale_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_propagate_scale"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_propagate_scale_decl, PARAM_LIPO_PROPAGATE_SCALE);
      gcov_lipo_dump_cgraph_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_dump_cgraph"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_dump_cgraph_decl, PARAM_LIPO_DUMP_CGRAPH);
      gcov_lipo_max_mem_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_max_mem"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_max_mem_decl, PARAM_MAX_LIPO_MEMORY);
      gcov_lipo_grouping_algorithm = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_grouping_algorithm"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_grouping_algorithm,
                        PARAM_LIPO_GROUPING_ALGORITHM);
      gcov_lipo_merge_modu_edges = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_merge_modu_edges"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_merge_modu_edges,
                        PARAM_LIPO_MERGE_MODU_EDGES);
      gcov_lipo_strict_inclusion = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_weak_inclusion"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_strict_inclusion,
                        PARAM_LIPO_WEAK_INCLUSION);
      gcov_lipo_comdat_algorithm = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_comdat_algorithm"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_comdat_algorithm,
                        PARAM_LIPO_COMDAT_ALGORITHM);
      gcov_lipo_sampling_period = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_lipo_sampling_period"),
          get_gcov_unsigned_t ());
      init_comdat_decl (gcov_lipo_sampling_period,
                        PARAM_LIPO_SAMPLING_PERIOD);
    }
}

static void
cleanup_instrumentation_sampling (void)
{
  /* Free the bitmap.  */
  if (flag_profile_generate_sampling && instrumentation_to_be_sampled)
    {
      pointer_set_destroy (instrumentation_to_be_sampled);
      instrumentation_to_be_sampled = NULL;
    }
}

/* Initialization function for FDO instrumentation.  */

void
tree_init_instrumentation (void)
{
  if (!gcov_profile_prefix_decl)
    {
      tree prefix_ptr;
      int prefix_len;
      tree prefix_string;

      /* Construct an initializer for __gcov_profile_prefix.  */
      gcov_profile_prefix_decl =
        build_decl (UNKNOWN_LOCATION, VAR_DECL,
                    get_identifier ("__gcov_profile_prefix"),
                    get_const_string_type ());
      TREE_PUBLIC (gcov_profile_prefix_decl) = 1;
      DECL_ARTIFICIAL (gcov_profile_prefix_decl) = 1;
      make_decl_one_only (gcov_profile_prefix_decl,
                          DECL_ASSEMBLER_NAME (gcov_profile_prefix_decl));
      TREE_STATIC (gcov_profile_prefix_decl) = 1;

      const char null_prefix[] = "\0";
      const char *prefix = null_prefix;
      prefix_len = 0;
      if (profile_data_prefix)
        {
          prefix_len = strlen (profile_data_prefix);
          prefix = profile_data_prefix;
        }
      prefix_string = build_string (prefix_len + 1, prefix);
      TREE_TYPE (prefix_string) = build_array_type
          (char_type_node, build_index_type
           (build_int_cst (NULL_TREE, prefix_len)));
      prefix_ptr = build1 (ADDR_EXPR, get_const_string_type (),
                           prefix_string);

      DECL_INITIAL (gcov_profile_prefix_decl) = prefix_ptr;
      varpool_finalize_decl (gcov_profile_prefix_decl);
    }

  if (!gcov_test_coverage_decl)
    {
      /* Initialize __gcov_test_coverage to 1 if -ftest-coverage
         specified, 0 otherwise. Used by libgcov to determine whether
         a binary was instrumented for coverage or profile optimization.  */
      gcov_test_coverage_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_test_coverage"),
          get_gcov_unsigned_t ());
      TREE_PUBLIC (gcov_test_coverage_decl) = 1;
      DECL_ARTIFICIAL (gcov_test_coverage_decl) = 1;
      DECL_COMDAT_GROUP (gcov_test_coverage_decl)
          = DECL_ASSEMBLER_NAME (gcov_test_coverage_decl);
      TREE_STATIC (gcov_test_coverage_decl) = 1;
      DECL_INITIAL (gcov_test_coverage_decl) = build_int_cst (
          get_gcov_unsigned_t (),
          flag_test_coverage ? 1 : 0);
      varpool_finalize_decl (gcov_test_coverage_decl);
    }
}

/* Initialization function for FDO sampling.  */

void
tree_init_instrumentation_sampling (void)
{
  if (!gcov_sampling_period_decl)
    {
      /* Define __gcov_sampling_period regardless of
         -fprofile-generate-sampling. Otherwise the extern reference to
         it from libgcov becomes unmatched.
      */
      gcov_sampling_period_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_sampling_period"),
          get_gcov_unsigned_t ());
      TREE_PUBLIC (gcov_sampling_period_decl) = 1;
      DECL_ARTIFICIAL (gcov_sampling_period_decl) = 1;
      DECL_COMDAT_GROUP (gcov_sampling_period_decl)
          = DECL_ASSEMBLER_NAME (gcov_sampling_period_decl);
      TREE_STATIC (gcov_sampling_period_decl) = 1;
      DECL_INITIAL (gcov_sampling_period_decl) = build_int_cst (
          get_gcov_unsigned_t (),
          PARAM_VALUE (PARAM_PROFILE_GENERATE_SAMPLING_PERIOD));
      varpool_finalize_decl (gcov_sampling_period_decl);
    }

  if (!gcov_has_sampling_decl)
    {
      /* Initialize __gcov_has_sampling to 1 if -fprofile-generate-sampling
         specified, 0 otherwise. Used by libgcov to determine whether
         a request to set the sampling period makes sense.  */
      gcov_has_sampling_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_has_sampling"),
          get_gcov_unsigned_t ());
      TREE_PUBLIC (gcov_has_sampling_decl) = 1;
      DECL_ARTIFICIAL (gcov_has_sampling_decl) = 1;
      DECL_COMDAT_GROUP (gcov_has_sampling_decl)
          = DECL_ASSEMBLER_NAME (gcov_has_sampling_decl);
      TREE_STATIC (gcov_has_sampling_decl) = 1;
      DECL_INITIAL (gcov_has_sampling_decl) = build_int_cst (
          get_gcov_unsigned_t (),
          flag_profile_generate_sampling ? 1 : 0);
      varpool_finalize_decl (gcov_has_sampling_decl);
    }

  if (flag_profile_generate_sampling && !instrumentation_to_be_sampled)
    {
      instrumentation_to_be_sampled = pointer_set_create ();
      gcov_sample_counter_decl = build_decl (
          UNKNOWN_LOCATION,
          VAR_DECL,
          get_identifier ("__gcov_sample_counter"),
          get_gcov_unsigned_t ());
      TREE_PUBLIC (gcov_sample_counter_decl) = 1;
      DECL_EXTERNAL (gcov_sample_counter_decl) = 1;
      DECL_ARTIFICIAL (gcov_sample_counter_decl) = 1;
      if (targetm.have_tls && !is_kernel_build)
        DECL_TLS_MODEL (gcov_sample_counter_decl) =
            decl_default_tls_model (gcov_sample_counter_decl);
    }
  if (PARAM_VALUE (PARAM_COVERAGE_EXEC_ONCE)
      && instrumentation_to_be_sampled == 0)
    instrumentation_to_be_sampled = pointer_set_create ();
}

/* Create the type and function decls for the interface with gcov.  */

void
gimple_init_edge_profiler (void)
{
  tree interval_profiler_fn_type;
  tree pow2_profiler_fn_type;
  tree one_value_profiler_fn_type;
  tree gcov_type_ptr;
  tree ic_profiler_fn_type;
  tree ic_topn_profiler_fn_type;
  tree dc_profiler_fn_type;
  tree average_profiler_fn_type;
  tree time_profiler_fn_type;


  if (!gcov_type_node)
    {
      char name_buf[32];
      gcov_type_node = get_gcov_type ();
      gcov_type_ptr = build_pointer_type (gcov_type_node);

      ASM_GENERATE_INTERNAL_LABEL (name_buf, "LPBX", 0);
      gcov_info_decl = build_decl (UNKNOWN_LOCATION, VAR_DECL,
                                   get_identifier (name_buf),
                                   get_gcov_unsigned_t ());
      DECL_EXTERNAL (gcov_info_decl) = 1;
      TREE_ADDRESSABLE (gcov_info_decl) = 1;

      /* void (*) (gcov_type *, gcov_type, int, unsigned)  */
      interval_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node,
					  integer_type_node,
					  unsigned_type_node, NULL_TREE);
      tree_interval_profiler_fn
	      = build_fn_decl ("__gcov_interval_profiler",
				     interval_profiler_fn_type);
      TREE_NOTHROW (tree_interval_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_interval_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_interval_profiler_fn));

      /* void (*) (gcov_type *, gcov_type)  */
      pow2_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node,
					  NULL_TREE);
      tree_pow2_profiler_fn = build_fn_decl ("__gcov_pow2_profiler",
						   pow2_profiler_fn_type);
      TREE_NOTHROW (tree_pow2_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_pow2_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_pow2_profiler_fn));

      /* void (*) (gcov_type *, gcov_type)  */
      one_value_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node,
					  NULL_TREE);
      if (PROFILE_GEN_VALUE_ATOMIC)
        tree_one_value_profiler_fn
	      = build_fn_decl ("__gcov_one_value_profiler_atomic",
				     one_value_profiler_fn_type);
      else
        tree_one_value_profiler_fn
	      = build_fn_decl ("__gcov_one_value_profiler",
				     one_value_profiler_fn_type);
      TREE_NOTHROW (tree_one_value_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_one_value_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_one_value_profiler_fn));

      init_ic_make_global_vars ();

      /* Workaround for binutils bug 14342.  Once it is fixed, remove lto path.  */
      if (flag_lto)
        {
	  /* void (*) (gcov_type, void *)  */
	  ic_profiler_fn_type
		   = build_function_type_list (void_type_node,
					      gcov_type_ptr, gcov_type_node,
					      ptr_void, ptr_void,
					      NULL_TREE);
          // TODO(xur): atomic support
	  tree_indirect_call_profiler_fn
		  = build_fn_decl ("__gcov_indirect_call_profiler",
					 ic_profiler_fn_type);
        }
      else
        {
	  /* void (*) (gcov_type, void *)  */
	  ic_profiler_fn_type
		   = build_function_type_list (void_type_node,
					      gcov_type_node,
					      ptr_void,
					      NULL_TREE);
          if (PROFILE_GEN_VALUE_ATOMIC)
            tree_indirect_call_profiler_fn
              = build_fn_decl ("__gcov_indirect_call_profiler_atomic_v2",
                               ic_profiler_fn_type);
          else
            tree_indirect_call_profiler_fn
	      = build_fn_decl ("__gcov_indirect_call_profiler_v2",
                               ic_profiler_fn_type);
        }
      TREE_NOTHROW (tree_indirect_call_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_indirect_call_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_indirect_call_profiler_fn));

      /* void (*) (void *, void *, gcov_unsigned_t)  */
      ic_topn_profiler_fn_type
	= build_function_type_list (void_type_node, ptr_void, ptr_void,
				    get_gcov_unsigned_t (), NULL_TREE);
      tree_indirect_call_topn_profiler_fn
	      = build_fn_decl ("__gcov_indirect_call_topn_profiler",
                               ic_topn_profiler_fn_type);
      TREE_NOTHROW (tree_indirect_call_topn_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_indirect_call_topn_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_indirect_call_topn_profiler_fn));

      /* void (*) (void *, void *, gcov_unsigned_t)  */
      dc_profiler_fn_type
	= build_function_type_list (void_type_node, ptr_void, ptr_void,
				    get_gcov_unsigned_t (), NULL_TREE);
      tree_direct_call_profiler_fn
	= build_fn_decl ("__gcov_direct_call_profiler",
			 dc_profiler_fn_type);
      TREE_NOTHROW (tree_direct_call_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_direct_call_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_direct_call_profiler_fn));

      /* void (*) (gcov_type *, gcov_type, void *)  */
      time_profiler_fn_type
	       = build_function_type_list (void_type_node,
					  gcov_type_ptr, NULL_TREE);
      tree_time_profiler_fn
	      = build_fn_decl ("__gcov_time_profiler",
				     time_profiler_fn_type);
      TREE_NOTHROW (tree_time_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_time_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_time_profiler_fn));

      /* void (*) (gcov_type *, gcov_type)  */
      average_profiler_fn_type
	      = build_function_type_list (void_type_node,
					  gcov_type_ptr, gcov_type_node, NULL_TREE);
      tree_average_profiler_fn
	      = build_fn_decl ("__gcov_average_profiler",
				     average_profiler_fn_type);
      TREE_NOTHROW (tree_average_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_average_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_average_profiler_fn));
      tree_ior_profiler_fn
	      = build_fn_decl ("__gcov_ior_profiler",
				     average_profiler_fn_type);
      TREE_NOTHROW (tree_ior_profiler_fn) = 1;
      DECL_ATTRIBUTES (tree_ior_profiler_fn)
	= tree_cons (get_identifier ("leaf"), NULL,
		     DECL_ATTRIBUTES (tree_ior_profiler_fn));

      /* LTO streamer needs assembler names.  Because we create these decls
         late, we need to initialize them by hand.  */
      DECL_ASSEMBLER_NAME (tree_interval_profiler_fn);
      DECL_ASSEMBLER_NAME (tree_pow2_profiler_fn);
      DECL_ASSEMBLER_NAME (tree_one_value_profiler_fn);
      DECL_ASSEMBLER_NAME (tree_indirect_call_profiler_fn);
      DECL_ASSEMBLER_NAME (tree_time_profiler_fn);
      DECL_ASSEMBLER_NAME (tree_average_profiler_fn);
      DECL_ASSEMBLER_NAME (tree_ior_profiler_fn);
    }
}

/* Output instructions as GIMPLE trees to increment the edge
   execution count, and insert them on E.  We rely on
   gsi_insert_on_edge to preserve the order.  */

void
gimple_gen_edge_profiler (int edgeno, edge e)
{
  tree ref, one, gcov_type_tmp_var;
  gimple stmt1, stmt2, stmt3;
  bool is_atomic = PROFILE_GEN_EDGE_ATOMIC;

  if (is_atomic)
    ref = tree_coverage_counter_addr (GCOV_COUNTER_ARCS, edgeno);
  else
    ref = tree_coverage_counter_ref (GCOV_COUNTER_ARCS, edgeno);

  one = build_int_cst (gcov_type_node, 1);

  /* insert a callback stmt stmt */
  if (COVERAGE_INSERT_CALL)
    {
      gimple call;
      tree tree_edgeno = build_int_cst (gcov_type_node, edgeno);

      tree tree_uid;
      if (PARAM_VALUE (PARAM_PROFILE_FUNC_INTERNAL_ID))
        {
          tree_uid  = build_int_cst (gcov_type_node,
                                     current_function_funcdef_no);
        }
      else
        {
          gcc_assert (coverage_node_map_initialized_p ());

          tree_uid = build_int_cst
      (gcov_type_node, cgraph_get_node (current_function_decl)->profile_id);
        }
      tree callback_fn_type
              = build_function_type_list (void_type_node,
                                          gcov_type_node,
                                          integer_type_node,
                                          NULL_TREE);
      tree tree_callback_fn = build_fn_decl (COVERAGE_CALLBACK_FUNC_NAME,
                                             callback_fn_type);
      TREE_NOTHROW (tree_callback_fn) = 1;
      DECL_ATTRIBUTES (tree_callback_fn)
        = tree_cons (get_identifier ("leaf"), NULL,
                     DECL_ATTRIBUTES (tree_callback_fn));
  
      call = gimple_build_call (tree_callback_fn, 2, tree_uid, tree_edgeno);
      gsi_insert_on_edge(e, call);
    }

  if (is_atomic)
    {
      /* __atomic_fetch_add (&counter, 1, MEMMODEL_RELAXED); */
      stmt3 = gimple_build_call (builtin_decl_explicit (
                                   GCOV_TYPE_ATOMIC_FETCH_ADD),
                                 3, ref, one,
                                 build_int_cst (integer_type_node,
                                   MEMMODEL_RELAXED));
      /* Suppress "'stmt1' may be used uninitialized" warning.  */
      stmt1 = stmt2 = 0;
    }
  else
    {
      gcov_type_tmp_var = make_temp_ssa_name (gcov_type_node,
            				  NULL, "PROF_edge_counter");
      stmt1 = gimple_build_assign (gcov_type_tmp_var, ref);
      gcov_type_tmp_var = make_temp_ssa_name (gcov_type_node,
            				  NULL, "PROF_edge_counter");
      stmt2 = gimple_build_assign_with_ops (PLUS_EXPR, gcov_type_tmp_var,
            				gimple_assign_lhs (stmt1), one);
      stmt3 = gimple_build_assign (unshare_expr (ref), gimple_assign_lhs (stmt2));
   }

  if (flag_profile_generate_sampling
      || PARAM_VALUE (PARAM_COVERAGE_EXEC_ONCE))
    pointer_set_insert (instrumentation_to_be_sampled, stmt3);

  if (!is_atomic)
    {
      gsi_insert_on_edge (e, stmt1);
      gsi_insert_on_edge (e, stmt2);
    }
  gsi_insert_on_edge (e, stmt3);

}

/* Emits code to get VALUE to instrument at GSI, and returns the
   variable containing the value.  */

static tree
prepare_instrumented_value (gimple_stmt_iterator *gsi, histogram_value value)
{
  tree val = value->hvalue.value;
  if (POINTER_TYPE_P (TREE_TYPE (val)))
    val = fold_convert (build_nonstandard_integer_type
			  (TYPE_PRECISION (TREE_TYPE (val)), 1), val);
  return force_gimple_operand_gsi (gsi, fold_convert (gcov_type_node, val),
				   true, NULL_TREE, true, GSI_SAME_STMT);
}

/* Output instructions as GIMPLE trees to increment the interval histogram
   counter.  VALUE is the expression whose value is profiled.  TAG is the
   tag of the section for counters, BASE is offset of the counter position.  */

void
gimple_gen_interval_profiler (histogram_value value, unsigned tag, unsigned base)
{
  gimple stmt = value->hvalue.stmt;
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  tree ref = tree_coverage_counter_ref (tag, base), ref_ptr;
  gimple call;
  tree val;
  tree start = build_int_cst_type (integer_type_node,
				   value->hdata.intvl.int_start);
  tree steps = build_int_cst_type (unsigned_type_node,
				   value->hdata.intvl.steps);

  ref_ptr = force_gimple_operand_gsi (&gsi,
				      build_addr (ref, current_function_decl),
				      true, NULL_TREE, true, GSI_SAME_STMT);
  val = prepare_instrumented_value (&gsi, value);
  call = gimple_build_call (tree_interval_profiler_fn, 4,
			    ref_ptr, val, start, steps);
  gsi_insert_before (&gsi, call, GSI_NEW_STMT);
}

/* Output instructions as GIMPLE trees to increment the power of two histogram
   counter.  VALUE is the expression whose value is profiled.  TAG is the tag
   of the section for counters, BASE is offset of the counter position.  */

void
gimple_gen_pow2_profiler (histogram_value value, unsigned tag, unsigned base)
{
  gimple stmt = value->hvalue.stmt;
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  tree ref_ptr = tree_coverage_counter_addr (tag, base);
  gimple call;
  tree val;

  ref_ptr = force_gimple_operand_gsi (&gsi, ref_ptr,
				      true, NULL_TREE, true, GSI_SAME_STMT);
  val = prepare_instrumented_value (&gsi, value);
  call = gimple_build_call (tree_pow2_profiler_fn, 2, ref_ptr, val);
  gsi_insert_before (&gsi, call, GSI_NEW_STMT);
}

/* Output instructions as GIMPLE trees for code to find the most common value.
   VALUE is the expression whose value is profiled.  TAG is the tag of the
   section for counters, BASE is offset of the counter position.  */

void
gimple_gen_one_value_profiler (histogram_value value, unsigned tag, unsigned base)
{
  gimple stmt = value->hvalue.stmt;
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  tree ref_ptr = tree_coverage_counter_addr (tag, base);
  gimple call;
  tree val;

  ref_ptr = force_gimple_operand_gsi (&gsi, ref_ptr,
				      true, NULL_TREE, true, GSI_SAME_STMT);
  val = prepare_instrumented_value (&gsi, value);
  call = gimple_build_call (tree_one_value_profiler_fn, 2, ref_ptr, val);
  gsi_insert_before (&gsi, call, GSI_NEW_STMT);
}


/* Output instructions as GIMPLE trees for code to find the most
   common called function in indirect call.
   VALUE is the call expression whose indirect callee is profiled.
   TAG is the tag of the section for counters, BASE is offset of the
   counter position.  */

void
gimple_gen_ic_profiler (histogram_value value, unsigned tag, unsigned base)
{
  tree tmp1;
  gimple stmt1, stmt2, stmt3;
  gimple stmt;
  gimple_stmt_iterator gsi;
  tree ref_ptr;

  stmt = value->hvalue.stmt;
  gsi = gsi_for_stmt (stmt);
  ref_ptr = tree_coverage_counter_addr (tag, base);
  ref_ptr = force_gimple_operand_gsi (&gsi, ref_ptr,
				      true, NULL_TREE, true, GSI_SAME_STMT);

  /* Insert code:

    stmt1: __gcov_indirect_call_counters = get_relevant_counter_ptr ();
    stmt2: tmp1 = (void *) (indirect call argument value)
    stmt3: __gcov_indirect_call_callee = tmp1;
   */

  stmt1 = gimple_build_assign (ic_gcov_type_ptr_var, ref_ptr);
  tmp1 = make_temp_ssa_name (ptr_void, NULL, "PROF");
  stmt2 = gimple_build_assign (tmp1, unshare_expr (value->hvalue.value));
  stmt3 = gimple_build_assign (ic_void_ptr_var, gimple_assign_lhs (stmt2));

  gsi_insert_before (&gsi, stmt1, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt2, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt3, GSI_SAME_STMT);
}


/* Output instructions as GIMPLE trees for code to find the most
   common called function in indirect call. Insert instructions at the
   beginning of every possible called function.
  */

void
gimple_gen_ic_func_profiler (void)
{
  struct cgraph_node * c_node = cgraph_get_create_node (current_function_decl);
  gimple_stmt_iterator gsi;
  gimple stmt1, stmt2;
  tree tree_uid, cur_func, void0;

  if (cgraph_only_called_directly_p (c_node))
    return;

  gimple_init_edge_profiler ();

  /* Insert code:

    stmt1: __gcov_indirect_call_profiler_v2 (profile_id,
					     &current_function_decl)
   */
  gsi =
					     gsi_after_labels (split_edge (single_succ_edge (ENTRY_BLOCK_PTR_FOR_FN (cfun))));

  cur_func = force_gimple_operand_gsi (&gsi,
				       build_addr (current_function_decl,
						   current_function_decl),
				       true, NULL_TREE,
				       true, GSI_SAME_STMT);
  tree_uid = build_int_cst
	      (gcov_type_node, cgraph_get_node (current_function_decl)->profile_id);
  /* Workaround for binutils bug 14342.  Once it is fixed, remove lto path.  */
  if (flag_lto)
    {
      tree counter_ptr, ptr_var;
      counter_ptr = force_gimple_operand_gsi (&gsi, ic_gcov_type_ptr_var,
					      true, NULL_TREE, true,
					      GSI_SAME_STMT);
      ptr_var = force_gimple_operand_gsi (&gsi, ic_void_ptr_var,
					  true, NULL_TREE, true,
					  GSI_SAME_STMT);

      stmt1 = gimple_build_call (tree_indirect_call_profiler_fn, 4,
				 counter_ptr, tree_uid, cur_func, ptr_var);
    }
  else
    {
      stmt1 = gimple_build_call (tree_indirect_call_profiler_fn, 2,
				 tree_uid, cur_func);
    }
  gsi_insert_before (&gsi, stmt1, GSI_SAME_STMT);

  /* Set __gcov_indirect_call_callee to 0,
     so that calls from other modules won't get misattributed
     to the last caller of the current callee. */
  void0 = build_int_cst (build_pointer_type (void_type_node), 0);
  stmt2 = gimple_build_assign (ic_void_ptr_var, void0);
  gsi_insert_before (&gsi, stmt2, GSI_SAME_STMT);
}

/* Output instructions as GIMPLE trees for code to find the most
   common called function in indirect call. Insert instructions at the
   beginning of every possible called function.
  */

static void
gimple_gen_ic_func_topn_profiler (void)
{
  gimple_stmt_iterator gsi;
  gimple stmt1;
  tree cur_func, gcov_info, cur_func_id;

  if (DECL_STATIC_CONSTRUCTOR (current_function_decl)
      || DECL_STATIC_CONSTRUCTOR (current_function_decl)
      || DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (current_function_decl))
    return;

  gimple_init_edge_profiler ();

  gsi = gsi_after_labels (single_succ (ENTRY_BLOCK_PTR_FOR_FN (cfun)));

  cur_func = force_gimple_operand_gsi (&gsi,
				       build_addr (current_function_decl,
						   current_function_decl),
				       true, NULL_TREE,
				       true, GSI_SAME_STMT);
  gcov_info = build_fold_addr_expr (gcov_info_decl);
  cur_func_id = build_int_cst (get_gcov_unsigned_t (),
			       FUNC_DECL_FUNC_ID (cfun));
  stmt1 = gimple_build_call (tree_indirect_call_topn_profiler_fn,
			     3, cur_func, gcov_info, cur_func_id);
  gsi_insert_before (&gsi, stmt1, GSI_SAME_STMT);
}


/* Output instructions as GIMPLE trees for code to find the number of
   calls at each direct call site.
   BASE is offset of the counter position, CALL_STMT is the direct call
   whose call-count is profiled.  */

static void
gimple_gen_dc_profiler (unsigned base, gimple call_stmt)
{
  gimple stmt1, stmt2, stmt3;
  gimple_stmt_iterator gsi = gsi_for_stmt (call_stmt);
  tree tmp1, tmp2, tmp3, callee = gimple_call_fn (call_stmt);

  /* Insert code:
     __gcov_direct_call_counters = get_relevant_counter_ptr ();
     __gcov_callee = (void *) callee;
   */
  tmp1 = tree_coverage_counter_addr (GCOV_COUNTER_DIRECT_CALL, base);
  tmp1 = force_gimple_operand_gsi (&gsi, tmp1, true, NULL_TREE,
				   true, GSI_SAME_STMT);
  stmt1 = gimple_build_assign (dc_gcov_type_ptr_var, tmp1);
  tmp2 = create_tmp_var (ptr_void, "PROF_dc");
  stmt2 = gimple_build_assign (tmp2, unshare_expr (callee));
  tmp3 = make_ssa_name (tmp2, stmt2);
  gimple_assign_set_lhs (stmt2, tmp3);
  stmt3 = gimple_build_assign (dc_void_ptr_var, tmp3);
  gsi_insert_before (&gsi, stmt1, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt2, GSI_SAME_STMT);
  gsi_insert_before (&gsi, stmt3, GSI_SAME_STMT);
}


/* Output instructions as GIMPLE trees for code to find the number of
   calls at each direct call site. Insert instructions at the beginning of
   every possible called function.  */

static void
gimple_gen_dc_func_profiler (void)
{
  gimple_stmt_iterator gsi;
  gimple stmt1;
  tree cur_func, gcov_info, cur_func_id;

  if (DECL_STATIC_CONSTRUCTOR (current_function_decl) 
      || DECL_STATIC_CONSTRUCTOR (current_function_decl)
      || DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (current_function_decl))
    return;

  gimple_init_edge_profiler ();

  gsi = gsi_after_labels (single_succ (ENTRY_BLOCK_PTR_FOR_FN (cfun)));

  cur_func = force_gimple_operand_gsi (&gsi,
				       build_addr (current_function_decl,
						   current_function_decl),
				       true, NULL_TREE,
				       true, GSI_SAME_STMT);
  gcov_info = build_fold_addr_expr (gcov_info_decl);
  cur_func_id = build_int_cst (get_gcov_unsigned_t (),
			       FUNC_DECL_FUNC_ID (cfun));
  stmt1 = gimple_build_call (tree_direct_call_profiler_fn, 3, cur_func,
			     gcov_info, cur_func_id);
  gsi_insert_before (&gsi, stmt1, GSI_SAME_STMT);
}

/* Output instructions as GIMPLE tree at the beginning for each function.
   TAG is the tag of the section for counters, BASE is offset of the
   counter position and GSI is the iterator we place the counter.  */

void
gimple_gen_time_profiler (unsigned tag, unsigned base,
                          gimple_stmt_iterator &gsi)
{
  tree ref_ptr = tree_coverage_counter_addr (tag, base);
  gimple call;

  ref_ptr = force_gimple_operand_gsi (&gsi, ref_ptr,
				      true, NULL_TREE, true, GSI_SAME_STMT);
  call = gimple_build_call (tree_time_profiler_fn, 1, ref_ptr);
  gsi_insert_before (&gsi, call, GSI_NEW_STMT);
}

/* Output instructions as GIMPLE trees for code to find the most common value
   of a difference between two evaluations of an expression.
   VALUE is the expression whose value is profiled.  TAG is the tag of the
   section for counters, BASE is offset of the counter position.  */

void
gimple_gen_const_delta_profiler (histogram_value value ATTRIBUTE_UNUSED,
			       unsigned tag ATTRIBUTE_UNUSED,
			       unsigned base ATTRIBUTE_UNUSED)
{
  /* FIXME implement this.  */
#ifdef ENABLE_CHECKING
  internal_error ("unimplemented functionality");
#endif
  gcc_unreachable ();
}

/* Output instructions as GIMPLE trees to increment the average histogram
   counter.  VALUE is the expression whose value is profiled.  TAG is the
   tag of the section for counters, BASE is offset of the counter position.  */

void
gimple_gen_average_profiler (histogram_value value, unsigned tag, unsigned base)
{
  gimple stmt = value->hvalue.stmt;
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  tree ref_ptr = tree_coverage_counter_addr (tag, base);
  gimple call;
  tree val;

  ref_ptr = force_gimple_operand_gsi (&gsi, ref_ptr,
				      true, NULL_TREE,
				      true, GSI_SAME_STMT);
  val = prepare_instrumented_value (&gsi, value);
  call = gimple_build_call (tree_average_profiler_fn, 2, ref_ptr, val);
  gsi_insert_before (&gsi, call, GSI_NEW_STMT);
}

/* Output instructions as GIMPLE trees to increment the ior histogram
   counter.  VALUE is the expression whose value is profiled.  TAG is the
   tag of the section for counters, BASE is offset of the counter position.  */

void
gimple_gen_ior_profiler (histogram_value value, unsigned tag, unsigned base)
{
  gimple stmt = value->hvalue.stmt;
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  tree ref_ptr = tree_coverage_counter_addr (tag, base);
  gimple call;
  tree val;

  ref_ptr = force_gimple_operand_gsi (&gsi, ref_ptr,
				      true, NULL_TREE, true, GSI_SAME_STMT);
  val = prepare_instrumented_value (&gsi, value);
  call = gimple_build_call (tree_ior_profiler_fn, 2, ref_ptr, val);
  gsi_insert_before (&gsi, call, GSI_NEW_STMT);
}

/* Profile all functions in the callgraph.  */

static unsigned int
tree_profiling (void)
{
  struct cgraph_node *node;

  /* This is a small-ipa pass that gets called only once, from
     cgraphunit.c:ipa_passes().  */
  gcc_assert (cgraph_state == CGRAPH_STATE_IPA_SSA);

  /* After value profile transformation, artificial edges (that keep
     function body from being deleted) won't be needed.  */
  if (L_IPO_COMP_MODE) 
    lipo_link_and_fixup ();
  else
    init_node_map (true);


  FOR_EACH_DEFINED_FUNCTION (node)
    {
      if (!gimple_has_body_p (node->decl))
	continue;

      /* Don't profile functions produced for builtin stuff.  */
      if (DECL_SOURCE_LOCATION (node->decl) == BUILTINS_LOCATION)
	continue;

      push_cfun (DECL_STRUCT_FUNCTION (node->decl));

      if (flag_emit_function_names)
        emit_function_name ();

      /* Local pure-const may imply need to fixup the cfg.  */
      if (execute_fixup_cfg () & TODO_cleanup_cfg)
	cleanup_tree_cfg ();

      branch_prob ();

      if (! flag_branch_probabilities
	  && flag_profile_values
	  && !flag_dyn_ipa)
	gimple_gen_ic_func_profiler ();

      if (flag_branch_probabilities
	  && flag_profile_values
	  && flag_value_profile_transformations)
	gimple_value_profile_transformations ();

      /* The above could hose dominator info.  Currently there is
	 none coming in, this is a safety valve.  It should be
	 easy to adjust it, if and when there is some.  */
      free_dominance_info (CDI_DOMINATORS);
      free_dominance_info (CDI_POST_DOMINATORS);
      pop_cfun ();
    }

  /* Drop pure/const flags from instrumented functions.  */
  FOR_EACH_DEFINED_FUNCTION (node)
    {
      if (!gimple_has_body_p (node->decl)
	  || !(!node->clone_of
	  || node->decl != node->clone_of->decl))
	continue;

      /* Don't profile functions produced for builtin stuff.  */
      if (DECL_SOURCE_LOCATION (node->decl) == BUILTINS_LOCATION)
	continue;

      cgraph_set_const_flag (node, false, false);
      cgraph_set_pure_flag (node, false, false);
    }

  /* Update call statements and rebuild the cgraph.  */
  FOR_EACH_DEFINED_FUNCTION (node)
    {
      basic_block bb;

      if (!gimple_has_body_p (node->decl)
	  || !(!node->clone_of
	  || node->decl != node->clone_of->decl))
	continue;

      /* Don't profile functions produced for builtin stuff.  */
      if (DECL_SOURCE_LOCATION (node->decl) == BUILTINS_LOCATION)
	continue;

      push_cfun (DECL_STRUCT_FUNCTION (node->decl));

      FOR_EACH_BB_FN (bb, cfun)
	{
	  gimple_stmt_iterator gsi;
	  for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      gimple stmt = gsi_stmt (gsi);
	      if (is_gimple_call (stmt))
                update_stmt (stmt);
	    }
	}

      /* re-merge split blocks.  */
      cleanup_tree_cfg ();
      update_ssa (TODO_update_ssa);

      rebuild_cgraph_edges ();
      pop_cfun ();
    }

  handle_missing_profiles ();

  del_node_map ();
  cleanup_instrumentation_sampling();
  return 0;
}

/* Return true if tree-based direct-call profiling is in effect, else false.  */

static bool
do_direct_call_profiling (void)
{
  return !flag_branch_probabilities
    && (profile_arc_flag || flag_test_coverage)
    && flag_dyn_ipa;
}

/* Instrument current function to collect direct call profile information.  */

static unsigned int
direct_call_profiling (void)
{
  basic_block bb;
  gimple_stmt_iterator gsi;

  /* Add code:
     extern gcov* __gcov_direct_call_counters; // pointer to actual counter
     extern void* __gcov_direct_call_callee;   // actual callee address
  */
  if (!dc_gcov_type_ptr_var)
    {
      dc_gcov_type_ptr_var
	= build_decl (UNKNOWN_LOCATION, VAR_DECL,
		      get_identifier ("__gcov_direct_call_counters"),
		      build_pointer_type (gcov_type_node));
      DECL_ARTIFICIAL (dc_gcov_type_ptr_var) = 1;
      DECL_EXTERNAL (dc_gcov_type_ptr_var) = 1;
      if (targetm.have_tls && !is_kernel_build)
        DECL_TLS_MODEL (dc_gcov_type_ptr_var) =
          decl_default_tls_model (dc_gcov_type_ptr_var);

      dc_void_ptr_var =
	build_decl (UNKNOWN_LOCATION, VAR_DECL,
	            get_identifier ("__gcov_direct_call_callee"),
		    ptr_void);
      DECL_ARTIFICIAL (dc_void_ptr_var) = 1;
      DECL_EXTERNAL (dc_void_ptr_var) = 1;
      if (targetm.have_tls && !is_kernel_build)
        DECL_TLS_MODEL (dc_void_ptr_var) =
          decl_default_tls_model (dc_void_ptr_var);
    }

  if (!DECL_STATIC_CONSTRUCTOR (current_function_decl))
    {
      FOR_EACH_BB_FN (bb, cfun)
	for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
	  {
	    gimple stmt = gsi_stmt (gsi);
	    /* Check if this is a direct call, and not a builtin call.  */
	    if (gimple_code (stmt) != GIMPLE_CALL
		|| gimple_call_fndecl (stmt) == NULL_TREE
		|| DECL_BUILT_IN (gimple_call_fndecl (stmt))
		|| DECL_IS_BUILTIN (gimple_call_fndecl (stmt)))
	      continue;

            if (PARAM_VALUE (PARAM_LIPO_SKIP_SPECIAL_SECTIONS))
            {
              tree callee = gimple_call_fndecl (stmt);
              if (DECL_IS_MALLOC (callee)
                  || DECL_IS_OPERATOR_NEW (callee)
                  || (DECL_ASSEMBLER_NAME_SET_P (callee)
                      && (!strcmp (IDENTIFIER_POINTER (
                          DECL_ASSEMBLER_NAME (callee)), "_ZdlPv")
                          || !strcmp (IDENTIFIER_POINTER (
                              DECL_ASSEMBLER_NAME (callee)), "_ZdaPv"))))
                continue;
            }

	    if (!coverage_counter_alloc (GCOV_COUNTER_DIRECT_CALL, 2))
	      continue;
	    gimple_gen_dc_profiler (0, stmt);
	  }
      coverage_dc_end_function ();
    }

  if (coverage_function_present (FUNC_DECL_FUNC_ID (cfun)))
    {
      gimple_gen_dc_func_profiler ();
      if (! flag_branch_probabilities
          && flag_profile_values)
        gimple_gen_ic_func_topn_profiler ();
    }

  return 0;
}

/* When profile instrumentation, use or test coverage shall be performed.  */

static bool
gate_tree_profile_ipa (void)
{
  return (!in_lto_p && !flag_auto_profile
	  && (flag_branch_probabilities || flag_test_coverage
	      || profile_arc_flag));
}

namespace {

const pass_data pass_data_ipa_tree_profile =
{
  SIMPLE_IPA_PASS, /* type */
  "profile", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  true, /* has_gate */
  true, /* has_execute */
  TV_IPA_PROFILE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_ipa_tree_profile : public simple_ipa_opt_pass
{
public:
  pass_ipa_tree_profile (gcc::context *ctxt)
    : simple_ipa_opt_pass (pass_data_ipa_tree_profile, ctxt)
  {}

  /* opt_pass methods: */
  bool gate () { return gate_tree_profile_ipa (); }
  unsigned int execute () { return tree_profiling (); }

}; // class pass_ipa_tree_profile

const pass_data pass_data_direct_call_profile =
{
  GIMPLE_PASS,
  "dc_profile",				/* name */
  OPTGROUP_NONE,                        /* optinfo_flags */
  true, /* has_gate */
  true, /* has_execute */
  TV_BRANCH_PROB,			/* tv_id */
  ( PROP_ssa | PROP_cfg),		/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_update_ssa                      	/* todo_flags_finish */
};
class pass_direct_call_profile : public gimple_opt_pass
{
public:
  pass_direct_call_profile (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_direct_call_profile, ctxt)
  {}

  /* opt_pass methods: */
  opt_pass * clone () { return new pass_direct_call_profile (m_ctxt); }
  bool gate () { return do_direct_call_profiling (); }
  unsigned int execute () { return direct_call_profiling (); }

}; // class pass_direct_call_profiling


} // anon namespace

simple_ipa_opt_pass *
make_pass_ipa_tree_profile (gcc::context *ctxt)
{
  return new pass_ipa_tree_profile (ctxt);
}

gimple_opt_pass *
make_pass_direct_call_profile (gcc::context *ctxt)
{
  return new pass_direct_call_profile (ctxt);
}

#include "gt-tree-profile.h"
