/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  gnome-print-preview.c: print preview driver
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors:
 *    Miguel de Icaza <miguel@ximian.com>
 *    Lauris Kaplinski <lauris@ximian.com>
 *
 *  Copyright (C) 1999-2002 Ximian Inc. and authors
 *
 */

#include <config.h>
#include "gnome-print-preview.h"

#include <string.h>
#include <math.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <libgnomecanvas/gnome-canvas-clipgroup.h>

#include <libgnomeprint/private/gnome-print-private.h>
#include <libgnomeprint/private/gp-gc-private.h>
#include <libgnomeprint/private/gnome-glyphlist-private.h>

#include "gnome-canvas-hacktext.h"

typedef struct _GnomePrintPreviewPrivate GnomePrintPreviewPrivate;

struct _GnomePrintPreview {
	GnomePrintContext pc;

	GPtrArray *groups;
	GnomeCanvasGroup *group;
	guint pages;

	/* Properties */
	GnomeCanvasGroup *page;
	gboolean theme_compliance;
	gboolean use_theme;
	gboolean only_first;
};

struct _GnomePrintPreviewClass {
	GnomePrintContextClass parent_class;
};

static GnomePrintContextClass *parent_class;

#define GPP_COLOR_RGBA(color, ALPHA) \
	((guint32) (ALPHA              | \
	 (((color).red   / 256) << 24) | \
	 (((color).green / 256) << 16) | \
	 (((color).blue  / 256) << 8)))

static void
outline_set_style_cb (GtkWidget *canvas, GnomeCanvasItem *item)
{
	gint32 color;
	GtkStyle *style;

	style = gtk_widget_get_style (GTK_WIDGET (canvas));
	color = GPP_COLOR_RGBA (style->text [GTK_STATE_NORMAL], 0xff);
	
	gnome_canvas_item_set (item, "outline_color_rgba", color, NULL);
}

static int
gnome_print_preview_stroke (GnomePrintContext *pc, const ArtBpath *bpath)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);
	GnomeCanvasItem *item;
	GnomeCanvasPathDef *path;

	if (pp->only_first && (pp->pages > 1))
		return GNOME_PRINT_OK;

	path = gnome_canvas_path_def_new_from_foreign_bpath ((ArtBpath *) bpath);

	item = gnome_canvas_item_new (pp->group,
		gnome_canvas_bpath_get_type (),
		"bpath",	path,
		"width_units",	gp_gc_get_linewidth (pc->gc),
		"cap_style",	gp_gc_get_linecap (pc->gc) + 1 /* See #104932 */,
		"join_style",	gp_gc_get_linejoin (pc->gc),
		"outline_color_rgba", gp_gc_get_rgba (pc->gc),
		"miterlimit",	gp_gc_get_miterlimit (pc->gc),
		"dash",		gp_gc_get_dash (pc->gc),
		NULL);

	gnome_canvas_path_def_unref (path);

	if (pp->use_theme)
		outline_set_style_cb (GTK_WIDGET (item->canvas), item);	
	return 1;
}

static void
fill_set_style_cb (GtkWidget *canvas, GnomeCanvasItem *item)
{
	gint32 color, outline_color;
	GtkStyle *style;

	style = gtk_widget_get_style (GTK_WIDGET (canvas));
	color = GPP_COLOR_RGBA (style->bg [GTK_STATE_NORMAL], 0xff);
	outline_color = GPP_COLOR_RGBA (style->fg [GTK_STATE_NORMAL], 0xff);

	gnome_canvas_item_set (item, "fill_color_rgba", color, NULL);
	gnome_canvas_item_set (item, "outline_color_rgba", outline_color, NULL);
}
	
static int
gnome_print_preview_fill (GnomePrintContext *pc, const ArtBpath *bpath, ArtWindRule rule)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);
	GnomeCanvasItem *item;
	GnomeCanvasPathDef *path;

	if (pp->only_first && (pp->pages > 1))
		return GNOME_PRINT_OK;

	path = gnome_canvas_path_def_new_from_foreign_bpath ((ArtBpath *) bpath);

	item = gnome_canvas_item_new (pp->group,
		gnome_canvas_bpath_get_type (),
		"bpath", path,
		"outline_color", NULL,
		"fill_color_rgba", gp_gc_get_rgba (pc->gc),
		"wind", rule,
		NULL);
	gnome_canvas_path_def_unref (path);

	if (pp->use_theme)
		fill_set_style_cb (GTK_WIDGET (item->canvas), item);
	return 1;
}

static int
gnome_print_preview_gsave (GnomePrintContext *pc)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);

	if (!pp->groups)
		pp->groups = g_ptr_array_new ();
	g_ptr_array_add (pp->groups, pp->group);
	pp->group = GNOME_CANVAS_GROUP (
		gnome_canvas_item_new (pp->page, GNOME_TYPE_CANVAS_GROUP, NULL));
	return GNOME_PRINT_OK;
}

static int
gnome_print_preview_grestore (GnomePrintContext *pc)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);

	pp->group = g_ptr_array_remove_index (pp->groups, pp->groups->len - 1);
	return GNOME_PRINT_OK;
}

static int
gnome_print_preview_clip (GnomePrintContext *pc, const ArtBpath *b, ArtWindRule rule)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);
	GnomeCanvasPathDef *path;

	if (pp->only_first && (pp->pages > 1))
		return GNOME_PRINT_OK;

	path = gnome_canvas_path_def_new_from_foreign_bpath ((ArtBpath *) b);

	pp->group = GNOME_CANVAS_GROUP (gnome_canvas_item_new (pp->group,
		gnome_canvas_clipgroup_get_type (),
		"path", path,
		"wind", rule,
		NULL));

	gnome_canvas_path_def_unref (path);

	return 1;
}

static void
gnome_print_preview_image_free_pix (guchar *pixels, gpointer data)
{
	g_free (pixels);

}

static int
gnome_print_preview_image (GnomePrintContext *pc, const gdouble *affine, const guchar *px, gint w, gint h, gint rowstride, gint ch)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);
	GnomeCanvasItem *item;
	GdkPixbuf *pixbuf;
	int size, bpp;
	void *dup;

	if (pp->only_first && (pp->pages > 1))
		return GNOME_PRINT_OK;

	/*
	 * We do convert gray scale images to RGB
	 */

	if (ch == 1) {
		bpp = 3;
	} else {
		bpp = ch;
	}
	
	size = w * h * bpp;
	dup = g_malloc (size);
	if (!dup) return -1;

	if (ch == 3) {
		memcpy (dup, px, size);
		pixbuf = gdk_pixbuf_new_from_data (dup, GDK_COLORSPACE_RGB,
						   FALSE, 8, w, h, rowstride,
						   gnome_print_preview_image_free_pix, NULL);
	} else if (ch == 4) {
		memcpy (dup, px, size);
		pixbuf = gdk_pixbuf_new_from_data (dup, GDK_COLORSPACE_RGB,
				                   TRUE, 8, w, h, rowstride,
						   gnome_print_preview_image_free_pix, NULL);
	} else if (ch == 1) {
		unsigned char const *source;
		char *target;
		int  ix, iy;

		source = px;
		target = dup;

		for (iy = 0; iy < h; iy++){
			for (ix = 0; ix < w; ix++){
				*target++ = *source;
				*target++ = *source;
				*target++ = *source;
				source++;
			}
		}
		pixbuf = gdk_pixbuf_new_from_data (dup, GDK_COLORSPACE_RGB,
						   FALSE, 8, w, h, rowstride * 3,
						   gnome_print_preview_image_free_pix, NULL);
	} else
		return -1;

	item = gnome_canvas_item_new (pp->group,
				      GNOME_TYPE_CANVAS_PIXBUF,
				      "pixbuf", pixbuf,
				      "x",      0.0,
				      "y",      0.0,
				      "width",  (gdouble) w,
				      "height", (gdouble) h,
				      "anchor", GTK_ANCHOR_NW,
				      NULL);
	g_object_unref (G_OBJECT (pixbuf));

	/* Apply the transformation for the image */
	{
		double transform[6];
		double flip[6];
		flip[0] = 1.0 / w;
		flip[1] = 0.0;
		flip[2] = 0.0;
		flip[3] = -1.0 / h;
		flip[4] = 0.0;
		flip[5] = 1.0;

		art_affine_multiply (transform, flip, affine);
		gnome_canvas_item_affine_absolute (item, transform);
	}
	
	return 1;
}

static void
gnome_print_preview_clear (GnomePrintPreview *pp)
{
	GnomeCanvasGroup *g;

	g_return_if_fail (GNOME_IS_PRINT_PREVIEW (pp));

	if (!pp->page)
		return;
	g = GNOME_CANVAS_GROUP (pp->page);
	while (g->item_list) {
		GnomeCanvasItem *i = g->item_list->data;
		g->item_list = g_list_delete_link (g->item_list, g->item_list);
		gtk_object_destroy (GTK_OBJECT (i));
	}
	pp->group = pp->page;
}

void
gnome_print_preview_reset (GnomePrintPreview *pp)
{
	g_return_if_fail (GNOME_IS_PRINT_PREVIEW (pp));

	pp->pages = 0;
	gnome_print_preview_clear (pp);
}

static int
gnome_print_preview_beginpage (GnomePrintContext *pc, const guchar *name)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);

	pp->pages++;
	if (pp->only_first && (pp->pages > 1))
		return GNOME_PRINT_OK;
	gnome_print_preview_clear (pp);

	return GNOME_PRINT_OK;
}

static void
glyphlist_set_style_cb (GtkWidget *canvas, GnomeCanvasItem *item)
{
	GnomeGlyphList *gl, *new;
	gint32 color;
	GtkStyle *style;
	gint i;

	style = gtk_widget_get_style (GTK_WIDGET (canvas));
	color = GPP_COLOR_RGBA (style->text [GTK_STATE_NORMAL], 0xff);

	g_object_get (G_OBJECT (item), "glyphlist", &gl, NULL);
	new = gnome_glyphlist_duplicate (gl);
	for (i = 0; i < new->r_length; i++) {
		if (new->rules[i].code ==  GGL_COLOR) {
			new->rules[i].value.ival = color;
		}
	}
	gnome_canvas_item_set (item, "glyphlist", new, NULL);
	gnome_glyphlist_unref (new);
}

static int
gnome_print_preview_glyphlist (GnomePrintContext *pc, const gdouble *affine, GnomeGlyphList * glyphlist)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (pc);
	GnomeCanvasItem *item;
	double transform[6], a[6];

	if (pp->only_first && (pp->pages > 1))
		return GNOME_PRINT_OK;

	art_affine_scale (a, 1.0, -1.0);
	art_affine_multiply (transform, a, affine);

	item = gnome_canvas_item_new (pp->group,
				      gnome_canvas_hacktext_get_type (),
				      "x", 0.0,
				      "y", 0.0,
				      "glyphlist", glyphlist,
				      NULL);

	gnome_canvas_item_affine_absolute (item, transform);

	if (pp->use_theme)
		glyphlist_set_style_cb (GTK_WIDGET (item->canvas), item);	
	return GNOME_PRINT_OK;
}

static void
gnome_print_preview_finalize (GObject *object)
{
	GnomePrintPreview *pp = GNOME_PRINT_PREVIEW (object);

	if (pp->groups) {
		g_ptr_array_free (pp->groups, TRUE);
		pp->groups = NULL;
	}
	if (pp->page) {
		g_object_unref (G_OBJECT (pp->page));
		pp->page = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

enum {
	PROP_0,
	PROP_GROUP,
	PROP_USE_THEME,
	PROP_THEME_COMPLIANCE,
	PROP_ONLY_FIRST
};

static void
gnome_print_preview_get_property (GObject *gobject, guint param_id,
		  GValue *value, GParamSpec *pspec)
{
	GnomePrintPreview *preview = GNOME_PRINT_PREVIEW (gobject);
	
	switch (param_id) {
	case PROP_GROUP:
		g_value_set_object (value, preview->page);
		break;
	case PROP_THEME_COMPLIANCE:
		g_value_set_boolean (value, preview->theme_compliance);
		break;
	case PROP_USE_THEME:
		g_value_set_boolean (value, preview->use_theme);
		break;
	case PROP_ONLY_FIRST:
		g_value_set_boolean (value, preview->only_first);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, param_id, pspec);
	}
}

static void
gnome_print_preview_set_property (GObject *gobject, guint param_id,
		  const GValue *value, GParamSpec *pspec)
{
	GnomePrintPreview *preview = GNOME_PRINT_PREVIEW (gobject);

	switch (param_id) {
	case PROP_GROUP:
		if (preview->page)
			g_object_unref (G_OBJECT (preview->page));
		preview->page = g_value_get_object (value);
		if (preview->page)
			g_object_ref (G_OBJECT (preview->page));
		break;
	case PROP_THEME_COMPLIANCE:
		preview->theme_compliance = g_value_get_boolean (value);
		break;
	case PROP_USE_THEME:
		preview->use_theme = g_value_get_boolean (value);
		break;
	case PROP_ONLY_FIRST:
		preview->only_first = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, param_id, pspec);
	}
}

static void
class_init (GnomePrintPreviewClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	GnomePrintContextClass *pc_class = (GnomePrintContextClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	/* Set up the print context class */
	pc_class->beginpage = gnome_print_preview_beginpage;
	pc_class->clip      = gnome_print_preview_clip;
	pc_class->fill      = gnome_print_preview_fill;
	pc_class->stroke    = gnome_print_preview_stroke;
	pc_class->image     = gnome_print_preview_image;
	pc_class->glyphlist = gnome_print_preview_glyphlist;
	pc_class->gsave     = gnome_print_preview_gsave;
	pc_class->grestore  = gnome_print_preview_grestore;

	/* Set upt the object class */
	object_class->finalize = gnome_print_preview_finalize;
	object_class->get_property = gnome_print_preview_get_property;
	object_class->set_property = gnome_print_preview_set_property;
	g_object_class_install_property (G_OBJECT_CLASS (object_class),
		PROP_GROUP, g_param_spec_object ("group",
			"Group", "Group", GNOME_TYPE_CANVAS_GROUP,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (G_OBJECT_CLASS (object_class),
		PROP_THEME_COMPLIANCE, g_param_spec_boolean ("theme_compliance",
			"Theme compliance", "Theme compliance", TRUE,
			G_PARAM_READWRITE));
	g_object_class_install_property (G_OBJECT_CLASS (object_class),
		PROP_USE_THEME, g_param_spec_boolean ("use_theme",
			"Use theme", "Use theme", FALSE,
			G_PARAM_READWRITE));
	g_object_class_install_property (G_OBJECT_CLASS (object_class),
		PROP_ONLY_FIRST, g_param_spec_boolean ("only_first",
			"Show only first page", "Show only first page", FALSE,
			G_PARAM_READWRITE));
}

GnomePrintContext *
gnome_print_preview_new_full (GnomePrintConfig *config, GnomeCanvas *canvas,
			      const gdouble *transform, const ArtDRect *region)
{
	GnomeCanvasItem *group;

	g_return_val_if_fail (config != NULL, NULL);
	g_return_val_if_fail (canvas != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_CANVAS (canvas), NULL);
	g_return_val_if_fail (transform != NULL, NULL);
	g_return_val_if_fail (region != NULL, NULL);

	gnome_canvas_set_scroll_region (canvas, region->x0, region->y0,
									region->x1, region->y1);
	group = gnome_canvas_item_new (gnome_canvas_root (canvas),
									GNOME_TYPE_CANVAS_GROUP, NULL);
	gnome_canvas_item_affine_absolute (group, transform);

	return g_object_new (GNOME_TYPE_PRINT_PREVIEW, "group", group, NULL);
}

/**
 * gnome_print_preview_new:
 * @config:
 * @canvas: Canvas on which we display the print preview
 *
 * Creates a new PrintPreview object that use the @canvas GnomeCanvas 
 * as its rendering backend.
 *
 * Returns: A GnomePrintContext suitable for using with the GNOME print API.
 */
GnomePrintContext *
gnome_print_preview_new (GnomePrintConfig *config, GnomeCanvas *canvas)
{
	ArtDRect bbox;
	gdouble page2root[6];
	const GnomePrintUnit *unit;
	
	g_return_val_if_fail (config != NULL, NULL);
	g_return_val_if_fail (canvas != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_CANVAS (canvas), NULL);

	if (getenv ("GNOME_PRINT_DEBUG_WIDE")) {
		bbox.x0 = bbox.y0 = -900.0;
		bbox.x1 = bbox.y1 = 900.0;
	} else {
		bbox.x0 = 0.0;
		bbox.y0 = 0.0;
		bbox.x1 = 21.0 * (72.0 / 2.54);
		bbox.y1 = 29.7 * (72.0 / 2.54);
		if (gnome_print_config_get_length (config, (const guchar *) GNOME_PRINT_KEY_PAPER_WIDTH, &bbox.x1, &unit)) {
			gnome_print_convert_distance (&bbox.x1, unit, GNOME_PRINT_PS_UNIT);
		}
		if (gnome_print_config_get_length (config, (const guchar *) GNOME_PRINT_KEY_PAPER_HEIGHT, &bbox.y1, &unit)) {
			gnome_print_convert_distance (&bbox.y1, unit, GNOME_PRINT_PS_UNIT);
		}
	}

	art_affine_scale (page2root, 1.0, -1.0);
	page2root[5] = bbox.y1;

	return gnome_print_preview_new_full (config, canvas, page2root, &bbox);
}

/**
 * gnome_print_preview_get_type:
 *
 * GType identification routine for #GnomePrintPreview
 *
 * Returns: The GType for the #GnomePrintPreview object
 */

GType
gnome_print_preview_get_type (void)
{
	static GType preview_type = 0;
	
	if (!preview_type) {
		static const GTypeInfo preview_info = {
			sizeof (GnomePrintPreviewClass),
			NULL, NULL,
			(GClassInitFunc) class_init,
			NULL, NULL,
			sizeof (GnomePrintPreview),
			0,
			(GInstanceInitFunc) NULL
		};
		preview_type = g_type_register_static (GNOME_TYPE_PRINT_CONTEXT, "GnomePrintPreview", &preview_info, 0);
	}

	return preview_type;
}
