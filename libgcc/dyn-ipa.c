/* Compile this one with gcc.  */
/* Copyright (C) 2009. Free Software Foundation, Inc.
   Contributed by Xinliang David Li (davidxl@google.com) and
                  Raksit Ashok  (raksit@google.com)

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

struct dyn_pointer_set;

#ifndef IN_GCOV_TOOL
#define XNEWVEC(type,ne) (type *)malloc(sizeof(type) * (ne))
#define XCNEWVEC(type,ne) (type *)calloc(1, sizeof(type) * (ne))
#define XNEW(type) (type *)malloc(sizeof(type))
#define XDELETEVEC(p) free(p)
#define XDELETE(p) free(p)
#endif

struct dyn_cgraph_node
{
  struct dyn_cgraph_edge *callees;
  struct dyn_cgraph_edge *callers;
  struct dyn_pointer_set *imported_modules;

  gcov_type guid;
  gcov_type sum_in_count;
  gcov_unsigned_t visited;
};

struct dyn_cgraph_edge
{
  struct dyn_cgraph_node *caller;
  struct dyn_cgraph_node *callee;
  struct dyn_cgraph_edge *next_caller;
  struct dyn_cgraph_edge *next_callee;
  gcov_type count;
  int indirect;
};

struct dyn_module_info
{
  struct dyn_pointer_set *imported_modules;
  gcov_unsigned_t max_func_ident;

  /* Used by new algorithm. This dyn_pointer_set only
     stored the gcov_info pointer, keyed by
     module ident.  */
  struct dyn_pointer_set *exported_to;
  gcov_unsigned_t group_ggc_mem;
};

struct dyn_cgraph
{
  struct dyn_pointer_set **call_graph_nodes;
  struct gcov_info **modules;
  /* supplement module information  */
  struct dyn_module_info *sup_modules;
  unsigned num_modules;
  unsigned num_nodes_executed;
  /* used by new algorithm  */
  struct modu_node *modu_nodes;
  /* Set indexed by lineno_checksum, returns a linked list of
     checksum_alias_info structs.  */
  struct dyn_pointer_set *lineno_pointer_sets;
};

/* Struct holding information for functions with the same lineno_checksum.  */
struct lineno_checksum_alias
{
  struct checksum_alias_info *cfg_checksum_list;
  unsigned lineno_checksum;
};

/* Struct holding information about functions with the same lineno and cfg
   checksums.  */
struct checksum_alias_info
{
  struct checksum_alias_info *next_cfg_checksum;
  struct checksum_alias *alias_list;
  unsigned cfg_checksum;
};

/* Implements list of guid and corresponding fi_ptr for functions with matching
   checksums.  */
struct checksum_alias
{
  struct checksum_alias *next_alias;
  gcov_type guid;
  const struct gcov_fn_info *fi_ptr;
  /* Non-NULL pointer to flag if this function has all-zero arc counts, to be
     set if we perform fixup.  */
  char *zero_count_fixup;
};

/* Module info is stored in dyn_caph->sup_modules
   which is indexed by m_ix.  */
struct modu_node
{
  struct gcov_info *module;
  struct modu_edge *callees;
  struct modu_edge *callers;
};

struct modu_edge
{
  struct modu_node *caller;
  struct modu_node *callee;
  struct modu_edge *next_caller;
  struct modu_edge *next_callee;
  unsigned n_edges;  /* used when combining edges */
  gcov_type sum_count;
  unsigned char visited;
};

struct dyn_pointer_set
{
  size_t log_slots;
  size_t n_slots;		/* n_slots = 2^log_slots */
  size_t n_elements;

  void **slots;
  unsigned (*get_key) (const void *);
};

typedef long dyn_fibheapkey_t;

typedef struct dyn_fibheap
{
  size_t nodes;
  struct fibnode *min;
  struct fibnode *root;
} *dyn_fibheap_t;

typedef struct fibnode
{
  struct fibnode *parent;
  struct fibnode *child;
  struct fibnode *left;
  struct fibnode *right;
  dyn_fibheapkey_t key;
  void *data;
  unsigned int degree : 31;
  unsigned int mark : 1;
} *fibnode_t;

static dyn_fibheap_t dyn_fibheap_new (void);
static fibnode_t dyn_fibheap_insert (dyn_fibheap_t, dyn_fibheapkey_t, void *);
static void *dyn_fibheap_extract_min (dyn_fibheap_t);

extern gcov_unsigned_t __gcov_lipo_cutoff;
extern gcov_unsigned_t __gcov_lipo_random_seed;
extern gcov_unsigned_t __gcov_lipo_random_group_size;
extern gcov_unsigned_t __gcov_lipo_propagate_scale;
extern gcov_unsigned_t __gcov_lipo_dump_cgraph;
extern gcov_unsigned_t __gcov_lipo_max_mem;
extern gcov_unsigned_t __gcov_lipo_comdat_algorithm;
extern gcov_unsigned_t __gcov_lipo_grouping_algorithm;
extern gcov_unsigned_t __gcov_lipo_merge_modu_edges;
extern gcov_unsigned_t __gcov_lipo_weak_inclusion;

#if defined(inhibit_libc)
void __gcov_build_callgraph (char **zero_counts) {}
#else

int __gcov_compute_module_groups (char **zero_counts) ATTRIBUTE_HIDDEN;
void __gcov_finalize_dyn_callgraph (void) ATTRIBUTE_HIDDEN;
static void gcov_dump_callgraph (gcov_type);
static void gcov_dump_cgraph_node_short (struct dyn_cgraph_node *node);
static void gcov_dump_cgraph_node (struct dyn_cgraph_node *node,
                                  unsigned m, unsigned f);
static int do_cgraph_dump (void);

static void
gcov_dump_cgraph_node_dot (struct dyn_cgraph_node *node,
                           unsigned m, unsigned f,
                           gcov_type cutoff_count);
static void
pointer_set_destroy (struct dyn_pointer_set *pset);
static void
pointer_set_destroy_not_free_value_pointer (struct dyn_pointer_set *);
static void **
pointer_set_find_or_insert (struct dyn_pointer_set *pset, unsigned key);
static struct dyn_pointer_set *
pointer_set_create (unsigned (*get_key) (const void *));

static struct dyn_cgraph the_dyn_call_graph;
static int total_zero_count = 0;
static int total_insane_count = 0;
static int fixup_type = 0;

enum GROUPING_ALGORITHM
{
  EAGER_PROPAGATION_ALGORITHM=0,
  INCLUSION_BASED_PRIORITY_ALGORITHM
};
static int flag_alg_mode;
static int flag_modu_merge_edges;
static int flag_weak_inclusion;
static int flag_use_existing_grouping;
static gcov_unsigned_t mem_threshold;

gcov_type
gcov_find_new_ic_target (gcov_type caller_guid, gcov_type callee_guid);
void
__gcov_dyn_ipa_merge_add (gcov_type *dest, gcov_type *src, unsigned n_counters);
void
__gcov_dyn_ipa_merge_ior (gcov_type *dest, gcov_type *src, unsigned n_counters);
void
__gcov_dyn_ipa_merge_dc (gcov_type *dest, gcov_type *src, unsigned n_counters);
void
__gcov_dyn_ipa_merge_icall_topn (gcov_type *dest, gcov_type *src,
                                 unsigned n_counters);
void
__gcov_dyn_ipa_merge_single (gcov_type *dest, gcov_type *src,
                             unsigned n_counters);
void
__gcov_dyn_ipa_merge_delta (gcov_type *dest, gcov_type *src,
                            unsigned n_counters);

/* Returns 0 if no dump is enabled. Returns 1 if text form graph
   dump is enabled. Returns 2 if .dot form dump is enabled.  */

static int
do_cgraph_dump (void)
{
  const char *dyn_cgraph_dump = 0;

  if (__gcov_lipo_dump_cgraph)
    return __gcov_lipo_dump_cgraph;

  dyn_cgraph_dump = getenv ("GCOV_DYN_CGRAPH_DUMP");

  if (!dyn_cgraph_dump || !strlen (dyn_cgraph_dump))
     return 0;

  if (dyn_cgraph_dump[0] == '1')
     return 1;
  if (dyn_cgraph_dump[0] == '2')
     return 2;

  return 0;
}

static void
init_dyn_cgraph_node (struct dyn_cgraph_node *node, gcov_type guid)
{
  node->callees = 0;
  node->callers = 0;
  node->imported_modules = 0;
  node->guid = guid;
  node->visited = 0;
}

/* Return module_id. FUNC_GUID is the global unique id.  
   This id is 1 based. 0 is the invalid id.  */

static inline gcov_unsigned_t
get_module_ident_from_func_glob_uid (gcov_type func_guid)
{
  return EXTRACT_MODULE_ID_FROM_GLOBAL_ID (func_guid);
}

/* Return module_id for MODULE_INFO.  */

static inline gcov_unsigned_t
get_module_ident (const struct gcov_info *module_info)
{
  return module_info->mod_info->ident;
}

/* Return intra-module function id given function global unique id
   FUNC_GUID.  */

static inline gcov_unsigned_t
get_intra_module_func_id (gcov_type func_guid)
{
  return EXTRACT_FUNC_ID_FROM_GLOBAL_ID (func_guid);
}

/* Return the pointer to the dynamic call graph node for FUNC_GUID.  */

static inline struct dyn_cgraph_node *
get_cgraph_node (gcov_type func_guid)
{
  gcov_unsigned_t mod_idx, func_id;

  mod_idx = get_module_ident_from_func_glob_uid (func_guid) - 1;

  /* This is to workaround: calls in __static_initialization_and_destruction
     should not be instrumented as the module id context for the callees have
     not setup yet -- this leads to mod_idx == (unsigned) (0 - 1). Multithreaded
     programs may also produce insane func_guid in the profile counter.  */
  if (mod_idx >= the_dyn_call_graph.num_modules)
    return 0;

  func_id = get_intra_module_func_id (func_guid);
  if (func_id > the_dyn_call_graph.sup_modules[mod_idx].max_func_ident)
    return 0;

  return (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
	   (the_dyn_call_graph.call_graph_nodes[mod_idx], func_id));
}

static inline unsigned
imp_mod_get_key (const void *p)
{
  return ((const struct dyn_imp_mod *) p)->imp_mod->mod_info->ident;
}

static int
imp_mod_set_insert (struct dyn_pointer_set *p, const struct gcov_info *imp_mod,
		    double wt)
{
  struct dyn_imp_mod **m = (struct dyn_imp_mod **)
    pointer_set_find_or_insert (p, get_module_ident (imp_mod));
  if (*m)
    {
      (*m)->weight += wt;
      return 1;
    }
  else
    {
      *m = XNEW (struct dyn_imp_mod);
      (*m)->imp_mod = imp_mod;
      (*m)->weight = wt;
      p->n_elements++;
      return 0;
    }
}

/* Return the gcov_info pointer for module with id MODULE_ID.  */

static inline struct gcov_info *
get_module_info (gcov_unsigned_t module_id)
{
  return the_dyn_call_graph.modules[module_id - 1];
}

struct gcov_info *__gcov_list ATTRIBUTE_HIDDEN;

static inline unsigned
cgraph_node_get_key (const void *p)
{
  return get_intra_module_func_id (((const struct dyn_cgraph_node *) p)->guid);
}

static inline unsigned
gcov_info_get_key (const void *p)
{
  return get_module_ident ((const struct gcov_info *)p);
}

/* The lineno_checksum value in P is the key for lineno_pointer_sets.  */

static inline unsigned
lineno_checksum_get_key (const void *p)
{
  return ((const struct lineno_checksum_alias *) p)->lineno_checksum;
}

/* Create a new checksum_alias struct for function with GUID, FI_PTR,
   and ZERO_COUNT_FIXUP flag pointer.  Prepends to list NEXT and returns
   new struct.  */

static struct checksum_alias *
new_checksum_alias (gcov_type guid, const struct gcov_fn_info *fi_ptr,
                    char *zero_count_fixup,
                    struct checksum_alias *next)
{
  struct checksum_alias *alias = XNEW (struct checksum_alias);
  alias->next_alias = next;
  alias->fi_ptr = fi_ptr;
  alias->guid = guid;
  alias->zero_count_fixup = zero_count_fixup;
  return alias;
}

/* Locate the checksum_alias_info in LIST that matches CFG_CHECKSUM.  */

static struct checksum_alias_info *
find_cfg_checksum (struct checksum_alias_info *list, unsigned cfg_checksum)
{
  for (; list; list = list->next_cfg_checksum)
    {
      if (list->cfg_checksum == cfg_checksum)
        return list;
    }
  return NULL;
}

/* Insert a new checksum_alias struct into LIST for function with
   CFG_CHECKSUM and associated GUID, FI_PTR, and ZERO_COUNT_FIXUP
   flag pointer.  */

static struct checksum_alias_info *
cfg_checksum_insert (unsigned cfg_checksum, gcov_type guid,
                     const struct gcov_fn_info *fi_ptr, char *zero_count_fixup,
                     struct checksum_alias_info *list)
{
  struct checksum_alias_info *alias_info;
  alias_info = find_cfg_checksum (list, cfg_checksum);
  if (alias_info)
    {
      gcc_assert (alias_info->alias_list);
      alias_info->alias_list = new_checksum_alias (guid, fi_ptr,
                                                   zero_count_fixup,
                                                   alias_info->alias_list);
      return list;
    }
  else
    {
      alias_info = XNEW (struct checksum_alias_info);
      alias_info->next_cfg_checksum = list;
      alias_info->cfg_checksum = cfg_checksum;
      alias_info->alias_list = new_checksum_alias (guid, fi_ptr,
                                                   zero_count_fixup,
                                                   NULL);
      return alias_info;
    }
}

/* Insert a new checksum_alias struct into lineno_pointer_sets for function with
   LINENO_CHECKSUM and CFG_CHECKSUM with associated GUID, FI_PTR, and
   ZERO_COUNT_FIXUP flag pointer.  */

static void
checksum_set_insert (unsigned lineno_checksum, unsigned cfg_checksum,
                     gcov_type guid, const struct gcov_fn_info *fi_ptr,
                     char *zero_count_fixup)
{
  struct dyn_pointer_set *p = the_dyn_call_graph.lineno_pointer_sets;
  if (!p)
    the_dyn_call_graph.lineno_pointer_sets = p =
        pointer_set_create (lineno_checksum_get_key);
  struct lineno_checksum_alias **m = (struct lineno_checksum_alias **)
    pointer_set_find_or_insert (p, lineno_checksum);
  if (*m)
    {
      (*m)->cfg_checksum_list = cfg_checksum_insert (cfg_checksum, guid,
                                                     fi_ptr, zero_count_fixup,
                                                     (*m)->cfg_checksum_list);
    }
  else
    {
      *m = XNEW (struct lineno_checksum_alias);
      (*m)->lineno_checksum = lineno_checksum;
      (*m)->cfg_checksum_list = cfg_checksum_insert (cfg_checksum, guid,
                                                     fi_ptr, zero_count_fixup,
                                                     NULL);
      p->n_elements++;
    }
}

static struct dyn_pointer_set *
get_exported_to (unsigned module_ident)
{
  gcc_assert (module_ident != 0);
  return the_dyn_call_graph.sup_modules[module_ident - 1].exported_to;
}

static struct dyn_pointer_set *
create_exported_to (unsigned module_ident)
{
  struct dyn_pointer_set *p;

  gcc_assert (module_ident != 0);
  p = pointer_set_create (gcov_info_get_key);
  the_dyn_call_graph.sup_modules[module_ident - 1].exported_to = p;
  return p;
}

static struct dyn_pointer_set *
get_imported_modus (unsigned module_ident)
{
  struct dyn_pointer_set *p;
  struct gcov_info *gi_ptr;

  gcc_assert (module_ident != 0);
  p = the_dyn_call_graph.sup_modules[module_ident - 1].imported_modules;

  if (p)
    return p;

  the_dyn_call_graph.sup_modules[module_ident - 1].imported_modules = p
    = pointer_set_create (imp_mod_get_key);

  gi_ptr = the_dyn_call_graph.modules[module_ident - 1];
  /* make the modules an auxiliay module to itself.  */
  imp_mod_set_insert (p, gi_ptr, 0);

  return p;
}

/* Initialize dynamic call graph.  */

static void
init_dyn_call_graph (void)
{
  unsigned num_modules = 0;
  unsigned max_module_id = 0;
  struct gcov_info *gi_ptr;
  const char *env_str;
  int do_dump = (do_cgraph_dump () != 0);

  the_dyn_call_graph.call_graph_nodes = 0;
  the_dyn_call_graph.modules = 0;
  the_dyn_call_graph.num_nodes_executed = 0;

  flag_alg_mode = __gcov_lipo_grouping_algorithm;
  flag_modu_merge_edges = __gcov_lipo_merge_modu_edges;
  flag_weak_inclusion = __gcov_lipo_weak_inclusion;
  mem_threshold = __gcov_lipo_max_mem * 1.25;

  gi_ptr = __gcov_list;

  for (; gi_ptr; gi_ptr = gi_ptr->next)
    {
      unsigned mod_id = get_module_ident (gi_ptr);
      num_modules++;
      if (max_module_id < mod_id)
        max_module_id = mod_id;
    }

  if (num_modules < max_module_id)
    num_modules = max_module_id;

  the_dyn_call_graph.num_modules = num_modules;

  the_dyn_call_graph.modules
    = XNEWVEC (struct gcov_info *, num_modules);
  memset (the_dyn_call_graph.modules, 0,
          num_modules * sizeof (struct gcov_info*));

  the_dyn_call_graph.sup_modules
    = XNEWVEC (struct dyn_module_info, num_modules);
  memset (the_dyn_call_graph.sup_modules, 0,
          num_modules * sizeof (struct dyn_module_info));

  the_dyn_call_graph.call_graph_nodes
    = XNEWVEC (struct dyn_pointer_set *, num_modules);

  gi_ptr = __gcov_list;

  if ((env_str = getenv ("GCOV_DYN_ALG")))
    {
      flag_alg_mode = atoi (env_str);

      if ((env_str = getenv ("GCOV_DYN_MERGE_EDGES")))
        flag_modu_merge_edges = atoi (env_str);

      if ((env_str = getenv ("GCOV_DYN_WEAK_INCLUSION")))
        flag_weak_inclusion = atoi (env_str);

      if (do_dump)
	fprintf (stderr, 
            "!!!! Using ALG=%d merge_edges=%d weak_inclusion=%d. \n",
            flag_alg_mode, flag_modu_merge_edges, flag_weak_inclusion);
    }

  if (do_dump)
    fprintf (stderr, "Group mem limit: %u KB \n",
             __gcov_lipo_max_mem);

  if (do_dump)
    fprintf (stderr, "COMDAT fixup algorithm: %u\n",
             __gcov_lipo_comdat_algorithm);

  for (; gi_ptr; gi_ptr = gi_ptr->next)
    {
      /* mod_idx is module_ident - 1.  */
      unsigned j, mod_id, mod_idx, max_func_ident = 0;
      struct dyn_cgraph_node *node;

      /* initialize flags field.  */
      gi_ptr->mod_info->flags = 0;

      mod_id = get_module_ident (gi_ptr);
      if (do_dump)
        fprintf (stderr, "Module %s %d uses %u KB memory in parsing\n",
	         gi_ptr->mod_info->source_filename, mod_id,
		 gi_ptr->mod_info->ggc_memory);

      if (mod_id == 0)
        {
          fprintf (stderr, "Bad module_ident of 0. Skipping.\n");
          continue;
        }
      mod_idx = mod_id - 1;

      the_dyn_call_graph.modules[mod_idx] = gi_ptr;

      the_dyn_call_graph.call_graph_nodes[mod_idx]
	= pointer_set_create (cgraph_node_get_key);

      for (j = 0; j < gi_ptr->n_functions; j++)
	{
          const struct gcov_fn_info *fi_ptr = gi_ptr->functions[j];
	  *(pointer_set_find_or_insert
	    (the_dyn_call_graph.call_graph_nodes[mod_idx], fi_ptr->ident))
	    = node = XNEW (struct dyn_cgraph_node);
	  the_dyn_call_graph.call_graph_nodes[mod_idx]->n_elements++;
	  init_dyn_cgraph_node (node, GEN_FUNC_GLOBAL_ID (gi_ptr->mod_info->ident,
							  fi_ptr->ident));
          if (fi_ptr->ident > max_func_ident)
            max_func_ident = fi_ptr->ident;
	}
      the_dyn_call_graph.sup_modules[mod_idx].max_func_ident = max_func_ident;
      if (flag_alg_mode == INCLUSION_BASED_PRIORITY_ALGORITHM)
        {
          struct dyn_module_info *sup_module =
	    &(the_dyn_call_graph.sup_modules[mod_idx]);

          sup_module->group_ggc_mem = gi_ptr->mod_info->ggc_memory;
          sup_module->imported_modules = 0;
          sup_module->exported_to = 0;
        }
    }
}

/* Free up memory allocated for dynamic call graph.  */

void
__gcov_finalize_dyn_callgraph (void)
{
  unsigned i;

  for (i = 0; i < the_dyn_call_graph.num_modules; i++)
    {
      struct gcov_info *gi_ptr = the_dyn_call_graph.modules[i];
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix;

      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
        {
          struct dyn_cgraph_node *node;
          struct dyn_cgraph_edge *callees, *next_callee;
          fi_ptr = gi_ptr->functions[f_ix];
          node = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
                   (the_dyn_call_graph.call_graph_nodes[i], fi_ptr->ident));
          gcc_assert (node);
          callees = node->callees;

          if (!callees)
            continue;
          while (callees != 0)
            {
              next_callee = callees->next_callee;
              XDELETE (callees);
              callees = next_callee;
            }
	  if (node->imported_modules)
	    pointer_set_destroy (node->imported_modules);
        }
      if (the_dyn_call_graph.call_graph_nodes[i])
        pointer_set_destroy (the_dyn_call_graph.call_graph_nodes[i]);
      /* Now delete sup modules */
      if (the_dyn_call_graph.sup_modules[i].imported_modules)
        pointer_set_destroy (the_dyn_call_graph.sup_modules[i].imported_modules);
      if (flag_alg_mode == INCLUSION_BASED_PRIORITY_ALGORITHM
          && the_dyn_call_graph.sup_modules[i].exported_to)
        pointer_set_destroy_not_free_value_pointer
          (the_dyn_call_graph.sup_modules[i].exported_to);
    }
  XDELETEVEC (the_dyn_call_graph.call_graph_nodes);
  XDELETEVEC (the_dyn_call_graph.sup_modules);
  XDELETEVEC (the_dyn_call_graph.modules);
}

/* Add outgoing edge OUT_EDGE for caller node CALLER.  */

static void
gcov_add_out_edge (struct dyn_cgraph_node *caller,
		   struct dyn_cgraph_edge *out_edge)
{
  if (!caller->callees)
    caller->callees = out_edge;
  else
    {
      out_edge->next_callee = caller->callees;
      caller->callees = out_edge;
    }
}

/* Add incoming edge IN_EDGE for callee node CALLEE.  */

static void
gcov_add_in_edge (struct dyn_cgraph_node *callee,
		  struct dyn_cgraph_edge *in_edge)
{
  if (!callee->callers)
    callee->callers = in_edge;
  else
    {
      in_edge->next_caller = callee->callers;
      callee->callers = in_edge;
    }
}

/* Add a call graph edge between caller CALLER and callee CALLEE.
   The edge count is COUNT and INDIRECT flags whether the call was
   direct or indirect.  */

static void
gcov_add_cgraph_edge (struct dyn_cgraph_node *caller,
		      struct dyn_cgraph_node *callee,
		      gcov_type count, int indirect)
{
  struct dyn_cgraph_edge *new_edge = XNEW (struct dyn_cgraph_edge);
  new_edge->caller = caller;
  new_edge->callee = callee;
  new_edge->count = count;
  new_edge->next_caller = 0;
  new_edge->next_callee = 0;
  new_edge->indirect = indirect;

  gcov_add_out_edge (caller, new_edge);
  gcov_add_in_edge (callee, new_edge);
}

/* Add call graph edges from direct calls for caller CALLER. DIR_CALL_COUNTERS
   is the array of call counters. N_COUNTS is the number of counters.  */

static void
gcov_build_callgraph_dc_fn (struct dyn_cgraph_node *caller,
                            gcov_type *dir_call_counters,
                            unsigned n_counts)
{
  unsigned i;

  for (i = 0; i < n_counts; i += 2)
    {
      struct dyn_cgraph_node *callee;
      gcov_type count;
      gcov_type callee_guid = dir_call_counters[i];

      count = dir_call_counters[i + 1];
      if (count == 0)
        {
          total_zero_count++;
          continue;
        }
      callee = get_cgraph_node (callee_guid);
      if (!callee)
        {
          total_insane_count++;
          continue;
        }
      gcov_add_cgraph_edge (caller, callee, count, 0);
    }
}

/* Add call graph edges from indirect calls for caller CALLER. ICALL_COUNTERS
   is the array of icall counters. N_COUNTS is the number of counters.  */

static void
gcov_build_callgraph_ic_fn (struct dyn_cgraph_node *caller,
                            gcov_type *icall_counters,
                            unsigned n_counts)
{
  unsigned i, j;

  for (i = 0; i < n_counts; i += GCOV_ICALL_TOPN_NCOUNTS)
    {
      gcov_type *value_array = &icall_counters[i + 1];
      for (j = 0; j < GCOV_ICALL_TOPN_NCOUNTS - 1; j += 2)
        {
          struct dyn_cgraph_node *callee;
          gcov_type count;
          gcov_type callee_guid = value_array[j];

          count = value_array[j + 1];
	  /* Do not update zero count edge count
	   * as it means there is no target in this entry.  */
          if (count == 0)
            continue;
          callee = get_cgraph_node (callee_guid);
          if (!callee)
	    {
              total_insane_count++;
              continue;
	    }
          gcov_add_cgraph_edge (caller, callee, count, 1);
        }
    }
}

/* Build the dynamic call graph and update ZERO_COUNTS flags.  */

static void
gcov_build_callgraph (char **zero_counts)
{
  struct gcov_info *gi_ptr;
  unsigned m_ix;

  init_dyn_call_graph ();

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix, i;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
        {
          struct dyn_cgraph_node *caller;
          const struct gcov_ctr_info *ci_ptr = 0;

          fi_ptr = gi_ptr->functions[f_ix];
          ci_ptr = fi_ptr->ctrs;

          caller = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
                    (the_dyn_call_graph.call_graph_nodes[m_ix],
                     fi_ptr->ident));
          gcc_assert (caller);

          for (i = 0; i < GCOV_COUNTERS; i++)
            {
              if (!gi_ptr->merge[i])
                continue;
              if (i == GCOV_COUNTER_DIRECT_CALL)
                gcov_build_callgraph_dc_fn (caller, ci_ptr->values, ci_ptr->num);

              if (i == GCOV_COUNTER_ICALL_TOPNV)
                gcov_build_callgraph_ic_fn (caller, ci_ptr->values, ci_ptr->num);

              if (i == GCOV_COUNTER_ARCS)
                {
                  gcov_type total_arc_count = 0;
                  unsigned arc;
                  for (arc = 0; arc < ci_ptr->num; arc++)
                    total_arc_count += ci_ptr->values[arc];
                  if (total_arc_count != 0)
                    the_dyn_call_graph.num_nodes_executed++;
                  if (fixup_type)
                    {
                      char *zero_count_fixup = NULL;
                      /* Passing in a non-NULL zero_count_fixup pointer
                         indicates that the counts were all zero for this
                         function, and the fixup routine will set the flag
                         if the function's counters are updated to non-zero
                         values.  */
                      if (total_arc_count == 0)
                        zero_count_fixup = &zero_counts[m_ix][f_ix];
                      checksum_set_insert (fi_ptr->lineno_checksum,
                                           fi_ptr->cfg_checksum, caller->guid,
                                           fi_ptr, zero_count_fixup);
                    }
                }
              ci_ptr++;
            }
        }
    }
}

static inline size_t
hash1 (unsigned p, unsigned long max, unsigned long logmax)
{
  const unsigned long long A = 0x9e3779b97f4a7c16ull;
  const unsigned long long shift = 64 - logmax;

  return ((A * (unsigned long) p) >> shift) & (max - 1);
}

/* Allocate an empty imported-modules set.  */

static struct dyn_pointer_set *
pointer_set_create (unsigned (*get_key) (const void *))
{
  struct dyn_pointer_set *result = XNEW (struct dyn_pointer_set);

  result->n_elements = 0;
  result->log_slots = 8;
  result->n_slots = (size_t) 1 << result->log_slots;

  result->slots = XNEWVEC (void *, result->n_slots);
  memset (result->slots, 0, sizeof (void *) * result->n_slots);
  result->get_key = get_key;

  return result;
}

/* Reclaim all memory associated with PSET.  */

static void
pointer_set_destroy (struct dyn_pointer_set *pset)
{
  size_t i;
  for (i = 0; i < pset->n_slots; i++)
    if (pset->slots[i])
      XDELETE (pset->slots[i]);
  XDELETEVEC (pset->slots);
  XDELETE (pset);
}

/* Reclaim the memory of PSET but not the value pointer.  */
static void
pointer_set_destroy_not_free_value_pointer (struct dyn_pointer_set *pset)
{
  XDELETEVEC (pset->slots);
  XDELETE (pset);
}

/* Subroutine of pointer_set_find_or_insert.  Return the insertion slot for KEY
   into an empty element of SLOTS, an array of length N_SLOTS.  */
static inline size_t
insert_aux (unsigned key, void **slots,
	    size_t n_slots, size_t log_slots,
	    unsigned (*get_key) (const void *))
{
  size_t n = hash1 (key, n_slots, log_slots);
  while (1)
    {
      if (slots[n] == 0 || get_key (slots[n]) == key)
	return n;
      else
	{
	  ++n;
	  if (n == n_slots)
	    n = 0;
	}
    }
}

/* Find slot for KEY. KEY must be nonnull.  */

static void **
pointer_set_find_or_insert (struct dyn_pointer_set *pset, unsigned key)
{
  size_t n;

  /* For simplicity, expand the set even if KEY is already there.  This can be
     superfluous but can happen at most once.  */
  if (pset->n_elements > pset->n_slots / 4)
    {
      size_t new_log_slots = pset->log_slots + 1;
      size_t new_n_slots = pset->n_slots * 2;
      void **new_slots = XNEWVEC (void *, new_n_slots);
      memset (new_slots, 0, sizeof (void *) * new_n_slots);
      size_t i;

      for (i = 0; i < pset->n_slots; ++i)
        {
	  void *value = pset->slots[i];
	  if (!value)
	    continue;
	  n = insert_aux (pset->get_key (value), new_slots, new_n_slots,
			  new_log_slots, pset->get_key);
	  new_slots[n] = value;
	}

      XDELETEVEC (pset->slots);
      pset->n_slots = new_n_slots;
      pset->log_slots = new_log_slots;
      pset->slots = new_slots;
    }

  n = insert_aux (key, pset->slots, pset->n_slots, pset->log_slots,
		  pset->get_key);
  return &pset->slots[n];
}


/* Pass each pointer in PSET to the function in FN, together with the fixed
   parameters DATA1, DATA2, DATA3.  If FN returns false, the iteration stops.  */

static void
pointer_set_traverse (const struct dyn_pointer_set *pset,
                      int (*fn) (const void *, void *, void *, void *),
		      void *data1, void *data2, void *data3)
{
  size_t i;
  for (i = 0; i < pset->n_slots; ++i)
    if (pset->slots[i] && !fn (pset->slots[i], data1, data2, data3))
      break;
}


/* Returns nonzero if PSET contains an entry with KEY as the key value.
   Collisions are resolved by linear probing.  */

static int
pointer_set_contains (const struct dyn_pointer_set *pset, unsigned key)
{
  size_t n = hash1 (key, pset->n_slots, pset->log_slots);

  while (1)
    {
      if (pset->slots[n] == 0)
       return 0;
      else if (pset->get_key (pset->slots[n]) == key)
       return 1;
      else
       {
         ++n;
         if (n == pset->n_slots)
           n = 0;
       }
    }
}

/* Callback function to propagate import module (VALUE) from callee to
   caller's imported-module-set (DATA1).
   The weight is scaled by the scaling-factor (DATA2) before propagation,
   and accumulated into DATA3.  */

static int
gcov_propagate_imp_modules (const void *value, void *data1, void *data2,
			    void *data3)
{
  const struct dyn_imp_mod *m = (const struct dyn_imp_mod *) value;
  struct dyn_pointer_set *receiving_set = (struct dyn_pointer_set *) data1;
  double *scale = (double *) data2;
  double *sum = (double *) data3;
  double wt = m->weight;
  if (scale)
    wt *= *scale;
  if (sum)
    (*sum) += wt;
  imp_mod_set_insert (receiving_set, m->imp_mod, wt);
  return 1;
}

static int
sort_by_count (const void *pa, const void *pb)
{
  const struct dyn_cgraph_edge *edge_a = *(struct dyn_cgraph_edge * const *)pa;
  const struct dyn_cgraph_edge *edge_b = *(struct dyn_cgraph_edge * const *)pb;

  /* This can overvlow.  */
  /* return edge_b->count - edge_a->count;  */
  if (edge_b->count > edge_a->count)
    return 1;
  else if (edge_b->count == edge_a->count)
    return 0;
  else
    return -1;
}

/* Compute the hot callgraph edge threhold.  */

static gcov_type
gcov_compute_cutoff_count (void)
{
  unsigned m_ix, capacity, i;
  unsigned num_edges = 0;
  gcov_type cutoff_count = 0;
  double total, cum, cum_cutoff;
  struct dyn_cgraph_edge **edges;
  struct gcov_info *gi_ptr;
  char *cutoff_str;
  char *num_perc_str;
  unsigned cutoff_perc;
  unsigned num_perc;
  int do_dump;

  capacity = 100;
  /* allocate an edge array */
  edges = XNEWVEC (struct dyn_cgraph_edge*, capacity);
  /* First count the number of edges.  */
  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	{
	  struct dyn_cgraph_node *node;
          struct dyn_cgraph_edge *callees;

	  fi_ptr = gi_ptr->functions[f_ix];

	  node = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
		   (the_dyn_call_graph.call_graph_nodes[m_ix], fi_ptr->ident));
	  gcc_assert (node);

          callees = node->callees;
          while (callees != 0)
            {
              num_edges++;
              if (num_edges < capacity)
                edges[num_edges - 1] = callees;
              else
                {
                  capacity = capacity + (capacity >> 1);
                  edges = (struct dyn_cgraph_edge **)xrealloc (edges, sizeof (void*) * capacity);
                  edges[num_edges - 1] = callees;
                }
              callees = callees->next_callee;
            }
	}
    }

  /* Now sort */
  qsort (edges, num_edges, sizeof (void *), sort_by_count);
#define CUM_CUTOFF_PERCENT 80
#define MIN_NUM_EDGE_PERCENT 0

  /* The default parameter value is 100 which is a reserved special value. When
     the cutoff parameter is 100, use the environment variable setting if it
     exists, otherwise, use the default value 80.  */
  if (__gcov_lipo_cutoff != 100)
    {
      cutoff_perc = __gcov_lipo_cutoff;
      num_perc = MIN_NUM_EDGE_PERCENT;
    }
  else
    {
      cutoff_str = getenv ("GCOV_DYN_CGRAPH_CUTOFF");
      if (cutoff_str && strlen (cutoff_str))
        {
          if ((num_perc_str = strchr (cutoff_str, ':')))
            {
              *num_perc_str = '\0';
              num_perc_str++;
            }
          cutoff_perc = atoi (cutoff_str);
          if (num_perc_str)
            num_perc = atoi (num_perc_str);
          else
            num_perc = MIN_NUM_EDGE_PERCENT;
        }
      else
        {
          cutoff_perc = CUM_CUTOFF_PERCENT;
          num_perc = MIN_NUM_EDGE_PERCENT;
        }
    }

  total = 0;
  cum = 0;
  for (i = 0; i < num_edges; i++)
    total += edges[i]->count;

  cum_cutoff = (total * cutoff_perc)/100;
  do_dump = (do_cgraph_dump () != 0);
  for (i = 0; i < num_edges; i++)
    {
      cum += edges[i]->count;
      if (do_dump)
        fprintf (stderr, "// edge[%d] count = %.0f [%llx --> %llx]\n",
                 i, (double) edges[i]->count,
                 (long long) edges[i]->caller->guid,
                 (long long) edges[i]->callee->guid);
      if (cum >= cum_cutoff && (i * 100 >= num_edges * num_perc))
        {
          cutoff_count = edges[i]->count;
          break;
        }
    }

  if (do_dump)
    fprintf (stderr,"cum count cutoff = %d%%, minimal num edge cutoff = %d%%\n",
             cutoff_perc, num_perc);

  if (do_dump)
    fprintf (stderr, "// total = %.0f cum = %.0f cum/total = %.0f%%"
             " cutoff_count = %lld [total edges: %d hot edges: %d perc: %d%%]\n"
	     " total_zero_count_edges = %d total_insane_count_edgess = %d\n"
             " total_nodes_executed = %d\n",
             total, cum, (cum * 100)/total, (long long) cutoff_count,
             num_edges, i, (i * 100)/num_edges, total_zero_count,
             total_insane_count, the_dyn_call_graph.num_nodes_executed);

  XDELETEVEC (edges);
  return cutoff_count;
}

/* Return the imported module set for NODE.  */

static struct dyn_pointer_set *
gcov_get_imp_module_set (struct dyn_cgraph_node *node)
{
  if (!node->imported_modules)
    node->imported_modules = pointer_set_create (imp_mod_get_key);

  return node->imported_modules;
}

/* Return the imported module set for MODULE MI.  */

static struct dyn_pointer_set *
gcov_get_module_imp_module_set (struct dyn_module_info *mi)
{
  if (!mi->imported_modules)
    mi->imported_modules = pointer_set_create (imp_mod_get_key);

  return mi->imported_modules;
}

/* Callback function to mark if a module needs to be exported.  */

static int
gcov_mark_export_modules (const void *value,
			  void *data1 ATTRIBUTE_UNUSED,
			  void *data2 ATTRIBUTE_UNUSED,
			  void *data3 ATTRIBUTE_UNUSED)
{
  const struct gcov_info *module_info
    = ((const struct dyn_imp_mod *) value)->imp_mod;

  SET_MODULE_EXPORTED (module_info->mod_info);
  return 1;
}

struct gcov_import_mod_array
{
  const struct dyn_imp_mod **imported_modules;
  struct gcov_info *importing_module;
  unsigned len;
};

/* Callback function to compute pointer set size.  */

static int
gcov_compute_mset_size (const void *value ATTRIBUTE_UNUSED,
                        void *data1,
			void *data2 ATTRIBUTE_UNUSED,
			void *data3 ATTRIBUTE_UNUSED)
{
  unsigned *len = (unsigned *) data1;
  (*len)++;
  return 1;
}

/* Callback function to collect imported modules.  */

static int
gcov_collect_imported_modules (const void *value,
			       void *data1,
			       void *data2 ATTRIBUTE_UNUSED,
			       void *data3 ATTRIBUTE_UNUSED)
{
  struct gcov_import_mod_array *out_array;
  const struct dyn_imp_mod *m
    = (const struct dyn_imp_mod *) value;

  out_array = (struct gcov_import_mod_array *) data1;

  if (m->imp_mod != out_array->importing_module)
  {
    out_array->imported_modules[out_array->len++] = m;
    /* Sanity check that the importing (primary) module is not
       actually the same as the new aux module. This could happen if
       we accidentally read in the same gcda file twice.  */
    gcc_assert (m->imp_mod->mod_info->ident !=
                out_array->importing_module->mod_info->ident);
  }

  return 1;
}

/* Comparator for sorting imported modules using weights.  */

static int
sort_by_module_wt (const void *pa, const void *pb)
{
  const struct dyn_imp_mod *m_a = *((const struct dyn_imp_mod * const *) pa);
  const struct dyn_imp_mod *m_b = *((const struct dyn_imp_mod * const *) pb);

  /* We want to sort in descending order of weights.  */
  if (m_a->weight < m_b->weight)
    return +1;
  if (m_a->weight > m_b->weight)
    return -1;
  return get_module_ident (m_a->imp_mod) - get_module_ident (m_b->imp_mod);
}

/* Return a dynamic array of imported modules that is sorted for
   the importing module MOD_INFO. The length of the array is returned
   in *LEN.  */

const struct dyn_imp_mod **
gcov_get_sorted_import_module_array (struct gcov_info *mod_info,
                                     unsigned *len)
{
  unsigned mod_idx;
  struct dyn_module_info *sup_mod_info;
  unsigned array_len = 0;
  struct gcov_import_mod_array imp_array;

  mod_idx = get_module_ident (mod_info) - 1;
  sup_mod_info = &the_dyn_call_graph.sup_modules[mod_idx];

  if (sup_mod_info->imported_modules == 0)
    return 0;

  pointer_set_traverse (sup_mod_info->imported_modules,
                        gcov_compute_mset_size, &array_len, 0, 0);
  imp_array.imported_modules = XNEWVEC (const struct dyn_imp_mod *, array_len);
  imp_array.len = 0;
  imp_array.importing_module = mod_info;
  pointer_set_traverse (sup_mod_info->imported_modules,
                        gcov_collect_imported_modules, &imp_array, 0, 0);
  *len = imp_array.len;
  qsort (imp_array.imported_modules, imp_array.len,
         sizeof (void *), sort_by_module_wt);
  return imp_array.imported_modules;
}

/* Compute modules that are needed for NODE (for cross module inlining).
   CUTTOFF_COUNT is the call graph edge count cutoff value.
   IMPORT_SCALE is the scaling-factor (percent) by which to scale the
   weights of imported modules of a callee before propagating them to
   the caller, if the callee and caller are in different modules.

   Each imported module is assigned a weight that corresponds to the
   expected benefit due to cross-module inlining. When the imported modules
   are written out, they are sorted with highest weight first.

   The following example illustrates how the weight is computed:

   Suppose we are processing call-graph node A. It calls function B 50 times,
   which calls function C 1000 times, and function E 800 times. Lets say B has
   another in-edge from function D, with edge-count of 50. Say all the
   functions are in separate modules (modules a, b, c, d, e, respectively):

              D
              |
              | 50
              |
       50     v     1000
  A --------> B ----------> C
              |
              | 800
              |
              v
              E

  Nodes are processed in depth-first order, so when processing A, we first
  process B. For node B, we are going to add module c to the imported-module
  set, with weight 1000 (edge-count), and module e with weight 800.
  Coming back to A, we are going to add the imported-module-set of B to A,
  after doing some scaling.
  The first scaling factor comes from the fact that A calls B 50 times, but B
  has in-edge-count total of 100. So this scaling factor is 50/100 = 0.5
  The second scaling factor is that since B is in a different module than A,
  we want to slightly downgrade imported modules of B, before adding to the
  imported-modules set of A. This scaling factor has a default value of 50%
  (can be set via env variable GCOV_DYN_IMPORT_SCALE).
  So we end up adding modules c and e to the imported-set of A, with weights
  0.5*0.5*1000=250 and 0.5*0.5*800=200, respectively.

  Next, we have to add module b itself to A. The weight is computed as the
  edge-count plus the sum of scaled-weights of all modules in the
  imported-module set of B, i.e., 50 + 250 + 200 = 500.

  In computing the weight of module b, we add the sum of scaled-weights of
  imported modules of b, because it doesn't make sense to import c, e in
  module a, until module b is imported.  */

static void
gcov_process_cgraph_node (struct dyn_cgraph_node *node,
                          gcov_type cutoff_count,
			  unsigned import_scale)
{
  unsigned mod_id;
  struct dyn_cgraph_edge *callees;
  struct dyn_cgraph_edge *callers;
  node->visited = 1;
  node->sum_in_count = 0;

  callers = node->callers;
  while (callers)
    {
      node->sum_in_count += callers->count;
      callers = callers->next_caller;
    }

  callees = node->callees;
  mod_id = get_module_ident_from_func_glob_uid (node->guid);

  while (callees)
    {
      if (!callees->callee->visited)
        gcov_process_cgraph_node (callees->callee,
                                  cutoff_count,
				  import_scale);
      callees = callees->next_callee;
    }

  callees = node->callees;
  while (callees)
    {
      if (callees->count >= cutoff_count)
        {
          unsigned callee_mod_id;
          struct dyn_pointer_set *imp_modules
              = gcov_get_imp_module_set (node);

          callee_mod_id
              = get_module_ident_from_func_glob_uid (callees->callee->guid);

	  double callee_mod_wt = (double) callees->count;
          if (callees->callee->imported_modules)
	    {
	      double scale = ((double) callees->count) /
		((double) callees->callee->sum_in_count);
	      /* Reduce weight if callee is in different module.  */
	      if (mod_id != callee_mod_id)
		scale = (scale * import_scale) / 100.0;
	      pointer_set_traverse (callees->callee->imported_modules,
				    gcov_propagate_imp_modules,
				    imp_modules, &scale, &callee_mod_wt);
	    }
          if (mod_id != callee_mod_id)
            {
              struct gcov_info *callee_mod_info
                  = get_module_info (callee_mod_id);
              if (callee_mod_info)
                imp_mod_set_insert (imp_modules, callee_mod_info, callee_mod_wt);
            }
        }

      callees = callees->next_callee;
    }
}

static void gcov_compute_module_groups_eager_propagation (gcov_type);
static void gcov_compute_module_groups_inclusion_based_with_priority
              (gcov_type);

/* dyn_fibheap */
static void dyn_fibheap_ins_root (dyn_fibheap_t, fibnode_t);
static void dyn_fibheap_rem_root (dyn_fibheap_t, fibnode_t);
static void dyn_fibheap_consolidate (dyn_fibheap_t);
static void dyn_fibheap_link (dyn_fibheap_t, fibnode_t, fibnode_t);
static fibnode_t dyn_fibheap_extr_min_node (dyn_fibheap_t);
static int dyn_fibheap_compare (dyn_fibheap_t, fibnode_t, fibnode_t);
static int dyn_fibheap_comp_data (dyn_fibheap_t, dyn_fibheapkey_t,
                                  void *, fibnode_t);
static fibnode_t fibnode_new (void);
static void fibnode_insert_after (fibnode_t, fibnode_t);
#define fibnode_insert_before(a, b) fibnode_insert_after (a->left, b)
static fibnode_t fibnode_remove (fibnode_t);

/* Create a new fibonacci heap.  */
static dyn_fibheap_t
dyn_fibheap_new (void)
{
  return (dyn_fibheap_t) xcalloc (1, sizeof (struct dyn_fibheap));
}

/* Create a new fibonacci heap node.  */
static fibnode_t
fibnode_new (void)
{
  fibnode_t node;

  node = (fibnode_t) xcalloc (1, sizeof *node);
  node->left = node;
  node->right = node;

  return node;
}

static inline int
dyn_fibheap_compare (dyn_fibheap_t heap ATTRIBUTE_UNUSED, fibnode_t a,
                     fibnode_t b)
{
  if (a->key < b->key)
    return -1;
  if (a->key > b->key)
    return 1;
  return 0;
}

static inline int
dyn_fibheap_comp_data (dyn_fibheap_t heap, dyn_fibheapkey_t key,
                       void *data, fibnode_t b)
{
  struct fibnode a;

  a.key = key;
  a.data = data;

  return dyn_fibheap_compare (heap, &a, b);
}

/* Insert DATA, with priority KEY, into HEAP.  */
static fibnode_t
dyn_fibheap_insert (dyn_fibheap_t heap, dyn_fibheapkey_t key, void *data)
{
  fibnode_t node;

  /* Create the new node.  */
  node = fibnode_new ();

  /* Set the node's data.  */
  node->data = data;
  node->key = key;

  /* Insert it into the root list.  */
  dyn_fibheap_ins_root (heap, node);

  /* If their was no minimum, or this key is less than the min,
     it's the new min.  */
  if (heap->min == 0 || node->key < heap->min->key)
    heap->min = node;

  heap->nodes++;

  return node;
}

/* Extract the data of the minimum node from HEAP.  */
static void *
dyn_fibheap_extract_min (dyn_fibheap_t heap)
{
  fibnode_t z;
  void *ret = 0;

  /* If we don't have a min set, it means we have no nodes.  */
  if (heap->min != 0)
    {
      /* Otherwise, extract the min node, free the node, and return the
         node's data.  */
      z = dyn_fibheap_extr_min_node (heap);
      ret = z->data;
      free (z);
    }

  return ret;
}

/* Delete HEAP.  */
static void
dyn_fibheap_delete (dyn_fibheap_t heap)
{
  while (heap->min != 0)
    free (dyn_fibheap_extr_min_node (heap));

  free (heap);
}

/* Extract the minimum node of the heap.  */
static fibnode_t
dyn_fibheap_extr_min_node (dyn_fibheap_t heap)
{
  fibnode_t ret = heap->min;
  fibnode_t x, y, orig;

  /* Attach the child list of the minimum node to the root list of the heap.
     If there is no child list, we don't do squat.  */
  for (x = ret->child, orig = 0; x != orig && x != 0; x = y)
    {
      if (orig == 0)
	orig = x;
      y = x->right;
      x->parent = 0;
      dyn_fibheap_ins_root (heap, x);
    }

  /* Remove the old root.  */
  dyn_fibheap_rem_root (heap, ret);
  heap->nodes--;

  /* If we are left with no nodes, then the min is 0.  */
  if (heap->nodes == 0)
    heap->min = 0;
  else
    {
      /* Otherwise, consolidate to find new minimum, as well as do the reorg
         work that needs to be done.  */
      heap->min = ret->right;
      dyn_fibheap_consolidate (heap);
    }

  return ret;
}

/* Insert NODE into the root list of HEAP.  */
static void
dyn_fibheap_ins_root (dyn_fibheap_t heap, fibnode_t node)
{
  /* If the heap is currently empty, the new node becomes the singleton
     circular root list.  */
  if (heap->root == 0)
    {
      heap->root = node;
      node->left = node;
      node->right = node;
      return;
    }

  /* Otherwise, insert it in the circular root list between the root
     and it's right node.  */
  fibnode_insert_after (heap->root, node);
}

/* Remove NODE from the rootlist of HEAP.  */
static void
dyn_fibheap_rem_root (dyn_fibheap_t heap, fibnode_t node)
{
  if (node->left == node)
    heap->root = 0;
  else
    heap->root = fibnode_remove (node);
}

/* Consolidate the heap.  */
static void
dyn_fibheap_consolidate (dyn_fibheap_t heap)
{
  fibnode_t a[1 + 8 * sizeof (long)];
  fibnode_t w;
  fibnode_t y;
  fibnode_t x;
  int i;
  int d;
  int D;

  D = 1 + 8 * sizeof (long);

  memset (a, 0, sizeof (fibnode_t) * D);

  while ((w = heap->root) != 0)
    {
      x = w;
      dyn_fibheap_rem_root (heap, w);
      d = x->degree;
      while (a[d] != 0)
	{
	  y = a[d];
	  if (dyn_fibheap_compare (heap, x, y) > 0)
	    {
	      fibnode_t temp;
	      temp = x;
	      x = y;
	      y = temp;
	    }
	  dyn_fibheap_link (heap, y, x);
	  a[d] = 0;
	  d++;
	}
      a[d] = x;
    }
  heap->min = 0;
  for (i = 0; i < D; i++)
    if (a[i] != 0)
      {
	dyn_fibheap_ins_root (heap, a[i]);
	if (heap->min == 0 || dyn_fibheap_compare (heap, a[i], heap->min) < 0)
	  heap->min = a[i];
      }
}

/* Make NODE a child of PARENT.  */
static void
dyn_fibheap_link (dyn_fibheap_t heap ATTRIBUTE_UNUSED,
              fibnode_t node, fibnode_t parent)
{
  if (parent->child == 0)
    parent->child = node;
  else
    fibnode_insert_before (parent->child, node);
  node->parent = parent;
  parent->degree++;
  node->mark = 0;
}

static void
fibnode_insert_after (fibnode_t a, fibnode_t b)
{
  if (a == a->right)
    {
      a->right = b;
      a->left = b;
      b->right = a;
      b->left = a;
    }
  else
    {
      b->right = a->right;
      a->right->left = b;
      a->right = b;
      b->left = a;
    }
}

static fibnode_t
fibnode_remove (fibnode_t node)
{
  fibnode_t ret;

  if (node == node->left)
    ret = 0;
  else
    ret = node->left;

  if (node->parent != 0 && node->parent->child == node)
    node->parent->child = ret;

  node->right->left = node->left;
  node->left->right = node->right;

  node->parent = 0;
  node->left = node;
  node->right = node;

  return ret;
}
/* end of dyn_fibheap */

/* Compute module grouping using CUTOFF_COUNT as the hot edge
   threshold.  */

static void
gcov_compute_module_groups (gcov_type cutoff_count)
{
  switch (flag_alg_mode)
    {
      case INCLUSION_BASED_PRIORITY_ALGORITHM:
        return gcov_compute_module_groups_inclusion_based_with_priority
                 (cutoff_count);
      case EAGER_PROPAGATION_ALGORITHM:
      default:
        return gcov_compute_module_groups_eager_propagation (cutoff_count);
    }
}

static void
modu_graph_add_edge (unsigned m_id, unsigned callee_m_id, gcov_type count)
{
  struct modu_node *mnode;
  struct modu_node *callee_mnode;
  struct modu_edge *e;

  if (m_id == 0 || callee_m_id == 0)
    return;

  mnode = &the_dyn_call_graph.modu_nodes[m_id - 1];
  callee_mnode = &the_dyn_call_graph.modu_nodes[callee_m_id - 1];

  if (flag_modu_merge_edges)
    {
       struct modu_edge *callees = mnode->callees;
       while (callees)
         {
            if (callees->callee == callee_mnode)
              {
                 callees->n_edges += 1;
                 callees->sum_count += count;
                 return;
              }
            callees = callees->next_callee;
         }
    }
  e = XNEW (struct modu_edge);
  e->caller = mnode;
  e->callee = callee_mnode;
  e->n_edges = 1;
  e->sum_count = count;
  e->next_callee = mnode->callees;
  e->next_caller = callee_mnode->callers;
  mnode->callees = e;
  callee_mnode->callers = e;
  e->visited = 0;
}

static void
modu_graph_process_dyn_cgraph_node (struct dyn_cgraph_node *node,
                                    gcov_type cutoff_count)
{
  unsigned m_id = get_module_ident_from_func_glob_uid (node->guid);
  struct dyn_cgraph_edge *callees;
  struct dyn_cgraph_node *callee;

  callees = node->callees;
  while (callees != 0)
    {
      callee = callees->callee;
      unsigned callee_m_id = 
        get_module_ident_from_func_glob_uid (callee->guid);
      if (callee_m_id != m_id)
        {
          if (callees->count >= cutoff_count)
            modu_graph_add_edge (m_id, callee_m_id, callees->count);
        }
      callees = callees->next_callee;
    }
}

static void
build_modu_graph (gcov_type cutoff_count)
{
  unsigned m_ix;
  struct gcov_info *gi_ptr;
  unsigned n_modules = the_dyn_call_graph.num_modules;
  struct modu_node *modu_nodes;

  /* Create modu graph nodes/edges.  */
  modu_nodes = XCNEWVEC (struct modu_node, n_modules);
  the_dyn_call_graph.modu_nodes = modu_nodes;
  for (m_ix = 0; m_ix < n_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;
      modu_nodes[m_ix].module = gi_ptr;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	{
	  struct dyn_cgraph_node *node;

	  fi_ptr = gi_ptr->functions[f_ix];
	  node = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
		   (the_dyn_call_graph.call_graph_nodes[m_ix], fi_ptr->ident));
	  if (!node)
            {
              fprintf (stderr, "Cannot find module_node (ix = %u)./n", m_ix);
              continue;
            }
          modu_graph_process_dyn_cgraph_node (node, cutoff_count);
	}
    }
}

/* Collect ggc_mem_size for the impored_module in VALUE
   if DATA1 (a pointer_set) is provided, only count these not in DATA1.
   Result is stored in DATA2.  */

static int
collect_ggc_mem_size (const void *value,
                 void *data1,
                 void *data2,
                 void *data3 ATTRIBUTE_UNUSED)
{
  const struct dyn_imp_mod *g = (const struct dyn_imp_mod *) value;
  struct dyn_pointer_set *s = (struct dyn_pointer_set *) data1;
  unsigned mod_id = get_module_ident (g->imp_mod);
  gcov_unsigned_t *size = (gcov_unsigned_t *) data2;

  if (s && pointer_set_contains (s, mod_id))
    return 1;

  (*size) += g->imp_mod->mod_info->ggc_memory;

  return 1;

}

/* Get the group ggc_memory size of a imported list.  */

static gcov_unsigned_t
get_group_ggc_mem (struct dyn_pointer_set *s)
{
  gcov_unsigned_t ggc_size = 0;

  pointer_set_traverse (s, collect_ggc_mem_size, 0, &ggc_size, 0);
  return ggc_size;
}

/* Get the group ggc_memory size of the unioned imported lists. */

static gcov_unsigned_t
modu_union_ggc_size (unsigned t_mid, unsigned s_mid)
{
  struct dyn_pointer_set *t_imported_mods = get_imported_modus (t_mid);
  struct dyn_pointer_set *s_imported_mods = get_imported_modus (s_mid);
  gcov_unsigned_t size = 0;

  pointer_set_traverse (s_imported_mods, collect_ggc_mem_size,
      t_imported_mods, &size, 0);

  size += get_group_ggc_mem (t_imported_mods);

  return size;
}

/* Insert one module (VALUE) to the target module (DATA1) */

static int
modu_add_auxiliary_1 (const void *value,
                      void *data1,
                      void *data2,
                      void *data3 ATTRIBUTE_UNUSED)
{
  const struct dyn_imp_mod *src = (const struct dyn_imp_mod *) value;
  const struct gcov_info *src_modu = src->imp_mod;
  unsigned t_m_id = *(unsigned *) data1;
  struct dyn_pointer_set *t_imported_mods = get_imported_modus (t_m_id);
  double wt = (double) *(gcov_type*)data2;
  unsigned s_m_id = get_module_ident (src_modu);
  struct gcov_info **gp;
  struct dyn_pointer_set *s_exported_to;
  int already_have = 0;

  if (pointer_set_contains (t_imported_mods, s_m_id))
    already_have = 1;

  /* Insert even it's already there. This is to update the wt.  */
  imp_mod_set_insert (t_imported_mods, src_modu, wt);

  if (already_have)
    return 1;

  /* add module t_m_id to s_m_id's exported list. */
  s_exported_to = get_exported_to (s_m_id);
  if (!s_exported_to)
    s_exported_to = create_exported_to (s_m_id);
  gp = (struct gcov_info **) pointer_set_find_or_insert
             (s_exported_to, t_m_id);
  *gp = the_dyn_call_graph.modules[t_m_id - 1];
  s_exported_to->n_elements++;

  return 1;
}

/* Insert module S_MID and it's imported modules to
   imported list of module T_MID.  */

static void
modu_add_auxiliary (unsigned t_mid, unsigned s_mid, gcov_type count)
{
  struct dyn_pointer_set *s_imported_mods = get_imported_modus (s_mid);

  pointer_set_traverse (s_imported_mods, modu_add_auxiliary_1,
                        &t_mid, &count, 0);

  /* Recompute the gcc_memory for the group.  */
  the_dyn_call_graph.sup_modules[t_mid - 1].group_ggc_mem =
    get_group_ggc_mem (get_imported_modus (t_mid));
}

/* Check if inserting the module specified by DATA1 (including
   it's imported list to grouping VALUE, makes the ggc_memory
   size exceed the memory threshold. 
   Return 0 if size is great than the thereshold and 0 otherwise.  */

static int
ps_check_ggc_mem (const void *value,
                  void *data1,
                  void *data2,
                  void *data3 ATTRIBUTE_UNUSED)
{
  const struct gcov_info *modu = (const struct gcov_info *) value;
  unsigned s_m_id = *(unsigned *) data1;
  unsigned *fail = (unsigned *) data2;
  unsigned m_id = get_module_ident (modu);
  gcov_unsigned_t new_ggc_size;

  new_ggc_size = modu_union_ggc_size (m_id, s_m_id);
  if (new_ggc_size > mem_threshold)
    {
      (*fail) = 1;
      return 0;
    }

  return 1;
}

/* Add module specified by DATA1 and it's imported list to
   the grouping specified by VALUE.  */

static int
ps_add_auxiliary (const void *value,
                  void *data1,
                  void *data2,
                  void *data3)
{
  const struct gcov_info *modu = (const struct gcov_info *) value;
  unsigned s_m_id = *(unsigned *) data1;
  unsigned m_id = get_module_ident (modu);
  int not_safe_to_insert = *(int *) data3;
  gcov_unsigned_t new_ggc_size;

  /* For strict inclusion, we know it's safe to insert.  */
  if (!not_safe_to_insert)
    {
      modu_add_auxiliary (m_id, s_m_id, *(gcov_type*)data2);
      return 1;
    }

  /* Check if we can do a partial insertion.  */
  new_ggc_size = modu_union_ggc_size (m_id, s_m_id);
  if (new_ggc_size > mem_threshold)
    return 1;

  modu_add_auxiliary (m_id, s_m_id, *(gcov_type*)data2);
  return 1;
}

/* Return 1 if insertion happened, otherwise 0.  */

static int
modu_edge_add_auxiliary (struct modu_edge *edge)
{
  struct modu_node *node;
  struct modu_node *callee;
  struct gcov_info *node_modu;
  struct gcov_info *callee_modu;
  gcov_unsigned_t group_ggc_mem;
  gcov_unsigned_t new_ggc_size;
  struct dyn_pointer_set *node_imported_mods;
  struct dyn_pointer_set *node_exported_to;
  unsigned m_id, callee_m_id;
  int fail = 0;

  node = edge->caller;
  callee = edge->callee;
  node_modu = node->module;
  callee_modu = callee->module;
  m_id = get_module_ident (node_modu);

  if (m_id == 0)
    return 0;

  group_ggc_mem = the_dyn_call_graph.sup_modules[m_id - 1].group_ggc_mem;

  if (group_ggc_mem >= mem_threshold)
    return 0;

  node_imported_mods = get_imported_modus (m_id);

  /* Check if the callee is already included.  */
  callee_m_id = get_module_ident (callee_modu);
  if (pointer_set_contains (node_imported_mods, callee_m_id))
    return 0;

  new_ggc_size = modu_union_ggc_size (m_id, callee_m_id);
  if (new_ggc_size > mem_threshold)
    return 0;

  /* check the size for the grouping that includes this node. */
  node_exported_to = get_exported_to (m_id);
  if (node_exported_to)
    {
      pointer_set_traverse (node_exported_to, ps_check_ggc_mem,
                            &callee_m_id, &fail, 0);
      if (fail && !flag_weak_inclusion)
        return 0;
    }

  /* Perform the insertion: first insert to node
  and then to all the exported_to nodes.  */
  modu_add_auxiliary (m_id, callee_m_id, edge->sum_count);

  if (node_exported_to)
    pointer_set_traverse (node_exported_to, ps_add_auxiliary,
       &callee_m_id, &(edge->sum_count), &fail);
  return 1;
}

static void
compute_module_groups_inclusion_impl (void)
{
  dyn_fibheap_t heap;
  unsigned i;
  unsigned n_modules = the_dyn_call_graph.num_modules;

  /* insert all the edges to the heap.  */
  heap = dyn_fibheap_new ();
  for (i = 0; i < n_modules; i++)
    {
      struct modu_edge * callees;
      struct modu_node *node = &the_dyn_call_graph.modu_nodes[i];

      callees = node->callees;
      while (callees != 0)
        {
	  dyn_fibheap_insert (heap, -1 * callees->sum_count, callees);
          callees = callees->next_callee;
        }
    }

  while (1)
    {
      struct modu_edge *curr
	= (struct modu_edge *) dyn_fibheap_extract_min (heap);

      if (!curr)
	break;
      if (curr->visited)
	continue;
      curr->visited = 1;

      modu_edge_add_auxiliary (curr);
    }

  dyn_fibheap_delete (heap);

  /* Now compute the export attribute  */
  for (i = 0; i < n_modules; i++)
    {
      struct dyn_module_info *mi
          = &the_dyn_call_graph.sup_modules[i];
      if (mi->exported_to)
        SET_MODULE_EXPORTED (the_dyn_call_graph.modules[i]->mod_info);
    }
}

static void
gcov_compute_module_groups_inclusion_based_with_priority
            (gcov_type cutoff_count)
{
  build_modu_graph (cutoff_count);
  compute_module_groups_inclusion_impl ();
}

static void
gcov_compute_module_groups_eager_propagation (gcov_type cutoff_count)
{
  unsigned m_ix;
  struct gcov_info *gi_ptr;
  const char *import_scale_str;
  unsigned import_scale = __gcov_lipo_propagate_scale;

  /* Different from __gcov_lipo_cutoff handling, the
     environment variable here takes precedance  */
  import_scale_str = getenv ("GCOV_DYN_IMPORT_SCALE");
  if (import_scale_str && strlen (import_scale_str))
    import_scale = atoi (import_scale_str);

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	{
	  struct dyn_cgraph_node *node;

	  fi_ptr = gi_ptr->functions[f_ix];
	  node = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
		   (the_dyn_call_graph.call_graph_nodes[m_ix], fi_ptr->ident));
	  gcc_assert (node);
          if (node->visited)
            continue;

          gcov_process_cgraph_node (node, cutoff_count, import_scale);
	}
    }

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	{
	  struct dyn_cgraph_node *node;
          unsigned mod_id;
          struct dyn_pointer_set *imp_modules;

	  fi_ptr = gi_ptr->functions[f_ix];
	  node = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
		   (the_dyn_call_graph.call_graph_nodes[m_ix], fi_ptr->ident));
	  gcc_assert (node);

          if (!node->imported_modules)
            continue;

          mod_id = get_module_ident_from_func_glob_uid (node->guid);
          gcc_assert (mod_id == (m_ix + 1));

          imp_modules
              = gcov_get_module_imp_module_set (
                  &the_dyn_call_graph.sup_modules[mod_id - 1]);

          pointer_set_traverse (node->imported_modules,
                                gcov_propagate_imp_modules,
                                imp_modules, 0, 0);
	}
    }

  /* Now compute the export attribute  */
  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      struct dyn_module_info *mi
          = &the_dyn_call_graph.sup_modules[m_ix];

      if (mi->imported_modules)
        pointer_set_traverse (mi->imported_modules,
                              gcov_mark_export_modules, 0, 0, 0);
    }
}

/* For each module, compute at random, the group of imported modules,
   that is of size at most MAX_GROUP_SIZE.  */

static void
gcov_compute_random_module_groups (unsigned max_group_size)
{
  unsigned m_ix;

  if (max_group_size > the_dyn_call_graph.num_modules)
    max_group_size = the_dyn_call_graph.num_modules;

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      struct dyn_pointer_set *imp_modules =
	gcov_get_module_imp_module_set (&the_dyn_call_graph.sup_modules[m_ix]);
      int cur_group_size = rand () % max_group_size;
      int i = 0;
      while (i < cur_group_size)
	{
	  struct gcov_info *imp_mod_info;
	  unsigned mod_idx = rand () % the_dyn_call_graph.num_modules;
	  if (mod_idx == m_ix)
	    continue;
	  imp_mod_info = get_module_info (mod_idx + 1);
          if (imp_mod_info &&
	      !imp_mod_set_insert (imp_modules, imp_mod_info, 1.0))
	    i++;
	}
    }

  /* Now compute the export attribute  */
  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      struct dyn_module_info *mi
	= &the_dyn_call_graph.sup_modules[m_ix];
      if (mi->imported_modules)
        pointer_set_traverse (mi->imported_modules,
                              gcov_mark_export_modules, 0, 0, 0);
    }
}

#if 0
/* Write out MOD_INFO into the gcda file. IS_PRIMARY is a flag
   indicating if the module is the primary module in the group.  */

static void
gcov_write_module_info (const struct gcov_info *mod_info,
                        unsigned is_primary)
{
  gcov_unsigned_t len = 0, filename_len = 0, src_filename_len = 0, i;
  gcov_unsigned_t num_strings;
  gcov_unsigned_t *aligned_fname;
  struct gcov_module_info  *module_info = mod_info->mod_info;
  filename_len = (strlen (module_info->da_filename) +
		  sizeof (gcov_unsigned_t)) / sizeof (gcov_unsigned_t);
  src_filename_len = (strlen (module_info->source_filename) +
		      sizeof (gcov_unsigned_t)) / sizeof (gcov_unsigned_t);
  len = filename_len + src_filename_len;
  len += 2; /* each name string is led by a length.  */

  num_strings = module_info->num_quote_paths + module_info->num_bracket_paths
    + module_info->num_system_paths
    + module_info->num_cpp_defines + module_info->num_cpp_includes
    + module_info->num_cl_args;
  len += gcov_compute_string_array_len (module_info->string_array,
                                        num_strings);

  len += 11; /* 11 more fields */

  gcov_write_tag_length (GCOV_TAG_MODULE_INFO, len);
  gcov_write_unsigned (module_info->ident);
  gcov_write_unsigned (is_primary);
  if (flag_alg_mode == INCLUSION_BASED_PRIORITY_ALGORITHM && is_primary)
    SET_MODULE_INCLUDE_ALL_AUX (module_info);
  gcov_write_unsigned (module_info->flags);
  gcov_write_unsigned (module_info->lang);
  gcov_write_unsigned (module_info->ggc_memory);
  gcov_write_unsigned (module_info->num_quote_paths);
  gcov_write_unsigned (module_info->num_bracket_paths);
  gcov_write_unsigned (module_info->num_system_paths);
  gcov_write_unsigned (module_info->num_cpp_defines);
  gcov_write_unsigned (module_info->num_cpp_includes);
  gcov_write_unsigned (module_info->num_cl_args);

  /* Now write the filenames */
  aligned_fname = (gcov_unsigned_t *) alloca ((filename_len + src_filename_len + 2) *
					      sizeof (gcov_unsigned_t));
  memset (aligned_fname, 0,
          (filename_len + src_filename_len + 2) * sizeof (gcov_unsigned_t));
  aligned_fname[0] = filename_len;
  strcpy ((char*) (aligned_fname + 1), module_info->da_filename);
  aligned_fname[filename_len + 1] = src_filename_len;
  strcpy ((char*) (aligned_fname + filename_len + 2), module_info->source_filename);

  for (i = 0; i < (filename_len + src_filename_len + 2); i++)
    gcov_write_unsigned (aligned_fname[i]);

  /* Now write the string array.  */
  gcov_write_string_array (module_info->string_array, num_strings);
}
#endif

/* Write out MOD_INFO and its imported modules into gcda file.  */

void
gcov_write_module_infos (struct gcov_info *mod_info)
{
  unsigned imp_len = 0;
  const struct dyn_imp_mod **imp_mods;

  if (flag_alg_mode == INCLUSION_BASED_PRIORITY_ALGORITHM)
    SET_MODULE_INCLUDE_ALL_AUX (mod_info->mod_info);
  gcov_write_module_info (mod_info, 1);

  imp_mods = gcov_get_sorted_import_module_array (mod_info, &imp_len);
  if (imp_mods)
    {
      unsigned i;

      for (i = 0; i < imp_len; i++)
        {
          const struct gcov_info *imp_mod = imp_mods[i]->imp_mod;
	  if (imp_mod != mod_info)
            gcov_write_module_info (imp_mod, 0);
        }
      free (imp_mods);
    }
}

/* Set to use module grouping from existing imports files in
   the profile directory.  */
void set_use_existing_grouping (void);

void
set_use_existing_grouping (void)
{
  flag_use_existing_grouping = 1;
}

#ifdef IN_GCOV_TOOL
extern const char *get_source_profile_dir (void);

/* find and open the imports files based on da_filename
   in GI_PTR.  */

static FILE *
open_imports_file (struct gcov_info *gi_ptr)
{
  const char *gcda_name;
  char *imports_name;
  const char *source_dir = "";

  if (gi_ptr == NULL || gi_ptr->mod_info == NULL)
    return NULL;

  gcda_name = gi_ptr->mod_info->da_filename;
  gcc_assert (gcda_name);

  source_dir = get_source_profile_dir ();
  gcc_assert (source_dir);
  imports_name = (char *) alloca (strlen (gcda_name) + strlen (source_dir) +
                                  strlen (".gcda.imports") + 2);
  strcpy (imports_name, source_dir);
  strcat (imports_name, "/");
  strcat (imports_name, gcda_name);
  strcat (imports_name, ".gcda.imports");
  return fopen (imports_name, "r");
}

extern int get_module_id_from_name (const char *);

#endif /* IN_GCOV_TOOL */

/* Use the module grouping from existing imports files in
   the profile directory.  */

static void
read_modu_groups_from_imports_files (void)
{
#ifdef IN_GCOV_TOOL
  unsigned m_ix;
  const int max_line_size = (1 << 12);
  char line[max_line_size];

  init_dyn_call_graph ();

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      struct gcov_info *gi_ptr = the_dyn_call_graph.modules[m_ix];
      FILE *fd;
      struct dyn_pointer_set *imp_modules;
      char buf[8192];

      if (gi_ptr == NULL)
        continue;

      imp_modules = gcov_get_module_imp_module_set
                      (&the_dyn_call_graph.sup_modules[m_ix]);

      if ((fd = open_imports_file (gi_ptr)) != NULL)
	{
#define MAX_MODU_SIZE 200000
          int w = MAX_MODU_SIZE;
          int i = 0;

          while (fgets (line, max_line_size, fd) != NULL)
            {
              unsigned mod_id = 0;
              char *name = strtok (line, " \t\n");

              if (name && (mod_id = get_module_id_from_name (name)))
                {
                  struct gcov_info *imp_mod_info;
           	  unsigned mod_idx = mod_id - 1;
           	  if (mod_idx == m_ix)
           	    continue;
           	  imp_mod_info = get_module_info (mod_idx + 1);
                  i++;
           	  imp_mod_set_insert (imp_modules, imp_mod_info, w - i);
                }
            }
          fclose (fd);
	}
    }

  /* Now compute the export attribute  */
  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      struct dyn_module_info *mi
	= &the_dyn_call_graph.sup_modules[m_ix];
      if (mi->imported_modules)
        pointer_set_traverse (mi->imported_modules,
                              gcov_mark_export_modules, 0, 0, 0);
    }
#else /* !IN_GCOV_TOOL */
  gcc_assert (0);
#endif /* IN_GCOV_TOOL */
}

/* Scan functions in MOD_INFO and return gcov_fn_info for matching
   FUNC_ID.  */

static const struct gcov_fn_info *
find_fn_info_from_func_id (struct gcov_info *mod_info, unsigned func_id)
{
  unsigned j;
  for (j = 0; j < mod_info->n_functions; j++)
    {
      const struct gcov_fn_info *fi_ptr = mod_info->functions[j];
      if (fi_ptr->ident == func_id)
        return fi_ptr;
    }
  gcc_assert (0);
  return NULL;
}

/* Look for a function in the same module as CALLER_GUID that has
   the same lineno and cfg checksums as CALLEE_GUID.  Return the
   guid of the matching function if exactly one is found, 0 otherwise.  */

gcov_type
gcov_find_new_ic_target (gcov_type caller_guid, gcov_type callee_guid)
{
  /* Obtain the callee's function info.  */
  unsigned callee_mod_id = get_module_ident_from_func_glob_uid (callee_guid);
  struct gcov_info *callee_mod_info = get_module_info (callee_mod_id);
  unsigned callee_func_id = get_intra_module_func_id (callee_guid);
  const struct gcov_fn_info *callee_fi_ptr
      = find_fn_info_from_func_id (callee_mod_info, callee_func_id);

  /* Obtain the list of checksum_alias structures for functions with
     the same lineno and cfg checksum as callee.  */
  struct dyn_pointer_set *p = the_dyn_call_graph.lineno_pointer_sets;
  gcc_assert (p);
  struct lineno_checksum_alias **line_alias = (struct lineno_checksum_alias **)
    pointer_set_find_or_insert (p, callee_fi_ptr->lineno_checksum);
  gcc_assert (*line_alias);
  struct checksum_alias_info *cfg_alias
      = find_cfg_checksum ((*line_alias)->cfg_checksum_list,
                           callee_fi_ptr->cfg_checksum);
  gcc_assert (cfg_alias);


  /* Scan the list of checksum aliases for one that is located in caller's
     module.  */
  gcov_type new_guid = 0;
  unsigned caller_mod_id = get_module_ident_from_func_glob_uid (caller_guid);
  struct checksum_alias *alias;
  for (alias = cfg_alias->alias_list; alias;
       alias = alias->next_alias)
    {
      if (get_module_ident_from_func_glob_uid (alias->guid)
          == caller_mod_id)
        {
          /* Give up if we found multiple matches.  */
          if (new_guid)
            return 0;
          new_guid = alias->guid;
        }
    }

  /* We found exactly one match, return it.  */
  return new_guid;
}

/* If any of CALLER's indirect call counters in CI_PTR has a target that is
   not in CALLER's module group, see if we can find a copy of the function in
   CALLER's module.  If so, replace the target to point to that copy.  See
   comments for gcov_fixup_icall_profile on why we would want to do this.
   Return 1 if any icall profiles were updated, 0 otherwise.  */

static int
gcov_fixup_ic_fn (struct dyn_cgraph_node *caller,
                  const struct gcov_ctr_info *ci_ptr)
{
  unsigned i, j;
  int changed = 0;
  gcov_type *icall_counters = ci_ptr->values;
  unsigned n_counts = ci_ptr->num;
  int do_dump = (do_cgraph_dump () != 0);

  unsigned caller_mod_id = get_module_ident_from_func_glob_uid (caller->guid);
  struct dyn_pointer_set *imported_mods = get_imported_modus (caller_mod_id);
  for (i = 0; i < n_counts; i += GCOV_ICALL_TOPN_NCOUNTS)
    {
      gcov_type *value_array = &icall_counters[i + 1];
      for (j = 0; j < GCOV_ICALL_TOPN_NCOUNTS - 1; j += 2)
        {
          struct dyn_cgraph_node *callee;
          gcov_type count;
          gcov_type callee_guid = value_array[j];

          count = value_array[j + 1];
          if (count == 0)
            continue;

          callee = get_cgraph_node (callee_guid);
          if (!callee)
            continue;

          /* Now check if callee is in the module group of caller.  If so,
             no need for any fixup.  */
          unsigned callee_mod_id
              = get_module_ident_from_func_glob_uid (callee_guid);
          if (pointer_set_contains (imported_mods, callee_mod_id))
            continue;

          /* Attempt to find a copy of callee in caller's module.  */
          gcov_type new_callee_guid
              = gcov_find_new_ic_target (caller->guid, callee_guid);

          if (do_dump == 1)
            {
              struct gcov_info *caller_mod_info
                  = the_dyn_call_graph.modules[caller_mod_id - 1];
              struct gcov_info *callee_mod_info
                  = the_dyn_call_graph.modules[callee_mod_id - 1];
              fprintf (stderr,
                       "Fixup icall %u:%u -> %u:%u with count %lld "
                       "(%s -> %s): ",
                       caller_mod_id, get_intra_module_func_id (caller->guid),
                       callee_mod_id, get_intra_module_func_id (callee_guid),
                       (long long) count,
                       caller_mod_info->mod_info->source_filename,
                       callee_mod_info->mod_info->source_filename);
              if (!new_callee_guid)
                fprintf (stderr,"No target found\n");
              else
                fprintf (stderr,"Found new target %u:%u (%llx)\n",
                         get_module_ident_from_func_glob_uid (new_callee_guid),
                         get_intra_module_func_id (new_callee_guid),
                         (long long) new_callee_guid);
            }

          if (new_callee_guid)
            {
              /* Update the profile info and note the need for a profile
                 counter rewrite.  */
              value_array[j] = new_callee_guid;
              changed = 1;
            }
        }
    }
  return changed;
}

/* Look for indirect call profiles that target callee's outside of the caller's
   module group, and see if we can find a copy of the function in caller's own
   module.  If so, replace the profile target to point to that copy.  This is
   useful when the callee is a COMDAT, in which case there should be a copy
   within CALLER's module.  The linker would have selected a single copy of
   COMDAT and all indirect call profiles would target that copy of the callee,
   which might not end up in the module group of CALLER.
   Return 1 if any icall profiles were updated, 0 otherwise.  */

static int
gcov_fixup_icall_profile (void)
{
  struct gcov_info *gi_ptr;
  unsigned m_ix;
  int changed = 0;

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix, i;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
        {
          struct dyn_cgraph_node *caller;
          const struct gcov_ctr_info *ci_ptr = 0;

          fi_ptr = gi_ptr->functions[f_ix];
          ci_ptr = fi_ptr->ctrs;

          caller = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
                    (the_dyn_call_graph.call_graph_nodes[m_ix],
                     fi_ptr->ident));
          gcc_assert (caller);

          for (i = 0; i < GCOV_COUNTERS; i++)
            {
              if (!gi_ptr->merge[i])
                continue;

              if (i == GCOV_COUNTER_ICALL_TOPNV)
                changed |= gcov_fixup_ic_fn (caller, ci_ptr);

              ci_ptr++;
            }
        }
    }
  return changed;
}

/* Create, zero-initialize and return an array to hold merged counter
   values.  */

static struct gcov_ctr_info *
init_merged_ctrs (void)
{
  struct gcov_ctr_info *merged_ctrs = XNEWVEC (struct gcov_ctr_info,
                                               GCOV_COUNTERS);
  int i;
  for (i = 0; i < GCOV_COUNTERS; i++)
    {
      merged_ctrs[i].num = 0;
      merged_ctrs[i].values = NULL;
    }
  return merged_ctrs;
}

/* The profile merging function that just adds N_COUNTERS counters from array
   SRC to those in DEST.  Adapted from __gcov_merge_add.  */

void
__gcov_dyn_ipa_merge_add (gcov_type *dest, gcov_type *src, unsigned n_counters)
{
  for (; n_counters; dest++, src++, n_counters--)
    *dest += *src;
}

/* The profile merging function that just ors N_COUNTERS counters from array
   SRC to those in DEST.  Adapted from __gcov_merge_ior.  */

void
__gcov_dyn_ipa_merge_ior (gcov_type *dest, gcov_type *src, unsigned n_counters)
{
  for (; n_counters; dest++, src++, n_counters--)
    *dest |= *src;
}


/* The profile merging function that just merges N_COUNTERS direct call counters
   from array SRC to those in DEST.  Adapted from __gcov_merge_dc.  */

void
__gcov_dyn_ipa_merge_dc (gcov_type *dest, gcov_type *src, unsigned n_counters)
{
  unsigned i;

  gcc_assert (!(n_counters % 2));
  for (i = 0; i < n_counters; i += 2)
    {
      gcov_type global_id = src[i];
      if (!global_id)
        continue;

      /* Simply skip non-matching call targets.  */
      if (dest[i] && dest[i] != global_id)
        {
          continue;
        }
      dest[i] = global_id;

      dest[i + 1] += src[i + 1];
    }
}

/* The profile merging function that just merges N_COUNTERS indirect call
   counters from array SRC to those in DEST.  Adapted from
   __gcov_merge_icall_topn.  */

void
__gcov_dyn_ipa_merge_icall_topn (gcov_type *dest, gcov_type *src,
                                 unsigned n_counters)
{
  unsigned i, j, k, m;

  gcc_assert (!(n_counters % GCOV_ICALL_TOPN_NCOUNTS));
  for (i = 0; i < n_counters; i += GCOV_ICALL_TOPN_NCOUNTS)
    {
      /* Skip the number_of_eviction entry (in dest[i]).  */
      gcov_type *value_array = &dest[i + 1];
      unsigned tmp_size = 2 * (GCOV_ICALL_TOPN_NCOUNTS - 1);
      gcov_type *tmp_array
          = (gcov_type *) alloca (tmp_size * sizeof (gcov_type));

      for (j = 0; j < tmp_size; j++)
        tmp_array[j] = 0;

      for (j = 0; j < GCOV_ICALL_TOPN_NCOUNTS - 1; j += 2)
        {
          tmp_array[j] = value_array[j];
          tmp_array[j + 1] = value_array [j + 1];
        }

      /* Skip the number_of_eviction entry (in src[i]).  */
      gcov_type *src_value_array = &src[i + 1];
      for (k = 0; k < GCOV_ICALL_TOPN_NCOUNTS - 1; k += 2)
        {
          int found = 0;
          gcov_type global_id = src_value_array[k];
          gcov_type call_count = src_value_array[k + 1];
          for (m = 0; m < j; m += 2)
            {
              if (tmp_array[m] == global_id)
                {
                  found = 1;
                  tmp_array[m + 1] += call_count;
                  break;
                }
            }
          if (!found)
            {
              tmp_array[j] = global_id;
              tmp_array[j + 1] = call_count;
              j += 2;
            }
        }
      /* Now sort the temp array.  */
      gcov_sort_n_vals (tmp_array, j);

      /* Now copy back the top half of the temp array.  */
      for (k = 0; k < GCOV_ICALL_TOPN_NCOUNTS - 1; k += 2)
        {
          value_array[k] = tmp_array[k];
          value_array[k + 1] = tmp_array[k + 1];
        }
    }
}


/* Time profiles are merged so that minimum from all valid (greater than zero)
   is stored. There could be a fork that creates new counters. To have
   the profile stable, we chosen to pick the smallest function visit time.  */


void
__gcov_dyn_ipa_merge_time_profile (gcov_type *dest, gcov_type *src,
                                   unsigned n_counters)
{
  unsigned i;
  gcov_type value;

  for (i = 0; i < n_counters; i++)
    {
      value = src[i];

      if (value && (!dest[i] || value < dest[i]))
        dest[i] = value;
    }
}


/* The profile merging function that just merges N_COUNTERS most common value
   counters from array SRC to those in DEST.  Adapted from
   __gcov_merge_single.  */

void
__gcov_dyn_ipa_merge_single (gcov_type *dest, gcov_type *src,
                             unsigned n_counters)
{
  unsigned i, n_measures;
  gcov_type value, counter, all;

  gcc_assert (!(n_counters % 3));
  n_measures = n_counters / 3;
  for (i = 0; i < n_measures; i++, dest += 3, src += 3)
    {
      value = src[0];
      counter = src[1];
      all = src[2];

      if (dest[0] == value)
	dest[1] += counter;
      else if (counter > dest[1])
	{
	  dest[0] = value;
	  dest[1] = counter - dest[1];
	}
      else
	dest[1] -= counter;
      dest[2] += all;
    }
}

/* The profile merging function that just merges N_COUNTERS most common
   difference counters from array SRC to those in DEST.  Adapted from
   __gcov_merge_delta.  */

void
__gcov_dyn_ipa_merge_delta (gcov_type *dest, gcov_type *src,
                            unsigned n_counters)
{
  unsigned i, n_measures;
  gcov_type value, counter, all;

  gcc_assert (!(n_counters % 4));
  n_measures = n_counters / 4;
  for (i = 0; i < n_measures; i++, dest += 4, src += 4)
    {
      value = src[1];
      counter = src[2];
      all = src[3];

      if (dest[1] == value)
	dest[2] += counter;
      else if (counter > dest[2])
	{
	  dest[1] = value;
	  dest[2] = counter - dest[2];
	}
      else
	dest[2] -= counter;
      dest[3] += all;
    }
}

/* Type of function used to merge counters.  */
typedef void (*gcov_dyn_ipa_merge_fn) (gcov_type *, gcov_type *,
                                       gcov_unsigned_t);

/* Merge functions for counters.  */
#define DEF_GCOV_COUNTER(COUNTER, NAME, FN_TYPE) __gcov_dyn_ipa_merge ## FN_TYPE,
static gcov_dyn_ipa_merge_fn ctr_merge_functions[GCOV_COUNTERS] = {
#include "gcov-counter.def"
};
#undef DEF_GCOV_COUNTER

#if 0
static gcov_dyn_ipa_merge_fn ctr_merge_functions[GCOV_COUNTERS] = {
    __gcov_dyn_ipa_merge_add,
    __gcov_dyn_ipa_merge_add,
    __gcov_dyn_ipa_merge_add,
    __gcov_dyn_ipa_merge_single,
    __gcov_dyn_ipa_merge_delta,
    __gcov_dyn_ipa_merge_single,
    __gcov_dyn_ipa_merge_add,
    __gcov_dyn_ipa_merge_ior,
    __gcov_dyn_ipa_merge_icall_topn,
    __gcov_dyn_ipa_merge_dc,
};
#endif

/* Copy counters from SRC_CTRS array to DEST_CTRS array, where SRC_CTRS is
   indexed by the GCOV_COUNTER type, and DEST_CTRS is an array holding only
   the mergable counters to emit to the gcda file for DEST_GUID.  */

static void
copy_ctrs (const struct gcov_ctr_info *dest_ctrs, gcov_type dest_guid,
           const struct gcov_ctr_info *src_ctrs)
{
  unsigned dest_mod_id
      = get_module_ident_from_func_glob_uid (dest_guid);
  struct gcov_info *dest_mod_info = the_dyn_call_graph.modules[dest_mod_id - 1];
  int i;
  for (i = 0; i < GCOV_COUNTERS; i++)
    {
      if (!dest_mod_info->merge[i])
        continue;

      gcov_unsigned_t num = dest_ctrs->num;
      // This could be different if code was optimized differently
      // (e.g. early-inlined in some modules but not others).
      // If they are different then just punt on merge of this counter
      // (what about other counters?).
      //gcc_assert (dest_ctrs[i].num == num);
      if (num && src_ctrs[i].num == num)
        (*ctr_merge_functions[i]) (dest_ctrs->values,
                                   src_ctrs[i].values, num);
      dest_ctrs++;
    }
}

/* Merge counters from SRC_CTRS array to DEST_CTRS array, where DEST_CTRS is
   indexed by the GCOV_COUNTER type, and SRC_CTRS is an array holding only
   the mergable counters from the gcda file for SRC_GUID.  */

static void
merge_ctrs (struct gcov_ctr_info *dest_ctrs,
            const struct gcov_ctr_info *src_ctrs, gcov_type src_guid)
{
  unsigned src_mod_id
      = get_module_ident_from_func_glob_uid (src_guid);
  struct gcov_info *src_mod_info = the_dyn_call_graph.modules[src_mod_id - 1];
  unsigned i, j;

  for (i = 0; i < GCOV_COUNTERS; i++)
    {
      if (!src_mod_info->merge[i])
        continue;

      gcov_unsigned_t num = src_ctrs->num;
      // This could be different if code was optimized differently
      // (e.g. call counters when ipa-inlined in some modules but not others?).
      // If they are different then just punt on merge of this counter
      // (what about other counters?).
      //gcc_assert (dest_ctrs[i].num == num);
      if (num)
        {
          /* If this is the first source counter array containing counters for
             this counter type, allocate the associated number of counter values
             in the dest counter array.  */
          if (!dest_ctrs[i].num)
            {
              dest_ctrs[i].values = XNEWVEC (gcov_type, num);
              for (j = 0; j < num; j++)
                dest_ctrs[i].values[j] = 0;
              dest_ctrs[i].num = num;
            }
          if (dest_ctrs[i].num == num)
            (*ctr_merge_functions[i]) (dest_ctrs[i].values,
                                       src_ctrs->values, num);
        }
      src_ctrs++;
    }
}

/* Walks the set of functions that have the same lineno and cfg checksum, and
   performs counter merging.  INFO contains the checksum_alias_info structure
   for a given lineno and cfg checksum combination. CHANGED points
   to a flag that should be set to 1 if any fixups were applied.  */

static int
gcov_fixup_counters_checksum (const struct checksum_alias_info *info,
                              int *changed)
{
  /* See if there are any zero count functions to fix.  */
  int found = 0;
  struct checksum_alias *alias;
  for (alias = info->alias_list; alias;
       alias = alias->next_alias)
    {
      if (alias->zero_count_fixup)
        {
          found = 1;
          break;
        }
    }
  if (!found)
    return 1;

  /* Walk the aliases and merge the non-zero counters into a dummy copy.  */
  struct gcov_ctr_info *merged_ctrs = init_merged_ctrs ();
  found = 0;
  for (alias = info->alias_list; alias;
       alias = alias->next_alias)
    {
      if (alias->zero_count_fixup)
        continue;
      merge_ctrs (merged_ctrs, alias->fi_ptr->ctrs, alias->guid);
      found = 1;
    }

  /* Check if we found a non-zero count function to fix up from.  */
  if (!found)
    return 1;

  /* At this point we know we have a zero count function to fixup, and data
     from which to fix it up.  */
  *changed = 1;

  /* Walk them again and copy the merged counters into 0-count copies.  */
  for (alias = info->alias_list; alias;
       alias = alias->next_alias)
    {
      if (!alias->zero_count_fixup)
        continue;
      copy_ctrs (alias->fi_ptr->ctrs, alias->guid, merged_ctrs);
      *alias->zero_count_fixup = 1;
    }

  return 1;
}

/* Walks the set of functions that have the same lineno_checksum, and
   performs counter merging for functions that have the same cfg_checksum
   as well.  VALUE contains the lineno_checksum_alias structure for a
   given lineno_checksum, and DATA1 contains a pointer to a flag that
   should be set to 1 if any fixups were applied.  */

static int
gcov_fixup_counters_lineno (const void *value,
                            void *data1,
                            void *data2 ATTRIBUTE_UNUSED,
                            void *data3 ATTRIBUTE_UNUSED)
{
  const struct lineno_checksum_alias *a
      = (const struct lineno_checksum_alias*) value;
  int *changed = (int *) data1;
  struct checksum_alias_info *cfg_alias_list = a->cfg_checksum_list;
  for (; cfg_alias_list; cfg_alias_list = cfg_alias_list->next_cfg_checksum)
    {
      gcov_fixup_counters_checksum (cfg_alias_list, changed);
    }
  return 1;
}

/* Routine to perform counter fixup for COMDAT functions with missing counters.
   Returns 1 if any updates were performed, 0 otherwise.  Walks the sets of
   functions having the same lineno and cfg checksums and merges all non-zero
   counters, copying the merged counters into any copies with all-zero counts.
   This is done because the linker will chose one out-of-line copy of a COMDAT,
   and only that copy will get non-zero counters.  Other copies that were IPA
   inlined may have non-zero counts, which we don't overwrite as they contain
   more context-sensitive data.  */

static int
gcov_fixup_zero_counters (void)
{
  int changed = 0;
  pointer_set_traverse (the_dyn_call_graph.lineno_pointer_sets,
                        gcov_fixup_counters_lineno,
                        &changed, 0, 0);
  return changed;
}

/* Compute module groups needed for L-IPO compilation.  The ZERO_COUNTS
   flags are set for functions with zero count fixups applied. Returns 1
   if any counter fixups were applied, requiring a profile rewrite,
   0 otherwise.  */

int
__gcov_compute_module_groups (char **zero_counts)
{
  gcov_type cut_off_count;
  char *seed = getenv ("LIPO_RANDOM_GROUPING");
  char *max_group_size = seed ? strchr (seed, ':') : 0;

  /* The random group is set via compile time parameter.  */
  if (__gcov_lipo_random_group_size != 0)
    {
      srand (__gcov_lipo_random_seed);
      init_dyn_call_graph ();
      gcov_compute_random_module_groups (__gcov_lipo_random_group_size);
      if (do_cgraph_dump () != 0)
        {
          fprintf (stderr, " Creating random grouping with %u:%u\n",
                   __gcov_lipo_random_seed, __gcov_lipo_random_group_size);
        }
      return 0;
    }
  else if (seed && max_group_size)
    {
      *max_group_size = '\0';
      max_group_size++;
      srand (atoi (seed));
      init_dyn_call_graph ();
      gcov_compute_random_module_groups (atoi (max_group_size));
      if (do_cgraph_dump () != 0)
        {
          fprintf (stderr, " Creating random grouping with %s:%s\n",
                   seed, max_group_size);
        }
      return 0;
    }

  if (flag_use_existing_grouping)
    {
      read_modu_groups_from_imports_files ();
      return 0;
    }

  const char *do_fixup = 0;
  fixup_type = __gcov_lipo_comdat_algorithm;
  do_fixup = getenv ("GCOV_DYN_DO_FIXUP");
  if (do_fixup)
    fixup_type = atoi (do_fixup);

  /* First compute dynamic call graph.  */
  gcov_build_callgraph (zero_counts);

  cut_off_count = gcov_compute_cutoff_count ();

  gcov_compute_module_groups (cut_off_count);

  gcov_dump_callgraph (cut_off_count);

  int changed = 0;
  if (fixup_type & 0x2)
    changed |= gcov_fixup_zero_counters ();
  if (fixup_type & 0x1)
    changed |= gcov_fixup_icall_profile ();

  return changed;
}

/* Dumper function for NODE.  */
static void
gcov_dump_cgraph_node_short (struct dyn_cgraph_node *node)
{
  unsigned mod_id, func_id;
  struct gcov_info *mod_info;
  mod_id = get_module_ident_from_func_glob_uid (node->guid);
  func_id = get_intra_module_func_id (node->guid);

  mod_info = the_dyn_call_graph.modules[mod_id - 1];

  fprintf (stderr, "NODE(%llx) module(%s) func(%u)",
           (long long)node->guid,
           mod_info->mod_info->source_filename, func_id);
}

/* Dumper function for NODE.   M is the module id and F is the function id.  */

static void
gcov_dump_cgraph_node (struct dyn_cgraph_node *node, unsigned m, unsigned f)
{
  unsigned mod_id, func_id;
  struct gcov_info *mod_info;
  struct dyn_cgraph_edge *callers;
  struct dyn_cgraph_edge *callees;

  mod_id = get_module_ident_from_func_glob_uid (node->guid);
  func_id = get_intra_module_func_id (node->guid);
  gcc_assert (mod_id == (m + 1) && func_id == f);

  mod_info = the_dyn_call_graph.modules[mod_id - 1];

  fprintf (stderr, "NODE(%llx) module(%s) func(%x)\n",
           (long long) node->guid,
           mod_info->mod_info->source_filename, f);

  /* Now dump callers.  */
  callers = node->callers;
  fprintf (stderr, "\t[CALLERS]\n");
  while (callers != 0)
    {
      fprintf (stderr,"\t\t[count=%ld] ", (long)  callers->count);
      gcov_dump_cgraph_node_short (callers->caller);
      fprintf (stderr,"\n");
      callers = callers->next_caller;
    }

  callees = node->callees;
  fprintf (stderr, "\t[CALLEES]\n");
  while (callees != 0)
    {
      fprintf (stderr,"\t\t[count=%ld] ", (long)  callees->count);
      gcov_dump_cgraph_node_short (callees->callee);
      fprintf (stderr,"\n");
      callees = callees->next_callee;
    }
}

/* Dumper function for NODE.   M is the module_ident -1
   and F is the function id.  */

static void
gcov_dump_cgraph_node_dot (struct dyn_cgraph_node *node,
                           unsigned m, unsigned f,
                           gcov_type cutoff_count)
{
  unsigned mod_id, func_id, imp_len = 0, i;
  struct gcov_info *mod_info;
  const struct dyn_imp_mod **imp_mods;
  struct dyn_cgraph_edge *callees;

  mod_id = get_module_ident_from_func_glob_uid (node->guid);
  func_id = get_intra_module_func_id (node->guid);
  gcc_assert (mod_id == (m + 1) && func_id == f);

  mod_info = the_dyn_call_graph.modules[mod_id - 1];

  fprintf (stderr, "NODE_%llx[label=\"MODULE\\n(%s)\\n FUNC(%x)\\n",
           (long long) node->guid, mod_info->mod_info->source_filename, f);

  imp_mods = gcov_get_sorted_import_module_array (mod_info, &imp_len);
  fprintf (stderr, "IMPORTS:\\n");
  if (imp_mods)
    {
      for (i = 0; i < imp_len; i++)
        fprintf (stderr, "%s\\n", imp_mods[i]->imp_mod->mod_info->source_filename);
      fprintf (stderr, "\"]\n");
      free (imp_mods);
    }
  else
    fprintf (stderr, "\"]\n");

  callees = node->callees;
  while (callees != 0)
    {
      if (callees->count >= cutoff_count)
        fprintf (stderr, "NODE_%llx -> NODE_%llx[label=%lld color=red]\n",
                 (long long) node->guid, (long long) callees->callee->guid,
                 (long long) callees->count);
      else
        fprintf (stderr, "NODE_%llx -> NODE_%llx[label=%lld color=blue]\n",
                 (long long) node->guid, (long long) callees->callee->guid,
                 (long long) callees->count);
      callees = callees->next_callee;
    }
}

/* Dump dynamic call graph.  CUTOFF_COUNT is the computed hot edge threshold.  */

static void
gcov_dump_callgraph (gcov_type cutoff_count)
{
  struct gcov_info *gi_ptr;
  unsigned m_ix;
  int do_dump;

  do_dump = do_cgraph_dump ();

  if (do_dump == 0)
    return;

  fprintf (stderr,"digraph dyn_call_graph {\n");
  fprintf (stderr,"node[shape=box]\nsize=\"11,8.5\"\n");

  for (m_ix = 0; m_ix < the_dyn_call_graph.num_modules; m_ix++)
    {
      const struct gcov_fn_info *fi_ptr;
      unsigned f_ix;

      gi_ptr = the_dyn_call_graph.modules[m_ix];
      if (gi_ptr == NULL)
        continue;

      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	{
	  struct dyn_cgraph_node *node;

	  fi_ptr = gi_ptr->functions[f_ix];
	  node = (struct dyn_cgraph_node *) *(pointer_set_find_or_insert
		   (the_dyn_call_graph.call_graph_nodes[m_ix], fi_ptr->ident));
	  gcc_assert (node);

          /* skip dead functions  */
          if (!node->callees && !node->callers)
            continue;

          if (do_dump == 1)
            gcov_dump_cgraph_node (node, m_ix, fi_ptr->ident);
          else
            gcov_dump_cgraph_node_dot (node, m_ix, fi_ptr->ident,
                                       cutoff_count);
	}
    }
  fprintf (stderr,"}\n");
}

static int
dump_imported_modules_1 (const void *value,
                    void *data1 ATTRIBUTE_UNUSED,
                    void *data2 ATTRIBUTE_UNUSED,
                    void *data3 ATTRIBUTE_UNUSED)
{
  const struct dyn_imp_mod *d = (const struct dyn_imp_mod*) value;
  fprintf (stderr, "%d ", get_module_ident (d->imp_mod));
  return 1;
}

static int
dump_exported_to_1 (const void *value,
                    void *data1 ATTRIBUTE_UNUSED,
                    void *data2 ATTRIBUTE_UNUSED,
                    void *data3 ATTRIBUTE_UNUSED)
{
  const struct gcov_info *modu = (const struct gcov_info *) value;
  fprintf (stderr, "%d ", get_module_ident (modu));
  return 1;
}

static void ATTRIBUTE_UNUSED
debug_dump_imported_modules (const struct dyn_pointer_set *p)
{
  fprintf (stderr, "imported: ");
  pointer_set_traverse (p, dump_imported_modules_1, 0, 0, 0);
  fprintf (stderr, "\n");
}

static void ATTRIBUTE_UNUSED
debug_dump_exported_to (const struct dyn_pointer_set *p)
{
  fprintf (stderr, "exported: ");
  pointer_set_traverse (p, dump_exported_to_1, 0, 0, 0);
  fprintf (stderr, "\n");
}
#endif
