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
static int setHRGN(Tcl_Interp*, Tk_Window, HWND, HWND, const POINT*, HRGN);
static int invertHRGN(Tcl_Interp*, Tk_Window, HWND, HWND, const SIZE*, const POINT*);
static int mixHRGN(Tcl_Interp*, Tk_Window, HWND, HWND, int, const POINT*, HRGN);
static int ShapeCombineHRGN(Tcl_Interp*, Tk_Window, int, ShapeOps, const POINT*, HRGN);

static const char *
formError(
    Tcl_DString *bufPtr)
{
    LPVOID lpMsgBuf;
    DWORD dw = GetLastError();
    const char *result;
    FormatMessageW(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER | 
	    FORMAT_MESSAGE_FROM_SYSTEM |
	    FORMAT_MESSAGE_IGNORE_INSERTS,
	    NULL,
	    dw,
	    MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
	    (LPWSTR) &lpMsgBuf,
	    0, NULL);
    result = Tcl_WCharToUtfDString((const WCHAR *)lpMsgBuf, TCL_AUTO_LENGTH,
	    bufPtr);
    LocalFree(lpMsgBuf);
    return result;
}

static inline HRGN
createRegion(void)
{
    return CreateRectRgn(0, 0, 0, 0);
}

static inline HRGN
createRectRegion(
    const SIZE *dimensions)
{
    return CreateRectRgn(0, 0, dimensions->cx - 1, dimensions->cy - 1);
}

static inline int
translateRegion(
    Tcl_Interp *interp,
    HRGN region,
    const POINT *offset)
{
    if (!offset || (offset->x == 0 && offset->y == 0)) {
	/* Nothing to do. */
	return TCL_OK;
    }
    if (OffsetRgn(region, offset->x, offset->y) == ERROR) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not apply offset to region: %s",
		formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "OFFSET", NULL);
	Tcl_DStringFree(&buf);
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
setBaseRegion(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    HWND window,
    HRGN region)
{
    if (SetWindowRgn(window, region, TRUE) == 0) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not set region of \"%s\": %s",
		Tk_PathName(tkwin), formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "SET", NULL);
	Tcl_DStringFree(&buf);
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
setShellRegion(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    HWND parent,
    HRGN region)
{
    if (parent != NULL && SetWindowRgn(parent, region, TRUE) == 0) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"set region failed for outer shell of \"%s\": %s",
		Tk_PathName(tkwin), formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "SET", NULL);
	Tcl_DStringFree(&buf);
	return TCL_ERROR;
    }
    return TCL_OK;
}

static inline int
getBaseRegion(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    HWND window,
    HRGN *regionPtr)
{
    HRGN region = createRegion();
    if (GetWindowRgn(window, region) == ERROR) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not read existing window region for \"%s\": %s",
		Tk_PathName(tkwin), formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "GET", NULL);
	Tcl_DStringFree(&buf);
	DeleteObject(region);
	return TCL_ERROR;
    }
    *regionPtr = region;
    return TCL_OK;
}

static inline int
getShellRegion(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    HWND parent,
    HRGN *regionPtr)
{
    HRGN region = createRegion();
    if (parent != NULL && GetWindowRgn(parent, region) == ERROR) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not read existing region of outer shell for \"%s\": %s",
		Tk_PathName(tkwin), formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "GET", NULL);
	Tcl_DStringFree(&buf);
	DeleteObject(region);
	return TCL_ERROR;
    }
    *regionPtr = region;
    return TCL_OK;
}

static inline int
combineRegions(
    Tcl_Interp *interp,
    HRGN regionDst,
    HRGN regionSrc1,
    HRGN regionSrc2,
    int op,
    const char *opDesc)
{
    if (CombineRgn(regionDst, regionSrc1, regionSrc2, op) == ERROR) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not %s region: %s", opDesc, formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "COMBINE", NULL);
	Tcl_DStringFree(&buf);
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
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "NONE", NULL);
	return TCL_ERROR;
    }
    *window = hwnd = Tk_GetHWND(w);
    if (parent == NULL) {
	/* Stop right here! */
	return TCL_OK;
    }
    if (ShapeApplyToParent(kind) && applyOperationToToplevelParent) {
	WCHAR myname[CLASSSIZE + 1];

	while (1) {
	    int returnval = GetClassNameW(hwnd, myname, CLASSSIZE);
	    if (returnval == 0) {
		Tcl_DString buf;
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"parental classname determinisation for window \"%s\" failed: %s",
			Tk_PathName(tkwin), formError(&buf)));
		Tcl_SetErrorCode(interp, "SHAPE", "WIN", "CLASSNAME", NULL);
		Tcl_DStringFree(&buf);
		return TCL_ERROR;
	    }

	    if (wcscmp(myname, L"TkTopLevel") == 0) {
		break;
	    }

	    hwnd = GetParent(hwnd);
	    if (hwnd == NULL) {
		Tcl_DString buf;
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"parental window search for window \"%s\" failed: %s",
			Tk_PathName(tkwin), formError(&buf)));
		Tcl_SetErrorCode(interp, "SHAPE", "WIN", "SEARCH", NULL);
		Tcl_DStringFree(&buf);
		return TCL_ERROR;
	    }
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
    Tk_Window tkwin,
    HWND window,
    HWND parent,
    const POINT *offset,
    HRGN region)
{
    HRGN tmp;

    if (translateRegion(interp, region, offset) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, tkwin, window, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }
    if (parent == NULL) {
        return TCL_OK;
    }

    tmp = createRegion();
    if (combineRegions(interp, tmp, region, tmp, RGN_COPY, "duplicate") != TCL_OK) {
        DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (setShellRegion(interp, tkwin, parent, tmp) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
invertHRGN(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    HWND window,
    HWND parent,
    const SIZE *dimensions,
    const POINT *offset)
{
    HRGN region;
    HRGN tmp;

    if (getBaseRegion(interp, tkwin, window, &region) != TCL_OK) {
	return TCL_ERROR;
    }
    tmp = createRectRegion(dimensions); /* assume *this* works... */
    if (combineRegions(interp, tmp, region, tmp, RGN_XOR, "invert") != TCL_OK) {
        DeleteObject(tmp);
        DeleteObject(region);
	return TCL_ERROR;
    }
    DeleteObject(region);
    if (translateRegion(interp, tmp, offset) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, tkwin, window, tmp) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (parent == NULL) {
        return TCL_OK;
    }

    if (getShellRegion(interp, tkwin, parent, &region) != TCL_OK) {
	return TCL_ERROR;
    }        
    tmp = createRectRegion(dimensions); /* assume *this* works... */
    if (combineRegions(interp, tmp, region, tmp, RGN_XOR, "invert") != TCL_OK) {
        DeleteObject(tmp);
        DeleteObject(region);
	return TCL_ERROR;
    }
    DeleteObject(region);
    if (translateRegion(interp, tmp, offset) != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    if (setShellRegion(interp, tkwin, parent, tmp) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
mixHRGN(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    HWND window,
    HWND parent,
    int op,
    const POINT *offset,
    HRGN region)
{
    HRGN tmp;
    if (translateRegion(interp, region, offset) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }

    if (getBaseRegion(interp, tkwin, window, &tmp) != TCL_OK) {
	DeleteObject(region);
        return TCL_ERROR;
    }
    if (combineRegions(interp, tmp, region, tmp, op, "apply operation to") != TCL_OK) {
        DeleteObject(tmp);
        DeleteObject(region);
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, tkwin, window, tmp) != TCL_OK) {
        DeleteObject(tmp);
        DeleteObject(region);
	return TCL_ERROR;
    }

    if (parent == NULL) {
        DeleteObject(region);
        return TCL_OK;
    }

    if (getShellRegion(interp, tkwin, parent, &tmp) != TCL_OK) {
	DeleteObject(region);
        return TCL_ERROR;
    }
    if (combineRegions(interp, tmp, region, tmp, op, "apply operation to") != TCL_OK) {
        DeleteObject(tmp);
        DeleteObject(region);
	return TCL_ERROR;
    }
    DeleteObject(region);
    if (setShellRegion(interp, tkwin, parent, tmp) != TCL_OK) {
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
    ShapeKind kind,
    ShapeOps op,
    const POINT *offset, 
    HRGN region)
{
    HWND w, parent;
    SIZE dimensions;

    if (getHWNDs(interp, tkwin, kind, &w, &parent) != TCL_OK) {
	return TCL_ERROR;
    }

    switch (op) {
    case ShapeInvert:
        /* region ignored, so delete! */
        DeleteObject(region);
	dimensions = (SIZE){Tk_Width(tkwin), Tk_Height(tkwin)};
        return invertHRGN(interp, tkwin, w, parent, &dimensions, offset);
    case ShapeSet:
        return setHRGN(interp, tkwin, w, parent, offset, region);
    case ShapeUnion:
        return mixHRGN(interp, tkwin, w, parent, RGN_OR, offset, region);
    case ShapeIntersect:
        return mixHRGN(interp, tkwin, w, parent, RGN_AND, offset, region);
    case ShapeSubtract:
        return mixHRGN(interp, tkwin, w, parent, RGN_DIFF, offset, region);

    default:
	/* Should be unreachable. */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"unknown operation code: %d", op));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "UNSUPPORTED", NULL);
        /* region ignored, so delete! */
        DeleteObject(region);
	return TCL_ERROR;
    }
}

static HRGN
addDataToRegion(
    Tcl_Interp *interp,
    HRGN region,
    RGNDATA *data,
    int count)
{
    HRGN tmp;

    data->rdh.nCount = count;
    tmp = ExtCreateRegion(NULL, offsetof(RGNDATA, Buffer[count * sizeof(RECT)]),
	    data);
    if (region == NULL) {
	return tmp;
    }
    if (combineRegions(interp, region, tmp, region, RGN_OR, "merge") != TCL_OK) {
	DeleteObject(tmp);
	return NULL;
    }
    DeleteObject(tmp);
    return region;
}

int
Shape_CombineRectangles(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    ShapeKind kind,
    ShapeOps op,
    int rectc,
    XRectangle *rectv)
{
    int i;
    HRGN region = NULL;
    RGNDATA *regionData = (RGNDATA *) Tcl_Alloc(
	    sizeof(RGNDATAHEADER) + sizeof(RECT));
    RECT *rect = (RECT *) regionData->Buffer;

    regionData->rdh.dwSize = sizeof(regionData->rdh);
    regionData->rdh.iType = RDH_RECTANGLES;
    regionData->rdh.nRgnSize = sizeof(RGNDATAHEADER) + sizeof(RECT);
    regionData->rdh.rcBound.left   = 0;
    regionData->rdh.rcBound.top    = 0;
    regionData->rdh.rcBound.right  = Tk_Width(tkwin)-1;
    regionData->rdh.rcBound.bottom = Tk_Height(tkwin)-1;

    /* Rectangle data is assumed unsorted, so we add one at a time. */
    for (i=0 ; i<rectc ; i++) {
	rect->left   = rectv[i].x;
	rect->top    = rectv[i].y;
	rect->right  = rectv[i].x + rectv[i].width - 1;
	rect->bottom = rectv[i].y + rectv[i].height - 1;
	region = addDataToRegion(interp, region, regionData, 1);
	if (region == NULL) {
	    break;
	}
    }
    Tcl_Free((char *) regionData);
    if (region == NULL) {
	return TCL_ERROR;
    }

    return ShapeCombineHRGN(interp, tkwin, kind, op, NULL, region);
}

int
Shape_CombineRectanglesOrdered(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    ShapeKind kind,
    ShapeOps op,
    int rectc,
    XRectangle *rectv)
{
    int i, j;
    HRGN region = NULL;
    RGNDATA *regionData = (RGNDATA *) Tcl_Alloc(
	    sizeof(RGNDATAHEADER) + RECTCOUNT * sizeof(RECT));
    RECT *rects = (RECT *) regionData->Buffer;

    regionData->rdh.dwSize = sizeof(regionData->rdh);
    regionData->rdh.iType = RDH_RECTANGLES;
    regionData->rdh.nRgnSize = sizeof(RGNDATAHEADER) + RECTCOUNT * sizeof(RECT);
    regionData->rdh.rcBound.left   = 0;
    regionData->rdh.rcBound.top    = 0;
    regionData->rdh.rcBound.right  = Tk_Width(tkwin)-1;
    regionData->rdh.rcBound.bottom = Tk_Height(tkwin)-1;

    for (i=j=0 ; i<rectc ; i++,j++) {
	if (j == RECTCOUNT) {
	    region = addDataToRegion(interp, region, regionData, j);
	    if (region == NULL) {
		Tcl_Free((char *) regionData);
		return TCL_ERROR;
	    }
	    j = 0;
	}
	rects[j].left   = rectv[i].x;
	rects[j].top    = rectv[i].y;
	rects[j].right  = rectv[i].x + rectv[i].width - 1;
	rects[j].bottom = rectv[i].y + rectv[i].height - 1;
    }
    /* Incorporate any rectangles not yet processed. */
    if (j > 0) {
	region = addDataToRegion(interp, region, regionData, j);
    }
    Tcl_Free((char *) regionData);
    if (region == NULL) {
	return TCL_ERROR;
    }

    return ShapeCombineHRGN(interp, tkwin, kind, op, NULL, region);
}

/*
 * Must copy the region to prevent problems from unsynchronised use of
 * modifiable regions...
 */
int
Shape_CombineRegion(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    ShapeKind kind,
    ShapeOps op,
    int x,
    int y,
    Region region)
{
    HRGN tmp = createRegion();
    POINT offset = {x, y};
    if (combineRegions(interp, tmp, region, tmp, RGN_COPY, "duplicate") != TCL_OK) {
	DeleteObject(tmp);
	return TCL_ERROR;
    }
    return ShapeCombineHRGN(interp, tkwin, kind, op, &offset, tmp);
}

int
Shape_CombineWindow(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    Tk_Window srcwin,
    ShapeKind kind,
    ShapeOps op,
    int x,
    int y)
{
    HWND src;
    HRGN region;
    POINT offset = {x, y};

    if (getHWNDs(interp, srcwin, kind, &src, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    if (getBaseRegion(interp,srcwin, src, &region) != TCL_OK) {
	return TCL_ERROR;
    }
    return ShapeCombineHRGN(interp, tkwin, kind, op, &offset, region);
}

/*
 * I think this does a reset, but the documentation is *not* clear at all.
 */
int
Shape_Reset(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    ShapeKind kind)
{
    HWND window, parent;

    if (getHWNDs(interp, tkwin, kind, &window, &parent) != TCL_OK) {
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, tkwin, window, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    if (setShellRegion(interp, tkwin, parent, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

int
Shape_MoveShape(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    ShapeKind kind,
    int x,
    int y)
{
    HWND window, parent;
    HRGN region;
    POINT point = {x, y};

    if (getHWNDs(interp, tkwin, kind, &window, &parent) != TCL_OK) {
	return TCL_ERROR;
    }

    if (getBaseRegion(interp, tkwin, window, &region) != TCL_OK) {
	return TCL_ERROR;
    }
    if (translateRegion(interp, region, &point) != TCL_OK) {
	DeleteObject(region);
	return TCL_ERROR;
    }
    if (setBaseRegion(interp, tkwin, window, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }

    if (parent == NULL) {
        return TCL_OK;
    }

    if (getShellRegion(interp, tkwin, parent, &region) != TCL_OK) {
	return TCL_ERROR;
    }
    if (translateRegion(interp, region, &point) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }
    if (setShellRegion(interp, tkwin, parent, region) != TCL_OK) {
        DeleteObject(region);
	return TCL_ERROR;
    }

    return TCL_OK;
}

int
Shape_GetBbox(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    ShapeKind kind,
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
    if (getBaseRegion(interp, tkwin, window, &region) != TCL_OK) {
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
    ShapeKind kind)
{
    HWND window;
    HRGN region;
    RGNDATA *buffer;
    int size, i;
    const RECT *rects;
    Tcl_Obj *result;
    Tcl_Obj *vec[4];

    /*** GET THE REGION FOR THE WINDOW ***/
    if (getHWNDs(interp, tkwin, kind, &window, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    if (getBaseRegion(interp, tkwin, window, &region) != TCL_OK) {
	return TCL_ERROR;
    }

    /*** GET THE RECTANGLES FOR THE REGION ***/
    size = GetRegionData(region, 0, NULL);
    if (size == 0) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not calculate buffer size: %s", formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "BUF_SIZE", NULL);
	Tcl_DStringFree(&buf);
	DeleteObject(region);
	return TCL_ERROR;
    }
    buffer = (RGNDATA *) Tcl_Alloc(size);
    if (GetRegionData(region, size, buffer) == 0) {
	Tcl_DString buf;
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"could not populate buffer: %s", formError(&buf)));
	Tcl_SetErrorCode(interp, "SHAPE", "WIN", "GET_DATA", NULL);
	Tcl_DStringFree(&buf);
	DeleteObject(region);
	Tcl_Free((char *)buffer);
	return TCL_ERROR;
    }
    rects = (RECT *) &buffer->Buffer;
    DeleteObject(region);

    /*** GET THE TCL_OBJ FOR THE RECTANGLES ***/
    result = Tcl_NewListObj(buffer->rdh.nCount, NULL);
    for (i=0 ; i<buffer->rdh.nCount ; i++) {
	/* reusing these objects between rectangles is impractical */
	vec[0] = Tcl_NewIntObj(rects[i].left);
	vec[1] = Tcl_NewIntObj(rects[i].top);
	vec[2] = Tcl_NewIntObj(rects[i].right);
	vec[3] = Tcl_NewIntObj(rects[i].bottom);
	/* assume this op will not fail, since object is under our control */
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewListObj(4, vec));
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
    ShapeKind kind,
    int op,
    int x,
    int y,
    Pixmap bitmap)
{
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
    	    "operation not supported yet"));
    Tcl_SetErrorCode(interp, "SHAPE", "WIN", "UNSUPPORTED", NULL);
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
    Tcl_SetErrorCode(interp, "SHAPE", "WIN", "UNSUPPORTED", NULL);
    return NULL;
}
