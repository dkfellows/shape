/*
 * shape.h --
 *
 *	The public platform-independent C interface to the
 *	non-rectangular window extension.
 *
 * Copyright (c) 1997-2000 by Donal K. Fellows
 *
 * See "license.txt" for details of the license this file is made
 * available under.
 *
 * $Id$
 */

#ifndef SHAPE_SHAPE_H
#define SHAPE_SHAPE_H

#include <tcl.h>
#include <tk.h>
#include <X11/Xutil.h>			/* For Region declaration. */

#define SHAPE_VERSION_MAJOR	0
#define SHAPE_VERSION_MINOR	4
#define SHAPE_VERSION_PATCH	4
#define SHAPE_VERSION		STRINGIFY(SHAPE_VERSION_MAJOR) "." STRINGIFY(SHAPE_VERSION_MINOR)
#define SHAPE_PATCHLEVEL	SHAPE_VERSION "." STRINGIFY(SHAPE_VERSION_PATCH)

#define SHAPE_KIND_BOUNDING (1<<0)
#define SHAPE_KIND_CLIP     (1<<1)
#define SHAPE_KIND_BOTH     (SHAPE_KIND_BOUNDING | SHAPE_KIND_CLIP)
#define SHAPE_KIND_TOPLEVEL (1<<8)
#define SHAPE_KIND_ALL	    (SHAPE_KIND_BOTH | SHAPE_KIND_TOPLEVEL)
#define SHAPE_BOUND_MASK    (SHAPE_KIND_BOUNDING | SHAPE_KIND_TOPLEVEL)
#define SHAPE_CLIP_MASK     (SHAPE_KIND_CLIP | SHAPE_KIND_TOPLEVEL)

#ifndef ShapeSet
/* Taken from /usr/include/X11/extensions/shape.h */
#define ShapeSet                        0
#define ShapeUnion                      1
#define ShapeIntersect                  2
#define ShapeSubtract                   3
#define ShapeInvert                     4
#endif /* ShapeSet */

#define SHAPE_OP_SET		ShapeSet
#define SHAPE_OP_UNION		ShapeUnion
#define SHAPE_OP_INTERSECT	ShapeIntersect
#define SHAPE_OP_SUBTRACT	ShapeSubtract
#define SHAPE_OP_INVERT		ShapeInvert

EXTERN int
Shape_GetBbox _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin, int getClip,
			   int *valid, int *x1, int *y1, int *x2, int *y2));
EXTERN int
Shape_GetShapeRectanglesObj _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin,
					 int getClip));
EXTERN int
Shape_MoveShape _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin,
			     int kind, int x, int y));
EXTERN int
Shape_CombineBitmap _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin,
				 int kind, int op, int x, int y,
				 Pixmap bitmap));
EXTERN int
Shape_CombineRectangles _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin,
				     int kind, int op,
				     int rectc, XRectangle *rectv));
EXTERN int
Shape_CombineRectanglesOrdered _ANSI_ARGS_((Tcl_Interp *interp,Tk_Window tkwin,
					    int kind, int op,
					    int rectc, XRectangle *rectv));
EXTERN int
Shape_CombineWindow _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin,
				 int kind, int op, int x, int y,
				 Tk_Window srcwin));
EXTERN int
Shape_CombineRegion _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin,
				 int kind, int op, int x, int y,
				 Region region));
EXTERN int
Shape_Reset _ANSI_ARGS_((Tcl_Interp *interp, Tk_Window tkwin, int kind));
EXTERN int
Shape_QueryVersion _ANSI_ARGS_((Tk_Window tkwin,
				int *majorPtr, int *minorPtr));
EXTERN int
Shape_ExtensionPresent _ANSI_ARGS_((Tk_Window tkwin));

#endif /* SHAPE_SHAPE_H */
