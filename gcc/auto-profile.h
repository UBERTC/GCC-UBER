/* auto-profile.h - Defines data exported from auto-profile.c
   Copyright (C) 2012. Free Software Foundation, Inc.
   Contributed by Dehao Chen (dehao@google.com)

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

#ifndef AUTO_PROFILE_H
#define AUTO_PROFILE_H

#include <vector>

/* Read, process, finalize AutoFDO data structures.  */
extern void init_auto_profile (void);
extern void end_auto_profile (void);

/* Returns TRUE if EDGE is hot enough to be inlined early.  */
extern bool afdo_callsite_hot_enough_for_early_inline (struct cgraph_edge *);

/* Stores all possible call targets for NODE to RET.  */
extern void get_all_possible_call_targets (struct cgraph_node *,
					   std::vector<const char *> *);
#endif /* AUTO_PROFILE_H */
