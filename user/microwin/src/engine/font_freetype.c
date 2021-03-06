/*
 * Copyright (c) 2000, 2002, 2003 Greg Haerr <greg@censoft.com>
 * Portions Copyright (c) 2002 by Koninklijke Philips Electronics N.V.
 *
 * Freetype TrueType routines originally contributed by Martin Jolicoeur
 * Heavily modified by Greg Haerr
 */
/*#define NDEBUG*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <freetype/freetype.h>
#include <freetype/ftxkern.h>
#include <freetype/ftnameid.h>
#include <freetype/ftxcmap.h>
#include <freetype/ftxwidth.h>
#include "device.h"
#include "devfont.h"

#if TT_FREETYPE_MAJOR != 1 || TT_FREETYPE_MINOR < 3
#error "You must link with freetype lib version 1.3.x +, and not freetype 2."
#endif

typedef struct MWFREETYPEFONT {
	PMWFONTPROCS	fontprocs;	/* common hdr*/
	MWCOORD		fontsize;
	int		fontrotation;
	int		fontattr;		

	TT_Face 	face;		/* freetype stuff*/
	TT_Instance	instance;
	TT_CharMap 	char_map;
	TT_Kerning 	directory;
	TT_Matrix 	matrix;
	TT_Glyph 	glyph;
	MWBOOL 		can_kern;
	short 		last_glyph_code;
	short 		last_pen_pos;
} MWFREETYPEFONT;

int  freetype_init(PSD psd);
PMWFREETYPEFONT freetype_createfont(const char *name, MWCOORD height, int attr);

static MWBOOL freetype_getfontinfo(PMWFONT pfont, PMWFONTINFO pfontinfo);
static void freetype_gettextsize(PMWFONT pfont, const void *text, int cc,
		MWTEXTFLAGS flags, MWCOORD *pwidth, MWCOORD *pheight,
		MWCOORD *pbase);
static void freetype_destroyfont(PMWFONT pfont);
static void freetype_drawtext(PMWFONT pfont, PSD psd, MWCOORD x, MWCOORD y,
		const void *text, int cc, MWTEXTFLAGS flags);
static void freetype_setfontsize(PMWFONT pfont, MWCOORD fontsize);
static void freetype_setfontrotation(PMWFONT pfont, int tenthdegrees);
		
/* handling routines for MWFREETYPEFONT*/
static MWFONTPROCS freetype_procs = {
	MWTF_UC16,			/* routines expect unicode 16*/
	freetype_getfontinfo,
	freetype_gettextsize,
	NULL,				/* gettextbits*/
	freetype_destroyfont,
	freetype_drawtext,
	freetype_setfontsize,
	freetype_setfontrotation,
	NULL,				/* setfontattr*/
	NULL,				/* duplicate not yet implemented */
};

static TT_Engine 	engine;		/* THE ONLY freetype engine */
static OUTPIXELVAL gray_palette[5];

/* temp extern decls*/
extern MWPIXELVAL gr_foreground;
extern MWPIXELVAL gr_background;
extern MWBOOL gr_usebg;

int
freetype_init(PSD psd)
{
	static int inited = 0;

	if (inited)
		return 1;
	
	/* Init freetype library */
	if (TT_Init_FreeType (&engine) != TT_Err_Ok) {
		return 0;
	}

	/* Init kerning extension */
	if (TT_Init_Kerning_Extension (engine) != TT_Err_Ok)
		return 0;

	inited = 1;
	return 1;
}

PMWFREETYPEFONT
freetype_createfont(const char *name, MWCOORD height, int attr)
{
	PMWFREETYPEFONT 	pf;
	unsigned short 		i, n;
	unsigned short 		platform, encoding;
	TT_Face_Properties 	properties;
	char *			p;
	char			fontname[128];

	/* check for pathname prefix*/
	if (strchr(name, '/') != NULL)
		strcpy(fontname, name);
	else {
		strcpy(fontname, FREETYPE_FONT_DIR);
		strcat(fontname, "/");
		strcat(fontname, name);
	}

	/* check for extension*/
	if ((p = strrchr(fontname, '.')) == NULL ||
	    strcmp(p, ".ttf") != 0) {
		strcat(fontname, ".ttf");
	}

	/* allocate font structure*/
	pf = (PMWFREETYPEFONT)calloc(sizeof(MWFREETYPEFONT), 1);
	if (!pf)
		return NULL;
	pf->fontprocs = &freetype_procs;

	/* Load face */
	if (TT_Open_Face (engine, fontname, &pf->face) != TT_Err_Ok)
		goto out;

	/* Load first kerning table */
	pf->can_kern = TRUE;
	if (TT_Load_Kerning_Table (pf->face, 0) != TT_Err_Ok)
		pf->can_kern = FALSE;
	else {
		if (TT_Get_Kerning_Directory (pf->face, &pf->directory)
		    != TT_Err_Ok)
			pf->can_kern = FALSE;
		else {
			/* Support only version 0 kerning table ... */
			if ((pf->directory.version != 0) ||
				(pf->directory.nTables <= 0) ||
				(pf->directory.tables->loaded != 1) ||
				(pf->directory.tables->version != 0) ||
				(pf->directory.tables->t.kern0.nPairs <= 0))
					pf->can_kern = FALSE;
		}
	}

	/* get face properties and allocate preload arrays */
	TT_Get_Face_Properties (pf->face, &properties);

#if 0
	/*
	 * Use header information for ascent and descent
	 * to compute scaled ascent/descent for current font height.
	 */
	h = properties.os2->sTypoAscender - properties.os2->sTypoDescender
		+ properties.os2->sTypoLineGap;
	ascent = properties.os2->sTypoAscender
		+ properties.os2->sTypoLineGap/2;
	pf->ascent = (ascent * height + h/2) / h;
	pf->descent = height - pf->ascent;
#endif
	/* Create a glyph container */
	if (TT_New_Glyph (pf->face, &pf->glyph) != TT_Err_Ok)
		goto out;

	/* create instance */
	if (TT_New_Instance (pf->face, &pf->instance) != TT_Err_Ok)
		goto out;

	/* Set the instance resolution */
	if (TT_Set_Instance_Resolutions (pf->instance, 96, 96) != TT_Err_Ok)
		goto out;

	/* Look for a Unicode charmap: Windows flavor of Apple flavor only */
	n = properties.num_CharMaps;

	for (i = 0; i < n; i++) {
		TT_Get_CharMap_ID (pf->face, i, &platform, &encoding);
		if (((platform == TT_PLATFORM_MICROSOFT) &&
			(encoding == TT_MS_ID_UNICODE_CS)) ||
				((platform == TT_PLATFORM_APPLE_UNICODE) &&
			 		(encoding == TT_APPLE_ID_DEFAULT)))
		{
			TT_Get_CharMap (pf->face, i, &pf->char_map);
			i = n + 1;
		}
	}
	if (i == n) {
		DPRINTF("freetype_createfont: no unicode map table\n");
		goto out;
	}
	
	GdSetFontSize((PMWFONT)pf, height);
	GdSetFontRotation((PMWFONT)pf, 0);
	GdSetFontAttr((PMWFONT)pf, attr, 0);

	return pf;

out:
	free(pf);
	return NULL;
}

static int
compute_kernval(PMWFREETYPEFONT pf, short current_glyph_code)
{
	int 		i = 0;
	int 		kernval;
	int 		nPairs = pf->directory.tables->t.kern0.nPairs;
	TT_Kern_0_Pair *pair = pf->directory.tables->t.kern0.pairs;

	if (pf->last_glyph_code != -1) {
		while ((pair->left != pf->last_glyph_code)
			&& (pair->right != current_glyph_code))
		{
			pair++;
			i++;
			if (i == nPairs)
			break;
		}

		if (i == nPairs)
			kernval = 0;
		else
			/* We round the value (hence the +32) */
			kernval = (pair->value + 32) & -64;
	} else
		kernval = 0;

	return kernval;
}

static TT_UShort
Get_Glyph_Width(PMWFREETYPEFONT pf, TT_UShort glyph_index)
{
	TT_Glyph_Metrics metrics;
	
    	if (TT_Load_Glyph ( pf->instance, pf->glyph,
		TT_Char_Index (pf->char_map,glyph_index),
		TTLOAD_SCALE_GLYPH|TTLOAD_HINT_GLYPH) != TT_Err_Ok)
    	{
		/* Try to load default glyph: index 0 */
		if (TT_Load_Glyph ( pf->instance, pf->glyph, 0,
			TTLOAD_SCALE_GLYPH|TTLOAD_HINT_GLYPH) != TT_Err_Ok)
		    return 0;
	}

	TT_Get_Glyph_Metrics (pf->glyph, &metrics);
	return((metrics.advance & 0xFFFFFFC0) >> 6);
}

/* Render a single glyph*/
static void
drawchar(PMWFREETYPEFONT pf, PSD psd, TT_Glyph glyph, int x_offset,
	int y_offset)
{
	TT_F26Dot6 	xmin, ymin, xmax, ymax, x, y, z;
	unsigned char 	*src, *srcptr;
	MWPIXELVAL 	*dst, *dstptr;
	MWPIXELVAL 	*bitmap;
	int 		size, width, height;
	TT_Outline 	outline;
	TT_BBox 	bbox;
	TT_Raster_Map 	Raster;
	TT_Error 	error;
	/*MWPIXELVAL 	save;*/

	/* we begin by grid-fitting the bounding box */
	TT_Get_Glyph_Outline (pf->glyph, &outline);
	TT_Get_Outline_BBox (&outline, &bbox);

	xmin = (bbox.xMin & -64) >> 6;
	ymin = (bbox.yMin & -64) >> 6;
	xmax = ((bbox.xMax + 63) & -64) >> 6;
	ymax = ((bbox.yMax + 63) & -64) >> 6;
	width = xmax - xmin;
	height = ymax - ymin;
	size = width * height;
	if (!size)
		return;

	/* now re-allocate the raster bitmap */
	Raster.rows = height;
	Raster.width = width;

	if (pf->fontattr&MWTF_ANTIALIAS)
		Raster.cols = (Raster.width + 3) & -4;	/* pad to 32-bits */
	else
		Raster.cols = (Raster.width + 7) & -8;	/* pad to 64-bits ??? */

	Raster.flow = TT_Flow_Up;
	Raster.size = Raster.rows * Raster.cols;
	Raster.bitmap = malloc (Raster.size);

	memset (Raster.bitmap, 0, Raster.size);

	/* now render the glyph in the small pixmap */

	/* IMPORTANT NOTE: the offset parameters passed to the function     */
	/* TT_Get_Glyph_Bitmap() must be integer pixel values, i.e.,        */
	/* multiples of 64.  HINTING WILL BE RUINED IF THIS ISN'T THE CASE! */
	/* This is why we _did_ grid-fit the bounding box, especially xmin  */
	/* and ymin.                                                        */

	if (!(pf->fontattr&MWTF_ANTIALIAS))
		error = TT_Get_Glyph_Bitmap (pf->glyph, &Raster,
				-xmin * 64, -ymin * 64);
	else
		error = TT_Get_Glyph_Pixmap (pf->glyph, &Raster,
				-xmin * 64, -ymin * 64);

	if (error) {
		free (Raster.bitmap);
		return;
	}

	bitmap = malloc (size * sizeof (MWPIXELVAL));
	memset (bitmap, 0, size * sizeof (MWPIXELVAL));

	src = (char *) Raster.bitmap;
	dst = bitmap + (size - width);

	for (y = ymin; y < ymax; y++) {
		srcptr = src;
		dstptr = dst;

		for (x = xmin; x < xmax; x++) {
			if (pf->fontattr&MWTF_ANTIALIAS)
				*dstptr++ = gray_palette[(int) *srcptr];
			else {
				for(z=0;
				    z <= ((xmax-x-1) < 7 ? (xmax-x-1) : 7);
				    z++) {
					*dstptr++ = ((*srcptr << z) & 0x80)?
						gr_foreground: gr_background;
				}
				x += 7;
			}

			srcptr++;
		}

		src += Raster.cols;
		dst -= width;
	}

	/* FIXME - must clear background upstairs if not gr_usebg*/
	/* FIXME: GdArea problem if fg == bg*/
	/*save = gr_background;*/
	/*gr_background = gr_foreground + 1;*/

	/* Now draw the bitmap ... */
	GdArea(psd, x_offset + xmin, y_offset - (ymin + height), width, height,
		bitmap, MWPF_PIXELVAL);

	/*gr_background = save;*/

	free (bitmap);
	free (Raster.bitmap);
}

/*
 * Draw unicode 16 text string using FREETYPE type font
 */
static void
freetype_drawtext(PMWFONT pfont, PSD psd, MWCOORD ax, MWCOORD ay,
	const void *text, int cc, MWTEXTFLAGS flags)
{
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;
	const unsigned short *	str = text;
	TT_F26Dot6 	x = ax, y = ay;
	TT_Pos 		vec_x, vec_y;
	int 		i;
	TT_F26Dot6	startx, starty;
	TT_Outline 	outline;
	TT_UShort 	curchar;
	TT_Glyph_Metrics metrics;
	TT_Face_Properties properties;
	TT_Instance_Metrics imetrics;
	TT_F26Dot6 ascent, descent;
	static unsigned char blend[5] = { 0x00, 0x44, 0x88, 0xcc, 0xff };
	static unsigned char virtual_palette[5] = { 0, 1, 2, 3, 4 };

	pf->last_glyph_code = -1;		/* reset kerning*/
	pf->last_pen_pos = -32767;

	/* 
	 * Compute instance ascent & descent values
	 * in fractional units (1/64th pixel)
	 */
	TT_Get_Face_Properties (pf->face, &properties);
	TT_Get_Instance_Metrics(pf->instance, &imetrics);	
  
	ascent = ((properties.horizontal->Ascender * imetrics.y_scale)/0x10000);
	descent = ((properties.horizontal->Descender*imetrics.y_scale)/0x10000);

	/* 
	 * Offset the starting point if necessary,
	 * FreeType always aligns at baseline
	 */
	if (flags&MWTF_BOTTOM) {
		vec_x = 0;
		vec_y = descent;
		TT_Transform_Vector(&vec_x, &vec_y,&pf->matrix);
		x -= vec_x / 64;
		y += vec_y / 64;
	} else if (flags&MWTF_TOP) {
		vec_x = 0;
		vec_y = ascent;
		TT_Transform_Vector(&vec_x, &vec_y,&pf->matrix);
		x -= vec_x / 64;
		y += vec_y / 64;
	}

	/* Set the "graylevels" */
	if (pf->fontattr&MWTF_ANTIALIAS) {
		TT_Set_Raster_Gray_Palette (engine, virtual_palette);

		alphablend(psd, gray_palette, gr_foreground, gr_background,
			blend, 5);
	}

	startx = x;
	starty = y;
	for (i = 0; i < cc; i++) {
		curchar = TT_Char_Index (pf->char_map, str[i]);

		if (TT_Load_Glyph (pf->instance, pf->glyph, curchar,
			TTLOAD_DEFAULT) != TT_Err_Ok)
				continue;

		if (pf->fontrotation) {
			TT_Get_Glyph_Outline (pf->glyph, &outline);
			TT_Transform_Outline (&outline, &pf->matrix);
		}

		TT_Get_Glyph_Metrics (pf->glyph, &metrics);

		if ((pf->fontattr&MWTF_KERNING) && pf->can_kern) {
			if (pf->fontrotation) {
				vec_x = compute_kernval(pf, curchar);
				vec_y = 0;
				TT_Transform_Vector(&vec_x, &vec_y,&pf->matrix);

				x += vec_x / 64;
				y -= vec_y / 64;
			} else
				x += compute_kernval(pf, curchar) / 64;
		}
			
		drawchar(pf, psd, pf->glyph, x, y);

		if (pf->fontrotation) {
			vec_x = metrics.advance;
			vec_y = 0;
			TT_Transform_Vector (&vec_x, &vec_y, &pf->matrix);

			x += vec_x / 64;
			y -= vec_y / 64;
		} else {
			x += metrics.advance / 64;

			/* Kerning point syndrome avoidance */
			if (pf->last_pen_pos > x)
				x = pf->last_pen_pos;
			pf->last_pen_pos = x;
		}

		pf->last_glyph_code = curchar;
	}

	if (pf->fontattr & MWTF_UNDERLINE)
		GdLine(psd, startx, starty, x, y, FALSE);
}

/*
 * Return information about a specified font.
 */
static MWBOOL
freetype_getfontinfo(PMWFONT pfont, PMWFONTINFO pfontinfo)
{
	int	i;
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;
	TT_Face_Properties 	properties;
    TT_Instance_Metrics imetrics;
    TT_UShort last_glyph_index;

	TT_Get_Face_Properties (pf->face, &properties);
    TT_Get_Instance_Metrics(pf->instance, &imetrics);

    /* Fill up the fields */
	pfontinfo->height = (((properties.horizontal->Ascender * \
	                        imetrics.y_scale)/ 0x10000) >> 6) -
	                    (((properties.horizontal->Descender * \
	                        imetrics.y_scale)/ 0x10000) >> 6);
	pfontinfo->maxwidth = ((properties.horizontal->xMax_Extent * \
	                        imetrics.x_scale)/ 0x10000) >> 6;
	pfontinfo->baseline = ((properties.horizontal->Ascender * \
	                        imetrics.y_scale)/ 0x10000) >> 6;
	pfontinfo->firstchar = TT_CharMap_First(pf->char_map, NULL);
	pfontinfo->lastchar = TT_CharMap_Last(pf->char_map, NULL);
	pfontinfo->fixed = properties.postscript->isFixedPitch;

	/* FIXME: Wild guesses - should be possible to get these properly
	 * from the library. */
	pfontinfo->linespacing = pfontinfo->height + 2;
	pfontinfo->descent = pfontinfo->height - pfontinfo->baseline;
	pfontinfo->maxascent = pfontinfo->baseline;
	pfontinfo->maxdescent = pfontinfo->descent;
		
    last_glyph_index = properties.num_Glyphs > 255 ? 255: properties.num_Glyphs-1;

    /* Doesn't work ... don't know why ....*/
#if 0
    if (TT_Get_Face_Widths( pf->face, 0,
                            last_glyph_index, widths, NULL ) != TT_Err_Ok) {
        return TRUE;
    }

    for(i=0; i<=last_glyph_index; i++)
		DPRINTF("widths[%d]: %d\n", i, widths[i]);
#endif

    /* Get glyphs widths */
	for(i=0; i<=last_glyph_index; i++)
		pfontinfo->widths[i] = Get_Glyph_Width(pf, i);

#if 0
    DPRINTF("x_ppem: %d\ny_ppem: %d\nx_scale: %d\ny_scale: %d\n\
    x_resolution: %d\ny_resolution: %d\n",
    imetrics.x_ppem, imetrics.y_ppem, imetrics.x_scale, \
    imetrics.y_scale, imetrics.x_resolution, imetrics.y_resolution);

    DPRINTF("Ascender: %d\nDescender: %d\nxMax_Extent: %d\n\
    Mac Style Say Italic?: %d\nMac Style Say Bold?: %d\n\
    sTypoAscender: %d\nsTypoDescender: %d\nusWinAscent: %d\n\
    usWinDescent: %d\nusFirstCharIndex: %d\nusLastCharIndex: %d\n\
    OS2 Say Italic?: %d\nOS2 Say Bold?: %d\nOS2 Say monospaced?: %d\n\
    Postscript Say monospaced?: %d\n",\
    properties.horizontal->Ascender,
    properties.horizontal->Descender,
    properties.horizontal->xMax_Extent,
    (properties.header->Mac_Style & 0x2)?1:0,
    (properties.header->Mac_Style & 0x1)?1:0,
    properties.os2->sTypoAscender,
    properties.os2->sTypoDescender,
    properties.os2->usWinAscent,
    properties.os2->usWinDescent,
    properties.os2->usFirstCharIndex,
    properties.os2->usLastCharIndex,
    (properties.os2->fsSelection & 0x1)?1:0,
    (properties.os2->fsSelection & 0x20)?1:0,
    properties.postscript->isFixedPitch,
    (properties.os2->panose[3] == 9)?1:0);
#endif	
			
	return TRUE;
}

static void
freetype_gettextsize(PMWFONT pfont, const void *text, int cc, MWTEXTFLAGS flags,
	MWCOORD *pwidth, MWCOORD *pheight, MWCOORD *pbase)
{
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;
	const unsigned short *	str = text;
	TT_F26Dot6 	x = 0;
	int 		i;
	TT_UShort 	curchar;
	TT_Glyph_Metrics metrics;
	TT_Face_Properties 	properties;
	TT_Instance_Metrics imetrics;

	TT_Get_Face_Properties (pf->face, &properties);
	TT_Get_Instance_Metrics(pf->instance, &imetrics);
	
	pf->last_glyph_code = -1;		/* reset kerning*/
	pf->last_pen_pos = -32767;
	
	for (i = 0; i < cc; i++) {
		curchar = TT_Char_Index (pf->char_map, str[i]);

		if (TT_Load_Glyph (pf->instance, pf->glyph, curchar,
			TTLOAD_SCALE_GLYPH|TTLOAD_HINT_GLYPH) != TT_Err_Ok)
				continue;

		TT_Get_Glyph_Metrics (pf->glyph, &metrics);

		if ((pf->fontattr&MWTF_KERNING) && pf->can_kern) {
				x += compute_kernval(pf, curchar) / 64;
		}
		
		x += metrics.advance / 64;

		/* Kerning point syndrome avoidance */
		if (pf->last_pen_pos > x)
			x = pf->last_pen_pos;
		pf->last_pen_pos = x;

		pf->last_glyph_code = curchar;
	}

	*pwidth = x;
	*pheight =  (((properties.horizontal->Ascender * 
	                imetrics.y_scale)/ 0x10000) >> 6) -
	            (((properties.horizontal->Descender * 
	            imetrics.y_scale)/ 0x10000) >> 6);
	/* FIXME: is it what's required ?? */
	*pbase = (((-properties.horizontal->Descender) * 
	            imetrics.y_scale)/ 0x10000) >> 6;
}

static void
freetype_destroyfont(PMWFONT pfont)
{
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;

	TT_Close_Face(pf->face);
	free(pf);
}

static void
freetype_setfontsize(PMWFONT pfont, MWCOORD fontsize)
{
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;
	
	pf->fontsize = fontsize;

	/* allow create font with height=0*/
	if (!fontsize)
		return;
	
	/* We want real pixel sizes ... not points ...*/
	TT_Set_Instance_PixelSizes( pf->instance, pf->fontsize,
                                pf->fontsize, pf->fontsize * 64 );
#if 0
	/* set charsize (convert to points for freetype)*/
	TT_Set_Instance_CharSize (pf->instance,
		((pf->fontsize * 72 + 96/2) / 96) * 64);
#endif
}

static void
freetype_setfontrotation(PMWFONT pfont, int tenthdegrees)
{
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;
	float 		angle;
	
	pf->fontrotation = tenthdegrees;

	/* Build the rotation matrix with the given angle */
	TT_Set_Instance_Transform_Flags (pf->instance, TRUE, FALSE);

	angle = pf->fontrotation * M_PI / 1800;
	pf->matrix.yy = (TT_Fixed) (cos (angle) * (1 << 16));
	pf->matrix.yx = (TT_Fixed) (sin (angle) * (1 << 16));
	pf->matrix.xx = pf->matrix.yy;
	pf->matrix.xy = -pf->matrix.yx;
}

/* FIXME: this routine should work for all font renderers...*/
int
GdGetTextSizeEx(PMWFONT pfont, const void *str, int cc,int nMaxExtent,
	int* lpnFit, int* alpDx,MWCOORD *pwidth,MWCOORD *pheight,
	MWCOORD *pbase, MWTEXTFLAGS flags)
{
	unsigned short	buf[256];
	unsigned short* text;
	PMWFREETYPEFONT	pf = (PMWFREETYPEFONT)pfont;
	MWTEXTFLAGS	defencoding = pf->fontprocs->encoding;
	int 		x = 0;
	int 		i;
	TT_UShort 	curchar;
	TT_Glyph_Metrics metrics;
	TT_Face_Properties 	properties;
	TT_Instance_Metrics imetrics;

	if ((cc<0)||(!str))
	{
		*pwidth = *pheight = *pbase = 0;
		return 0;
	}
	/* convert encoding if required*/
	if((flags & (MWTF_PACKMASK|MWTF_DBCSMASK)) != defencoding) 
	{
		cc = GdConvertEncoding(str, flags, cc, buf, defencoding);
		flags &= ~(MWTF_PACKMASK|MWTF_DBCSMASK);
		flags |= defencoding;
		text=buf;
	} else text =(unsigned short*)str;
	if(cc <= 0)
	{
		*pwidth = *pheight = *pbase = 0;
		return 0;
	}

	TT_Get_Face_Properties (pf->face, &properties);
	TT_Get_Instance_Metrics(pf->instance, &imetrics);
	
	pf->last_glyph_code = -1;		/* reset kerning*/
	pf->last_pen_pos = -32767;
	if (lpnFit)
	     *lpnFit=-1;
	for (i = 0; i < cc; i++) 
	{
		curchar = TT_Char_Index (pf->char_map,text[i]);
		
		if (TT_Load_Glyph (pf->instance, pf->glyph, curchar,
			TTLOAD_SCALE_GLYPH|TTLOAD_HINT_GLYPH) != TT_Err_Ok)			
		{		
			printf("Unable to load glyph with index=%d\n",curchar);    
			return 0;
		}
		TT_Get_Glyph_Metrics (pf->glyph, &metrics);
		if ((pf->fontattr&MWTF_KERNING) && pf->can_kern)
		{
			x += compute_kernval(pf, curchar) / 64;
		}
		x += metrics.advance / 64;
     		if((lpnFit)&&(alpDx))
		{
			if (x<=nMaxExtent)
			     alpDx[i]=x;
			else
			     if (*lpnFit==-1)
					(*lpnFit)=i;               		     
		}
		/* Kerning point syndrome avoidance */
		if (pf->last_pen_pos > x)
			x = pf->last_pen_pos;
		pf->last_pen_pos = x;
		pf->last_glyph_code = curchar;
	}
     	if ((lpnFit)&&(*lpnFit==-1))
    		*lpnFit=cc;
	*pwidth = x;
	*pheight = (((properties.horizontal->Ascender * 
	                imetrics.y_scale)/ 0x10000) >> 6) -
			    (((properties.horizontal->Descender * 
			    imetrics.y_scale)/ 0x10000) >> 6);
	/* FIXME: is it what's required ??*/
	if (pbase)
		*pbase = (((-properties.horizontal->Descender) * 
			    imetrics.y_scale)/ 0x10000) >> 6;
	return 1;
}

/* 
 * This function is taken almost verbatim from ftdump.c from 
 * the freetype library (version 1.3.1)
 */
static char *
tt_lookup_name(TT_Face face)
{
	TT_Face_Properties prop;
	unsigned short i, n;
	unsigned short platform, encoding, language, id;
	char *string;
	char *name_buffer;
	unsigned short string_len;
	int j, found;
	int index = 4; /* I dont know why as yet.. */
	int name_len;


	TT_Get_Face_Properties(face, &prop);
	n = prop.num_Names;

	for ( i = 0; i < n; i++ ) {
		TT_Get_Name_ID( face, i, &platform, &encoding, &language, &id );
		TT_Get_Name_String( face, i, &string, &string_len );

		if (id == index ) {
			/* The following code was inspired from Mark Leisher's */
			/* ttf2bdf package                                     */
			found = 0;

			/* Try to find a Microsoft English name */
			if ( platform == 3 )
				for ( j = 1; j >= 0; j-- )
					if ( encoding == j )  /* Microsoft ? */
						if ( (language & 0x3FF) == 0x009 ) {
							/* English language */
							found = 1;
							break;
						}

			if ( !found && platform == 0 && language == 0 )
				found = 1;

			/* Found a Unicode Name. */
			if ( found ) {
				if ( string_len > 512 )
					string_len = 512;

				name_len = 0;
				name_buffer = (char*)malloc((string_len / 2) + 1);

				for ( i = 1; i < string_len; i += 2 )
					name_buffer[name_len++] = string[i];

				name_buffer[name_len] = '\0';

				return name_buffer;
			}
		}
	}

	/* Not found */
	return NULL;
}

static char *
get_tt_name(char *p)
{
	TT_Face face;
	char *ret;

	/*printf("Trying to open: %s!\n",p);*/

	if (TT_Open_Face(engine, p, &face) != TT_Err_Ok) {
		printf("Error opening font: %s\n", p);
		return NULL;
	}

	ret = tt_lookup_name(face);

	TT_Close_Face(face);

	return ret;
}

void
GdFreeFontList(MWFONTLIST ***fonts, int n)
{
	int i;
	MWFONTLIST *g, **list = *fonts;

	for (i = 0; i < n; i++) {
		g = list[i];
		if(g) {
			if(g->mwname) 
				free(g->mwname);
			if(g->ttname) 
				free(g->ttname);
			free(g);
		}
	}
	free(list);
	*fonts = 0;
}

void
GdGetFontList(MWFONTLIST ***fonts, int *numfonts)
{
	DIR *dir;
	struct dirent *dent;
	char *p, *ftmp;
	int pl, idx = 0;
	MWFONTLIST **list;
	

	if (TT_Err_Ok != TT_Init_FreeType(&engine)) {
		printf("Unable to initialize freetype\n");
		*numfonts = -1;
		return ;
	}

	dir = opendir(FREETYPE_FONT_DIR);

	if (dir <= 0) {
		printf("Error opening font directory\n");
		*numfonts = -1;
		return ;
	}

	/* get the number of strings we need to allocate */
	while ((dent = readdir(dir)) != NULL) {
		p = strrchr(dent->d_name, '.');
		if (strcasecmp(p, ".ttf") == 0)
			idx++;
	}

	*numfonts = idx;
	rewinddir(dir);

	/* allocate strings */
	list = (MWFONTLIST**)malloc(idx * sizeof(MWFONTLIST*));
	for (pl = 0; pl < idx; pl++)
		list[pl] = (MWFONTLIST*)malloc(sizeof(MWFONTLIST));

	*fonts = list;

	idx = 0;

	while ((dent = readdir(dir)) != NULL) {
		/* check extension */
		p = strrchr(dent->d_name, '.');

		if (strcasecmp(p, ".ttf") == 0) {
			
			/* get full path */
			p = 0;
			pl = strlen(FREETYPE_FONT_DIR) + strlen(dent->d_name) *	
						sizeof(char) + 2;
			p = (char*)malloc(pl);
			p = (char*)memset(p, '\0', pl);
			p = (char*)strcat(p, FREETYPE_FONT_DIR);
			p = (char*)strcat(p, "/");
			p = (char*)strcat(p, dent->d_name);


			if((ftmp = get_tt_name(p)) != NULL) {
				list[idx]->ttname = ftmp;
				list[idx]->mwname = malloc(strlen(dent->d_name) + 1);
				list[idx]->mwname = strcpy(list[idx]->mwname, dent->d_name);

				idx++;
			}

			free(p);
		}
	}
	
	closedir(dir);
}
