/*
 * shape.c --
 *
 *	The Tcl-level interface to non-rectangular windows.
 *
 * Copyright (c) 1997-2000 by Donal K. Fellows
 *
 * See "license.txt" for details of the license this file is made
 * available under.
 */

#include "shapeInt.h"

#ifdef __WIN32__
#define SUPPORTS_PHOTO_REGION
#else
#if (SHAPE_PHOTO == 1)
#define SUPPORTS_PHOTO_REGION
#endif
#endif

#ifdef SUPPORTS_PHOTO_REGION
#include <tkInt.h>
#endif
#include <X11/Xutil.h>

#define min(x,y)	((x)<(y) ? (x) : (y))
#define max(x,y)	((x)<(y) ? (y) : (x))
#define encodeKind(idx,flag)	((idx) | (flag ? SHAPE_KIND_TOPLEVEL : 0))

typedef int (*shapeCommandHandler) (Tk_Window tkwin, Tcl_Interp *interp,
			int opnum, int objc, Tcl_Obj *const objv[]);
typedef int (*shapeApplicator) (Tk_Window tkwin, Tcl_Interp *interp,
			int x, int y, int op, int kind, int objc,
			Tcl_Obj *const objv[]);

static int		shapeBoundClipOps(Tk_Window tkwin, Tcl_Interp *interp,
			    int opnum, int objc, Tcl_Obj *const objv[]);
static int		shapeOffsetOp(Tk_Window tkwin, Tcl_Interp *interp,
			    int opnum, int objc, Tcl_Obj *const objv[]);
static int		shapeSetUpdateOps(Tk_Window tkwin, Tcl_Interp *interp,
			    int opnum, int objc, Tcl_Obj *const objv[]);
static int		shapeBitmap(Tk_Window tkwin, Tcl_Interp *interp,
			    int x, int y, int op, int kind, int objc,
			    Tcl_Obj *const objv[]);
static int		shapeRects(Tk_Window tkwin, Tcl_Interp *interp,
			    int x, int y, int op, int kind, int objc,
			    Tcl_Obj *const objv[]);
static int		shapeReset(Tk_Window tkwin, Tcl_Interp *interp,
			    int x, int y, int op, int kind, int objc,
			    Tcl_Obj *const objv[]);
static int		shapeText(Tk_Window tkwin, Tcl_Interp *interp,
			    int x, int y, int op, int kind, int objc,
			    Tcl_Obj *const objv[]);
static int		shapeWindow(Tk_Window tkwin, Tcl_Interp *interp,
			    int x, int y, int op, int kind, int objc,
			    Tcl_Obj *const objv[]);
#ifdef SUPPORTS_PHOTO_REGION
static int		shapePhoto(Tk_Window tkwin, Tcl_Interp *interp,
			    int x, int y, int op, int kind, int objc,
			    Tcl_Obj *const objv[]);
#endif

static int		shapeCmd(ClientData clientData, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);

enum {
    boundsCmd, getCmd, offsetCmd, setCmd, updateCmd, versionCmd
};
static char *subcommands[] = {
    "bounds", "get", "offset", "set", "update", "version", NULL
};
static shapeCommandHandler shapeCommandHandlers[] = {
    shapeBoundClipOps, shapeBoundClipOps,
    shapeOffsetOp, shapeSetUpdateOps, shapeSetUpdateOps,
    NULL
};

static Tk_Window
getWindow(
    Tk_Window apptkwin,
    Tcl_Interp *interp,
    Tcl_Obj *pathName,
    int *isToplevel)
{
    char *winName   = Tcl_GetStringFromObj(pathName, NULL);
    Tk_Window tkwin = Tk_NameToWindow(interp, winName, apptkwin);

    if (!tkwin) {
	return NULL;
    }
    if (Tk_Display(tkwin) != Tk_Display(apptkwin)) {
	Tcl_AppendResult(interp, "can only apply shape operations to windows"
			 " on the same display as the main window of the"
			 " application", NULL);
	return NULL;
    }
    if (Tk_WindowId(tkwin) == None) {
	Tk_MakeWindowExist(tkwin);
	if (Tk_WindowId(tkwin) == None) {
	    Tcl_AppendResult(interp, "failed to create window ",
			     Tk_PathName(tkwin), NULL);
	    return NULL;
	}
    }
    *isToplevel = Tk_IsTopLevel(tkwin);
    return tkwin;
}

static int
shapeBoundClipOps(
    Tk_Window tkwin0,
    Tcl_Interp *interp,
    int opnum,
    int objc,
    Tcl_Obj *const objv[])
{
    static char *options[] = {
	"-bounding", "-clip", NULL
    };
    int idx = 0, toplevel;
    Tk_Window tkwin;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "pathName ?-bounding/-clip?");
	return TCL_ERROR;
    } else if (objc == 4 && Tcl_GetIndexFromObj(interp, objv[3], options,
	    "option", 0, &idx) != TCL_OK) {
	return TCL_ERROR;
    }
    tkwin = getWindow(tkwin0, interp, objv[2], &toplevel);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }

    switch (opnum) {
    case boundsCmd: {
	int x1, y1, x2, y2, valid;

	if (Shape_GetBbox(interp, tkwin, idx, &valid, &x1, &y1, &x2,
		&y2) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (valid) {
	    Tcl_Obj *r, *result[4];

	    result[0] = Tcl_NewIntObj(x1);
	    result[1] = Tcl_NewIntObj(y1);
	    result[2] = Tcl_NewIntObj(x2);
	    result[3] = Tcl_NewIntObj(y2);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(4, result));
	}
	return TCL_OK;
    }
    case getCmd:
	/*
	 * Would have to hard-code the fact that freeing is done with
	 * XFree() or something equally grotty in order to not push
	 * Objs into *this* interface... <sigh>
	 */
	return Shape_GetShapeRectanglesObj(interp, tkwin, idx);
    default: /* should be impossible to get here! */
	Tcl_Panic("unexpected operation number %d", opnum);
    }
}

static int
shapeOffsetOp(
    Tk_Window tkwin0,
    Tcl_Interp *interp,
    int opnum,
    int objc,
    Tcl_Obj *const objv[])
{
    static char *opts[] = {
	"-bounding", "-clip", "-both", NULL
    };
    int x, y, toplevel, i = SHAPE_KIND_BOTH - 1;
    Tk_Window tkwin;

    /* Argument parsing */
    switch (objc) {
    default:
        Tcl_WrongNumArgs(interp, 2, objv,
		"pathName ?-bounding/-clip/-both? x y");
	return TCL_ERROR;
    case 6:
        if (Tcl_GetIndexFromObj(interp, objv[3], opts, "option", 0,
		&i) != TCL_OK) {
	    return TCL_ERROR;
	}
    case 5:
	tkwin = getWindow(tkwin0, interp, objv[2], &toplevel);
	if (tkwin == NULL ||
		Tcl_GetIntFromObj(interp, objv[objc-2], &x) != TCL_OK ||
		Tcl_GetIntFromObj(interp, objv[objc-1], &y) != TCL_OK) {
	    return TCL_ERROR;
	}
    }
    return Shape_MoveShape(interp, tkwin, encodeKind(i+1, toplevel), x, y);
}

static int
shapeBitmap(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    int x,
    int y,
    int op,
    int kind,
    int objc,
    Tcl_Obj *const objv[])
{
    char *bmap_name;
    Pixmap bmap;
    int result;

    if (objc != 1) {
	Tcl_AppendResult(interp, "bitmap requires one argument; a bitmap "
		"name", NULL);
	return TCL_ERROR;
    }

    bmap_name = Tcl_GetStringFromObj(objv[0], NULL);
    bmap = Tk_GetBitmap(interp, tkwin, Tk_GetUid(bmap_name));

    if (bmap == None) {
	return TCL_ERROR;
    }

    result = Shape_CombineBitmap(interp, tkwin, kind, op, x, y, bmap);

    Tk_FreeBitmap(Tk_Display(tkwin), bmap);
    return result;
}

static int
shapeRects(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    int x,
    int y,
    int op,
    int kind,
    int objc,
    Tcl_Obj *const objv[])
{
    Tcl_Obj **ovec;
    XRectangle *rects;
    int count, i, result;

    if (objc != 1) {
	Tcl_AppendResult(interp, "rectangles requires one argument; "
		"a list of rectangles", NULL);
	return TCL_ERROR;
    }

    if (Tcl_ListObjGetElements(interp, objv[0], &count, &ovec) != TCL_OK) {
	return TCL_ERROR;
    } else if (count<1) {
	Tcl_AppendResult(interp, "need at least one rectangle", NULL);
	return TCL_ERROR;
    }

    rects = (XRectangle *) Tcl_Alloc(sizeof(XRectangle) * count);
    for (i=0 ; i<count ; i++) {
	int x1, y1, x2, y2;
	Tcl_Obj **rvec;
	int rlen;

	if (Tcl_ListObjGetElements(interp, ovec[i], &rlen, &rvec) != TCL_OK) {
	    Tcl_Free((char *)rects);
	    return TCL_ERROR;
	} else if (rlen != 4) {
	    Tcl_AppendResult(interp, "rectangles are described by four "
		    "numbers; x1, y1, x2, and y2", NULL);
	    Tcl_Free((char *) rects);
	    return TCL_ERROR;
	} else if (Tcl_GetIntFromObj(interp, rvec[0], &x1) != TCL_OK ||
		Tcl_GetIntFromObj(interp, rvec[1], &y1) != TCL_OK ||
		Tcl_GetIntFromObj(interp, rvec[2], &x2) != TCL_OK ||
		Tcl_GetIntFromObj(interp, rvec[3], &y2) != TCL_OK) {
	    Tcl_Free((char *) rects);
	    return TCL_ERROR;
	}
	rects[i].x = min(x1, x2);
	rects[i].y = min(y1, y2);
	rects[i].width  = max(x1-x2, x2-x1) + 1;
	rects[i].height = max(y1-y2, y2-y1) + 1;
    }

    result = Shape_CombineRectangles(interp, tkwin, kind, op, count, rects);

    Tcl_Free((char *) rects);
    return result;
}

static int
shapeReset(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    int x,
    int y,
    int op,
    int kind,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_AppendResult(interp, "reset takes no arguments", NULL);
	return TCL_ERROR;
    }

    return Shape_Reset(interp, tkwin, kind);
}

static int
shapeText(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    int x,
    int y,
    int op,
    int kind,
    int objc,
    Tcl_Obj *const objv[])
{
    XRectangle *rects;
    int count, result;

    if (objc != 2) {
	Tcl_AppendResult(interp, "text requires two arguments; the string "
		"to display and the font to use to display it", NULL);
	return TCL_ERROR;
    }

    rects = ShapeRenderTextAsRectangles(tkwin, interp, objv[0], objv[1],
	    &count);
    if (rects == NULL) {
	return TCL_ERROR;
    }

    /*
     * Now we've got a set of rectangles, we can apply it...
     */

    result = Shape_CombineRectanglesOrdered(interp, tkwin, kind, op,
	    count, rects);

    Tcl_Free((char *) rects);
    return result;
}

static int
shapeWindow(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    int x,
    int y,
    int op,
    int kind,
    int objc,
    Tcl_Obj *const objv[])
{
    Tk_Window srcwin;
    int ignore, result;
    Display *dpy;

    if (objc != 1) {
	Tcl_AppendResult(interp, "window requires one argument; a window "
		"pathName", NULL);
	return TCL_ERROR;
    }
    srcwin = getWindow(tkwin, interp, objv[0], &ignore);
    if (srcwin == None) {
	return TCL_ERROR;
    }

    return Shape_CombineWindow(interp, tkwin, kind, op, x, y, srcwin);
}

#ifdef SUPPORTS_PHOTO_REGION
static int
shapePhoto(
    Tk_Window tkwin,
    Tcl_Interp *interp,
    int x,
    int y,
    int op,
    int kind,
    int objc,
    Tcl_Obj *const objv[])
{
    char *imageName;
    Tk_PhotoHandle handle;
    Region region;

    if (objc != 1) {
	Tcl_AppendResult(interp, "photo requires one argument; "
		"a photo image name", NULL);
	return TCL_ERROR;
    }

    imageName = Tcl_GetStringFromObj(objv[0], &NULL);
    handle = Tk_FindPhoto(interp, imageName);
    if (handle == NULL) {
	return TCL_ERROR;
    }

    /*
     * Deep implementation magic!  Relies on knowing a TkRegion is
     * implemented as a Region under X...
     */

    region = (Region) TkPhotoGetValidRegion(handle);

    if (region == None) {
	Tcl_AppendResult(interp, "bad transparency info in photo image ",
		imageName, NULL);
	return TCL_ERROR;
    }

    return Shape_CombineRegion(interp, tkwin, kind, op, x, y, region);
}
#endif

static int
shapeSetUpdateOps(
    Tk_Window tkwin0,
    Tcl_Interp *interp,
    int opnum,
    int objc,
    Tcl_Obj *const objv[])
{
    enum optkind {
	shapekind, offsetargs, sourceargs
    };
    static char *options[] = {
	"-offset",
	"-bounding", "-clip", "-both",
	"bitmap", "rectangles", "reset", "text", "window",
#ifdef SUPPORTS_PHOTO_REGION
	"photo",
#endif
	NULL
    };
    static enum optkind optk[] = {
	offsetargs,
	shapekind, shapekind, shapekind,
	sourceargs, sourceargs, sourceargs, sourceargs, sourceargs
#ifdef SUPPORTS_PHOTO_REGION
	, sourceargs
#endif
    };
    static shapeApplicator applicators[] = {
	NULL, NULL, NULL, NULL,
	shapeBitmap, shapeRects, shapeReset, shapeText, shapeWindow,
#ifdef SUPPORTS_PHOTO_REGION
	shapePhoto,
#endif
	NULL
    };

    int operation = ShapeSet;
    int idx, kind, x = 0, y = 0, toplevel, opidx;
    Tk_Window tkwin;

    switch (opnum) {
    case setCmd:
	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "set pathName ?options?");
	    return TCL_ERROR;
	}
	idx = 3;
	break;
    case updateCmd: {
	static char *operations[] = {
	    "set", "union", "intersect", "subtract", "invert",
	    ":=", "+=", "*=", "-=", "=", "||", "&&", NULL
	};
	static int opmap[] = {
	    SHAPE_OP_SET,       SHAPE_OP_UNION,     SHAPE_OP_INTERSECT,
	    SHAPE_OP_SUBTRACT,  SHAPE_OP_INVERT,    SHAPE_OP_SET,
	    SHAPE_OP_UNION,     SHAPE_OP_INTERSECT, SHAPE_OP_SUBTRACT,
	    SHAPE_OP_SET,       SHAPE_OP_UNION,     SHAPE_OP_INTERSECT
	};

	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 1, objv,
		    "update pathName operation ?options?");
	    return TCL_ERROR;
	}

	if (Tcl_GetIndexFromObj(interp, objv[3], operations, "operation", 0,
		&opidx) != TCL_OK) {
	    return TCL_ERROR;
	}
	operation = opmap[opidx];
	idx = 4;
	break;
    }
    default: /* should be impossible to get here! */
	Tcl_Panic("bad operation: %d", opnum);
    }

    tkwin = getWindow(tkwin0, interp, objv[2], &toplevel);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }
    kind = encodeKind(SHAPE_KIND_BOTH, toplevel);

    for (; idx<objc ; idx++) {
	if (Tcl_GetIndexFromObj(interp, objv[idx], options, "option", 0,
		&opidx) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch (optk[opidx]) {
	case shapekind:
	    kind = encodeKind(opidx, toplevel);
	    break;
	case offsetargs:
	    if (idx + 2 >= objc) {
		Tcl_AppendResult(interp, "-offset reqires two args; x and y",
			NULL);
		return TCL_ERROR;
	    } else if (Tcl_GetIntFromObj(interp, objv[idx+1], &x) != TCL_OK ||
		    Tcl_GetIntFromObj(interp, objv[idx+2], &y) != TCL_OK) {
		return TCL_ERROR;
	    }
	    idx += 2;
	    break;
	case sourceargs: {
	    shapeApplicator app = applicators[opidx];
	    if (app != NULL) {
		objc -= idx + 1;
		objv += idx + 1;
		return app(tkwin, interp, x, y, operation, kind, objc, objv);
	    }
	    Tcl_AppendResult(interp, "not supported", NULL);
	    return TCL_ERROR;
	}
	}
    }
    Tcl_AppendResult(interp, "no source to take shape from", NULL);
    return TCL_ERROR;
}

static int
shapeCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    int subcmdidx;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "subcommand ?window arg ...?");
	return TCL_ERROR;
    } else if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand",
	    0, &subcmdidx) != TCL_OK) {
	return TCL_ERROR;
    }

    if (shapeCommandHandlers[subcmdidx]) {
	/* Farm out the more complex operations */
	return shapeCommandHandlers[subcmdidx](clientData, interp, subcmdidx,
		objc, objv);
    } else switch (subcmdidx) {  
    case versionCmd:
	/* shape version */
	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 1, objv, "version");
	    return TCL_ERROR;
	} else {
	    int major = -1, minor = -1;
	    char buffer[64];

	    if (Shape_QueryVersion((Tk_Window)clientData, &major, &minor)) {
		sprintf(buffer, "%d.%d", major, minor);
		Tcl_AppendResult(interp, buffer, NULL);
	    }
	    return TCL_OK;
	}

    default: /* should be impossible to get here! */
	Tcl_Panic("switch fallthrough");
    }
}

int
Shape_Init(
    Tcl_Interp *interp)
{
    Tk_Window tkwin = Tk_MainWindow(interp);

    if (Tcl_PkgRequire(interp, "Tk", "8", 0) == NULL) {
	return TCL_ERROR;
    }
    if (!Shape_ExtensionPresent(tkwin)) {
	Tcl_AppendResult(interp, "shaped window extension not supported on "
		"this X server", NULL);
	return TCL_ERROR;
    }

    Tcl_CreateObjCommand(interp, "shape", shapeCmd, tkwin, NULL);
    /* See the head of the shape.h file for the definitions of these macros */
    Tcl_SetVar(interp, "shape_version", SHAPE_VERSION, TCL_GLOBAL_ONLY);
    Tcl_SetVar(interp, "shape_patchLevel", SHAPE_PATCHLEVEL, TCL_GLOBAL_ONLY);
    return Tcl_PkgProvide(interp, "shape", SHAPE_VERSION);
}
