/* Diagnostic subroutines for printing source-code
   Copyright (C) 1999-2016 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@codesourcery.com>

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
#include "version.h"
#include "demangle.h"
#include "intl.h"
#include "backtrace.h"
#include "diagnostic.h"
#include "diagnostic-color.h"

#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

#ifdef GWINSZ_IN_SYS_IOCTL
# include <sys/ioctl.h>
#endif

/* Classes for rendering source code and diagnostics, within an
   anonymous namespace.
   The work is done by "class layout", which embeds and uses
   "class colorizer" and "class layout_range" to get things done.  */

namespace {

/* The state at a given point of the source code, assuming that we're
   in a range: which range are we in, and whether we should draw a caret at
   this point.  */

struct point_state
{
  int range_idx;
  bool draw_caret_p;
};

/* A class to inject colorization codes when printing the diagnostic locus.

   It has one kind of colorization for each of:
     - normal text
     - range 0 (the "primary location")
     - range 1
     - range 2

   The class caches the lookup of the color codes for the above.

   The class also has responsibility for tracking which of the above is
   active, filtering out unnecessary changes.  This allows
   layout::print_source_line and layout::print_annotation_line
   to simply request a colorization code for *every* character they print,
   via this class, and have the filtering be done for them here.  */

class colorizer
{
 public:
  colorizer (diagnostic_context *context,
	     const diagnostic_info *diagnostic);
  ~colorizer ();

  void set_range (int range_idx) { set_state (range_idx); }
  void set_normal_text () { set_state (STATE_NORMAL_TEXT); }
  void set_fixit_hint () { set_state (0); }

 private:
  void set_state (int state);
  void begin_state (int state);
  void finish_state (int state);

 private:
  static const int STATE_NORMAL_TEXT = -1;

  diagnostic_context *m_context;
  const diagnostic_info *m_diagnostic;
  int m_current_state;
  const char *m_caret_cs;
  const char *m_caret_ce;
  const char *m_range1_cs;
  const char *m_range2_cs;
  const char *m_range_ce;
};

/* A point within a layout_range; similar to an expanded_location,
   but after filtering on file.  */

class layout_point
{
 public:
  layout_point (const expanded_location &exploc)
  : m_line (exploc.line),
    m_column (exploc.column) {}

  int m_line;
  int m_column;
};

/* A class for use by "class layout" below: a filtered location_range.  */

class layout_range
{
 public:
  layout_range (const location_range *loc_range);

  bool contains_point (int row, int column) const;

  layout_point m_start;
  layout_point m_finish;
  bool m_show_caret_p;
  layout_point m_caret;
};

/* A struct for use by layout::print_source_line for telling
   layout::print_annotation_line the extents of the source line that
   it printed, so that underlines can be clipped appropriately.  */

struct line_bounds
{
  int m_first_non_ws;
  int m_last_non_ws;
};

/* A class to control the overall layout when printing a diagnostic.

   The layout is determined within the constructor.
   It is then printed by repeatedly calling the "print_source_line",
   "print_annotation_line" and "print_any_fixits" methods.

   We assume we have disjoint ranges.  */

class layout
{
 public:
  layout (diagnostic_context *context,
	  const diagnostic_info *diagnostic);

  int get_first_line () const { return m_first_line; }
  int get_last_line () const { return m_last_line; }

  bool print_source_line (int row, line_bounds *lbounds_out);
  void print_annotation_line (int row, const line_bounds lbounds);
  void print_any_fixits (int row, const rich_location *richloc);

 private:
  bool
  get_state_at_point (/* Inputs.  */
		      int row, int column,
		      int first_non_ws, int last_non_ws,
		      /* Outputs.  */
		      point_state *out_state);

  int
  get_x_bound_for_row (int row, int caret_column,
		       int last_non_ws);

  void
  move_to_column (int *column, int dest_column);

 private:
  diagnostic_context *m_context;
  pretty_printer *m_pp;
  diagnostic_t m_diagnostic_kind;
  expanded_location m_exploc;
  colorizer m_colorizer;
  bool m_colorize_source_p;
  auto_vec <layout_range> m_layout_ranges;
  int m_first_line;
  int m_last_line;
  int m_x_offset;
};

/* Implementation of "class colorizer".  */

/* The constructor for "colorizer".  Lookup and store color codes for the
   different kinds of things we might need to print.  */

colorizer::colorizer (diagnostic_context *context,
		      const diagnostic_info *diagnostic) :
  m_context (context),
  m_diagnostic (diagnostic),
  m_current_state (STATE_NORMAL_TEXT)
{
  m_caret_ce = colorize_stop (pp_show_color (context->printer));
  m_range1_cs = colorize_start (pp_show_color (context->printer), "range1");
  m_range2_cs = colorize_start (pp_show_color (context->printer), "range2");
  m_range_ce = colorize_stop (pp_show_color (context->printer));
}

/* The destructor for "colorize".  If colorization is on, print a code to
   turn it off.  */

colorizer::~colorizer ()
{
  finish_state (m_current_state);
}

/* Update state, printing color codes if necessary if there's a state
   change.  */

void
colorizer::set_state (int new_state)
{
  if (m_current_state != new_state)
    {
      finish_state (m_current_state);
      m_current_state = new_state;
      begin_state (new_state);
    }
}

/* Turn on any colorization for STATE.  */

void
colorizer::begin_state (int state)
{
  switch (state)
    {
    case STATE_NORMAL_TEXT:
      break;

    case 0:
      /* Make range 0 be the same color as the "kind" text
	 (error vs warning vs note).  */
      pp_string
	(m_context->printer,
	 colorize_start (pp_show_color (m_context->printer),
			 diagnostic_get_color_for_kind (m_diagnostic->kind)));
      break;

    case 1:
      pp_string (m_context->printer, m_range1_cs);
      break;

    case 2:
      pp_string (m_context->printer, m_range2_cs);
      break;

    default:
      /* We don't expect more than 3 ranges per diagnostic.  */
      gcc_unreachable ();
      break;
    }
}

/* Turn off any colorization for STATE.  */

void
colorizer::finish_state (int state)
{
  switch (state)
    {
    case STATE_NORMAL_TEXT:
      break;

    case 0:
      pp_string (m_context->printer, m_caret_ce);
      break;

    default:
      /* Within a range.  */
      gcc_assert (state > 0);
      pp_string (m_context->printer, m_range_ce);
      break;
    }
}

/* Implementation of class layout_range.  */

/* The constructor for class layout_range.
   Initialize various layout_point fields from expanded_location
   equivalents; we've already filtered on file.  */

layout_range::layout_range (const location_range *loc_range)
: m_start (loc_range->m_start),
  m_finish (loc_range->m_finish),
  m_show_caret_p (loc_range->m_show_caret_p),
  m_caret (loc_range->m_caret)
{
}

/* Is (column, row) within the given range?
   We've already filtered on the file.

   Ranges are closed (both limits are within the range).

   Example A: a single-line range:
     start:  (col=22, line=2)
     finish: (col=38, line=2)

  |00000011111111112222222222333333333344444444444
  |34567890123456789012345678901234567890123456789
--+-----------------------------------------------
01|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
02|bbbbbbbbbbbbbbbbbbbSwwwwwwwwwwwwwwwFaaaaaaaaaaa
03|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa

   Example B: a multiline range with
     start:  (col=14, line=3)
     finish: (col=08, line=5)

  |00000011111111112222222222333333333344444444444
  |34567890123456789012345678901234567890123456789
--+-----------------------------------------------
01|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
02|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
03|bbbbbbbbbbbSwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
04|wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
05|wwwwwFaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
06|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
--+-----------------------------------------------

   Legend:
   - 'b' indicates a point *before* the range
   - 'S' indicates the start of the range
   - 'w' indicates a point within the range
   - 'F' indicates the finish of the range (which is
	 within it).
   - 'a' indicates a subsequent point *after* the range.  */

bool
layout_range::contains_point (int row, int column) const
{
  gcc_assert (m_start.m_line <= m_finish.m_line);
  /* ...but the equivalent isn't true for the columns;
     consider example B in the comment above.  */

  if (row < m_start.m_line)
    /* Points before the first line of the range are
       outside it (corresponding to line 01 in example A
       and lines 01 and 02 in example B above).  */
    return false;

  if (row == m_start.m_line)
    /* On same line as start of range (corresponding
       to line 02 in example A and line 03 in example B).  */
    {
      if (column < m_start.m_column)
	/* Points on the starting line of the range, but
	   before the column in which it begins.  */
	return false;

      if (row < m_finish.m_line)
	/* This is a multiline range; the point
	   is within it (corresponds to line 03 in example B
	   from column 14 onwards) */
	return true;
      else
	{
	  /* This is a single-line range.  */
	  gcc_assert (row == m_finish.m_line);
	  return column <= m_finish.m_column;
	}
    }

  /* The point is in a line beyond that containing the
     start of the range: lines 03 onwards in example A,
     and lines 04 onwards in example B.  */
  gcc_assert (row > m_start.m_line);

  if (row > m_finish.m_line)
    /* The point is beyond the final line of the range
       (lines 03 onwards in example A, and lines 06 onwards
       in example B).  */
    return false;

  if (row < m_finish.m_line)
    {
      /* The point is in a line that's fully within a multiline
	 range (e.g. line 04 in example B).  */
      gcc_assert (m_start.m_line < m_finish.m_line);
      return true;
    }

  gcc_assert (row ==  m_finish.m_line);

  return column <= m_finish.m_column;
}

/* Given a source line LINE of length LINE_WIDTH, determine the width
   without any trailing whitespace.  */

static int
get_line_width_without_trailing_whitespace (const char *line, int line_width)
{
  int result = line_width;
  while (result > 0)
    {
      char ch = line[result - 1];
      if (ch == ' ' || ch == '\t')
	result--;
      else
	break;
    }
  gcc_assert (result >= 0);
  gcc_assert (result <= line_width);
  gcc_assert (result == 0 ||
	      (line[result - 1] != ' '
	       && line[result -1] != '\t'));
  return result;
}

/* Implementation of class layout.  */

/* Constructor for class layout.

   Filter the ranges from the rich_location to those that we can
   sanely print, populating m_layout_ranges.
   Determine the range of lines that we will print.
   Determine m_x_offset, to ensure that the primary caret
   will fit within the max_width provided by the diagnostic_context.  */

layout::layout (diagnostic_context * context,
		const diagnostic_info *diagnostic)
: m_context (context),
  m_pp (context->printer),
  m_diagnostic_kind (diagnostic->kind),
  m_exploc (diagnostic->richloc->lazily_expand_location ()),
  m_colorizer (context, diagnostic),
  m_colorize_source_p (context->colorize_source_p),
  m_layout_ranges (rich_location::MAX_RANGES),
  m_first_line (m_exploc.line),
  m_last_line  (m_exploc.line),
  m_x_offset (0)
{
  rich_location *richloc = diagnostic->richloc;
  for (unsigned int idx = 0; idx < richloc->get_num_locations (); idx++)
    {
      /* This diagnostic printer can only cope with "sufficiently sane" ranges.
	 Ignore any ranges that are awkward to handle.  */
      const location_range *loc_range = richloc->get_range (idx);

      /* If any part of the range isn't in the same file as the primary
	 location of this diagnostic, ignore the range.  */
      if (loc_range->m_start.file != m_exploc.file)
	continue;
      if (loc_range->m_finish.file != m_exploc.file)
	continue;
      if (loc_range->m_show_caret_p)
	if (loc_range->m_caret.file != m_exploc.file)
	  continue;

      /* Everything is now known to be in the correct source file,
	 but it may require further sanitization.  */
      layout_range ri (loc_range);

      /* If we have a range that finishes before it starts (perhaps
	 from something built via macro expansion), printing the
	 range is likely to be nonsensical.  Also, attempting to do so
	 breaks assumptions within the printing code  (PR c/68473).  */
      if (loc_range->m_start.line > loc_range->m_finish.line)
	{
	  /* Is this the primary location?  */
	  if (m_layout_ranges.length () == 0)
	    {
	      /* We want to print the caret for the primary location, but
		 we must sanitize away m_start and m_finish.  */
	      ri.m_start = ri.m_caret;
	      ri.m_finish = ri.m_caret;
	    }
	  else
	    /* This is a non-primary range; ignore it.  */
	    continue;
	}

      /* Passed all the tests; add the range to m_layout_ranges so that
	 it will be printed.  */
      m_layout_ranges.safe_push (ri);

      /* Update m_first_line/m_last_line if necessary.  */
      if (ri.m_start.m_line < m_first_line)
	m_first_line = ri.m_start.m_line;
      if (ri.m_finish.m_line > m_last_line)
	m_last_line = ri.m_finish.m_line;
    }

  /* Adjust m_x_offset.
     Center the primary caret to fit in max_width; all columns
     will be adjusted accordingly.  */
  int max_width = m_context->caret_max_width;
  int line_width;
  const char *line = location_get_source_line (m_exploc.file, m_exploc.line,
					       &line_width);
  if (line && m_exploc.column <= line_width)
    {
      int right_margin = CARET_LINE_MARGIN;
      int column = m_exploc.column;
      right_margin = MIN (line_width - column, right_margin);
      right_margin = max_width - right_margin;
      if (line_width >= max_width && column > right_margin)
	m_x_offset = column - right_margin;
      gcc_assert (m_x_offset >= 0);
    }
}

/* Attempt to print line ROW of source code, potentially colorized at any
   ranges.
   Return true if the line was printed, populating *LBOUNDS_OUT.
   Return false if the source line could not be read, leaving *LBOUNDS_OUT
   untouched.  */

bool
layout::print_source_line (int row, line_bounds *lbounds_out)
{
  int line_width;
  const char *line = location_get_source_line (m_exploc.file, row,
					       &line_width);
  if (!line)
    return false;

  m_colorizer.set_normal_text ();

  /* We will stop printing the source line at any trailing
     whitespace.  */
  line_width = get_line_width_without_trailing_whitespace (line,
							   line_width);
  line += m_x_offset;

  pp_space (m_pp);
  int first_non_ws = INT_MAX;
  int last_non_ws = 0;
  int column;
  for (column = 1 + m_x_offset; column <= line_width; column++)
    {
      /* Assuming colorization is enabled for the caret and underline
	 characters, we may also colorize the associated characters
	 within the source line.

	 For frontends that generate range information, we color the
	 associated characters in the source line the same as the
	 carets and underlines in the annotation line, to make it easier
	 for the reader to see the pertinent code.

	 For frontends that only generate carets, we don't colorize the
	 characters above them, since this would look strange (e.g.
	 colorizing just the first character in a token).  */
      if (m_colorize_source_p)
	{
	  bool in_range_p;
	  point_state state;
	  in_range_p = get_state_at_point (row, column,
					   0, INT_MAX,
					   &state);
	  if (in_range_p)
	    m_colorizer.set_range (state.range_idx);
	  else
	    m_colorizer.set_normal_text ();
	}
      char c = *line == '\t' ? ' ' : *line;
      if (c == '\0')
	c = ' ';
      if (c != ' ')
	{
	  last_non_ws = column;
	  if (first_non_ws == INT_MAX)
	    first_non_ws = column;
	}
      pp_character (m_pp, c);
      line++;
    }
  pp_newline (m_pp);

  lbounds_out->m_first_non_ws = first_non_ws;
  lbounds_out->m_last_non_ws = last_non_ws;
  return true;
}

/* Print a line consisting of the caret/underlines for the given
   source line.  */

void
layout::print_annotation_line (int row, const line_bounds lbounds)
{
  int x_bound = get_x_bound_for_row (row, m_exploc.column,
				     lbounds.m_last_non_ws);

  pp_space (m_pp);
  for (int column = 1 + m_x_offset; column < x_bound; column++)
    {
      bool in_range_p;
      point_state state;
      in_range_p = get_state_at_point (row, column,
				       lbounds.m_first_non_ws,
				       lbounds.m_last_non_ws,
				       &state);
      if (in_range_p)
	{
	  /* Within a range.  Draw either the caret or an underline.  */
	  m_colorizer.set_range (state.range_idx);
	  if (state.draw_caret_p)
	    /* Draw the caret.  */
	    pp_character (m_pp, m_context->caret_chars[state.range_idx]);
	  else
	    pp_character (m_pp, '~');
	}
      else
	{
	  /* Not in a range.  */
	  m_colorizer.set_normal_text ();
	  pp_character (m_pp, ' ');
	}
    }
  pp_newline (m_pp);
}

/* If there are any fixit hints on source line ROW within RICHLOC, print them.
   They are printed in order, attempting to combine them onto lines, but
   starting new lines if necessary.  */

void
layout::print_any_fixits (int row, const rich_location *richloc)
{
  int column = 0;
  for (unsigned int i = 0; i < richloc->get_num_fixit_hints (); i++)
    {
      fixit_hint *hint = richloc->get_fixit_hint (i);
      if (hint->affects_line_p (m_exploc.file, row))
	{
	  /* For now we assume each fixit hint can only touch one line.  */
	  switch (hint->get_kind ())
	    {
	    case fixit_hint::INSERT:
	      {
		fixit_insert *insert = static_cast <fixit_insert *> (hint);
		/* This assumes the insertion just affects one line.  */
		int start_column
		  = LOCATION_COLUMN (insert->get_location ());
		move_to_column (&column, start_column);
		m_colorizer.set_fixit_hint ();
		pp_string (m_pp, insert->get_string ());
		m_colorizer.set_normal_text ();
		column += insert->get_length ();
	      }
	      break;

	    case fixit_hint::REMOVE:
	      {
		fixit_remove *remove = static_cast <fixit_remove *> (hint);
		/* This assumes the removal just affects one line.  */
		source_range src_range = remove->get_range ();
		int start_column = LOCATION_COLUMN (src_range.m_start);
		int finish_column = LOCATION_COLUMN (src_range.m_finish);
		move_to_column (&column, start_column);
		for (int column = start_column; column <= finish_column; column++)
		  {
		    m_colorizer.set_fixit_hint ();
		    pp_character (m_pp, '-');
		    m_colorizer.set_normal_text ();
		  }
	      }
	      break;

	    case fixit_hint::REPLACE:
	      {
		fixit_replace *replace = static_cast <fixit_replace *> (hint);
		int start_column
		  = LOCATION_COLUMN (replace->get_range ().m_start);
		move_to_column (&column, start_column);
		m_colorizer.set_fixit_hint ();
		pp_string (m_pp, replace->get_string ());
		m_colorizer.set_normal_text ();
		column += replace->get_length ();
	      }
	      break;

	    default:
	      gcc_unreachable ();
	    }
	}
    }
}

/* Return true if (ROW/COLUMN) is within a range of the layout.
   If it returns true, OUT_STATE is written to, with the
   range index, and whether we should draw the caret at
   (ROW/COLUMN) (as opposed to an underline).  */

bool
layout::get_state_at_point (/* Inputs.  */
			    int row, int column,
			    int first_non_ws, int last_non_ws,
			    /* Outputs.  */
			    point_state *out_state)
{
  layout_range *range;
  int i;
  FOR_EACH_VEC_ELT (m_layout_ranges, i, range)
    {
      if (range->contains_point (row, column))
	{
	  out_state->range_idx = i;

	  /* Are we at the range's caret?  is it visible? */
	  out_state->draw_caret_p = false;
	  if (row == range->m_caret.m_line
	      && column == range->m_caret.m_column)
	    out_state->draw_caret_p = range->m_show_caret_p;

	  /* Within a multiline range, don't display any underline
	     in any leading or trailing whitespace on a line.
	     We do display carets, however.  */
	  if (!out_state->draw_caret_p)
	    if (column < first_non_ws || column > last_non_ws)
	      return false;

	  /* We are within a range.  */
	  return true;
	}
    }

  return false;
}

/* Helper function for use by layout::print_line when printing the
   annotation line under the source line.
   Get the column beyond the rightmost one that could contain a caret or
   range marker, given that we stop rendering at trailing whitespace.
   ROW is the source line within the given file.
   CARET_COLUMN is the column of range 0's caret.
   LAST_NON_WS_COLUMN is the last column containing a non-whitespace
   character of source (as determined when printing the source line).  */

int
layout::get_x_bound_for_row (int row, int caret_column,
			     int last_non_ws_column)
{
  int result = caret_column + 1;

  layout_range *range;
  int i;
  FOR_EACH_VEC_ELT (m_layout_ranges, i, range)
    {
      if (row >= range->m_start.m_line)
	{
	  if (range->m_finish.m_line == row)
	    {
	      /* On the final line within a range; ensure that
		 we render up to the end of the range.  */
	      if (result <= range->m_finish.m_column)
		result = range->m_finish.m_column + 1;
	    }
	  else if (row < range->m_finish.m_line)
	    {
	      /* Within a multiline range; ensure that we render up to the
		 last non-whitespace column.  */
	      if (result <= last_non_ws_column)
		result = last_non_ws_column + 1;
	    }
	}
    }

  return result;
}

/* Given *COLUMN as an x-coordinate, print spaces to position
   successive output at DEST_COLUMN, printing a newline if necessary,
   and updating *COLUMN.  */

void
layout::move_to_column (int *column, int dest_column)
{
  /* Start a new line if we need to.  */
  if (*column > dest_column)
    {
      pp_newline (m_pp);
      *column = 0;
    }

  while (*column < dest_column)
    {
      pp_space (m_pp);
      (*column)++;
    }
}

} /* End of anonymous namespace.  */

/* Print the physical source code corresponding to the location of
   this diagnostic, with additional annotations.  */

void
diagnostic_show_locus (diagnostic_context * context,
		       const diagnostic_info *diagnostic)
{
  if (!context->show_caret
      || diagnostic_location (diagnostic, 0) <= BUILTINS_LOCATION
      || diagnostic_location (diagnostic, 0) == context->last_location)
    return;

  context->last_location = diagnostic_location (diagnostic, 0);

  pp_newline (context->printer);

  const char *saved_prefix = pp_get_prefix (context->printer);
  pp_set_prefix (context->printer, NULL);

  {
    layout layout (context, diagnostic);
    int last_line = layout.get_last_line ();
    for (int row = layout.get_first_line ();
	 row <= last_line;
	 row++)
      {
	/* Print the source line, followed by an annotation line
	   consisting of any caret/underlines, then any fixits.
	   If the source line can't be read, print nothing.  */
	line_bounds lbounds;
	if (layout.print_source_line (row, &lbounds))
	  {
	    layout.print_annotation_line (row, lbounds);
	    layout.print_any_fixits (row, diagnostic->richloc);
	  }
      }

    /* The closing scope here leads to the dtor for layout and thus
       colorizer being called here, which affects the precise
       place where colorization is turned off in the unittest
       for colorized output.  */
  }

  pp_set_prefix (context->printer, saved_prefix);
}
