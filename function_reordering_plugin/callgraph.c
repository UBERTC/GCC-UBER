/* Callgraph implementation.
   Copyright (C) 2011 Free Software Foundation, Inc.
   Contributed by Sriraman Tallam (tmsriram@google.com)
   and Easwaran Raman (eraman@google.com).

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "callgraph.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <hashtab.h>

/*****************************************************************************/
/* section_map hashtable definition and helpers. */

/* Maps section name to its corresponding object handle and section index.  */
static htab_t section_map = NULL;

/* Hashtable helper for section_map htab.  */
static hashval_t
section_map_htab_hash_descriptor (const void *p)
{
  const Section_id *s = (const Section_id *)p;
  const char *name = s->name;
  return htab_hash_string(name);
}

/* Hashtable helper for section_map htab.  */
static int
section_map_htab_eq_descriptor (const void *p1, const void *p2)
{
  const Section_id *s1 = (const Section_id *)p1;
  const char *c1 = s1->name;
  const char *c2 = (const char *)p2;

  return (strcmp (c1, c2) == 0);
}
/*****************************************************************************/


/*****************************************************************************/
/* function_map hashtable definition and helpers.
   Maps function name to a unique Node.  */
static htab_t function_map = NULL;
static unsigned int last_function_id = 0;

/* Hashtable helper for function_map htab.  */
static hashval_t
function_map_htab_hash_descriptor (const void *p)
{
  const Node *s = (const Node *)p;
  const char *name = s->name;
  return htab_hash_string(name);
}

/* Hashtable helper for section_map htab.  */
static int
function_map_htab_eq_descriptor (const void *p1, const void *p2)
{
  const Node *s1 = (const Node *)p1;
  const char *c1 = s1->name;
  const char *c2 = (const char *)p2;

  return (strcmp (c1, c2) == 0);
}
/*****************************************************************************/

/*****************************************************************************/
/* edge_map hashtable definition and helpers.
   Maps two node ids to a unique edge.  */
static htab_t edge_map = NULL;

static inline hashval_t
edge_hash_function (unsigned int id1, unsigned int id2)
{
  return (id1 << 16) | id2;
}

/* Hashtable helper for edge_map htab.  */
static hashval_t
edge_map_htab_hash_descriptor (const void *p)
{
  Edge *e = (Edge *) p;
  return edge_hash_function (e->first_function->id, e->second_function->id);
}

/* Hashtable helper for edge_map htab.  */
static int
edge_map_htab_eq_descriptor (const void *p1, const void *p2)
{
  Edge *e1 = (Edge *) p1;
  Raw_edge *r1 = (Raw_edge *) p2;
  return ((e1->first_function->id == r1->n1->id)
	  && (e1->second_function->id == r1->n2->id));
}


/*****************************************************************************/


/* Keep track of all allocated memory.  */
typedef struct
{
  void *ptr;
  void *next;
} mm_node;

mm_node *mm_node_chain = NULL;

void
push_allocated_ptr (void *ptr)
{
  mm_node *node = XNEW (mm_node);
  node->ptr = ptr;
  node->next = mm_node_chain;
  mm_node_chain = node;
}

/* Chain of all the created nodes.  */
Node *node_chain = NULL;
/* Number of nodes that correspond to functions which will be reordered.  */
unsigned int num_real_nodes = 0;
/* Chain of all edges in the merged callgraph.  */
Edge *active_edges = NULL;
/* Chain of all the merged edges.  */
Edge *inactive_edges = NULL;

/* Initial value of number of functions to allocate hash tables.  */
const int NUM_FUNCTIONS = 100;

/* Reads off the next string from the char stream CONTENTS and updates
   READ_LENGTH to the length of the string read.  The value of CONTENTS
   is updated to start at the next string.   UPDATE_CONTENTS tells if
   CONTENTS must be moved past the read string to the next string.  To
   peek at the string, UPDATE_CONTENTS can be set to false.  */

static char *
get_next_string (char **contents, unsigned int *read_length,
		 int update_contents)
{
  char *s = *contents;
  *read_length = strlen (*contents) + 1;
  if (update_contents)
    *contents += *read_length;
  return s;
}

/* Add an EDGE to the list of edges in the call graph.  */

static void
add_edge_to_list (Edge *edge)
{
  assert (edge != NULL);
  edge->next = active_edges;
  if (active_edges != NULL)
    active_edges->prev = edge;
  active_edges = edge;
}

/* Remove the edge from the list of edges in the call graph. This is done
   when the nodes corresponding to this edge are merged.  */

static void
remove_edge_from_list (Edge * curr_edge)
{
  assert (curr_edge != NULL);
  if (curr_edge->prev != NULL)
    curr_edge->prev->next = curr_edge->next;
  if (curr_edge->next != NULL)
    curr_edge->next->prev = curr_edge->prev;
  if (active_edges == curr_edge)
    active_edges = curr_edge->next;
  curr_edge->next = NULL;
  curr_edge->prev = NULL;

  /* Add to inactive edges to be freed later.  */
  curr_edge->next = inactive_edges;
  inactive_edges = curr_edge;
  return;
}

/* Adds the WEIGHT value to the edge count of CALLER and CALLEE.  */

static void
update_edge (Node *n1, Node *n2, unsigned long long weight)
{
  void **slot;
  Raw_edge re, *r;
  Edge *e;

  if (n1->id == n2->id)
    return;
  if (weight == 0)
    return;

  if (edge_map == NULL)
    {
      edge_map = htab_create ((NUM_FUNCTIONS * 2),
			      edge_map_htab_hash_descriptor,
			      edge_map_htab_eq_descriptor , NULL);
      assert (edge_map != NULL);
    }

  r = &re;
  init_raw_edge (r, n1, n2);
  slot = htab_find_slot_with_hash (edge_map, r,
				   edge_hash_function (r->n1->id, r->n2->id),
				   INSERT);
  if (*slot == NULL)
    {
      e = make_edge (r->n1, r->n2, weight);
      *slot = e;
      add_edge_to_list (e);
    }
  else
    {
      e = *slot;
      e->weight += weight;
    }
  /* Update the computed node weight for n2,  which is the sum of its incoming
     edge weights.  */
  n2->computed_weight += weight;
}

/* Create a unique node for a function.  */

static Node *
get_function_node (char *name)
{
  void **slot = NULL;
  Node *node;

  if (function_map == NULL)
    {
      function_map = htab_create (NUM_FUNCTIONS,
				  function_map_htab_hash_descriptor,
				  function_map_htab_eq_descriptor , NULL);
      assert (function_map != NULL);
    }

  slot = htab_find_slot_with_hash (function_map, name, htab_hash_string (name),
				   INSERT);

  if (*slot == NULL)
    {
      node = make_node (last_function_id, name);
      /* Chain the node to the node_chain.  */
      node->next = node_chain;
      node_chain = node;
      *slot = node;
      last_function_id++;
    }
  else
    {
      node = (Node *)*slot;
    }
  return node;
}

/* Dumper funcction to print the list of functions that will be considered for
   re-ordering.  */

void
dump_functions ()
{
  Node *node = node_chain;
  while (node)
  {
    if (node->is_real_node)
      fprintf (stderr, "Dumping function %s\n", node->name);
    node = node->next;
  }
}

/* Dump all the edges existing in the callgraph.  */

void dump_edges (FILE *fp)
{
  Edge *it;
  for (it = active_edges;
       it != NULL;
       it = it->next)
    {
      fprintf (fp,"# %s (%llu, %llu) ---- (%llu)---- %s (%llu, %llu)\n",
               it->first_function->name,
	       it->first_function->weight,
	       it->first_function->computed_weight,
	       it->weight,
               it->second_function->name,
	       it->second_function->weight,
	       it->second_function->computed_weight);
    }
}

/* For file local functions, append a unique identifier corresponding to
   the file, FILE_HANDLE, to the NAME to keep the name unique.  */

static char *
canonicalize_function_name (void *file_handle, char *name)
{
  /* Number of hexadecimal digits in file_handle, plus length of "0x".  */
  const int FILE_HANDLE_LEN = sizeof (void *) * 2 + 2;
  char *canonical_name;

  /* File local functions have _ZL prefix in the mangled name.  */
  /* XXX: Handle file local functions exhaustively, like functions in
     anonymous name spaces.  */
  if (!is_prefix_of ("_ZL", name))
    return name;

  XNEWVEC_ALLOC (canonical_name, char, (strlen(name) + FILE_HANDLE_LEN + 2));
  sprintf (canonical_name, "%s.%p", name, file_handle);
  return canonical_name;
}

/* Parse the section contents of ".gnu.callgraph.text"  sections and create
   call graph edges with appropriate weights. The section contents have the
   following format :
   Function  <caller_name>
   Weight <entry_count> <max_count> (optional line)
   ColdWeight <max_count> (optional line)
   <callee_1>
   <edge count between caller and callee_1>
   <callee_2>
   <edge count between caller and callee_2>
   ....  */
void
parse_callgraph_section_contents (void *file_handle,
				  unsigned char *section_contents,
				  unsigned int length)
{
  char *contents;
  char *caller;
  char *node_weight_s = NULL;
  unsigned int read_length = 0, curr_length = 0;
  Node *caller_node;

  /* HEADER_LEN is the length of string 'Function '.  */
  const int HEADER_LEN = 9;

  /* Prefix of line containing node weights.  */
  const char *NODE_WEIGHT_PREFIX = "Weight ";
  /* Prefix of line containing max bb count of cold split part.  */
  const char *SPLIT_FUNCTION_PREFIX = "ColdWeight ";

  /* First string in contents is 'Function <function-name>'.  */
  assert (length > 0);
  contents = (char*) (section_contents);
  caller = get_next_string (&contents, &read_length, 1);
  assert (read_length > HEADER_LEN);
  caller = canonicalize_function_name (file_handle, caller + HEADER_LEN);
  curr_length = read_length;
  caller_node = get_function_node (caller);

  /* Check if next string is a node weight, which has the format
     "Weight <entry_count> <max_count>".  We could have callgraph
     sections with or without node weights.  */

  /* Peek at the next string.  */
  if (curr_length < length)
    node_weight_s = get_next_string (&contents, &read_length, 0);
  if (node_weight_s != NULL
      && is_prefix_of (NODE_WEIGHT_PREFIX, node_weight_s))
    {
      char *max_count_s;
      unsigned long long max_count;
      unsigned long long node_weight
	= atoll (node_weight_s + strlen (NODE_WEIGHT_PREFIX));
      /* Functions like comdats only have one caller_node and can
	 have multiple node weights from multiple modules.  */
      caller_node->weight += node_weight;

      /* Find the space and get the max_count.  */
      max_count_s = strstr (node_weight_s + strlen (NODE_WEIGHT_PREFIX), " ");
      if (max_count_s != NULL)
	{
	  max_count = atoll (max_count_s + 1);
          /* Functions like comdats only have one caller_node and can
	     have multiple node weights from multiple modules.  */
	  caller_node->max_count += max_count;
	}
      /* Actually read the node weight here.  */
      get_next_string (&contents, &read_length, 1);
      curr_length += read_length;
    }

  /* If the function is split it could have the weight of the split cold
     section here as "SplitWeight <max_count>".  */

  /* Peek at the next string.  */
  if (curr_length < length)
    node_weight_s = get_next_string (&contents, &read_length, 0);
  if (node_weight_s != NULL
      && is_prefix_of (SPLIT_FUNCTION_PREFIX, node_weight_s))
    {
      unsigned long long split_weight
	= atoll (node_weight_s + strlen (SPLIT_FUNCTION_PREFIX));
      caller_node->split_weight = split_weight;
      /* Actually read the node weight here.  */
      get_next_string (&contents, &read_length, 1);
      curr_length += read_length;
    }

  while (curr_length < length)
    {
      /* Read callee, weight tuples.  */
      char *callee;
      char *weight_str;
      unsigned long long weight;
      Node *callee_node;

      callee = get_next_string (&contents, &read_length, 1);
      curr_length += read_length;

      /* We can have multiple header lines; such a situation arises when
         we've linked objects into a shared library, and we use that
         library as input to the linker for something else.  Deal
         gracefully with such cases.  */
      if (strncmp (callee, "Function ", HEADER_LEN) == 0)
	continue;

      callee = canonicalize_function_name (file_handle, callee);
      callee_node = get_function_node (callee);

      assert (curr_length < length);
      weight_str = get_next_string (&contents, &read_length, 1);
      weight = atoll (weight_str);
      curr_length += read_length;
      update_edge (caller_node, callee_node, weight);
    }
}

/* Traverse the list of edges and find the edge with the maximum weight.  */

static Edge *
find_max_edge ()
{
  Edge *it, *max_edge;

  if (active_edges == NULL)
    return NULL;

  max_edge = active_edges;
  assert (!active_edges->is_merged);

  it = active_edges->next;
  for (;it != NULL; it = it->next)
    {
      assert (!it->is_merged);
      if (edge_lower (max_edge , it))
          max_edge = it;
    }

  return max_edge;
}

/* Change the EDGE from OLD_NODE to KEPT_NODE to be between NEW_NODE
   and KEPT_NODE.  */

static void
merge_edge (Edge *edge, Node *new_node, Node *old_node,
            Node *kept_node)
{
  void **slot;
  Raw_edge re, *r;

  r = &re;
  init_raw_edge (r, new_node, kept_node);
  slot = htab_find_slot_with_hash (edge_map, r,
				   edge_hash_function (r->n1->id, r->n2->id),
				   INSERT);

  if (*slot == NULL)
    {
      reset_functions (edge, new_node, kept_node);
      *slot = edge;
      add_edge_to_node (new_node, edge);
    }
  else
    {
      Edge *new_edge = *slot;
      new_edge->weight += edge->weight;
      edge->is_merged = 1;
      remove_edge_from_list (edge);
    }
}

/* Merge the two nodes in this EDGE. The new node's edges are the union of
   the edges of the original nodes.  */

static void
collapse_edge (Edge * edge)
{
  Edge_list *it;
  Node *kept_node = edge->first_function;
  Node *merged_node = edge->second_function;

  /* Go through all merged_node edges and merge with kept_node.  */
  for (it = merged_node->edge_list; it != NULL; it = it->next)
    {
      Node *other_node = NULL;
      Edge *this_edge = it->edge;
      if (this_edge->is_merged)
        continue;
      if (this_edge == edge)
        continue;
      assert (this_edge->first_function->id == merged_node->id
              || this_edge->second_function->id == merged_node->id);
      other_node = (this_edge->first_function->id
		    == merged_node->id)
		   ? this_edge->second_function
                   : this_edge->first_function;
      merge_edge (this_edge, kept_node, merged_node, other_node);
    }

  merge_node (kept_node, merged_node);
  edge->is_merged = 1;
  remove_edge_from_list (edge);
}

/* Make node N a real node if it can be reordered, that is, its .text
   section is available.  */
static void set_node_type (Node *n)
{
  void *slot;
  char *name = n->name;
  slot = htab_find_with_hash (section_map, name, htab_hash_string (name));
  if (slot != NULL)
    {
      /* Update the section instance corresponding to the node instance.
	 Assign the weights from the node instance to the section instance.  */
      Section_id *s = (Section_id *)(slot);
      Section_id *s_comdat;
      assert (s->weight == 0 && s->computed_weight == 0 && s->max_count == 0);
      s->weight = n->weight;
      s->computed_weight = n->computed_weight; 
      s->max_count = n->max_count;

      /* If s is split into a cold section, assign the split weight to the
         max count of the split section.   Use this also for the weight of the
         split section.  */
      if (s->split_section)
        {
          s->split_section->max_count = s->split_section->weight = n->split_weight;
          /* If split_section is comdat, update all the comdat
    	     candidates for weight.  */
          s_comdat = s->split_section->comdat_group;
          while (s_comdat != NULL)
            {
	      /* Set the different weights for comdat candidates.  No need to se
		 computed_weight as it is zero for split sections.  A split cold
		 section is never called, it is only jumped into from the parent
		 section.  */
              s_comdat->weight = s->split_section->weight;
              s_comdat->max_count = s->split_section->max_count;
              s_comdat = s_comdat->comdat_group;
            }
	}

      /* If s is comdat, update all the comdat candidates for weight.  */
      s_comdat = s->comdat_group;
      while (s_comdat != NULL)
        {
          s_comdat->weight = s->weight;
          s_comdat->computed_weight = s->computed_weight;
          s_comdat->max_count = s->max_count; 
          s_comdat = s_comdat->comdat_group;
        }
      set_as_real_node (n);
      num_real_nodes++;
    }
}

/* Return true if WEIGHT is more than the cutoff, specified either as
   as percent, CUTOFF_P, of MAX or as an absolute value, CUTOFF_A.   */
int 
edge_over_cutoff (unsigned long long weight, unsigned long long max,
		  unsigned int cutoff_p, unsigned long long cutoff_a)
{
  /* First check if weight if more than cutoff_p% of max.  */
  if (((double)(max) * (cutoff_p/100.0)) >= (double) weight)
    return 0;
  if (cutoff_a >= weight)
    return 0;
  return 1;
}

/* Edge cutoff is used to discard callgraph edges that are not above a
   certain threshold.  cutoff_p is to express this as a percent of the
   maximum value and cutoff_a is used to express this as an absolute
   value.  */
extern unsigned int edge_cutoff_p;
extern unsigned long long edge_cutoff_a;

void
find_pettis_hansen_function_layout (FILE *fp)
{
  Node *n_it;
  Edge *it;
  unsigned int max_edge_value = 0;

  assert (node_chain != NULL);
  assert (active_edges != NULL);
  if (fp != NULL)
    dump_edges (fp);

  /* Go over all the nodes and set it as real node only if a corresponding
     function section exists.  */
  for (n_it = node_chain; n_it != NULL; n_it = n_it->next)
    set_node_type (n_it);

  /* Set edge types. A WEAK_EDGE has one of its nodes corresponding to a
     function that cannot be re-ordered.  */
  for (it = active_edges; it != NULL; it = it->next)
    set_edge_type (it);

  it = find_max_edge ();
  if (it != NULL)
    max_edge_value = it->weight;
  while (it != NULL)
    {
      if (!edge_over_cutoff (it->weight, max_edge_value, edge_cutoff_p,
			     edge_cutoff_a))
	{
	  if (fp !=NULL)
	    fprintf (fp, "Not considering edge with weight %llu and below\n",
		     it->weight);
          break;
	}
      collapse_edge (it);
      it = find_max_edge ();
    }
}

/* The list of sections created, excluding comdat duplicates.  */
Section_id *first_section = NULL;
/* The number of sections.  */
int num_sections = 0;

const int NUM_SECTION_TYPES = 4;
const char *section_types[] = {".text.hot.",
			       ".text.unlikely.",
			       ".text.startup.",
			       ".text." };

/* For sections that are not in the callgraph, the priority gives the
   importance of each section type.  Sections are grouped according to
   priority, higher priority (lower number).  */
const int section_priority[] = {0, 3, 1, 2};

/* Order in which the sections must be laid out is given by
   section_position[section_type].  The order in which the section
   types are laid out from address low to high are: .text.unlikely,
   .text.startup, .text., .text.hot followed by the sections grouped
   by the callgraph.  */
const int section_position[] = {3, 0, 1, 2};

/* The position of the sections grouped using the callgraph.  It comes after
   all the sections not present in the callgraph are laid out.  */
#define CALLGRAPH_POSITION NUM_SECTION_TYPES

/* Maps the function name corresponding to section SECTION_NAME to the
   object handle and the section index.  */

void
map_section_name_to_index (char *section_name, void *handle, int shndx)
{
  void **slot;
  char *function_name = NULL;
  int i, section_type = -1;

  for (i = 0; i < ARRAY_SIZE (section_types); ++i)
    {
      if (is_prefix_of (section_types[i], section_name))
        {
          function_name = section_name + strlen (section_types[i]);
  	  section_type = i;
	  break;
        }
    }

  assert (function_name != NULL && section_type >= 0);
  function_name = canonicalize_function_name (handle, function_name);
  num_sections++;

  /* Allocate section_map.  */
  if (section_map == NULL)
    {
      section_map = htab_create (NUM_FUNCTIONS,
				 section_map_htab_hash_descriptor,
				 section_map_htab_eq_descriptor , NULL);
      assert (section_map != NULL);
    }

  slot = htab_find_slot_with_hash (section_map, function_name,
				   htab_hash_string (function_name),
				   INSERT);
  if (*slot == NULL)
    {
      Section_id *section = make_section_id (function_name, section_name,
					     section_type, handle, shndx);
      /* Chain it to the list of sections.  */
      section->next = first_section;
      first_section = section;
      *slot = section;
    }
  else
    {
      /* Handle function splitting here.  With function splitting, the split
         function sections have the same name and they are in the same module.
	 Here, we only care about the section that is marked with prefix
	 like ".text.hot".  The other section is cold.  The plugin should not
	 be adding this cold section to the section_map.  In get_layout it will
	 later be picked up when processing the non-callgraph sections and it
	 will laid out appropriately.  */
      Section_id *kept = (Section_id *)(*slot);
      Section_id *section = make_section_id (function_name, section_name,
                                             section_type, handle, shndx);
      int is_split_function = 0;
      Section_id *split_comdat = NULL;
      /* Check if this is a split function. The modules are the same means this
	 is not comdat and we assume it is split.  It can be split and comdat
	 too, in which case we have to search the comdat list of kept.  */
      if (kept->handle == handle)
	is_split_function = 1;
      else if (kept->comdat_group != NULL)
	{
	  split_comdat = kept;
	  do
	    {
	      if (split_comdat->comdat_group->handle == handle)
		break;
	      split_comdat = split_comdat->comdat_group;
	    }
	  while (split_comdat->comdat_group != NULL);
	}

      /* It is split and it is comdat.  */
      if (split_comdat != NULL
	  && split_comdat->comdat_group != NULL)
	{
	  /* The comdat_section that is split.  */
	  Section_id *comdat_section = split_comdat->comdat_group;
	  Section_id *cold_section = NULL;
	  /* If the existing section is cold, the newly detected split must 
	     be hot.  */
	  if (is_prefix_of (".text.unlikely", comdat_section->full_name))
	    {
	      assert (!is_prefix_of (".text.unlikely", section_name));
	      cold_section = comdat_section;
	      /* Replace the comdat_section in the kept section list with the
		 new section.  */
	      split_comdat->comdat_group = section;
	      section->comdat_group = comdat_section->comdat_group;
	      comdat_section->comdat_group = NULL;
	    }
	  else
	    {
	      assert (is_prefix_of (".text.unlikely", section_name));
	      cold_section = section;
	    }
	  assert (cold_section != NULL && cold_section->comdat_group == NULL);
	  cold_section->is_split_cold_section = 1;
	  /* The cold section must be added to the unlikely chain of comdat
	     groups.  */
	  if (kept->split_section == NULL)
	    {	
	      /* This happens if no comdat function in this group so far has
		 been split.  */
	      kept->split_section = cold_section;
	    }
	  else
	    {
	      /* Add the cold_section to the unlikely chain of comdats.  */
	      cold_section->comdat_group = kept->split_section->comdat_group;
	      kept->split_section->comdat_group = cold_section;
	    }
	}
      /* It is split and it is not comdat.  */
      else if (is_split_function)
	{
	  Section_id *cold_section = NULL;
	  /* Function splitting means that the "hot" part is really the
	     relevant section and the other section is unlikely executed and
	     should not be part of the callgraph.  */

	  /* Store the new section in the section list.  */
	  section->next = first_section;
	  first_section = section;
	  /* If the existing section is cold, the newly detected split must 
	     be hot.  */
          if (is_prefix_of (".text.unlikely", kept->full_name))
	    {
	      assert (!is_prefix_of (".text.unlikely", section_name));
	      /* The kept section was the unlikely section.  Change the section
		 in section_map to be the new section which is the hot one.  */
	      *slot = section;
	      /* Record the split cold section in the hot section.  */
	      section->split_section = kept;
	      /* Comdats and function splitting are already handled.  */
	      assert (kept->comdat_group == NULL);
	      cold_section = kept;
	    }
	  else
	    {
	      /* Record the split cold section in the hot section.  */
	      assert (is_prefix_of (".text.unlikely", section_name));
	      kept->split_section = section;
	      cold_section = section;
	    }
	  assert (cold_section != NULL && cold_section->comdat_group == NULL);
	  cold_section->is_split_cold_section = 1;
	}
      else
	{
          /* The function already exists, it must be a COMDAT.  Only one section
	     in the comdat group will be kept, we don't know which.  Chain all
	     the comdat sections in the same comdat group to be emitted
	     together later.  Keep one section as representative (kept) and
	     update its section_type to be equal to the type of the highest
	     priority section in the group.  */

          /* Two comdats in the same group can have different priorities.  This
	     ensures that the "kept" comdat section has the priority of the
	     highest section in that comdat group.   This is necessary because
	     the plugin does not know which section will be kept.  */
          if (section_priority[kept->section_type]
	      > section_priority[section_type])
            kept->section_type = section_type;

          section->comdat_group = kept->comdat_group;
          kept->comdat_group = section;
	}
    }
}

/* Add section S to the chain SECTION_START ... SECTION_END.
   If it is a comdat, get all the comdat sections in the group.
   Chain these sections to SECTION_END.  Set SECTION_START if it
   is NULL.  */

static void
write_out_node (Section_id *s, Section_id **section_start,
	        Section_id **section_end)
{
  assert (s != NULL && s->processed == 0);
  s->processed = 1;
  if (*section_start == NULL)
    {
      *section_start = s;
      *section_end = s;
    }
  else
    {
      (*section_end)->group = s;
      *section_end = s;
    }

  /* Print all other sections in the same comdat group.  */
  while (s->comdat_group)
    {
      s = s->comdat_group;
      s->processed = 1;
      (*section_end)->group = s;
      *section_end = s;
    }
}

/* Find the max of a, b and c.  */
static unsigned long long
get_max (unsigned long long a, unsigned long long b, unsigned long long c)
{
  unsigned long long max = a;
  if (b > max)
    max = b;
  if (c > max)
    max = c;
  return max;
}

/* This is true if the max count of any bb in a function should be used as
   the node weight rather than the count of the entry bb.  */
extern int use_max_count;

/* Comparison function for sorting two sections a and b by their
   weight.  */
static 
int section_weight_compare (const void *a, const void *b)
{
  Section_id *s_a = *(Section_id **)a;
  Section_id *s_b = *(Section_id **)b;
  assert (use_max_count <= 1);
  unsigned long long max_sa_weight = get_max (s_a->weight, s_a->computed_weight,
					      s_a->max_count * use_max_count);
  unsigned long long max_sb_weight = get_max (s_b->weight, s_b->computed_weight,
					      s_b->max_count * use_max_count);

  if (max_sa_weight < max_sb_weight)
    return -1;
  else if (max_sa_weight == max_sb_weight)
    return 0;

  return 1;
}

/* s is a pointer to a section and the group of sections is linked
   via s->group.  The output is the list of sections sorted by their
   node weights (which is the maximum of their profile count, computed
   weights or the max bb count if use_max_count is true).  */
static Section_id *
sort_section_group (Section_id *s)
{
  Section_id **sort_array;
  Section_id *s_tmp;
  int num_elements = 0;
  int i;

  if (s == NULL)
    return s;

  s_tmp = s;
  while (s_tmp != NULL)
    {
      num_elements++;
      s_tmp = s_tmp->group;
    }

  if (num_elements == 1)
    return s;

  XNEWVEC_ALLOC (sort_array, Section_id *, num_elements);
  s_tmp = s;
  for (i = 0; i < num_elements; ++i)
    {
      sort_array[i] = s_tmp;
      s_tmp = s_tmp->group;
    }

  for (i = 0; i < num_elements; ++i)
    {
      sort_array[i]->group = NULL;
    }

  qsort (sort_array, num_elements, sizeof (Section_id *),
	 section_weight_compare);

  s_tmp = sort_array[0];
  for (i = 1; i < num_elements; ++i)
    {
      s_tmp->group = sort_array[i];
      s_tmp = s_tmp->group;
    }
  s_tmp->group = NULL;
  return sort_array[0];
}

/* If sort_name_prefix is true then the sections not touched by the callgraph
   are grouped according to their name prefix.  When sort_name_prefix is zero,
   all the sections are put together and sorted according to their node
   weights.  The default value of sort_name_prefix is 0.  Even when sections
   are grouped by their prefix, each group is sorted by the node weights.  */
extern int sort_name_prefix;
static int section_position_index (int section_type)
{
  assert (section_type >= 0  && section_type < NUM_SECTION_TYPES);
  if (!sort_name_prefix)
    return 0;
  else
    return section_position[section_type];
}

/* Track where the unlikely sections start and end.  This will be needed if
   the unlikely sections need to be split into a separate segment.  */
int unlikely_segment_start = -1;
int unlikely_segment_end = -1;

/* This value is used to determine the profile threshold below which the
   section is considered unlikely.  The default is zero.  */
extern unsigned long long unlikely_segment_profile_cutoff;

/* Visit each node and print the chain of merged nodes to the file.  Update
   HANDLES and SHNDX to contain the ordered list of sections.  */

unsigned int
get_layout (FILE *fp, void*** handles,
            unsigned int** shndx)
{
  Node *n_it;
  int  i = 0;
  int position;
  void *slot;
  int unlikely_section_index;

  /* Form NUM_SECTION_TYPES + 1 groups of sections.  Index 5 corresponds
     to the list of sections that correspond to functions in the callgraph.
     For other sections, they are grouped by section_type and stored in
     index: section_position[section_type]).
     SECTION_START points to the first section in each section group and
     SECTION_END points to the last.  */
  Section_id *section_start[NUM_SECTION_TYPES + 1];
  Section_id *section_end[NUM_SECTION_TYPES + 1];
  Section_id *s_it;

  XNEWVEC_ALLOC (*handles, void *, num_sections);
  XNEWVEC_ALLOC (*shndx, unsigned int, num_sections);

  for (i = 0; i < NUM_SECTION_TYPES + 1; i++)
    {
      section_start[i] = NULL;
      section_end[i] = NULL;
    }

  /* Dump edges to the final reordering file.  */
  for (n_it = node_chain; n_it != NULL; n_it = n_it->next)
    {
      Section_id *s;
      Node *node;
      /* First, only consider nodes that are real and that have other
	 nodes merged with it. */
      if (n_it->is_merged || !n_it->is_real_node || !n_it->merge_next)
        continue;

      slot = htab_find_with_hash (section_map, n_it->name,
				  htab_hash_string (n_it->name));
      assert (slot != NULL);
      s = (Section_id *)slot;
      write_out_node (s, &section_start[CALLGRAPH_POSITION],
		      &section_end[CALLGRAPH_POSITION]);

      if (fp)
	fprintf (fp, "# Callgraph group : %s", n_it->name);

      node = n_it->merge_next;
      while (node != NULL)
        {
          if (node->is_real_node)
	    {
	      slot = htab_find_with_hash (section_map, node->name,
					  htab_hash_string (node->name));
	      assert (slot != NULL);
	      s = (Section_id *)slot;
	      write_out_node (s, &section_start[CALLGRAPH_POSITION],
			      &section_end[CALLGRAPH_POSITION]);
	      if (fp)
		fprintf (fp, " %s", node->name);
	    }
          node = node->merge_next;
	}

      if (fp)
	fprintf (fp, "\n");
    }

  
  /*  Now handle all the sections that were not processed above during
      callgraph handling.  Go through all the sections and sort unprocessed
      sections into different section_type groups.  */
  s_it = first_section;
  while (s_it)
    {
      if (!s_it->processed)
	{
	  int index = section_position_index(s_it->section_type);
	  write_out_node (s_it, &section_start[index], &section_end[index]);
	}
      s_it = s_it->next;
    } 
     
  /* Determine the unlikely section index  */
  unlikely_section_index = -1;
  for (i = 0; i < ARRAY_SIZE (section_types); ++i)
    if (strcmp (".text.unlikely.", section_types[i]) == 0)
      break;

  assert (i < ARRAY_SIZE (section_types));
  unlikely_section_index = section_position_index(i);

  position = 0;
  for (i = 0; i < NUM_SECTION_TYPES + 1; ++i)
    {
      s_it = section_start[i];

      if (s_it == NULL)
	continue;

      /* Sort all section groups by weight except the callgraph group.  */
      if (i != CALLGRAPH_POSITION)
	s_it = sort_section_group (s_it);

      /* Start the unlikely segment if necessary.  */
      assert (use_max_count <= 1);
      if (i == unlikely_section_index
	  && (get_max (s_it->weight, s_it->computed_weight,
		       s_it->max_count * use_max_count)
		<= unlikely_segment_profile_cutoff))
	{
	  assert (unlikely_segment_start == -1);
	  unlikely_segment_start = position;
	  if (fp != NULL)
	    fprintf (fp, "=== Unlikely sections start ===\n");
	}

      do
        {
	  assert (position < num_sections);
          (*handles)[position] = s_it->handle;
          (*shndx)[position] = s_it->shndx;

	  /* Check if this section will end the unlikely segment.  */
	  if (i == unlikely_section_index
	      && unlikely_segment_start >= 0
              && unlikely_segment_start != position
	      && unlikely_segment_end == -1
	      && (get_max (s_it->weight, s_it->computed_weight,
		           s_it->max_count * use_max_count)
		    > unlikely_segment_profile_cutoff))
	    {
	      unlikely_segment_end = position - 1;
	      if (fp != NULL)
		fprintf (fp, "=== Unlikely sections end ===\n");
	    }

          position++;
	  if (fp != NULL)
	    {
	      fprintf (fp, "%s entry count = %llu computed = %llu "
		       "max count = %llu split = %d\n", 
		       s_it->full_name, s_it->weight,
		       s_it->computed_weight, s_it->max_count,
		       s_it->is_split_cold_section);
	    }
	  s_it = s_it->group;
        }
      while (s_it);

      /* End the unlikely segment if it has not been done already.  */
      if (i == unlikely_section_index
	  && unlikely_segment_start != -1
	  && unlikely_segment_end == -1)
	{
	  unlikely_segment_end = position - 1;
	  if (fp != NULL)
	    fprintf (fp, "=== Unlikely sections end ===\n");
	}
    }
  return position;
}

void
cleanup ()
{
  /* Go through heap allocated objects and free them.  */
  while (mm_node_chain)
    {
      mm_node *node = mm_node_chain;
      free (node->ptr);
      mm_node_chain = node->next;
      free (node);
    }

  /*  Delete all htabs. */
  htab_delete (section_map);
  htab_delete (function_map);
  htab_delete (edge_map);
}

/* Check if the callgraph is empty.  */
unsigned int
is_callgraph_empty ()
{
  if (active_edges == NULL)
    return 1;
  return 0;
}
