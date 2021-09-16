/*
 * shapeInt.h --
 *
 *	The private C interface between modules of the non-rectangular
 *	window extension.
 *
 * Copyright (c) 2000 by Donal K. Fellows
 *
 * See "license.txt" for details of the license this file is made
 * available under.
 *
 * $Id$
 */

#ifndef SHAPE_SHAPEINT_H
#define SHAPE_SHAPEINT_H

#include <shape.h>

#define ShapeApplyToBounding(kind) ((kind) & SHAPE_KIND_BOUNDING)
#define ShapeApplyToClip(kind)     ((kind) & SHAPE_KIND_CLIP)
#define ShapeApplyToParent(kind)   ((kind) & SHAPE_KIND_TOPLEVEL)

EXTERN XRectangle *
ShapeRenderTextAsRectangles _ANSI_ARGS_((Tk_Window tkwin, Tcl_Interp *interp,
					 Tcl_Obj *string, Tcl_Obj *font,
					 int *numRects));

#endif
