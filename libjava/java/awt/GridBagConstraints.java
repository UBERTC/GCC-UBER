// GridBagConstraints.java - Constraints for GridBag layout manager

/* Copyright (C) 2000  Free Software Foundation

   This file is part of libgcj.

This software is copyrighted work licensed under the terms of the
Libgcj License.  Please consult the file "LIBGCJ_LICENSE" for
details.  */

package java.awt;

import java.io.Serializable;

/** This specifies the constraints for a component managed by the
 * GridBagLayout layout manager.  */
public class GridBagConstraints implements Cloneable, Serializable
{
  /** Fill in both directions.  */
  public static final int BOTH;
  /** Don't fill.  */
  public static final int NONE;
  /** Fill horizontally.  */
  public static final int HORIZONTAL;
  /** Fill vertically.  */
  public static final int VERTICAL;

  /** Position in the center.  */
  public static final int CENTER;
  /** Position to the east.  */
  public static final int EAST;
  /** Position to the north.  */
  public static final int NORTH;
  /** Position to the northeast.  */
  public static final int NORTHEAST;
  /** Position to the northwest.  */
  public static final int NORTHWEST;
  /** Position to the south.  */
  public static final int SOUTH;
  /** Position to the southeast.  */
  public static final int SOUTHEAST;
  /** Position to the southwest.  */
  public static final int SOUTHWEST;
  /** Position to the west.  */
  public static final int WEST;

  /** Occupy all remaining cells except last cell.  */
  public static final int RELATIVE;
  /** Occupy all remaining cells.  */
  public static final int REMAINDER;

  public int anchor;
  public int fill;
  public int gridheight;
  public int gridwidth;
  public int gridx;
  public int gridy;
  public Insets insets;
  public int ipadx;
  public int ipady;
  public double weightx;
  public double weighty;

  /** Create a copy of this object.  */
  public Object clone ()
  {
    // This is lazy but it works.
    GridBagConstraints g = (GridBagConstraints) super.clone ();
    g.insets = (Insets) insets.clone ();
    return g;
  }

  /** Create a new GridBagConstraints object with the default
   * parameters.  */
  public GridBagConstraints ()
  {
    this.anchor = CENTER;
    this.fill = NONE;
    this.gridx = RELATIVE;
    this.gridy = RELATIVE;
    this.gridwidth = 1;
    this.gridheight = 1;
    this.ipadx = 0;
    this.ipady = 0;
    this.insets = new Insets (0, 0, 0, 0);
    this.weightx = 0;
    this.weighty = 0;
  }
}
