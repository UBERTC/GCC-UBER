/* Function re-ordering plugin for gold.
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

/* This plugin should be invoked only when callgraph edge profile
   information is available in the object files generated using the
   compiler flag -fcallgraph-profiles-sections.  The callgraph edge
   profiles are stored in special sections marked .gnu.callgraph.*

   This plugin reads the callgraph sections and constructs an annotated
   callgraph.  It then repeatedly groups sections that are connected by
   hot edges and passes the new function layout to the linker.  The
   layout is based on the procedure reordering algorithm described
   in the paper :

   "Profile guided code positioning", K. Pettis, R. Hansen
   Proceedings of PLDI 1990.

   This plugin dumps the final layout order of the functions in a file
   called "final_layout.txt".  To change the output file, pass the new
   file name with --plugin-opt,file=<name>.  To dump to stderr instead,
   just pass stderr as the file name.

   This plugin also allows placing all functions found cold in a separate
   segment.  This can be enabled with the linker option:
   --plugin-opt,split_segment=yes.  */

#if HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#if  defined (__ELF__)
  #include <elf.h>
#endif
#include "config.h"
#include "plugin-api.h"
#include "callgraph.h"

/* #include <elf.h>   Not available on Darwin. 
   Rather than dealing with cross-compilation includes, hard code the
   values we need, as these will not change.  */
#ifndef SHT_NULL
 #define SHT_NULL 0
#endif
#ifndef SHT_PROGBITS
 #define SHT_PROGBITS 1
#endif

enum ld_plugin_status claim_file_hook (const struct ld_plugin_input_file *file,
                                       int *claimed);
enum ld_plugin_status all_symbols_read_hook ();

static ld_plugin_message message = NULL;
static ld_plugin_register_claim_file register_claim_file_hook = NULL;
static ld_plugin_register_all_symbols_read
  register_all_symbols_read_hook = NULL;
static ld_plugin_get_input_section_count get_input_section_count = NULL;
static ld_plugin_get_input_section_type get_input_section_type = NULL;
static ld_plugin_get_input_section_name get_input_section_name = NULL;
static ld_plugin_get_input_section_contents get_input_section_contents = NULL;
static ld_plugin_update_section_order update_section_order = NULL;
static ld_plugin_allow_section_ordering allow_section_ordering = NULL;
static ld_plugin_allow_unique_segment_for_sections 
    allow_unique_segment_for_sections = NULL;
static ld_plugin_unique_segment_for_sections unique_segment_for_sections = NULL;

/* The file where the final function order will be stored.
   It can be set by using the  plugin option  as --plugin-opt
   "file=<name>".  To dump to stderr, say --plugin-opt "file=stderr".  */

static char *out_file = NULL;

/* The plugin does nothing when no-op is 1.  */
static int no_op = 0;

/* The plugin creates a new segment for unlikely code if split_segment
   is set.  This can be set with the linker option:
   "--plugin-opt,split_segment=yes".  */
static int split_segment = 0;

/* If SORT_NAME_PREFIX is true then the sections not touched by the callgraph
   are grouped according to their name prefix.  When SORT_NAME_PREFIX is zero,
   all the sections are put together and sorted according to their node
   weights.  The default value of SORT_NAME_PREFIX is 0.  Even when sections
   are grouped by their prefix, each group is sorted by the node weights.  */
int sort_name_prefix = 0;

/* Edge cutoff is used to discard callgraph edges that are not above a
   certain threshold.  cutoff_p is to express this as a percent of the
   maximum value and cutoff_a is used to express this as an absolute
   value.  The default is to consider all edges.  */
unsigned int edge_cutoff_p = 0;
unsigned long long edge_cutoff_a = 0;

/* This is true if the max count of any bb in a function should be used as
   the node weight rather than the count of the entry bb.  */
int use_max_count = 1;

/* This is used to decide which sections are considered unlikely.  If the
   section profile is greater than this value then it is not unlikely
   executed.  */
unsigned long long unlikely_segment_profile_cutoff = 0;

/* Copies new output file name out_file  */
void get_filename (const char *name)
{
  XNEWVEC_ALLOC (out_file, char, (strlen (name) + 1));
  strcpy (out_file, name);
}

/* MSG_FATAL prints a format string and aborts.  Uses the plugin API if
   available, otherwise falls back to using fprintf.  */

#define MSG_FATAL(...) \
  if (message) { \
    message (LDPL_FATAL, __VA_ARGS__); } \
  else { \
    fprintf (stderr, "fatal: " __VA_ARGS__); abort (); }

/* MSG_ERROR prints a format string. Uses the plugin API if
   available, otherwise falls back to using fprintf.  */

#define MSG_ERROR(...) \
  if (message) { \
    message (LDPL_ERROR, __VA_ARGS__); } \
  else { \
    fprintf (stderr, "error: " __VA_ARGS__); }

/* Process options to plugin.  Options with prefix "group=" are special.
   They specify the type of grouping. The option "group=none" makes the
   plugin do nothing.   Options with prefix "file=" set the output file
   where the final function order must be stored.  Option "segment=none"
   does not place the cold code in a separate ELF segment.  */
static int
process_option (const char *name)
{
  const char *option_group = "group=";
  const char *option_file = "file=";
  const char *option_segment = "split_segment=";
  const char *option_edge_cutoff = "edge_cutoff=";
  const char *option_sort_name_prefix = "sort_name_prefix=";
  const char *option_max_count = "use_maxcount=";
  const char *option_unlikely_cutoff = "unlikely_cutoff=";

  /* Check if option is "group="  */
  if (strncmp (name, option_group, strlen (option_group)) == 0)
    {
      if (strcmp (name + strlen (option_group), "none") == 0)
	no_op = 1;
      else
	no_op = 0;
      return 0;
    }
  /* Check if option is "file=" */
  else if (strncmp (name, option_file, strlen (option_file)) == 0)
    {
      get_filename (name + strlen (option_file));
      return 0;
    }
  /* Check if options is "split_segment=[yes|no]"  */
  else if (strncmp (name, option_segment, strlen (option_segment)) == 0)
    {
      const char *option_val = name + strlen (option_segment);
      if (strcmp (option_val, "no") == 0)
	{
	  split_segment = 0;
	  return 0;
	}
      else if (strcmp (option_val, "yes") == 0)
	{
	  split_segment = 1;
	  return 0;
	}
    }
  else if (strncmp (name, option_edge_cutoff,
	   strlen (option_edge_cutoff)) == 0)
    {
      const char *a_or_p = name + strlen (option_edge_cutoff);
      char *endptr = NULL;
      if (a_or_p[0] == 'p')
	{
          edge_cutoff_p = strtol (a_or_p + 1, &endptr, 10);
	  /* Sanity check value entered.  */
	  if (*endptr == '\0' && edge_cutoff_p <= 100)
	    return 0;
	  if (edge_cutoff_p > 100)
	    {
	      MSG_ERROR ("Percent value > 100 in option %s\n", name);
	      return 1;
	    }
	}
      else if (a_or_p[0] == 'a')
	{
          edge_cutoff_a = strtoll (a_or_p + 1, &endptr, 10);
	  /* Sanity check value entered.  */
	  if (*endptr == '\0')
	    return 0;
	}
      MSG_ERROR ("Wrong format/non-numeric value for edge_cutoff in %s, "
   	        "use edge_cutoff=[p|a]<value>\n", name);
      return 1;
    }
  else if (strncmp (name, option_sort_name_prefix,
	   strlen (option_sort_name_prefix)) == 0)
    {
      const char *option_val = name + strlen (option_sort_name_prefix);
      if (strcmp (option_val, "no") == 0)
	{
	  sort_name_prefix = 0;
	  return 0;
	}
      else if (strcmp (option_val, "yes") == 0)
	{
	  sort_name_prefix = 1;
	  return 0;
	}
    }
  else if (strncmp (name, option_max_count,
	   strlen (option_max_count)) == 0)
    {
      const char *option_val = name + strlen (option_max_count);
      if (strcmp (option_val, "no") == 0)
	{
	  use_max_count = 0;
	  return 0;
	}
      else if (strcmp (option_val, "yes") == 0)
	{
	  use_max_count = 1;
	  return 0;
	}
    }
  /* Check if option is unlikely_cutoff.  This decides what sections are
     considered unlikely for segment splitting.  The default cutoff is 0.  */
  else if (strncmp (name, option_unlikely_cutoff,
	   strlen (option_unlikely_cutoff)) == 0)
    {
      const char *option_val = name + strlen (option_unlikely_cutoff);
      char *endptr = NULL;
      unlikely_segment_profile_cutoff = strtoll (option_val, &endptr, 10);
      /* Sanity check value entered.  */
      if (*endptr == '\0')
	return 0;
      MSG_ERROR ("Non-numeric value in option %s\n", name);
      return 1;
    }

  /* Flag error on unknown plugin option.  */
  MSG_ERROR ("Unknown option to function reordering plugin :%s\n", name);
  return 1;
}

/* Plugin entry point.  */
enum ld_plugin_status
onload (struct ld_plugin_tv *tv)
{
  struct ld_plugin_tv *entry;
  for (entry = tv; entry->tv_tag != LDPT_NULL; ++entry)
    {
      switch (entry->tv_tag)
        {
        case LDPT_API_VERSION:
          break;
        case LDPT_GOLD_VERSION:
          break;
        case LDPT_OPTION:
	  if (process_option (entry->tv_u.tv_string) == 1)
	    return LDPS_ERR;
	  /* If no_op is set, do not do anything else.  */
	  if (no_op) return LDPS_OK;
	  break;
	case LDPT_MESSAGE:
	  message = *entry->tv_u.tv_message;
	  break;
        case LDPT_REGISTER_CLAIM_FILE_HOOK:
	  register_claim_file_hook = *entry->tv_u.tv_register_claim_file;
          break;
	case LDPT_REGISTER_ALL_SYMBOLS_READ_HOOK:
	  register_all_symbols_read_hook
	    = *entry->tv_u.tv_register_all_symbols_read;
          break;
        case LDPT_GET_INPUT_SECTION_COUNT:
          get_input_section_count = *entry->tv_u.tv_get_input_section_count;
          break;
        case LDPT_GET_INPUT_SECTION_TYPE:
          get_input_section_type = *entry->tv_u.tv_get_input_section_type;
          break;
        case LDPT_GET_INPUT_SECTION_NAME:
          get_input_section_name = *entry->tv_u.tv_get_input_section_name;
          break;
        case LDPT_GET_INPUT_SECTION_CONTENTS:
          get_input_section_contents = *entry->tv_u.tv_get_input_section_contents;
          break;
	case LDPT_UPDATE_SECTION_ORDER:
	  update_section_order = *entry->tv_u.tv_update_section_order;
	  break;
	case LDPT_ALLOW_SECTION_ORDERING:
	  allow_section_ordering = *entry->tv_u.tv_allow_section_ordering;
	  break;
	case LDPT_ALLOW_UNIQUE_SEGMENT_FOR_SECTIONS:
	  allow_unique_segment_for_sections
	      = *entry->tv_u.tv_allow_unique_segment_for_sections;
	  break;
	case LDPT_UNIQUE_SEGMENT_FOR_SECTIONS:
	  unique_segment_for_sections = *entry->tv_u.tv_unique_segment_for_sections;
	  break;
        default:
          break;
        }
    }

  assert (!no_op);

  /* If the API for code reordering is missing, abort!  */
  if (register_all_symbols_read_hook == NULL
      || register_claim_file_hook == NULL
      || get_input_section_count == NULL
      || get_input_section_type == NULL
      || get_input_section_name == NULL
      || get_input_section_contents == NULL
      || update_section_order == NULL
      || allow_section_ordering == NULL)
    {
      MSG_FATAL ("API for code reordering not available\n");
    }

  /* If segment splitting is desired and the API is missing, flag error.  */
  if (split_segment == 1
      && (allow_unique_segment_for_sections == NULL
          || unique_segment_for_sections == NULL))
    {
      MSG_FATAL ("Segment splitting API not available for split_segment\n");
    }

  /* Register handlers.  */
  assert ((*register_all_symbols_read_hook) (all_symbols_read_hook)
	   == LDPS_OK);
  assert ((*register_claim_file_hook) (claim_file_hook)
	  == LDPS_OK);
  return LDPS_OK;
}

static int is_ordering_specified = 0;

/* This function is called by the linker for every new object it encounters.  */

enum ld_plugin_status
claim_file_hook (const struct ld_plugin_input_file *file, int *claimed)
{
  unsigned int count = 0;
  struct ld_plugin_section section;
  unsigned int shndx;

  (void) claimed;

  if (is_ordering_specified == 0)
    {
      /* Inform the linker to prepare for section reordering.  */
      (*allow_section_ordering) ();
      /* Inform the linker to allow certain sections to be placed in
	 a separate segment.  */
      if (split_segment == 1)
        (*allow_unique_segment_for_sections) ();
      is_ordering_specified = 1;
    }

  (*get_input_section_count) (file->handle, &count);

  for (shndx = 0; shndx < count; ++shndx)
    {
      unsigned int type = SHT_NULL;
      char *name = NULL;

      section.handle = file->handle;
      section.shndx = shndx;
      (*get_input_section_type) (section, &type);

      (*get_input_section_name) (section, &name);
      push_allocated_ptr (name);
      if (type == SHT_PROGBITS && is_prefix_of (".text.", name))
        {
          map_section_name_to_index (name, file->handle, shndx);
        }
      else if (is_prefix_of (".gnu.callgraph.text", name))
        {
	  /* Process callgraph sections.  */
          unsigned char *section_contents_ptr = NULL;
          size_t length;
          (*get_input_section_contents) (section,
	    (const unsigned char **)&section_contents_ptr,
	    &length);
	  unsigned char *section_contents;
	  XNEWVEC_ALLOC (section_contents, unsigned char, length);
	  memcpy (section_contents, section_contents_ptr, length);
          parse_callgraph_section_contents (file->handle,
					    section_contents,
					    (unsigned int)length);
        }
    }

  return LDPS_OK;
}

/* This function is called by the linker after all the symbols have been read.
   At this stage, it is fine to tell the linker the desired function order.  */

/* These globals are set to the start and end of the unlikely function sections
   in the section list, which can then be mapped to a separate segment.  */
extern int unlikely_segment_start;
extern int unlikely_segment_end;

enum ld_plugin_status
all_symbols_read_hook (void)
{
  unsigned int num_entries;
  unsigned int i;
  struct ld_plugin_section *section_list;
  void **handles;
  unsigned int *shndx;
  FILE *fp = NULL;

  if (is_callgraph_empty ())
    return LDPS_OK;

  /* Open the file to write the final layout  */
  if (out_file != NULL)
    {
      if (strcmp (out_file, "stderr") == 0)
	fp = stderr;
      else
	fp = fopen (out_file, "w");

      fprintf (fp, "# Remove lines starting with \'#\' to"
		   " pass to --section-ordering-file\n");
      fprintf (fp, "# Lines starting with \'#\' are the edge profiles\n");
    }

  find_pettis_hansen_function_layout (fp);
  num_entries = get_layout (fp, &handles, &shndx);
  XNEWVEC_ALLOC (section_list, struct ld_plugin_section, num_entries);

  for (i = 0; i < num_entries; i++)
    {
      section_list[i].handle = handles[i];
      section_list[i].shndx = shndx[i];
    }

  if (split_segment == 1
      && unlikely_segment_start >= 0
      && (unlikely_segment_end >= unlikely_segment_start))
    {
      /* Pass the new order of functions to the linker.  */
      /* Fix the order of all sections upto the beginning of the
	 unlikely section.  */
      update_section_order (section_list, unlikely_segment_start);
      assert (num_entries > unlikely_segment_end);
      /* Fix the order of all sections after the end of the unlikely
	 section.  */
      update_section_order (section_list + unlikely_segment_end + 1,
			    num_entries - unlikely_segment_end - 1);
      /* Map all unlikely code into a new segment.  */
      unique_segment_for_sections (
	  ".text.unlikely_executed", 0, 0x1000,
	  section_list + unlikely_segment_start,
	  unlikely_segment_end - unlikely_segment_start + 1);
      if (fp != NULL)
	fprintf (fp, "Moving %u section(s) to new segment\n",
		 unlikely_segment_end - unlikely_segment_start + 1);
    }
  else
    {
      /* Pass the new order of functions to the linker.  */
      update_section_order (section_list, num_entries);
    }

  if (out_file != NULL
      && strcmp (out_file, "stderr") != 0)
    fclose (fp);

  cleanup ();
  return LDPS_OK;
}
