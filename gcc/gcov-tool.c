/* Gcc offline profile processing tool support. */
/* Copyright (C) 2014 Free Software Foundation, Inc.
   Contributed by Rong Xu <xur@google.com>.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "intl.h"
#include "hashtab.h"
#include "diagnostic.h"
#include "version.h"
#include "gcov-io.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#if !defined(_WIN32)
#include <ftw.h>
#endif
#include <getopt.h>
#include "params.h"
#include <string.h>

extern int gcov_profile_merge (struct gcov_info*, struct gcov_info*, int, int);
extern int gcov_profile_overlap (struct gcov_info*, struct gcov_info*);
extern int gcov_profile_normalize (struct gcov_info*, gcov_type);
extern int gcov_profile_scale (struct gcov_info*, float, int, int);
extern struct gcov_info* gcov_read_profile_dir (const char*, int);
extern void gcov_exit (void);
extern void set_gcov_list (struct gcov_info *);
extern void gcov_set_verbose (void);
extern void set_use_existing_grouping (void);
extern void set_use_modu_list (void);
extern void lipo_set_substitute_string (const char *);

/* The following defines are needed by dyn-ipa.c.
   They will also be emitted by the compiler with -fprofile-generate,
   which means this file cannot be compiled with -fprofile-generate
   -- otherwise we get duplicated defintions.
   Make the defines weak to link with other objects/libraries
   that potentially compiled with -fprofile-generate.  */
#if !defined(_WIN32)
#define WEAK_ATTR __attribute__ ((weak))
#else
#define WEAK_ATTR
#endif

WEAK_ATTR gcov_unsigned_t __gcov_lipo_grouping_algorithm;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_merge_modu_edges;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_weak_inclusion;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_max_mem;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_comdat_algorithm;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_random_group_size;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_cutoff;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_random_seed;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_dump_cgraph;
WEAK_ATTR gcov_unsigned_t __gcov_lipo_propagate_scale;

#undef WEAK_ATTR

/* Set to verbose output mode.  */
static bool verbose;

/* Remove file NAME if it has a gcda suffix. */

#if !defined(_WIN32)
static int
unlink_gcda_file (const char *name,
                  const struct stat *status ATTRIBUTE_UNUSED,
                  int type ATTRIBUTE_UNUSED,
                  struct FTW *ftwbuf ATTRIBUTE_UNUSED)
{
  int ret = 0;
  int len = strlen (name);
  int len1 = strlen (GCOV_DATA_SUFFIX);

  if (len > len1 && !strncmp (len -len1 + name, GCOV_DATA_SUFFIX, len1))
    ret = remove (name);

  if (ret)
    fatal_error ("error in removing %s\n", name);

  return ret;
}
#endif

/* Remove the gcda files in PATH recursively.  */

static int
unlink_profile_dir (const char *path)
{
#if !defined(_WIN32)
    return nftw(path, unlink_gcda_file, 64, FTW_DEPTH | FTW_PHYS);
#else
    return 0;
#endif
}


/* Output GCOV_INFO lists PROFILE to directory OUT. Note that
   we will remove all the gcda files in OUT.  */

static void
gcov_output_files (const char *out, struct gcov_info *profile)
{
  char *pwd;
  int ret;

  /* Try to make directory if it doesn't already exist.  */
  if (access (out, F_OK) == -1)
    {
      if (mkdir (out, S_IRWXU | S_IRWXG | S_IRWXO) == -1 && errno != EEXIST)
        fatal_error ("Cannot make directory %s", out);
    } else
      unlink_profile_dir (out);

  /* Output new profile.  */
  pwd = getcwd (NULL, 0);

  if (pwd == NULL)
    fatal_error ("Cannot get current directory name");

  ret = chdir (out);
  if (ret)
    fatal_error ("Cannot change directory to %s", out);

  set_gcov_list (profile);
  gcov_exit ();

  ret = chdir (pwd);
  if (ret)
    fatal_error ("Cannot change directory to %s", pwd);

  free (pwd);
}

/* Merging profile D1 and D2 with weight as W1 and W2, respectively.
   The result profile is written to directory OUT.
   Return 0 on success.  */

static int
profile_merge (const char *d1, const char *d2, const char *out, int w1, int w2)
{
  struct gcov_info *d1_profile;
  struct gcov_info *d2_profile;
  int ret;

  d1_profile = gcov_read_profile_dir (d1, 0);
  if (!d1_profile)
    return 1;

  if (d2)
    {
      d2_profile = gcov_read_profile_dir (d2, 0);
      if (!d2_profile)
        return 1;

      /* The actual merge: we overwrite to d1_profile.  */
      ret = gcov_profile_merge (d1_profile, d2_profile, w1, w2);

      if (ret)
        return ret;
    }

  gcov_output_files (out, d1_profile);

  return 0;
}

/* Usage message for profile merge.  */

static void
print_merge_usage_message (int error_p)
{
  FILE *file = error_p ? stderr : stdout;

  fnotice (file, "  merge [options] <dir1> <dir2>         Merge coverage file contents\n");
  fnotice (file, "    -v, --verbose                       Verbose mode\n");
  fnotice (file, "    -o, --output <dir>                  Output directory\n");
  fnotice (file, "    -w, --weight <w1,w2>                Set weights (float point values)\n");
}

static const struct option merge_options[] =
{
  { "verbose",                no_argument,       NULL, 'v' },
  { "output",                 required_argument, NULL, 'o' },
  { "weight",                 required_argument, NULL, 'w' },
  { 0, 0, 0, 0 }
};

/* Print merge usage and exit.  */

static void
merge_usage (void)
{
  fnotice (stderr, "Merge subcomand usage:");
  print_merge_usage_message (true);
  exit (FATAL_EXIT_CODE);
}

/* Driver for profile merge sub-command.  */

static int
do_merge (int argc, char **argv)
{
  int opt;
  int ret;
  const char *output_dir = 0;
  int w1 = 1, w2 = 1;

  optind = 0;
  while ((opt = getopt_long (argc, argv, "vo:w:", merge_options, NULL)) != -1)
    {
      switch (opt)
        {
        case 'v':
          verbose = true;
          gcov_set_verbose ();
          break;
        case 'o':
          output_dir = optarg;
          break;
        case 'w':
          sscanf (optarg, "%d,%d", &w1, &w2);
          if (w1 < 0 || w2 < 0)
            fatal_error ("weights need to be non-negative\n");
          break;
        default:
          merge_usage ();
        }
    }

  if (output_dir == NULL)
    output_dir = "merged_profile";

  if (argc - optind == 2)
    ret = profile_merge (argv[optind], argv[optind+1], output_dir, w1, w2);
  else
    merge_usage ();

  return ret;
}

/* If N_VAL is no-zero, normalize the profile by setting the largest counter
   counter value to N_VAL and scale others counters proportionally.
   Otherwise, multiply the all counters by SCALE.  */

static int
profile_rewrite (const char *d1, const char *out, long long n_val,
                 float scale, int n, int d)
{
  struct gcov_info * d1_profile;

  d1_profile = gcov_read_profile_dir (d1, 0);
  if (!d1_profile)
    return 1;

  if (n_val)
    gcov_profile_normalize (d1_profile, (gcov_type) n_val);
  else
    gcov_profile_scale (d1_profile, scale, n, d);

  gcov_output_files (out, d1_profile);
  return 0;
}

/* This is the hashtab entry to store a name and mod_id pair. */
typedef struct {
  const char *name;
  unsigned id;
} mod_name_id;

/* Hash and comparison functions for strings.  */

static unsigned
mod_name_id_htab_hash (const void *s_p)
{
  const char *s = ((const mod_name_id *) s_p)->name;
  return (*htab_hash_string) (s);
}

static int
mod_name_id_hash_eq (const void *s1_p, const void *s2_p)
{
  return strcmp (((const mod_name_id *) s1_p)->name,
                 ((const mod_name_id *) s2_p)->name) == 0;
}

static htab_t mod_name_id_hash_table;

/* Look up an entry in the hash table. STRING is the module name.
   CREATE controls to insert to htab or not.
   If (*ID_P != 0), we write (*ID_P) to htab.
   If (*ID_P == 0), we write module_id to (*ID_P).
   return 1 if an entry is found and otherwise 0.  */

static int
module_name_hash_lookup (const char *string, unsigned *id_p, int create)
{
  void **e;
  mod_name_id t;

  t.name = string;
  e = htab_find_slot (mod_name_id_hash_table, &t,
                      create ? INSERT : NO_INSERT);
  if (e == NULL)
    return 0;
  if (*e == NULL)
    {
      *e = XNEW (mod_name_id *);
      (*(mod_name_id **)e)->name = xstrdup (string);
    }
  if (id_p)
    {
      if (*id_p != 0)
        (*(mod_name_id **)e)->id = *id_p;
      else
        *id_p = (*(mod_name_id **)e)->id;
    }
  return 1;
}

#if !defined(_WIN32)
#define STRCASESTR strcasestr
#else
#define STRCASESTR strstr
#endif

/* Return 1 if NAME is of a source type that LIPO targets.
   Return 0 otherwise.  */

static int
is_lipo_source_type (char *name)
{
  char *p;

  if (STRCASESTR (name, ".c") ||
      STRCASESTR (name, ".cc") ||
      STRCASESTR (name, ".cpp") ||
      STRCASESTR (name, ".c++"))
    return 1;

  /* Replace ".proto" with ".pb.cc". Since the two strings have the same
     length, we simplfy do a strcpy.  */
  if ((p = STRCASESTR (name, ".proto")) != NULL)
    {
      strcpy (p, ".pb.cc");
      return 1;
    }

  return 0;
}

/* Convert/process the names from dependence query to a
   stardard format. Return NULL if this is not a lipo
   target source. */

static char *
lipo_process_name_string (char *name)
{
  char *p;

  if (name == NULL)
    return NULL;
  if (strlen (name) == 0)
    return NULL;

  if (!is_lipo_source_type (name))
    return NULL;

  /* Overwrite ':' with '/'.  */
  if ((p = strchr (name, ':')) != NULL)
    *p = '/';

  /* Remove "//".  */
  if (name[0] == '/' && name[1] =='/')
    name += 2;

  return name;
}

/* Store the list of source modules in INPUT_FILE to internal hashtab.  */

static int
lipo_process_modu_list (const char *input_file)
{
  FILE *fd;
  const int max_line_size = (1 << 12);
  char line[max_line_size];
  char *name;

  set_use_modu_list ();

  if ((fd = fopen (input_file, "r")) == NULL)
    {
      fnotice (stderr, "Cannot open %s\n", input_file);
      return -1;
    }

  /* Read all the modules */
  while (fgets (line, max_line_size, fd) != NULL)
    {
      name = strtok (line, " \t\n");
      name = lipo_process_name_string (name);
      if (name)
        module_name_hash_lookup (name, 0, 1);
    }

  return 0;
}

#define GENFILE_PREFIX "/genfiles/"

/* Return 1 if module NAME is available to be used in the target
   profile.  CREATE controls to insert to htab or not.
   If (*ID_P != 0), we write (*ID_P) to htab.
   If (*ID_P == 0), we write module_id to (*ID_P).
   return 1 if an entry is found and otherwise 0.  */

int
is_module_available (const char *name, unsigned *id_p, int create)
{
  char *buf, *p;
  int ret;

  if (mod_name_id_hash_table == NULL)
    return 1;

  buf = xstrdup (name);
  /* Remove genfile string.  */
  if ((p = strstr (buf, GENFILE_PREFIX)) != NULL)
    p += strlen (GENFILE_PREFIX);
  else
    p = buf;

   ret = module_name_hash_lookup (p, id_p, create);
   free (buf);
   return ret;
}

/* Return module_ident for module NAME.
   return 0 if the module NAME is not available.  */

int
get_module_id_from_name (const char *name)
{
  unsigned mod_id = 0;
  if (is_module_available (name, &mod_id, 0) == 1)
    return mod_id;
  return 0;
}

/* Usage function for profile rewrite.  */

static void
print_rewrite_usage_message (int error_p)
{
  FILE *file = error_p ? stderr : stdout;

  fnotice (file, "  rewrite [options] <dir>               Rewrite coverage file contents\n");
  fnotice (file, "    -v, --verbose                       Verbose mode\n");
  fnotice (file, "    -o, --output <dir>                  Output directory\n");
  fnotice (file, "    -l, --modu_list <file>              Only use the modules in this file\n");
  fnotice (file, "    -r, --path_substr_replace <str>     Replace string in path\n");
  fnotice (file, "    -s, --scale <float or simple-frac>  Scale the profile counters\n");
  fnotice (file, "    -u, --use_imports_file <file>       Use the grouping in import files.\n");
  fnotice (file, "    -n, --normalize <long long>         Normalize the profile\n");
}

static const struct option rewrite_options[] =
{
  { "verbose",                no_argument,       NULL, 'v' },
  { "output",                 required_argument, NULL, 'o' },
  { "modu_list",              required_argument, NULL, 'l' },
  { "string",                 required_argument, NULL, 'r' },
  { "scale",                  required_argument, NULL, 's' },
  { "use_imports_file",       no_argument,       NULL, 'u' },
  { "normalize",              required_argument, NULL, 'n' },
  { 0, 0, 0, 0 }
};

/* Print profile rewrite usage and exit.  */

static void
rewrite_usage (void)
{
  fnotice (stderr, "Rewrite subcommand usage:");
  print_rewrite_usage_message (true);
  exit (FATAL_EXIT_CODE);
}

/* Driver for profile rewrite sub-command. */

static int
do_rewrite (int argc, char **argv)
{
  int opt;
  int ret;
  const char *output_dir = 0;
  long long normalize_val = 0;
  float scale = 0.0;
  int numerator = 1;
  int denominator = 1;
  int do_scaling = 0;

  mod_name_id_hash_table = htab_create (500, mod_name_id_htab_hash,
                                        mod_name_id_hash_eq, NULL);

  optind = 0;
  while ((opt = getopt_long (argc, argv, "vo:l:r:s:un:", rewrite_options,
                             NULL)) != -1)
    {
      switch (opt)
        {
        case 'v':
          verbose = true;
          gcov_set_verbose ();
          break;
        case 'o':
          output_dir = optarg;
          break;
        case 'l':
          lipo_process_modu_list (optarg);
          break;
        case 'r':
          lipo_set_substitute_string (optarg);
          break;
        case 'u':
          set_use_existing_grouping ();
          break;
        case 'n':
          if (!do_scaling)
            normalize_val = atoll (optarg);
          else
            fnotice (stderr, "scaling cannot co-exist with normalization,"
                " skipping\n");
          break;
        case 's':
          ret = 0;
          do_scaling = 1;
          if (strstr (optarg, "/"))
            {
              ret = sscanf (optarg, "%d/%d", &numerator, &denominator);
              if (ret == 2)
                {
                  if (numerator < 0 || denominator <= 0)
                    {
                      fnotice (stderr, "incorrect format in scaling, using 1/1\n");
                      denominator = 1;
                      numerator = 1;
                    }
                }
            }
          if (ret != 2)
            {
              ret = sscanf (optarg, "%f", &scale);
              if (ret != 1)
                fnotice (stderr, "incorrect format in scaling, using 1/1\n");
              else
                denominator = 0;
            }

          if (scale < 0.0)
            fatal_error ("scale needs to be non-negative\n");

          if (normalize_val != 0)
            {
              fnotice (stderr, "normalization cannot co-exist with scaling\n");
              normalize_val = 0;
            }
          break;
        default:
          rewrite_usage ();
        }
    }

  if (output_dir == NULL)
    output_dir = "rewrite_profile";

  if (argc - optind == 1)
    {
      if (denominator > 0)
        ret = profile_rewrite (argv[optind],  output_dir, 0, 0.0, numerator, denominator);
      else
        ret = profile_rewrite (argv[optind],  output_dir, normalize_val, scale, 0, 0);
    }
  else
    rewrite_usage ();

  return ret;
}

/* Driver function to computer the overlap score b/w profile D1 and D2.
   Return 1 on error and 0 if OK.  */

static int
profile_overlap (const char *d1, const char *d2)
{
  struct gcov_info *d1_profile;
  struct gcov_info *d2_profile;

  d1_profile = gcov_read_profile_dir (d1, 0);
  if (!d1_profile)
    return 1;

  if (d2)
    {
      d2_profile = gcov_read_profile_dir (d2, 0);
      if (!d2_profile)
        return 1;

      return gcov_profile_overlap (d1_profile, d2_profile);
    }

  return 1;
}

/* Usage message for profile overlap.  */

static void
print_overlap_usage_message (int error_p)
{
  FILE *file = error_p ? stderr : stdout;

  fnotice (file, "  overlap [options] <dir1> <dir2>       Compute the overlap of two profiles\n");
  fnotice (file, "    -v, --verbose                       Verbose mode\n");
  fnotice (file, "    -h, --hotonly                       Only print info for hot objects/functions\n");
  fnotice (file, "    -f, --function                      Print function level info\n");
  fnotice (file, "    -F, --fullname                      Print full filename\n");
  fnotice (file, "    -o, --object                        Print object level info\n");
  fnotice (file, "    -t <float>, --hot_threshold <float> Set the threshold for hotness\n");

}

static const struct option overlap_options[] =
{
  { "verbose",                no_argument,       NULL, 'v' },
  { "function",               no_argument,       NULL, 'f' },
  { "fullname",               no_argument,       NULL, 'F' },
  { "object",                 no_argument,       NULL, 'o' },
  { "hotonly",                no_argument,       NULL, 'h' },
  { "hot_threshold",          required_argument, NULL, 't' },
  { 0, 0, 0, 0 }
};

/* Print overlap usage and exit.  */

static void
overlap_usage (void)
{
  fnotice (stderr, "Overlap subcomand usage:");
  print_overlap_usage_message (true);
  exit (FATAL_EXIT_CODE);
}

int overlap_func_level;
int overlap_obj_level;
int overlap_hot_only;
int overlap_use_fullname;
double overlap_hot_threshold = 0.005;

/* Driver for profile overlap sub-command.  */

static int
do_overlap (int argc, char **argv)
{
  int opt;
  int ret;

  optind = 0;
  while ((opt = getopt_long (argc, argv, "vfFoht:", overlap_options, NULL)) != -1)
    {
      switch (opt)
        {
        case 'v':
          verbose = true;
          gcov_set_verbose ();
          break;
        case 'f':
          overlap_func_level = 1;
          break;
        case 'F':
          overlap_use_fullname = 1;
          break;
        case 'o':
          overlap_obj_level = 1;
          break;
        case 'h':
          overlap_hot_only = 1;
          break;
        case 't':
          overlap_hot_threshold = atof (optarg);
          break;
        default:
          overlap_usage ();
        }
    }

  if (argc - optind == 2)
    ret = profile_overlap (argv[optind], argv[optind+1]);
  else
    overlap_usage ();

  return ret;
}


/* Print a usage message and exit.  If ERROR_P is nonzero, this is an error,
   otherwise the output of --help.  */

static void
print_usage (int error_p)
{
  FILE *file = error_p ? stderr : stdout;
  int status = error_p ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE;

  fnotice (file, "Usage: %s [OPTION]... SUB_COMMAND [OPTION]...\n\n", progname);
  fnotice (file, "Offline tool to handle gcda counts\n\n");
  fnotice (file, "  -h, --help                            Print this help, then exit\n");
  fnotice (file, "  -v, --version                         Print version number, then exit\n");
  fnotice (file, "  -A, --lipo_algorithm <0|1>            Choose LIPO module grouping algorithm\n");
  fnotice (file, "  -E, --lipo_merge_edge                 Merge module edges in LIPO module grouping\n");
  fnotice (file, "  -W, --lipo_weak_inclusion             Don't force strict inclusion in grouping\n");
  fnotice (file, "  -C, --lipo_cutoff <0..100>            Set LIPO module grouping cutoff\n");
  fnotice (file, "  -M, --lipo_max_memory <int>           Set the max memory in LIPO module grouping\n");
  fnotice (file, "  -F, --lipo_comdat_algorithm <0|1|2|3> Set the COMDAT fixup algorithm\n");
  fnotice (file, "  -R, --lipo_random_group_size <int>    Set LIPO random grouping size\n");
  fnotice (file, "  -S, --lipo_random_group_seed <int>    Set LIPO random grouping seed\n");
  fnotice (file, "  -D, --lipo_dump_cgraph                Dump dynamic call graph\n");
  fnotice (file, "  -P, --lipo_propagate_scale            Set LIPO propagate scale to true\n");
  fnotice (file, "\n");
  print_merge_usage_message (error_p);
  print_rewrite_usage_message (error_p);
  print_overlap_usage_message (error_p);
  fnotice (file, "\nFor bug reporting instructions, please see:\n%s.\n",
           bug_report_url);
  exit (status);
}

/* Print version information and exit.  */

static void
print_version (void)
{
  fnotice (stdout, "%s %s%s\n", progname, pkgversion_string, version_string);
  fnotice (stdout, "Copyright %s 2014 Free Software Foundation, Inc.\n",
           _("(C)"));
  fnotice (stdout,
           _("This is free software; see the source for copying conditions.\n"
             "There is NO warranty; not even for MERCHANTABILITY or \n"
             "FITNESS FOR A PARTICULAR PURPOSE.\n\n"));
  exit (SUCCESS_EXIT_CODE);
}

static const struct option options[] =
{
  { "help",                   no_argument,       NULL, 'h' },
  { "version",                no_argument,       NULL, 'v' },
  { "lipo_algorithm",         required_argument, NULL, 'A' },
  { "lipo_merge_edge",        no_argument,       NULL, 'E' },
  { "lipo_weak_inclusion",    no_argument,       NULL, 'W' },
  { "lipo_cutoff",            required_argument, NULL, 'C' },
  { "lipo_max_memory",        required_argument, NULL, 'M' },
  { "lipo_comdat_algorithm",  required_argument, NULL, 'F' },
  { "lipo_random_group_size", required_argument, NULL, 'R' },
  { "lipo_random_group_seed", required_argument, NULL, 'S' },
  { "lipo_dump_cgraph",       no_argument,       NULL, 'D' },
  { "lipo_propagate_scale",   no_argument,       NULL, 'P' },
  { 0, 0, 0, 0 }
};

/* Process args, return index to first non-arg.  */

static int
process_args (int argc, char **argv)
{
  int opt;
  int ret;

  while ((opt = getopt_long (argc, argv, "+hvA:EWC:M:R:S:DPF:", options, NULL)) != -1)
    {
      switch (opt)
        {
        case 'h':
          print_usage (false);
          /* Print_usage will exit.  */
        case 'v':
          print_version ();
          /* Print_version will exit.  */
        case 'E':
          __gcov_lipo_merge_modu_edges = 1;
          break;
        case 'W':
          __gcov_lipo_weak_inclusion = 1;
          break;
        case 'D':
          __gcov_lipo_dump_cgraph = 1;
          break;
        case 'P':
          __gcov_lipo_propagate_scale = 1;
          break;
        case 'A':
          sscanf (optarg, "%d", &ret);
          if (ret != 0 && ret != 1)
            {
              fnotice (stderr, "LIPO grouping algorithm can only be 0 or 1\n");
              exit (-1);
            }
          __gcov_lipo_grouping_algorithm = ret;
          break;
        case 'R':
          sscanf (optarg, "%d", &ret);
          if (ret < 1)
            {
              fnotice (stderr, "LIPO random group size needs to be positive\n");
              exit (-1);
            }
          __gcov_lipo_random_group_size = ret;
          break;
        case 'S':
          sscanf (optarg, "%d", &ret);
          __gcov_lipo_random_seed = ret;;
          break;
        case 'M':
          sscanf (optarg, "%d", &ret);
          if (ret < 0)
            {
              fnotice (stderr, "LIPO max-memory size needs to be positive\n");
              exit (-1);
            }
          __gcov_lipo_max_mem = ret;
          break;
        case 'F':
          sscanf (optarg, "%d", &ret);
          if (ret < 0)
            {
              fnotice (stderr,
                       "LIPO COMDAT fixup algorithm needs to be positive\n");
              exit (-1);
            }
          __gcov_lipo_comdat_algorithm = ret;
          break;
        case 'C':
          sscanf (optarg, "%d", &ret);
          if (ret < 0 || ret > 100)
            {
              fnotice (stderr, "LIPO cutoff value range is [0, 100]\n");
              exit (-1);
            }
          __gcov_lipo_cutoff = ret;;
          break;
        default:
          print_usage (true);
          /* Print_usage will exit.  */
        }
    }

  return optind;
}

/* Get the default param value from params.def.  */

#define GET_DEFAULT_PARAM_VALUE(p) compiler_params[p].default_value
static void
set_lipo_default_params (void)
{
  __gcov_lipo_grouping_algorithm = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_GROUPING_ALGORITHM);
  __gcov_lipo_merge_modu_edges   = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_MERGE_MODU_EDGES);
  __gcov_lipo_weak_inclusion     = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_WEAK_INCLUSION);
  __gcov_lipo_max_mem            = GET_DEFAULT_PARAM_VALUE (PARAM_MAX_LIPO_MEMORY);
  __gcov_lipo_comdat_algorithm   = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_COMDAT_ALGORITHM);
  __gcov_lipo_random_group_size  = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_RANDOM_GROUP_SIZE);
  __gcov_lipo_cutoff             = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_CUTOFF);
  __gcov_lipo_random_seed        = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_RANDOM_SEED);
  __gcov_lipo_dump_cgraph        = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_DUMP_CGRAPH);
  __gcov_lipo_propagate_scale    = GET_DEFAULT_PARAM_VALUE (PARAM_LIPO_PROPAGATE_SCALE);
}

/* Main function for gcov-tool.  */

int
main (int argc, char **argv)
{
  const char *p;
  const char *sub_command;

  p = argv[0] + strlen (argv[0]);
  while (p != argv[0] && !IS_DIR_SEPARATOR (p[-1]))
    --p;
  progname = p;

  xmalloc_set_program_name (progname);

  /* Unlock the stdio streams.  */
  unlock_std_streams ();

  gcc_init_libintl ();

  diagnostic_initialize (global_dc, 0);

  /* Handle response files.  */
  expandargv (&argc, &argv);

  /* Register the language-independent parameters.  */
  global_init_params ();
  finish_params ();
  set_lipo_default_params ();

  process_args (argc, argv);
  if (optind >= argc)
    print_usage (true);

  sub_command = argv[optind];

  if (!strcmp (sub_command, "merge"))
    return do_merge (argc - optind, argv + optind);
  else if (!strcmp (sub_command, "rewrite"))
    return do_rewrite (argc - optind, argv + optind);
  else if (!strcmp (sub_command, "overlap"))
    return do_overlap (argc - optind, argv + optind);

  print_usage (true);
}
