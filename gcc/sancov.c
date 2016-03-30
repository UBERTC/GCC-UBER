/* Code coverage instrumentation for fuzzing.
   Copyright (C) 2015 Free Software Foundation, Inc.
   Contributed by Dmitry Vyukov <dvyukov@google.com>

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
#include "tree.h"
#include "basic-block.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "tree-pass.h"
#include "asan.h"

unsigned
sancov_pass ()
{
  initialize_sanitizer_builtins ();

  /* Insert callback into beginning of every BB. */
  tree fndecl = builtin_decl_implicit (BUILT_IN_SANITIZER_COV_TRACE_PC);
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfun)
    {
      gimple_stmt_iterator gsi = gsi_after_labels (bb);
      if (gsi_end_p (gsi))
	continue;
      gimple stmt = gsi_stmt (gsi);
      gimple gcall = gimple_build_call (fndecl, 0);
      gimple_set_location (gcall, gimple_location (stmt));
      gsi_insert_before (&gsi, gcall, GSI_SAME_STMT);
    }
  return 0;
}
/* The pass's gate.  */

static bool
sancov_gate (void)
{
  return flag_sanitize_coverage;
}

/* The pass descriptor.  */

namespace {

const pass_data pass_data_sancov =
{
  GIMPLE_PASS, /* type */
  "sancov", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  true, /* has_gate */
  true, /* has_execute */
  TV_NONE, /* tv_id */
  ( PROP_cfg ), /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  ( TODO_update_ssa ), /* todo_flags_finish */
};

class pass_sancov : public gimple_opt_pass
{
public:
  pass_sancov (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_sancov, ctxt)
  {}

  /* opt_pass methods: */
  opt_pass * clone () { return new pass_sancov (m_ctxt); }
  bool gate () { return sancov_gate (); }
  unsigned int execute () { return sancov_pass (); }

}; // class pass_sancov

} // anon namespace

gimple_opt_pass *
make_pass_sancov (gcc::context *ctxt)
{
  return new pass_sancov (ctxt);
}

static bool
sancov_gate_O0 (void)
{
  return flag_sanitize_coverage && !optimize;
}

namespace {

const pass_data pass_data_sancov_O0 =
{
  GIMPLE_PASS, /* type */
  "sancov0", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  true, /* has_gate */
  true, /* has_execute */
  TV_NONE, /* tv_id */
  ( PROP_cfg ), /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  ( TODO_update_ssa ), /* todo_flags_finish */
};

class pass_sancov_O0 : public gimple_opt_pass
{
public:
  pass_sancov_O0 (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_sancov_O0, ctxt)
  {}

  /* opt_pass methods: */
  bool gate () { return sancov_gate_O0 (); }
  unsigned int execute () { return sancov_pass (); }

}; // class pass_sancov_O0

} // anon namespace

gimple_opt_pass *
make_pass_sancov_O0 (gcc::context *ctxt)
{
  return new pass_sancov_O0 (ctxt);
}
