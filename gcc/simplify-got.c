/* Simplify the code to load global variable's address from GOT.
   Copyright (C) 2011
   Free Software Foundation, Inc.
   Contributed by Wei Guozhi <carrot@google.com>.

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

/* This file contains optimization for global variable's address loading
   from GOT.

   When generating PIC code, we need to load global variable's address from
   GOT. Many targets do this as following:

        (set pic_reg ...)       # load the base address of GOT into pic_reg.
            ...
        (set off_set ...)       # load the offset from the base of GOT to
                                # a global variable's GOT entry.
        (set address            # load the address from GOT.
             (mem (plus pic_reg off_set)))
            ...

   If the target has an alternative method (usually uses a different
   relocation) to load the global address and in some cases it has less
   cost and avoid the pic_reg, we can use this pass to improve it.

   In order to employ this optimization the target must satisfy the
   following constraints:

   1. There should be at least 2 methods to load a global variable's
      address from GOT.

   2. By default all global variables accesses use the method described
      above.

   3. There is a target dependent situation that the alternative method is
      better when considering the number of global variable accesses and
      the number of accessed variables.

   4. The alternative method doesn't use the base of GOT (pic_reg).
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "flags.h"
#include "target.h"
#include "tree-pass.h"
#include "df.h"
#include "timevar.h"

#define VAR_TABLE_SIZE 10

/* Information needed when rewrite the GOT access insns.  */
struct got_access_info
{
  rtx symbol;         /* The global variable.  */
  rtx offset_reg;     /* Register contains the GOT entry offset.  */
  rtx address_reg;    /* Register contains the final global address.  */
  rtx offset_insn;    /* The insn loads the offset.  */
  rtx load_insn;      /* The insn which loads the address from GOT.  */
};

/* This optimization is enabled only when the pic_reg is actually used.  */
static bool
gate_handle_simplify_got (void)
{
  return optimize && targetm.got_access.get_pic_reg ();
}

static unsigned int
rest_of_handle_simplify_got (void)
{
  df_ref ref;
  rtx use = NULL_RTX;
  int i, n_symbol, n_access = 0;
  struct got_access_info* got_accesses;
  htab_t var_table = htab_create (VAR_TABLE_SIZE,
				  htab_hash_pointer,
				  htab_eq_pointer,
				  NULL);
  rtx pic_reg = targetm.got_access.get_pic_reg ();
  gcc_assert (pic_reg);

  ref = DF_REG_USE_CHAIN (REGNO (pic_reg));
  got_accesses = XNEWVEC(struct got_access_info,
			 DF_REG_USE_COUNT (REGNO (pic_reg)));

  /* Check if all uses of pic_reg are loading global address through the
     default method.  */
  while (ref)
    {
      rtx insn = DF_REF_INSN (ref);

      /* Check for the special USE insn, it is not a real usage of pic_reg.  */
      if (GET_CODE (PATTERN (insn)) == USE)
	use = insn;
      else
	{
	  /* If an insn both set and use pic_reg, it is in the process of
	     constructing the value of pic_reg. We should also ignore it.  */
	  rtx set = single_set (insn);
	  if (!(set && SET_DEST (set) == pic_reg))
	    {
	      rtx offset_reg;
	      rtx offset_insn;
	      rtx symbol = targetm.got_access.loaded_global_var (insn,
								 &offset_reg,
								 &offset_insn);
	      if (symbol)
		{
		  rtx* slot = (rtx*) htab_find_slot (var_table, symbol, INSERT);
		  if (*slot == HTAB_EMPTY_ENTRY)
		    *slot = symbol;

		  gcc_assert (set);
		  got_accesses[n_access].symbol = symbol;
		  got_accesses[n_access].offset_reg = offset_reg;
		  got_accesses[n_access].address_reg = SET_DEST (set);
		  got_accesses[n_access].load_insn = insn;
		  got_accesses[n_access].offset_insn = offset_insn;
		  n_access++;
		}
	      else
		{
		  /* This insn doesn't load a global address, but it has
		     other unexpected usage of pic_reg, give up.  */
		  free (got_accesses);
		  htab_delete (var_table);
		  return 0;
		}
	    }
	}
      ref = DF_REF_NEXT_REG(ref);
    }

  /* Check if we can simplify it.  */
  n_symbol = htab_elements (var_table);
  gcc_assert (n_symbol <= n_access);
  if (!targetm.got_access.can_simplify_got_access (n_symbol, n_access))
    {
      free (got_accesses);
      htab_delete (var_table);
      return 0;
    }

  /* Rewrite the global address loading insns.  */
  for (i=0; i<n_access; i++)
    targetm.got_access.load_global_address (got_accesses[i].symbol,
					    got_accesses[i].offset_reg,
					    got_accesses[i].address_reg,
					    got_accesses[i].load_insn,
					    got_accesses[i].offset_insn);

  /* Since there is no usage of pic_reg now, we can remove it.  */
  if (use)
    remove_insn (use);
  targetm.got_access.clear_pic_reg ();
  free (got_accesses);
  htab_delete (var_table);
  return 0;
}

namespace
{

const pass_data pass_data_simplify_got =
{
  RTL_PASS,                             /* type */
  "simplify_got",                       /* name */
  OPTGROUP_NONE,                        /* optinfo_flags */
  true,                                 /* has_gate */
  true,                                 /* has_execute */
  TV_SIMPLIFY_GOT,                      /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0                                     /* todo_flags_finish */
};

class pass_simplify_got : public rtl_opt_pass
{
public:
  pass_simplify_got (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_simplify_got, ctxt)
  {}

  /* opt_pass methods: */
  bool gate () { return gate_handle_simplify_got (); }
  unsigned int execute () { return rest_of_handle_simplify_got (); }

}; // class pass_simplify_got

} // anon namespace

rtl_opt_pass *
make_pass_simplify_got (gcc::context *ctxt)
{
  return new pass_simplify_got (ctxt);
}
