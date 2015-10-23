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

#ifndef CALLGRAPH_H
#define CALLGRAPH_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <hashtab.h>
#include <string.h>
#include "libiberty.h"

/* All heap allocations are tracked to be cleaned up later.  */
#define XNEW_ALLOC(A, T)	A = XNEW (T); push_allocated_ptr (A);
#define XNEWVEC_ALLOC(A, T, N)	A = XNEWVEC (T,N); push_allocated_ptr (A);

/* Push a pointer that should be freed after the plugin is done.  */
void push_allocated_ptr (void *ptr);

struct edge_d;
typedef struct edge_d Edge;

/* Maintain a list of edges.  */
typedef struct edge_list_d
{
  Edge *edge;
  struct edge_list_d *next;
  struct edge_list_d *prev;
} Edge_list;

inline static Edge_list *
make_edge_list (Edge *e)
{
  Edge_list *list;
  XNEW_ALLOC (list, Edge_list);
  list->edge = e;
  list->next = NULL;
  list->prev = NULL;
  return list;
}

/* Represents a node in the call graph. */
typedef struct node_d
{
  unsigned int id;
  char *name;
  /* Node weight, execution count of entry bb.  */
  unsigned long long weight;
  /* Weight computed by adding weights of incoming edges to
     this node.  */
  unsigned long long computed_weight;
  /* Max count of any bb executed.  */
  unsigned long long max_count;
  /* Stores the max count of any bb in the split cold section.  */
  unsigned long long split_weight;
  /* Chain all the Nodes created.  */
  struct node_d *next;
  /* Pointer to the next node in the chain of merged nodes.  */
  struct node_d *merge_next;
  /* List of all edges with this node.  */
  Edge_list *edge_list;
  /* Pointer to the last node in the chain of merged nodes.  */
  struct node_d *last_merge_node;
  unsigned int is_merged;
  /* 1 if the function corresponding to this node can be re-ordered.  */
  unsigned int is_real_node;
} Node;

inline static Node *
make_node (unsigned int id, char *name)
{
  Node *node;
  XNEW_ALLOC (node, Node);
  node->id = id;
  node->name = name;
  node->weight = 0;
  node->computed_weight = 0;
  node->max_count = 0;
  node->split_weight = 0;
  node->is_real_node = 0;
  node->next = NULL;
  node->edge_list = NULL;
  node->last_merge_node = node;
  node->is_merged = 0;
  node->merge_next = NULL;
  return node;
}

/* Chain the nodes that are merged. Maintain a pointer to the last
   node in the chain.  After merging at the end, the last node in the
   current chain is the last node in the chain of the merged node.  */
inline static void
merge_node (Node *merger, Node *mergee)
{
  merger->last_merge_node->merge_next = mergee;
  merger->last_merge_node = mergee->last_merge_node;
  mergee->is_merged = 1;
}

inline static void
add_edge_to_node (Node *n, Edge *e)
{
  Edge_list *list;
  assert (n != NULL && e != NULL);
  list = make_edge_list (e);
  list->next = n->edge_list;
  if (n->edge_list != NULL)
    n->edge_list->prev = list;
  n->edge_list = list;
}

/* A node is real only if the function can be reordered.  */
inline static void
set_as_real_node (Node *node)
{
  node->is_real_node = 1;
}

/* WEAK if one of the nodes is not real. STRONG if both
   nodes are real.  */
typedef enum edge_type_
{
  STRONG_EDGE = 0,
  WEAK_EDGE
} Edge_type;

/*Represents an edge in the call graph.  */
struct edge_d
{
  Node *first_function;
  Node *second_function;
  unsigned long long weight;
  Edge_type type;
  /* 1 if the nodes corresponding to this edge have been merged.  */
  unsigned int is_merged;
  /* Doubly linked chain of created edges.  */
  struct edge_d *prev;
  struct edge_d *next;
};

inline static Edge *
make_edge (Node *first, Node *second, unsigned long long weight)
{
  Edge *edge;
  XNEW_ALLOC (edge, Edge);
  edge->first_function = first;
  edge->second_function = second;
  edge->weight = weight;
  edge->type = WEAK_EDGE;
  edge->is_merged = 0;
  edge->prev = NULL;
  edge->next = NULL;
  add_edge_to_node (first, edge);
  add_edge_to_node (second, edge);
  return edge;
}

inline static void
set_edge_type (Edge *edge)
{
  if (edge->first_function->is_real_node
      && edge->second_function->is_real_node)
    edge->type = STRONG_EDGE;
  else
    edge->type = WEAK_EDGE;
}

inline static unsigned int
edge_lower (Edge *e1, Edge *e2)
{
  if (e1->type == e2->type)
    return (e1->weight < e2->weight) ? 1 : 0;
  if (e1->type == STRONG_EDGE)
    return 0;
  return 1;
}

inline static void
reset_functions (Edge *e, Node *n1, Node *n2)
{
  /* No self edges.  */
  assert (n1->id != n2->id);
  if (n1->id < n2->id)
    {
      e->first_function = n1;
      e->second_function = n2;
    }
  else
    {
      e->first_function = n2;
      e->second_function = n1;
    }
}

/* A Section is represented by its object handle and the section index. */
typedef struct section_id_
{
  /* Name of the function.  */
  char *name;
  /* Full name of the section.  */
  char *full_name;
  void *handle;
  int shndx;
  /* Corresponds to node weight.  */
  unsigned long long weight;
  /* Corresponds to node's computed weight.  */
  unsigned long long computed_weight;
  /* Max count of bb executed in this function.  */
  unsigned long long max_count;
  /* Type of prefix in section name.  */
  int section_type;
  /* Pointer to the next section in the same comdat_group.  */
  struct section_id_ *comdat_group;
  /* Chain all the sections created.  */
  struct section_id_ *next;
  /* Used for grouping sections.  */
  struct section_id_ *group;
  /* Pointer to the cold split section if any.   If this function
     is comdat hot and kept, pointer to the kept cold split
     section.  */
  struct section_id_ *split_section;
  /* If this is the cold part of a split section.  */
  char is_split_cold_section;
  /* Check if this section has been considered for output.  */
  char processed;
} Section_id;

inline static Section_id *
make_section_id (char *name, char *full_name,
		 int section_type,
		 void *handle, int shndx)
{
  Section_id *s;
  XNEW_ALLOC (s, Section_id);
  s->name = name;
  s->full_name = full_name;
  s->section_type = section_type;
  s->handle = handle;
  s->shndx = shndx;
  s->comdat_group = NULL;
  s->next = NULL;
  s->group = NULL;
  s->processed = 0;
  s->weight = 0;
  s->computed_weight = 0;
  s->max_count = 0;
  s->split_section = NULL;
  s->is_split_cold_section = 0;

  return s;
}

/* A pair of nodes make a raw edge.  Also, N1->id < N2->id.  */
typedef struct
{
  Node *n1;
  Node *n2;
} Raw_edge;

inline static void
init_raw_edge (Raw_edge *r, Node *n1, Node *n2)
{
  assert (n1 ->id != n2->id);
  if (n1->id < n2->id)
    {
      r->n1 = n1;
      r->n2 = n2;
    }
  else
    {
      r->n1 = n2;
      r->n2 = n1;
    }
}

inline static int is_prefix_of (const char *prefix, const char *str)
{
  return strncmp (prefix, str, strlen (prefix)) == 0;
}

/* Maps the function corresponding to section name to its
   corresponding object handle and the section index.  */
void
map_section_name_to_index (char *section_name, void *handle, int shndx);

void
parse_callgraph_section_contents (void *handle,
				  unsigned char *section_contents,
				  unsigned int length);

void dump_functions ();
void dump_edges ();
void find_pettis_hansen_function_layout (FILE *fp);

unsigned int get_layout (FILE *fp, void*** handles,
			 unsigned int** shndx);

void cleanup ();
/* Returns 1 if callgraph is empty.  */
unsigned int is_callgraph_empty ();
#endif
