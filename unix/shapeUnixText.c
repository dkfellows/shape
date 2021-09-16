/*
 * shapeUnixText.c --
 *
 *	This module converts a string and font into an array of rectangles.
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
#include "shapeInt.h"

/*
 * ----------------------------------------------------------------------
 *
 * ShapeRenderTextAsRectangles --
 *
 *	Convert a string and font into an array of rectangles that
 *	describe the shape of the rendered string.
 *
 * Returns:
 *	An array of XRectangles (allocated with Tcl_Alloc()) or NULL if
 *	an error occurred (when an error message will be left in the
 *	interpreter.)
 *
 * Notes:
 *	You are warned that the operation of this code is _highly_
 *	arcane, requiring the drawing of the text first on a pixmap,
 *	then the conversion of that pixmap to an image, and then that
 *	image to a (sorted) rectangle list which can then be combined
 *	with the shape region.  AAARGH!
 *
 *	In other words, this code is awful and slow, but it works.
 *	Modify at your own risk...
 *
 *	(Of course, if you can make it go faster, please be my guest!)
 *
 * ----------------------------------------------------------------------
 */

XRectangle *
ShapeRenderTextAsRectangles(tkwin, interp, stringObj, font, numRects)
     Tk_Window tkwin;    /*in*/
     Tcl_Interp *interp; /*in*/
     Tcl_Obj *stringObj; /*in*/
     Tcl_Obj *font;      /*in*/
     int *numRects;      /*out*/
{
    Tk_FontMetrics metrics;
    Pixmap pixmap;
    int width,length,i,j,k;
    XGCValues gcValues;
    GC gc;
    XImage *xi;
    XRectangle *rects;

    Display *dpy = Tk_Display(tkwin);
    Window win = Tk_WindowId(tkwin);
    char *string = Tcl_GetStringFromObj(stringObj, &length);
    char *fontname = Tcl_GetStringFromObj(font, NULL);
    unsigned long black = BlackPixel(dpy, Tk_ScreenNumber(tkwin));
    unsigned long white = WhitePixel(dpy, Tk_ScreenNumber(tkwin));
    Tk_Font tkfont = Tk_GetFont(interp, tkwin, fontname);

    if (!tkfont) {
	return NULL;
    }
    Tk_GetFontMetrics(tkfont, &metrics);
    width = Tk_TextWidth(tkfont, string, length);

    /* Draw the text */
    pixmap = Tk_GetPixmap(dpy,win, width, metrics.linespace, Tk_Depth(tkwin));

    /* Start by clearing the background; life is amusing if you don't! */
    gcValues.foreground = white;
    gcValues.graphics_exposures = False;
    gc = Tk_GetGC(tkwin, GCForeground | GCGraphicsExposures, &gcValues);
    XFillRectangle(dpy, pixmap, gc, 0,0, width, metrics.linespace);
    Tk_FreeGC(dpy, gc);

    /* Now paint the string in the given font. */
    gcValues.font = Tk_FontId(tkfont);
    gcValues.foreground = black;
    gcValues.background = white;
    gc = Tk_GetGC(tkwin, GCForeground | GCBackground | GCFont |
		  GCGraphicsExposures, &gcValues);
    Tk_DrawChars(dpy, pixmap, gc, tkfont, string, length, 0, metrics.ascent);
    Tk_FreeGC(dpy, gc);
    Tk_FreeFont(tkfont);

    /* Convert to rectangles using an image.  YUCK! */
    xi = XGetImage(dpy, pixmap,  0, 0,  width, metrics.linespace,
		   AllPlanes, ZPixmap);
    Tk_FreePixmap(dpy, pixmap);

    /* Note that this allocates far, far too much memory */
    rects = (XRectangle *)
	Tcl_Alloc(sizeof(XRectangle) * width * metrics.linespace);
    for (j=1,k=0 ; j<metrics.linespace ; j++) {
	for (i=0 ; i<width ; i++) {
	    if (XGetPixel(xi, i, j) == black) {
		rects[k].x = i;
		rects[k].y = j;
		rects[k].width = 1;
		rects[k].height = 1;
		k++;
	    }
	}
    }
    XDestroyImage(xi);

    *numRects = k;
    /* Would be more efficient to realloc() down to the size of memory
     * block actually needed to represent the rectangles reported.
     * But I can't be bothered, since we're only going to keep this
     * block long enough to pass it off to the XShape extension
     * anyway. */
    return /*(XRectangle*)Tcl_Realloc((char *)*/rects/*,
	     k*sizeof(XRectangle))*/;
}
