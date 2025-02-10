/*
 * shape.c --
 *
 *	This module implements raw access to the Get/SetWindowRgn
 *	interface (Win32 non-rectangular window engine.)
 *
 * Copyright (c) 2000 by Donal K. Fellows
 *
 * See "license.txt" for details of the license this file is made
 * available under.
 */

#include <windows.h>
#include <tcl.h>
#include <tk.h>
#include "shapeInt.h"

#ifdef DKF_SHAPE_DEBUGGING
static int
applyOperationToToplevelParent = 1;
#else
#define applyOperationToToplevelParent 1
#endif

#define CLASSSIZE 23
#define RECTCOUNT 64

static int getHWNDs(Tcl_Interp*, Tk_Window, int, HWND*, HWND*);
static int setHRGN(Tcl_Interp*, char*, HWND, HWND, int, int, HRGN);
static int invertHRGN(Tcl_Interp*, char*, HWND, HWND, int, int, int, int);
static int mixHRGN(Tcl_Interp*, char*, HWND, HWND, int, int, int, HRGN);

static inline int
offsetRegion(
    Tcl_Interp *interp,
    HRGN region,
    int x,
    int y)
{
    if (OffsetRgn(region, x, y) == ERROR) {
        TclWinConvertError(GetLastError());
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not apply offset to region"));
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
setBaseRegion(
    Tcl_Interp *interp,
    const char *pathName,
    HWND window,
    HRGN region)
{
    if (SetWindowRgn(window, region, TRUE) == 0) {
        TclWinConvertError(GetLastError());
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not set region of \"%s\"",
		pathName));
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
setShellRegion(
    Tcl_Interp *interp,
    const char *pathName,
    HWND parent,
    HRGN region)
{
    if (parent != NULL && SetWindowRgn(parent, region, TRUE) == 0) {
        TclWinConvertError(GetLastError());
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"set region failed for outer shell of \"%s\"",
		pathName));
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
getBaseRegion(
    Tcl_Interp *interp,
    const char *pathName,
    HWND window,
    HRGN region)
{
    if (GetWindowRgn(window, region) == ERROR) {
        TclWinConvertError(GetLastError());
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not read existing window region for \"%s\"",
		pathName));
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
getShellRegion(
    Tcl_Interp *interp,
    const char *pathName,
    HWND parent,
    HRGN region)
{
    if (parent != NULL && GetWindowRgn(parent, region) == ERROR) {
        TclWinConvertError(GetLastError());
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not read existing region of outer shell for \"%s\"",
		pathName));
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
getHWNDs(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    HWND *window,
    HWND *parent)
{
    Window w = Tk_WindowId(tkwin);
    HWND hwnd;

    if (w == None) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" does not properly exist",
		Tk_PathName(tkwin)));
	return TCL_ERROR;
    }
    *window = hwnd = Tk_GetHWND(w);
    if (parent == NULL) {
	/* Stop right here! */
	return TCL_OK;
    }
    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	int returnval;
	TCHAR myname[CLASSSIZE + 1];

	while ((returnval = GetClassName(hwnd, myname, CLASSSIZE)) != 0 &&
		// FIXME: Use the ...W API explicitly
		strcmp(myname, "TkTopLevel") != 0) {
	    hwnd = GetParent(hwnd);
	    if (hwnd == NULL) {
	        TclWinConvertError(GetLastError());
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"parental window search for window \"%s\" failed",
			Tk_PathName(tkwin)));
		return TCL_ERROR;
	    }
	}

	if (returnval == 0) {
	    TclWinConvertError(GetLastError());
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "parental classname determinisation for window \"%s\" failed",
		    Tk_PathName(tkwin)));
	    return TCL_ERROR;
	}

	*parent = hwnd;
    } else {
	*parent = NULL;
    }

    return TCL_OK;
}

/*
 * Note that this code assumes that we *own* the region; no other code
 * must reference it after this function returns!
 */
int
setHRGN(
    Tcl_Interp *interp,
    char *pathname,
    HWND window,
    HWND parent,
    int x,
    int y,
    HRGN region)
{
    HRGN tmp;

    if ((x != 0 || y != 0) && offsetRegion(interp, region, x, y) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, pathname, window, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }
    if (parent == NULL) {
        return TCL_OK;
    }
    tmp = (HRGN) TkCreateRegion();
    if (CombineRgn(tmp, region, tmp, RGN_COPY) == ERROR) {
        TclWinConvertError(GetLastError());
        DeleteObject(tmp);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not duplicate region"));
	return TCL_ERROR;
    }
    if (setShellRegion(interp, pathname, parent, tmp) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
invertHRGN(
    Tcl_Interp *interp,
    char *pathname,
    HWND window,
    HWND parent,
    int w,
    int h,
    int x,
    int y)
{
    HRGN region = (HRGN)TkCreateRegion();
    HRGN tmp;

    if (getBaseRegion(interp, pathname, window, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }
    tmp = CreateRectRgn(0, 0, w-1, h-1); /* assume *this* works... */
    if (CombineRgn(tmp, region, tmp, RGN_XOR) == ERROR) {
        TclWinConvertError(GetLastError());
        DeleteObject(tmp);
        DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not invert region"));
	return TCL_ERROR;
    }
    DeleteObject(region);
    if ((x != 0 || y != 0) && offsetRegion(interp, tmp, x, y) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, pathname, window, tmp) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (parent == NULL) {
        return TCL_OK;
    }

    region = (HRGN) TkCreateRegion();
    if (getShellRegion(interp, pathname, parent, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }        
    tmp = CreateRectRgn(0, 0, w-1, h-1); /* assume *this* works... */
    if (CombineRgn(tmp, region, tmp, RGN_XOR) == ERROR) {
        TclWinConvertError(GetLastError());
        DeleteObject(tmp);
        DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not invert region"));
	return TCL_ERROR;
    }
    DeleteObject(region);
    if ((x != 0 || y != 0) && offsetRegion(interp, tmp, x, y) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (setShellRegion(interp, pathname, parent, tmp) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
mixHRGN(
    Tcl_Interp *interp,
    char *pathname,
    HWND window,
    HWND parent,
    int op,
    int x,
    int y,
    HRGN region)
{
    HRGN tmp;
    if ((x != 0 || y != 0) && offsetRegion(interp, region, x, y) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }

    tmp = (HRGN) TkCreateRegion();
    if (getBaseRegion(interp, pathname, window, tmp) != TCL_OK) {
        DeleteObject(tmp);
	DeleteObject(region);
        return TCL_ERROR;
    }
    if (CombineRgn(tmp, region, tmp, op) == ERROR) {
        TclWinConvertError(GetLastError());
        DeleteObject(tmp);
        DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not apply operation to region"));
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, pathname, window, tmp) != TCL_OK) {
        DeleteObject(tmp);
        DeleteObject(region);
	return TCL_ERROR;
    }

    if (parent == NULL) {
        DeleteObject(region);
        return TCL_OK;
    }

    tmp = (HRGN) TkCreateRegion();
    if (getShellRegion(interp, pathname, parent, tmp) != TCL_OK) {
        DeleteObject(tmp);
	DeleteObject(region);
        return TCL_ERROR;
    }
    if (CombineRgn(tmp, region, tmp, op) == ERROR) {
        TclWinConvertError(GetLastError());
        DeleteObject(tmp);
        DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not apply operation to region"));
	return TCL_ERROR;
    }
    DeleteObject(region);
    if (setShellRegion(interp, pathname, parent, tmp) != TCL_OK) {
        DeleteObject(tmp);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * Assume that we can deallocate region. Callers must duplicate if
 * necessary!
 */
int
ShapeCombineHRGN(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    int op,
    int x, int y,	/* Ignored, for now... */
    HRGN region)
{
    HWND w, parent;

    if (getHWNDs(interp, tkwin, kind, &w, &parent) != TCL_OK) {
	return TCL_ERROR;
    }

    switch (op) {
    case ShapeInvert:
        /* region ignored, so delete! */
        DeleteObject(region);
        return invertHRGN(interp, Tk_PathName(tkwin), w, parent,
		Tk_Width(tkwin), Tk_Height(tkwin), x, y);

    case ShapeSet:
        return setHRGN(interp, Tk_PathName(tkwin), w, parent, x, y, region);

    case ShapeUnion:
        return mixHRGN(interp, Tk_PathName(tkwin), w, parent, RGN_OR, x, y,
		region);
    case ShapeIntersect:
        return mixHRGN(interp, Tk_PathName(tkwin), w, parent, RGN_AND, x, y,
		region);
    case ShapeSubtract:
        return mixHRGN(interp, Tk_PathName(tkwin), w, parent, RGN_DIFF, x, y,
		region);
    default:
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"unknown operation code"));
        /* region ignored, so delete! */
        DeleteObject(region);
	return TCL_ERROR;
    }
}

HRGN
ShapeAddDataToRegion(
    Tcl_Interp *interp,
    HRGN region,
    LPRGNDATA data,
    int size)
{
    HRGN tmp;

    data->rdh.nCount = size;
    tmp = ExtCreateRegion(NULL, sizeof(RGNDATAHEADER)+size*sizeof(RECT), data);
    if (region == NULL) return tmp;
    if (CombineRgn(tmp, tmp, region, RGN_OR) == ERROR) {
        TclWinConvertError(GetLastError());
	DeleteObject(tmp);
	DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"combination operation failed"));
	return NULL;
    }
    DeleteObject(region);
    return tmp;
}

int
Shape_CombineRectangles(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    int op,
    int rectc,
    XRectangle *rectv)
{
    int i, j;
    HRGN region = NULL, tmprgn;
    int size = sizeof(RGNDATAHEADER) + RECTCOUNT*sizeof(RECT);
    LPRGNDATA data = (LPRGNDATA) Tcl_Alloc(size);
    LPRECT rects = (RECT *) data->Buffer;

    data->rdh.dwSize = sizeof(data->rdh);
    data->rdh.nRgnSize = 0;
    data->rdh.rcBound.left   = 0;
    data->rdh.rcBound.top    = 0;
    data->rdh.rcBound.right  = Tk_Width(tkwin)-1;
    data->rdh.rcBound.bottom = Tk_Height(tkwin)-1;

    for (i=j=0 ; i<rectc ; i++,j++) {
	if (j == RECTCOUNT) {
	    region = ShapeAddDataToRegion(interp, region, data, size);
	    if (region == NULL) {
		Tcl_Free((char *) data);
		return TCL_ERROR;
	    }
	    j = 0;
	}
	rects[j].left   = rectv[i].x;
	rects[j].top    = rectv[i].y;
	rects[j].right  = rectv[i].x + rectv[i].width - 1;
	rects[j].bottom = rectv[i].y + rectv[i].height - 1;
    }
    if (region == NULL) {
	region = ShapeAddDataToRegion(interp, region, data, size);
	if (region == NULL) {
	    Tcl_Free((char *)data);
	    return TCL_ERROR;
	}
    }

    Tcl_Free((char *)data);
    return ShapeCombineHRGN(interp, tkwin, kind, op, 0, 0, region);
}

/* No efficiency gain here for windows AFAICT... */
int
Shape_CombineRectanglesOrdered(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    int op,
    int rectc,
    XRectangle *rectv)
{
    return Shape_CombineRectangles(interp, tkwin, kind, op, rectc, rectv);
}

/*
 * Must copy the region to prevent problems from unsynchronised use of
 * modifiable regions...
 */
int
Shape_CombineRegion(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    int op,
    int x,
    int y,
    Region region)
{
    HRGN tmp = (HRGN) TkCreateRegion();
    if (CombineRgn(tmp, region, tmp, RGN_COPY) == ERROR) {
        TclWinConvertError(GetLastError());
	DeleteObject(tmp);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not duplicate region"));
	return TCL_ERROR;
    }
    return ShapeCombineHRGN(interp, tkwin, kind, op, x, y, tmp);
}

int
Shape_CombineWindow(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    Tk_Window srcwin,
    int kind,
    int op,
    int x,
    int y)
{
    HWND src;
    HRGN region;

    if (getHWNDs(interp, srcwin, kind, &src, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    region = (HRGN)TkCreateRegion();
    if (getBaseRegion(interp, Tk_PathName(srcwin), src, region) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }
    return ShapeCombineHRGN(interp, tkwin, kind, op, x, y, region);
}

/*
 * I think this does a reset, but the documentation is *not* clear at all.
 */
int
Shape_Reset(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind)
{
    HWND window, parent;

    if (getHWNDs(interp, tkwin, kind, &window, &parent) != TCL_OK) {
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, Tk_PathName(tkwin), window, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    if (setShellRegion(interp, Tk_PathName(tkwin), parent, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

int
Shape_MoveShape(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    int x,
    int y)
{
    HWND window, parent;
    HRGN region;

    if (getHWNDs(interp, tkwin, kind, &window, &parent) != TCL_OK) {
	return TCL_ERROR;
    }

    region = (HRGN)TkCreateRegion();
    if (getBaseRegion(interp, Tk_PathName(tkwin), window, region) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }
    if (OffsetRegion(region, x, y) == ERROR) {
        TclWinConvertError(GetLastError());
	DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not apply offset to region"));
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, Tk_PathName(tkwin), window, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }

    if (parent == NULL) {
        return TCL_OK;
    }

    region = (HRGN)TkCreateRegion();
    if (getShellRegion(interp, Tk_PathName(tkwin), parent, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }
    if (OffsetRegion(region, x, y) == ERROR) {
        TclWinConvertError(GetLastError());
        DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not apply offset to region"));
	return TCL_ERROR;
    }
    if (setShellRegion(interp, Tk_PathName(tkwin), parent, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }

    return TCL_OK;
}

int
Shape_GetBbox(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,	/* ignored */
    int *valid,
    int *x1,
    int *y1,
    int *x2,
    int *y2)
{
    HWND window;
    HRGN region;
    RECT rect;

    if (getHWNDs(interp, tkwin, kind, &window, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    region = (HRGN)TkCreateRegion();
    if (getBaseRegion(interp, Tk_PathName(tkwin), window, region) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }
    if (GetRgnBox(region, &rect) == 0) {
	*valid = 0;
    } else {
	*valid = 1;
	*x1 = rect.left;
	*y1 = rect.top;
	*x2 = rect.right;
	*y2 = rect.bottom;
    }
    DeleteObject(region);
    return TCL_OK;
}

int
Shape_GetShapeRectanglesObj(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind)
{
    HWND window;
    HRGN region;
    LPRGNDATA buffer;
    int size, i;
    RECT *rects;
    Tcl_Obj *result;
    Tcl_Obj *vec[4];

    /*** GET THE REGION FOR THE WINDOW ***/
    if (getHWNDs(interp, tkwin, kind, &window, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    region = (HRGN)TkCreateRegion();
    if (getBaseRegion(interp, Tk_PathName(tkwin), window, region) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }

    /*** GET THE RECTANGLES FOR THE REGION ***/
    size = GetRegionData(region, 0, NULL);
    if (size == 0) {
        TclWinConvertError(GetLastError());
	DeleteObject(region);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not calculate buffer size"));
	return TCL_ERROR;
    }
    buffer = (LPRGNDATA)Tcl_Alloc(size);
    if (GetRegionData(region, size, buffer) == 0) {
        TclWinConvertError(GetLastError());
	DeleteObject(region);
	Tcl_Free((char *)buffer);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not populate buffer"));
	return TCL_ERROR;
    }
    rects = (RECT*)&(buffer->Buffer);
    CloseRegion(region);

    /*** GET THE TCL_OBJ FOR THE RECTANGLES ***/
    result = Tcl_NewObj();
    for (i=0 ; i<buffer->rdh.nCount ; i++) {
	/* reusing these objects between rectangles is impractical */
	vec[0] = Tcl_NewIntObj(rects[i].left);
	vec[1] = Tcl_NewIntObj(rects[i].top);
	vec[2] = Tcl_NewIntObj(rects[i].right);
	vec[3] = Tcl_NewIntObj(rects[i].bottom);
	/* assume this op will not fail, since object is under our control */
	Tcl_ListObjAppendElement(interp, result, Tcl_NewListObj(4, vec));
    }
    Tcl_Free((char *)buffer);
    Tcl_SetObjResult(interp, result);

    return TCL_OK;
}

int
Shape_ExtensionPresent(
    Tk_Window tkwin)
{
    /* Test Windows version here? No, since we're already loaded, and the DLL
     * won't make it that far without support for the functions we need... */
    return 1;
}

int
Shape_QueryVersion(
    Tk_Window tkwin,
    int *majorPtr,
    int *minorPtr)
{
    *majorPtr = -1; /* Or maybe 0 instead? */
    *minorPtr = 0;
    return 1;
}

/* Placeholders for stuff not yet done... */

int
Shape_CombineBitmap(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    int kind,
    int op,
    int x,
    int y,
    Pixmap bitmap)
{
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
    	    "operation not supported yet"));
    return TCL_ERROR;
}

XRectangle *
ShapeRenderTextAsRectangles(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    Tcl_Obj *string,
    Tcl_Obj *font,
    int *numRects)
{
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
    	    "operation not supported yet"));
    return NULL;
}
