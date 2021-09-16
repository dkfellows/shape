/*
 * shapeUnixImpl.c --
 *
 *	This module implements raw access to the X Shaped-Window extension.
 *
 * Copyright (c) 2000 by Donal K. Fellows
 *
 * See "license.txt" for details of the license this file is made
 * available under.
 *
 * $Id$
 */

#include <tcl.h>
#include <tk.h>				/* Includes conventional X stuff. */
#include <X11/Xutil.h>			/* For Region declaration. */
#include <X11/extensions/shape.h>
#include "shape.h"

#ifdef DKF_SHAPE_DEBUGGING
static int
applyOperationToToplevelParent = 1;
#else
#define applyOperationToToplevelParent 1
#endif

/*
 * ----------------------------------------------------------------------
 *
 * getXParent -
 *	Return the X window token for the given window on the given
 *	display.
 *
 * Returns:
 *	A window token, or None if an error occurred or the parent
 *	is the root window of the display.
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------
 */

static Window
getXParent(dpy, win)
     Display *dpy;
     Window win;
{
    Window root, parent, *kids;
    unsigned int nkids;

    kids = (Window *)NULL;
    if (XQueryTree(dpy, win, &root, &parent, &kids, &nkids) != False) {
	if (kids != NULL) {
	    XFree(kids);
	}
	if (parent != root) {
	    return parent;
	}
    }
    return None;
}

/*
 * ----------------------------------------------------------------------
 *
 * Shape_GetBbox -
 *	Get the bounding box of the window shape.  The getClip parameter
 *	specifies whether to get the clip region of the window, otherwise
 *	the bounding region of the window is got.
 *
 * Result:
 *	A standard Tcl result value.  The variable pointed to by valid
 *	indicates whether the variables pointed to by x1...y2
 *	represent a valid bounding box, or whether none has been
 *	designated for the window.
 *
 * Side effects:
 *	None other than as described above.
 *
 * ----------------------------------------------------------------------
 */

int
Shape_GetBbox(interp, tkwin, getClip, valid, x1, y1, x2, y2)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int getClip, *valid, *x1, *y1, *x2, *y2;
{
    Display *dpy = Tk_Display(tkwin);
    Window win = Tk_WindowId(tkwin);
    Tcl_Obj *result[4];
    Status s;
    int bShaped, xbs, ybs, cShaped, xcs, ycs;
    unsigned int wbs, hbs, wcs, hcs;

    if (win == None) {
	Tcl_AppendResult(interp, "window ", Tk_PathName(tkwin),
			 " doesn't fully exist", NULL);
	return TCL_ERROR;
    }

    s = XShapeQueryExtents(dpy, win,
			   &bShaped, &xbs, &ybs, &wbs, &hbs,
			   &cShaped, &xcs, &ycs, &wcs, &hcs);

    if (!s) {
	Tcl_AppendResult(interp, "extents query failed", NULL);
	return TCL_ERROR;
    } else if (bShaped && !getClip) {
	/* Bounding box of window. */
	*valid = 1;
	*x1 = xbs;
	*y1 = ybs;
	*x2 = xbs+wbs-1;
	*y2 = ybs+hbs-1;
    } else if (cShaped && getClip) {
	/* Bounding box of drawable area. */
	*valid = 1;
	*x1 = xcs;
	*y1 = ycs;
	*x2 = xcs+wcs-1;
	*y2 = ycs+hcs-1;
    } else {
	*valid = 0;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Shape_GetShapeRectanglesObj -
 *	Get a list of rectangles that describe the window shape.  The
 *	getClip parameter specifies whether to get the clip region of the
 *	window, otherwise the bounding region of the window is got.
 *
 * Result:
 *	A standard Tcl result value.  The actual result value or an
 *	error message is left in the interpreter's result.
 *
 * Side effects:
 *	None other than as described above.
 *
 * ----------------------------------------------------------------------
 */

int
Shape_GetShapeRectanglesObj(interp, tkwin, getClip)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int getClip;
{
    Display *dpy = Tk_Display(tkwin);
    Window win = Tk_WindowId(tkwin);
    XRectangle *rects = NULL;
    int count = 0;
    int order,i;
    Tcl_Obj *rect[4], **retvals;

    if (win == None) {
	Tcl_AppendResult(interp, "window ", Tk_PathName(tkwin),
			 " doesn't fully exist", NULL);
	return TCL_ERROR;
    }

    rects = XShapeGetRectangles(dpy, win, (getClip?ShapeClip:ShapeBounding),
				&count, &order);

    if (count) {
	/* alloca() would be more efficient */
	retvals = (Tcl_Obj **)Tcl_Alloc(sizeof(Tcl_Obj *)*count);
	for (i=0 ; i<count ; i++) {
	    rect[0] = Tcl_NewIntObj(rects[i].x);
	    rect[1] = Tcl_NewIntObj(rects[i].y);
	    rect[2] = Tcl_NewIntObj(rects[i].x + rects[i].width  - 1);
	    rect[3] = Tcl_NewIntObj(rects[i].y + rects[i].height - 1);
	    retvals[i] = Tcl_NewListObj(4, rect);
	}
	Tcl_SetObjResult(interp, Tcl_NewListObj(count, retvals));
	Tcl_Free((char *)retvals);
    }
    if (rects) {
	XFree(rects);
    }
    return TCL_OK;
}

int
Shape_MoveShape(interp, tkwin, kind, x, y)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int kind, x, y;
{
    Display *dpy = Tk_Display(tkwin);
    Window window = Tk_WindowId(tkwin);
    Window parent = None;

    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	parent = getXParent(dpy, window);
    } else {
	kind &= ~SHAPE_KIND_TOPLEVEL;
    }

    switch (kind & SHAPE_BOUND_MASK) {
    case SHAPE_BOUND_MASK:
	if (parent != None)
	    XShapeOffsetShape(dpy, parent, ShapeBounding, x, y);
    case SHAPE_KIND_BOUNDING:
	XShapeOffsetShape(dpy, window, ShapeBounding, x, y);
    }
    switch (kind & SHAPE_CLIP_MASK) {
    case SHAPE_CLIP_MASK:
	if (parent != None)
	    XShapeOffsetShape(dpy, parent, ShapeClip, x, y);
    case SHAPE_KIND_CLIP:
	XShapeOffsetShape(dpy, window, ShapeClip, x, y);
    }

    return TCL_OK;
}

/* Shape combination engines */
int
Shape_CombineBitmap(interp, tkwin, kind, op, x, y, bitmap)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int kind, op, x, y;
     Pixmap bitmap;
{
    Display *dpy = Tk_Display(tkwin);
    Window window = Tk_WindowId(tkwin);
    Window parent = None;

    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	parent = getXParent(dpy, window);
    } else {
	kind &= ~SHAPE_KIND_TOPLEVEL;
    }

    switch (kind & SHAPE_BOUND_MASK) {
    case SHAPE_BOUND_MASK:
	if (parent != None)
	    XShapeCombineMask(dpy, parent, ShapeBounding, x, y, bitmap, op);
    case SHAPE_KIND_BOUNDING:
	XShapeCombineMask(dpy, window, ShapeBounding, x, y, bitmap, op);
    }
    switch (kind & SHAPE_CLIP_MASK) {
    case SHAPE_CLIP_MASK:
	if (parent != None)
	    XShapeCombineMask(dpy, parent, ShapeClip, x, y, bitmap, op);
    case SHAPE_KIND_CLIP:
	XShapeCombineMask(dpy, window, ShapeClip, x, y, bitmap, op);
    }

    return TCL_OK;
}

int
Shape_CombineRectangles(interp, tkwin, kind, op, rectc, rectv)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int kind, op, rectc;
     XRectangle *rectv;
{
    Display *dpy = Tk_Display(tkwin);
    Window window = Tk_WindowId(tkwin);
    Window parent = None;

    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	parent = getXParent(dpy, window);
    } else {
	kind &= ~SHAPE_KIND_TOPLEVEL;
    }

    switch (kind & SHAPE_BOUND_MASK) {
    case SHAPE_BOUND_MASK:
	if (parent != None)
	    XShapeCombineRectangles(dpy, parent, ShapeBounding, 0, 0, rectv,
				    rectc, op, Unsorted);
    case SHAPE_KIND_BOUNDING:
	XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectv,
				rectc, op, Unsorted);
    }
    switch (kind & SHAPE_CLIP_MASK) {
    case SHAPE_CLIP_MASK:
	if (parent != None)
	    XShapeCombineRectangles(dpy, parent, ShapeClip, 0, 0, rectv,
				    rectc, op, Unsorted);
    case SHAPE_KIND_CLIP:
	XShapeCombineRectangles(dpy, window, ShapeClip, 0, 0, rectv,
				rectc, op, Unsorted);
    }

    return TCL_OK;
}

/* A slightly more efficient version for those cases where you can
 * guarantee that the rectangles are non-overlapping, sorted by
 * scanline, and left-to-right within each scanline.  This is a
 * situation that code can generate quite easily, but which it is
 * unwise to rely on in user-generated data...
 */
int
Shape_CombineRectanglesOrdered(interp, tkwin, kind, op, rectc, rectv)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int kind, op, rectc;
     XRectangle *rectv;
{
    Display *dpy = Tk_Display(tkwin);
    Window window = Tk_WindowId(tkwin);
    Window parent = None;

    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	parent = getXParent(dpy, window);
    } else {
	kind &= ~SHAPE_KIND_TOPLEVEL;
    }

    switch (kind & SHAPE_BOUND_MASK) {
    case SHAPE_BOUND_MASK:
	if (parent != None)
	    XShapeCombineRectangles(dpy, parent, ShapeBounding, 0, 0, rectv,
				    rectc, op, YXBanded);
    case SHAPE_KIND_BOUNDING:
	XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectv,
				rectc, op, YXBanded);
    }
    switch (kind & SHAPE_CLIP_MASK) {
    case SHAPE_CLIP_MASK:
	if (parent != None)
	    XShapeCombineRectangles(dpy, parent, ShapeClip, 0, 0, rectv,
				    rectc, op, YXBanded);
    case SHAPE_KIND_CLIP:
	XShapeCombineRectangles(dpy, window, ShapeClip, 0, 0, rectv,
				rectc, op, YXBanded);
    }

    return TCL_OK;
}

int
Shape_CombineWindow(interp, tkwin, kind, op, x, y, srcwin)
     Tcl_Interp *interp;
     Tk_Window tkwin, srcwin;
     int kind, op, x, y;
{
    Display *dpy  = Tk_Display(tkwin);
    Display *sdpy = Tk_Display(srcwin);
    Window window = Tk_WindowId(tkwin);
    Window src    = Tk_WindowId(srcwin);
    Window parent = None;

    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	parent = getXParent(dpy, window);
    } else {
	kind &= ~SHAPE_KIND_TOPLEVEL;
    }

    if (dpy != sdpy) {
	/* DIFFERENT DISPLAYS!  Must convert to rectangles to pass... */
	XRectangle *rects = NULL;
	int count, order;
	XRectangle backup;

	backup.x = backup.y = 0;
	backup.width = Tk_Width(srcwin);
	backup.height = Tk_Height(srcwin);

	if (kind & SHAPE_KIND_CLIP) {
	    rects = XShapeGetRectangles(sdpy, src, ShapeClip,
					&count, &order);
	    if (!count) {
		if (rects) {
		    XFree(rects);
		}
		rects = &backup;
		count = 1;
		order = Unsorted;
	    }
	    switch (kind & SHAPE_CLIP_MASK) {
	    case SHAPE_CLIP_MASK:
		if (parent != None)
		    XShapeCombineRectangles(dpy, parent, ShapeClip, 0, 0,
					    rects, count, op, order);
	    case SHAPE_KIND_CLIP:
		XShapeCombineRectangles(dpy, window, ShapeClip, 0, 0,
					rects, count, op, order);
	    }
	    if (rects && rects!=&backup) {
		XFree(rects);
	    }
	    rects = NULL;
	}
	if (kind & SHAPE_KIND_BOUNDING) {
	    rects = XShapeGetRectangles(sdpy, src, ShapeBounding,
					&count, &order);
	    if (!count) {
		if (rects) {
		    XFree(rects);
		}
		rects = &backup;
		count = 1;
		order = Unsorted;
	    }
	    switch (kind & SHAPE_BOUND_MASK) {
	    case SHAPE_BOUND_MASK:
		if (parent != None)
		    XShapeCombineRectangles(dpy, parent, ShapeBounding, 0, 0,
					    rects, count, op, order);
	    case SHAPE_KIND_BOUNDING:
		XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0,
					rects, count, op, order);
	    }
	    if (rects && rects!=&backup) {
		XFree(rects);
	    }
	}
    } else {
	switch (kind & SHAPE_BOUND_MASK) {
	case SHAPE_BOUND_MASK:
	    if (parent != None)
		XShapeCombineShape(dpy, parent, ShapeBounding, x, y,
				   src, ShapeBounding, op);
	case SHAPE_KIND_BOUNDING:
	    XShapeCombineShape(dpy, window, ShapeBounding, x, y,
			       src, ShapeBounding, op);
	}
	switch (kind & SHAPE_CLIP_MASK) {
	case SHAPE_CLIP_MASK:
	    if (parent != None)
		XShapeCombineShape(dpy, parent, ShapeClip, x, y,
				   src, ShapeClip, op);
	case SHAPE_KIND_CLIP:
	    XShapeCombineShape(dpy, window, ShapeClip, x, y,
			       src, ShapeClip, op);
	}
    }

    return TCL_OK;
}

int
Shape_CombineRegion(interp, tkwin, kind, op, x, y, region)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int kind, op, x, y;
     Region region;
{
    Display *dpy = Tk_Display(tkwin);
    Window window = Tk_WindowId(tkwin);
    Window parent;

    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	parent = getXParent(dpy, window);
    } else {
	parent = None;
	kind &= ~SHAPE_KIND_TOPLEVEL;
    }

    switch (kind & SHAPE_BOUND_MASK) {
    case SHAPE_BOUND_MASK:
	if (parent != None)
	    XShapeCombineRegion(dpy, parent, ShapeBounding, x, y, region, op);
    case SHAPE_KIND_BOUNDING:
	XShapeCombineRegion(dpy, window, ShapeBounding, x, y, region, op);
    }
    switch (kind & SHAPE_CLIP_MASK) {
    case SHAPE_CLIP_MASK:
	if (parent != None)
	    XShapeCombineRegion(dpy, parent, ShapeClip, x, y, region, op);
    case SHAPE_KIND_CLIP:
	XShapeCombineRegion(dpy, window, ShapeClip, x, y, region, op);
    }

    return TCL_OK;
}

int
Shape_Reset(interp, tkwin, kind)
     Tcl_Interp *interp;
     Tk_Window tkwin;
     int kind;
{
    /* Maps to this on X... */
    return Shape_CombineBitmap(interp, tkwin, kind, SHAPE_OP_SET, 0, 0, None);
}

int
Shape_QueryVersion(tkwin, majorPtr, minorPtr)
     Tk_Window tkwin;
     int *majorPtr, *minorPtr;
{
    Status result;

    result = XShapeQueryVersion(Tk_Display(tkwin), majorPtr, minorPtr);
    return (result == True);
}

int
Shape_ExtensionPresent(tkwin)
     Tk_Window tkwin;
{
    int eventBase, errorBase; /* These are ignored by us! */
    Status result;

    result = XShapeQueryExtension(Tk_Display(tkwin), &eventBase, &errorBase);
    return (result == True);
}
