/* Command line option handling.  Code involving global state that
   should not be shared with the driver.
   Copyright (C) 2002-2014 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "diagnostic.h"
#include "opts.h"
#include "flags.h"
#include "tree.h" /* Required by langhooks.h.  */
#include "basic-block.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "langhooks.h"
#include "tm.h" /* Required by rtl.h.  */
#include "rtl.h"
#include "dbgcnt.h"
#include "debug.h"
#include "lto-streamer.h"
#include "output.h"
#include "plugin.h"
#include "toplev.h"
#include "tree-pass.h"
#include "params.h"
#include "l-ipo.h"
#include "context.h"
#include "xregex.h"
#include "attribs.h"
#include "stringpool.h"

typedef const char *const_char_p; /* For DEF_VEC_P.  */

static vec<const_char_p> ignored_options;

/* Input file names.  */
const char **in_fnames;
unsigned num_in_fnames;

static struct reg_func_attr_patterns
{
  regex_t r;
  const char *attribute;
  struct reg_func_attr_patterns *next;
} *reg_func_attr_patterns;

/* Return a malloced slash-separated list of languages in MASK.  */

static char *
write_langs (unsigned int mask)
{
  unsigned int n = 0, len = 0;
  const char *lang_name;
  char *result;

  for (n = 0; (lang_name = lang_names[n]) != 0; n++)
    if (mask & (1U << n))
      len += strlen (lang_name) + 1;

  result = XNEWVEC (char, len);
  len = 0;
  for (n = 0; (lang_name = lang_names[n]) != 0; n++)
    if (mask & (1U << n))
      {
	if (len)
	  result[len++] = '/';
	strcpy (result + len, lang_name);
	len += strlen (lang_name);
      }

  result[len] = 0;

  return result;
}

/* Add strings like attribute_str:pattern... to attribute pattern list.  */

static void
add_attribute_pattern (const char *arg)
{
  char *tmp;
  char *pattern_str;
  struct reg_func_attr_patterns *one_pat;
  int ec;
  
  /* We never free this string.  */
  tmp = xstrdup (arg);

  pattern_str = strchr (tmp, ':');
  if (!pattern_str)
    error ("invalid pattern in -ffunction-attribute-list option: %qs", tmp);

  *pattern_str = '\0';
  pattern_str ++;

  one_pat = XCNEW (struct reg_func_attr_patterns);
  one_pat->next = reg_func_attr_patterns;
  one_pat->attribute = tmp;
  reg_func_attr_patterns = one_pat;
  if ((ec= regcomp (&one_pat->r, pattern_str, REG_EXTENDED|REG_NOSUB) != 0))
    {
      char err[100];
      regerror (ec, &one_pat->r, err, 99);
      error ("invalid pattern in -ffunction-attribute-list option: %qs: %qs",
	     pattern_str, err);
    }
}

/* Match FNDECL's name with user specified patterns, and add attributes
   to FNDECL.  */

void
pattern_match_function_attributes (tree fndecl)
{
  const char *name;
  struct reg_func_attr_patterns *one_pat;

  if (!fndecl)
    return;

  if (!reg_func_attr_patterns)
    return;

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (fndecl));

  for (one_pat = reg_func_attr_patterns; one_pat; one_pat = one_pat->next)
    if (regexec (&one_pat->r, name, 0, NULL, 0) == 0)
      decl_attributes (&fndecl, tree_cons (
	  get_identifier (one_pat->attribute), NULL, NULL), 0);
}

/* Complain that switch DECODED does not apply to this front end (mask
   LANG_MASK).  */

static void
complain_wrong_lang (const struct cl_decoded_option *decoded,
		     unsigned int lang_mask)
{
  const struct cl_option *option = &cl_options[decoded->opt_index];
  const char *text = decoded->orig_option_with_args_text;
  char *ok_langs = NULL, *bad_lang = NULL;
  unsigned int opt_flags = option->flags;

  if (!lang_hooks.complain_wrong_lang_p (option))
    return;

  opt_flags &= ((1U << cl_lang_count) - 1) | CL_DRIVER;
  if (opt_flags != CL_DRIVER)
    ok_langs = write_langs (opt_flags);
  if (lang_mask != CL_DRIVER)
    bad_lang = write_langs (lang_mask);

  if (opt_flags == CL_DRIVER)
    error ("command line option %qs is valid for the driver but not for %s",
	   text, bad_lang);
  else if (lang_mask == CL_DRIVER)
    gcc_unreachable ();
  else
    /* Eventually this should become a hard error IMO.  */
    warning (0, "command line option %qs is valid for %s but not for %s",
	     text, ok_langs, bad_lang);

  free (ok_langs);
  free (bad_lang);
}

/* Buffer the unknown option described by the string OPT.  Currently,
   we only complain about unknown -Wno-* options if they may have
   prevented a diagnostic. Otherwise, we just ignore them.  Note that
   if we do complain, it is only as a warning, not an error; passing
   the compiler an unrecognised -Wno-* option should never change
   whether the compilation succeeds or fails.  */

static void
postpone_unknown_option_warning (const char *opt)
{
  ignored_options.safe_push (opt);
}

/* Produce a warning for each option previously buffered.  */

void
print_ignored_options (void)
{
  while (!ignored_options.is_empty ())
    {
      const char *opt;

      opt = ignored_options.pop ();
      warning_at (UNKNOWN_LOCATION, 0,
		  "unrecognized command line option \"%s\"", opt);
    }
}

/* Handle an unknown option DECODED, returning true if an error should
   be given.  */

static bool
unknown_option_callback (const struct cl_decoded_option *decoded)
{
  const char *opt = decoded->arg;

  if (opt[1] == 'W' && opt[2] == 'n' && opt[3] == 'o' && opt[4] == '-'
      && !(decoded->errors & CL_ERR_NEGATIVE))
    {
      /* We don't generate warnings for unknown -Wno-* options unless
	 we issue diagnostics.  */
      postpone_unknown_option_warning (opt);
      return false;
    }
  else
    return true;
}

/* Handle a front-end option; arguments and return value as for
   handle_option.  */

static bool
lang_handle_option (struct gcc_options *opts,
		    struct gcc_options *opts_set,
		    const struct cl_decoded_option *decoded,
		    unsigned int lang_mask ATTRIBUTE_UNUSED, int kind,
		    location_t loc,
		    const struct cl_option_handlers *handlers,
		    diagnostic_context *dc)
{
  gcc_assert (opts == &global_options);
  gcc_assert (opts_set == &global_options_set);
  gcc_assert (dc == global_dc);
  gcc_assert (decoded->canonical_option_num_elements <= 2);
  return lang_hooks.handle_option (decoded->opt_index, decoded->arg,
				   decoded->value, kind, loc, handlers);
}

/* Handle FILENAME from the command line.  */

void
add_input_filename (const char *filename)
{
  num_in_fnames++;
  in_fnames = XRESIZEVEC (const char *, in_fnames, num_in_fnames);
  in_fnames[num_in_fnames - 1] = filename;
}

/* GCC command-line options saved to the LIPO profile data file.
   See detailed comment in opts.h.  */
const char **lipo_cl_args;
unsigned num_lipo_cl_args;

/* Inspect the given GCC command-line arguments, which are part of one GCC
   switch, and decide whether or not to store these to the LIPO profile data
   file.  */
static void
lipo_save_cl_args (struct cl_decoded_option *decoded)
{
  const char *opt = decoded->orig_option_with_args_text;
  /* Store the following command-line flags to the lipo profile data file:
     (1) -f... (except -frandom-seed...)
     (2) -m...
     (3) -W...
     (4) -O...
     (5) --param...
     (6) -std=... (-std=c99 for restrict keyword)
  */
  if (opt[0] == '-'
      && (opt[1] == 'f' || opt[1] == 'm' || opt[1] == 'W' || opt[1] == 'O'
	  || (strstr (opt, "--param") == opt)
	  || (strstr (opt, "-std=")))
      && !strstr(opt, "-frandom-seed")
      && !strstr(opt, "-fripa-disallow-opt-mismatch")
      && !strstr(opt, "-Wripa-opt-mismatch"))
    {
      num_lipo_cl_args++;
      lipo_cl_args = XRESIZEVEC (const char *, lipo_cl_args, num_lipo_cl_args);
      lipo_cl_args[num_lipo_cl_args - 1] = opt;
    }
}

/* Handle the vector of command line options (located at LOC), storing
   the results of processing DECODED_OPTIONS and DECODED_OPTIONS_COUNT
   in OPTS and OPTS_SET and using DC for diagnostic state.  LANG_MASK
   contains has a single bit set representing the current language.
   HANDLERS describes what functions to call for the options.
   If COMMAND_LINE is true, this is being invoked for file level command
   line options, otherwise for an optimize pragma or function attribute.  */

static void
read_cmdline_options (struct gcc_options *opts, struct gcc_options *opts_set,
		      struct cl_decoded_option *decoded_options,
		      unsigned int decoded_options_count,
		      location_t loc,
		      unsigned int lang_mask,
		      const struct cl_option_handlers *handlers,
		      diagnostic_context *dc,
                      bool command_line)
{
  unsigned int i;
  int force_multi_module = 0;
  static int cur_mod_id = 0;

  force_multi_module = PARAM_VALUE (PARAM_FORCE_LIPO_MODE);

  for (i = 1; i < decoded_options_count; i++)
    {
      if (decoded_options[i].opt_index == OPT_SPECIAL_input_file)
	{
	  /* Input files should only ever appear on the main command
	     line.  */
	  gcc_assert (opts == &global_options);
	  gcc_assert (opts_set == &global_options_set);

	  if (opts->x_main_input_filename == NULL)
	    {
	      opts->x_main_input_filename = decoded_options[i].arg;
	      opts->x_main_input_baselength
		= base_of_path (opts->x_main_input_filename,
				&opts->x_main_input_basename);
	    }
	  add_input_filename (decoded_options[i].arg);
          if (force_multi_module)
            add_module_info (++cur_mod_id, (num_in_fnames == 1), num_in_fnames - 1);
	  continue;
	}

      read_cmdline_option (opts, opts_set,
			   decoded_options + i, loc, lang_mask, handlers,
			   dc);
      if (command_line)
        lipo_save_cl_args (decoded_options + i);
    }
}

/* Language mask determined at initialization.  */
static unsigned int initial_lang_mask;

/* Initialize global options-related settings at start-up.  */

void
init_options_once (void)
{
  /* Perform language-specific options initialization.  */
  initial_lang_mask = lang_hooks.option_lang_mask ();

  lang_hooks.initialize_diagnostics (global_dc);
}

/* Decode command-line options to an array, like
   decode_cmdline_options_to_array and with the same arguments but
   using the default lang_mask.  */

void
decode_cmdline_options_to_array_default_mask (unsigned int argc,
					      const char **argv, 
					      struct cl_decoded_option **decoded_options,
					      unsigned int *decoded_options_count)
{
  decode_cmdline_options_to_array (argc, argv,
				   initial_lang_mask | CL_COMMON | CL_TARGET,
				   decoded_options, decoded_options_count);
}

/* Set *HANDLERS to the default set of option handlers for use in the
   compilers proper (not the driver).  */
void
set_default_handlers (struct cl_option_handlers *handlers)
{
  handlers->unknown_option_callback = unknown_option_callback;
  handlers->wrong_lang_callback = complain_wrong_lang;
  handlers->num_handlers = 3;
  handlers->handlers[0].handler = lang_handle_option;
  handlers->handlers[0].mask = initial_lang_mask;
  handlers->handlers[1].handler = common_handle_option;
  handlers->handlers[1].mask = CL_COMMON;
  handlers->handlers[2].handler = target_handle_option;
  handlers->handlers[2].mask = CL_TARGET;
}

/* Parse command line options and set default flag values.  Do minimal
   options processing.  The decoded options are in *DECODED_OPTIONS
   and *DECODED_OPTIONS_COUNT; settings go in OPTS, OPTS_SET and DC;
   the options are located at LOC.  If COMMAND_LINE is true, this is
   being invoked for file level command line options, otherwise for
   an optimize pragma or function attribute.  */
void
decode_options (struct gcc_options *opts, struct gcc_options *opts_set,
		struct cl_decoded_option *decoded_options,
		unsigned int decoded_options_count,
		location_t loc, diagnostic_context *dc, bool command_line)
{
  struct cl_option_handlers handlers;

  unsigned int lang_mask;

  lang_mask = initial_lang_mask;

  set_default_handlers (&handlers);

  default_options_optimization (opts, opts_set,
				decoded_options, decoded_options_count,
				loc, lang_mask, &handlers, dc);

  read_cmdline_options (opts, opts_set,
			decoded_options, decoded_options_count,
			loc, lang_mask,
			&handlers, dc, command_line);

  finish_options (opts, opts_set, loc);
}

/* Process common options that have been deferred until after the
   handlers have been called for all options.  */

void
handle_common_deferred_options (void)
{
  unsigned int i;
  cl_deferred_option *opt;
  vec<cl_deferred_option> v;

  if (common_deferred_options)
    v = *((vec<cl_deferred_option> *) common_deferred_options);
  else
    v = vNULL;

  if (flag_dump_all_passed)
    enable_rtl_dump_file ();

  if (flag_opt_info)
    opt_info_switch_p (NULL);

  FOR_EACH_VEC_ELT (v, i, opt)
    {
      switch (opt->opt_index)
	{
	case OPT_fcall_used_:
	  fix_register (opt->arg, 0, 1);
	  break;

	case OPT_fcall_saved_:
	  fix_register (opt->arg, 0, 0);
	  break;

	case OPT_fdbg_cnt_:
	  dbg_cnt_process_opt (opt->arg);
	  break;

	case OPT_fdbg_cnt_list:
	  dbg_cnt_list_all_counters ();
	  break;

	case OPT_fdebug_prefix_map_:
	  add_debug_prefix_map (opt->arg);
	  break;

	case OPT_fdump_:
	  if (!g->get_dumps ()->dump_switch_p (opt->arg))
	    error ("unrecognized command line option %<-fdump-%s%>", opt->arg);
	  break;

        case OPT_fopt_info_:
	  if (!opt_info_switch_p (opt->arg))
	    error ("unrecognized command line option %<-fopt-info-%s%>",
                   opt->arg);
          break;

	case OPT_fenable_:
	case OPT_fdisable_:
	  if (opt->opt_index == OPT_fenable_)
	    enable_pass (opt->arg);
          else
	    disable_pass (opt->arg);
          break;

	case OPT_ffixed_:
	  /* Deferred.  */
	  fix_register (opt->arg, 1, 1);
	  break;

	case OPT_fplugin_:
#ifdef ENABLE_PLUGIN
	  add_new_plugin (opt->arg);
#else
	  error ("plugin support is disabled; configure with --enable-plugin");
#endif
	  break;

	case OPT_fplugin_arg_:
#ifdef ENABLE_PLUGIN
	  parse_plugin_arg_opt (opt->arg);
#else
	  error ("plugin support is disabled; configure with --enable-plugin");
#endif
	  break;

	case OPT_frandom_seed:
	  /* The real switch is -fno-random-seed.  */
	  if (!opt->value)
	    set_random_seed (NULL);
	  break;

	case OPT_frandom_seed_:
	  set_random_seed (opt->arg);
	  break;

	case OPT_ffunction_attribute_list_:
	  add_attribute_pattern (opt->arg);
	  break;

	case OPT_fstack_limit:
	  /* The real switch is -fno-stack-limit.  */
	  if (!opt->value)
	    stack_limit_rtx = NULL_RTX;
	  break;

	case OPT_fstack_limit_register_:
	  {
	    int reg = decode_reg_name (opt->arg);
	    if (reg < 0)
	      error ("unrecognized register name %qs", opt->arg);
	    else
	      stack_limit_rtx = gen_rtx_REG (Pmode, reg);
	  }
	  break;

	case OPT_fstack_limit_symbol_:
	  stack_limit_rtx = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (opt->arg));
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}
