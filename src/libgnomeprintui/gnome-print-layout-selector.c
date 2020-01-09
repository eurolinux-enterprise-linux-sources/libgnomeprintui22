/*
 *  gnome-print-layout-selector.c:
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
 *    Lutz Mueller <lutz@users.sourceforge.net>
 *
 *  Copyright (C) 2005 Lutz Mueller
 *
 */
#include <config.h>

#include <libgnomeprintui/gnome-print-layout-selector.h>
#include <libgnomeprintui/gnome-print-preview.h>
#include <libgnomeprintui/gnome-print-i18n.h>

#include <libgnomeprint/gnome-print-filter.h>
#include <libgnomeprint/gnome-print-meta.h>
#include <libgnomeprint/private/gnome-print-private.h>

#include <libgnomecanvas/gnome-canvas.h>

#include <gtk/gtk.h>

#include <string.h>
#include <math.h>

#include <libart_lgpl/art_affine.h>

#define DEFAULT_WIDTH  (210. * 72. / 25.4)
#define DEFAULT_HEIGHT (297. * 72. / 25.4)

struct _GnomePrintLayoutSelector {
	GtkVBox parent;

	GtkWidget *r_plain, *r_leaflet_stapled, *r_leaflet_folded;
	struct {
		GtkRadioButton *r;
		GtkAdjustment *a;
		guint nx, ny;
		gboolean rot;
	} o_n_to_1, o_1_to_n;

	GtkWidget *canvas;
	GnomeCanvasItem *group, *page;
	GnomePrintContext *preview;
	gboolean needs_update_preview;
	gboolean needs_update_spin_buttons;

	/* Properties */
	guint total;
	gdouble iw, ih, ow, oh; /* Input/output width/height */
	GnomePrintFilter *filter;
	GnomePrintContext *meta, *meta_default;

	guint signal;
	gboolean loading;
};

struct _GnomePrintLayoutSelectorClass {
	GtkVBoxClass parent_class;
};

static GtkVBoxClass *parent_class = NULL;

enum {
	PROP_0,
	PROP_FILTER,
	PROP_INPUT_WIDTH,
	PROP_INPUT_HEIGHT,
	PROP_OUTPUT_WIDTH,
	PROP_OUTPUT_HEIGHT,
	PROP_META,
	PROP_TOTAL
};

static gboolean
_g_value_array_equal (GValueArray *va1, GValueArray *va2)
{
	guint i;
	gdouble d1 = 0., d2 = 0.;

	g_return_val_if_fail (va1 != NULL, FALSE);
	g_return_val_if_fail (va2 != NULL, FALSE);

	if (va1->n_values != va2->n_values)
		return FALSE;
	for (i = 0; ((guint) (d1 * 1000.) == (guint) (d2 * 1000.)) &&
			(i < va1->n_values); i++) {
		d1 = g_value_get_double (g_value_array_get_nth (va1, i));
		d2 = g_value_get_double (g_value_array_get_nth (va2, i));
	}
	return ((guint) (d1 * 1000.) == ((guint) (d2 * 1000.)));
}

static void
gnome_print_layout_selector_get_dim (GnomePrintLayoutSelector *cs,
		guint nx, guint ny, gboolean rotate, gdouble *w, gdouble *h)
{
	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));
	g_return_if_fail (nx);
	g_return_if_fail (ny);
	g_return_if_fail (w);
	g_return_if_fail (h);
	g_return_if_fail (cs->iw && cs->ih);
	g_return_if_fail (cs->ow && cs->oh);

	*w = rotate ? cs->oh / ny : cs->ow / nx;
	*h = rotate ? cs->ow / nx : cs->oh / ny;
	if (*w / *h > cs->iw / cs->ih)
		*w = *h * cs->iw / cs->ih;
	else
		*h = *w * cs->ih / cs->iw;
}

static void
_g_value_array_append_affines (GValueArray *va, gdouble *a)
{
	GValue v = {0,};
	guint k;

	g_return_if_fail (va);
	g_return_if_fail (a);

	g_value_init (&v, G_TYPE_DOUBLE);
	for (k = 0; k < 6; k++) {
		g_value_set_double (&v, a[k]);
		g_value_array_append (va, &v);
	}
	g_value_unset (&v);
}

static void
gnome_print_layout_selector_save_plain (GnomePrintLayoutSelector *cs)
{
	GnomePrintFilter *f;
	gdouble a[6];
	GValueArray *ga;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	f = gnome_print_filter_get_filter (cs->filter, 0);
	art_affine_identity (a);
	ga = g_value_array_new (0);
	_g_value_array_append_affines (ga, a);
	g_object_set (G_OBJECT (f), "affines", a, NULL);
	g_value_array_free (ga);
	
	while (gnome_print_filter_count_filters (f))
		gnome_print_filter_remove_filter (f,
				gnome_print_filter_get_filter (f, 0));
}

static GValueArray *
gnome_print_layout_selector_get_array (GnomePrintLayoutSelector *cs,
		guint nx, guint ny, gboolean rot)
{
	gdouble w, h, a1[6], a2[6], a[6];
	GValueArray *va;
	guint i, j;

	g_return_val_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs), NULL);
	g_return_val_if_fail (nx * ny > 0, NULL);

	gnome_print_layout_selector_get_dim (cs, nx, ny, rot, &w, &h);
	art_affine_scale (a1, w / cs->iw, w / cs->iw);
	va = g_value_array_new (0);
	if (rot) {
		art_affine_rotate (a2, -90.);
		art_affine_multiply (a1, a1, a2);
		for (i = nx; i > 0; i--)
			for (j = ny; j > 0; j--) {
				art_affine_translate (a2, (i - 1) * h, j * w);
				art_affine_multiply (a, a1, a2);
				_g_value_array_append_affines (va, a);
			}
	} else {
		for (j = ny; j > 0; j--)
			for (i = 1; i <= nx; i++) {
				art_affine_translate (a2, (i - 1) * w, (j - 1) * h);
				art_affine_multiply (a, a1, a2);
				_g_value_array_append_affines (va, a);
			}
	}

	return va;
}

static void
gnome_print_layout_selector_save_n_to_1 (GnomePrintLayoutSelector *cs,
		guint nx, guint ny, gboolean rot)
{
	GnomePrintFilter *f;
	GValueArray *va, *vae;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));
	g_return_if_fail (nx * ny > 1);

	gnome_print_layout_selector_save_plain (cs);
	f = gnome_print_filter_get_filter (cs->filter, 0);
	va = gnome_print_layout_selector_get_array (cs, nx, ny, rot);
	g_object_get (G_OBJECT (f), "affines", &vae, NULL);
	if (!vae || !_g_value_array_equal (va, vae))
		g_object_set (G_OBJECT (f), "affines", va, NULL);
	if (vae)
		g_value_array_free (vae);
	g_value_array_free (va);
}

static void
gnome_print_layout_selector_save_1_to_n (GnomePrintLayoutSelector *cs,
		guint tnx, guint tny, gboolean trot)
{
	GValueArray *va, *vae;
	GnomePrintFilter *f, *filter;
	gdouble a[6], w, h, a1[6], a2[6];
	guint j, i;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));
	g_return_if_fail (tnx * tny > 1);

	gnome_print_layout_selector_save_plain (cs);
	f = gnome_print_filter_get_filter (cs->filter, 0);
	while (gnome_print_filter_count_filters (f) < tnx * tny) {
		filter = gnome_print_filter_new_from_module_name ("clip", NULL);
		gnome_print_filter_add_filter (f, filter);
		g_object_unref (G_OBJECT (filter));
	}
	gnome_print_layout_selector_get_dim (cs, tnx, tny, trot, &w, &h);
	art_affine_scale (a1, cs->iw / w, cs->iw / w);
	if (trot) {
		art_affine_rotate (a2, -90.);
		art_affine_multiply (a1, a1, a2);
	}
	for (j = tny; j > 0; j--)
		for (i = 0; i < tnx; i++) {
			filter = gnome_print_filter_get_filter (f, (tny - j) * tnx + i);
			g_object_get (G_OBJECT (filter), "transform", &vae, NULL);
			art_affine_translate (a2,
					- (gdouble) (trot ? (tny - j) * cs->ow :          i * cs->ow),
					  (gdouble) (trot ? (i   + 1) * cs->oh : - ((j - 1) * cs->oh)));
			art_affine_multiply (a, a1, a2);
			va = g_value_array_new (0);
			_g_value_array_append_affines (va, a);
			if (!_g_value_array_equal (va, vae))
				g_object_set (G_OBJECT (filter), "transform", va, NULL);
			g_value_array_free (vae);
			g_value_array_free (va);
			g_object_set (G_OBJECT (filter),
					"left" , (gdouble) (trot ? (tnx - i - 2) * h : i * w),
					"right", (gdouble) (trot ? (tnx - i - 1) * h : (i + 1) * w),
					"bottom", (gdouble) (trot ? (tny - j) * w : (j - 1) * h),
					"top", (gdouble) (trot ? (tny - j + 1) * w : j * h), NULL);
		}
}

static void
gnome_print_layout_selector_save_leaflet_stapled (GnomePrintLayoutSelector *cs)
{
	GnomePrintFilter *f, *filter, *m;
	GValueArray *va;
	GValue v = {0,};
	guint i, n;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));
	g_return_if_fail (cs->total);

	gnome_print_layout_selector_save_plain (cs);
	f = gnome_print_filter_get_filter (cs->filter, 0);

	filter = gnome_print_filter_new_from_module_name ("reorder", NULL);
	gnome_print_filter_add_filter (f, filter);
	va = g_value_array_new (0);
	g_value_init (&v, G_TYPE_UINT);
	n = (guint) ceil ((gdouble) cs->total / 4.);
	for (i = 0; i < n; i++) {
		g_value_set_uint (&v, 4 * n - 2 * i - 1);
		g_value_array_append (va, &v);
		g_value_set_uint (&v, 2 * i);
		g_value_array_append (va, &v);
		g_value_set_uint (&v, 2 * i + 1);
		g_value_array_append (va, &v);
		g_value_set_uint (&v, 4 * n - 2 * i - 2);
		g_value_array_append (va, &v);
	}
	g_value_unset (&v);
	g_object_set (filter, "order", va, NULL);
	g_value_array_free (va);
	g_object_unref (filter);

	m = gnome_print_filter_new_from_module_name ("multipage", NULL);
	gnome_print_filter_append_predecessor (m, filter);
	va = gnome_print_layout_selector_get_array (cs, 1, 2, TRUE);
	g_object_set (G_OBJECT (m), "affines", va, NULL);
	g_value_array_free (va);
}

static GValueArray *
gnome_print_layout_selector_get_array_leaflet_folded (
		GnomePrintLayoutSelector *cs)
{
	gdouble a1[6], a2[6], a3[6], a[6];
	GValueArray *va;

	g_return_val_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs), NULL);

	art_affine_scale (a1, 0.5, 0.5);
	art_affine_rotate (a2, 180.);
	va = g_value_array_new (0);
	art_affine_translate (a3, cs->iw * 0.5, 0.);
	art_affine_multiply (a, a1, a3);
	_g_value_array_append_affines (va, a);
	art_affine_multiply (a, a1, a2);
	art_affine_translate (a3, cs->iw, cs->ih);
	art_affine_multiply (a, a, a3);
	_g_value_array_append_affines (va, a);
	art_affine_multiply (a, a1, a2);
	art_affine_translate (a3, cs->iw * 0.5, cs->ih);
	art_affine_multiply (a, a, a3);
	_g_value_array_append_affines (va, a);
	_g_value_array_append_affines (va, a1);

	return va;
}
static void
gnome_print_layout_selector_save_leaflet_folded (GnomePrintLayoutSelector *cs)
{
	GnomePrintFilter *f;
	GValueArray *va;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	gnome_print_layout_selector_save_plain (cs);
	f = gnome_print_filter_get_filter (cs->filter, 0);
	va = gnome_print_layout_selector_get_array_leaflet_folded (cs);
	g_object_set (G_OBJECT (f), "affines", va, NULL);
	g_value_array_free (va);
}

static void
gnome_print_layout_selector_update_preview (GnomePrintLayoutSelector *cs)
{
	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	gnome_print_preview_reset (GNOME_PRINT_PREVIEW (cs->preview));
	if (!cs->meta && !cs->meta_default)
		return;
	gnome_print_filter_reset (cs->filter);
	gnome_print_meta_render (
		GNOME_PRINT_META (cs->meta ? cs->meta : cs->meta_default), cs->preview);
	gnome_print_filter_flush (cs->filter);
}

static gboolean
update_preview (gpointer data)
{
	GnomePrintLayoutSelector *cs = GNOME_PRINT_LAYOUT_SELECTOR (data);

	gnome_print_layout_selector_update_preview (cs);
	cs->needs_update_preview = FALSE;
	return FALSE;
}

static void
gnome_print_layout_selector_schedule_update_preview (GnomePrintLayoutSelector *cs)
{
	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	if (!cs->needs_update_preview) {
		cs->needs_update_preview = TRUE;
		g_idle_add (update_preview, cs);
	}
}

static void
gnome_print_layout_selector_save (GnomePrintLayoutSelector *cs)
{
	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	if (cs->loading || !cs->filter)
		return;

	g_signal_handler_block (cs->filter, cs->signal);
	if (GTK_TOGGLE_BUTTON (cs->r_plain)->active)
		gnome_print_layout_selector_save_plain (cs);
	else if (GTK_TOGGLE_BUTTON (cs->o_1_to_n.r)->active)
		gnome_print_layout_selector_save_1_to_n (cs,
				cs->o_1_to_n.nx, cs->o_1_to_n.ny, cs->o_1_to_n.rot);
	else if (GTK_TOGGLE_BUTTON (cs->o_n_to_1.r)->active)
		gnome_print_layout_selector_save_n_to_1 (cs,
				cs->o_n_to_1.nx, cs->o_n_to_1.ny, cs->o_n_to_1.rot);
	else if (GTK_TOGGLE_BUTTON (cs->r_leaflet_stapled)->active)
		gnome_print_layout_selector_save_leaflet_stapled (cs);
	else if (GTK_TOGGLE_BUTTON (cs->r_leaflet_folded)->active)
		gnome_print_layout_selector_save_leaflet_folded (cs);
	g_signal_handler_unblock (cs->filter, cs->signal);

	gnome_print_layout_selector_schedule_update_preview (cs);
}

static gboolean
gnome_print_layout_selector_load_filter (GnomePrintLayoutSelector *cs,
		GnomePrintFilter *f)
{
	GValueArray *vae = NULL;
	guint n;

	g_return_val_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs), FALSE);
	g_return_val_if_fail (GNOME_IS_PRINT_FILTER (f), FALSE);

	if (strcmp ("GnomePrintFilterClip", G_OBJECT_TYPE_NAME (G_OBJECT (f))))
		return FALSE;
	if (gnome_print_filter_count_filters (f) != 1)
		return FALSE;
	f = gnome_print_filter_get_filter (f, 0);
	if (strcmp ("GnomePrintFilterMultipage", G_OBJECT_TYPE_NAME (G_OBJECT (f))))
		return FALSE;
	g_object_get (G_OBJECT (f), "affines", &vae, NULL);
	if (vae && vae->n_values % 6 > 0) {
		g_value_array_free (vae);
		return FALSE;
	}
	n = vae ? vae->n_values / 6 : 1;
	if (vae) {
		GValueArray *va;
		
		va = gnome_print_layout_selector_get_array_leaflet_folded (cs);
		if (_g_value_array_equal (va, vae)) {
			g_value_array_free (va);
			g_value_array_free (vae);
			cs->loading = TRUE;
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->r_leaflet_folded), TRUE);
			cs->loading = FALSE;
			goto loading_succeeded;
		}
		g_value_array_free (va);
		g_value_array_free (vae);
	}
	if (n > 1) {
		cs->loading = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->o_n_to_1.r), TRUE);
		gtk_adjustment_set_value (cs->o_n_to_1.a, (gdouble) n);
		cs->loading = FALSE;
		goto loading_succeeded;
	}

	n = gnome_print_filter_count_filters (f);
	switch (n) {
	case 0:
		cs->loading = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->r_plain), TRUE);
		cs->loading = FALSE;
		goto loading_succeeded;
	case 1:
		cs->loading = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->r_leaflet_stapled), TRUE);
		cs->loading = FALSE;
		goto loading_succeeded;
	default:
		cs->loading = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->o_1_to_n.r), TRUE);
		gtk_adjustment_set_value (cs->o_1_to_n.a, (gdouble) n);
		cs->loading = FALSE;
		goto loading_succeeded;
	}

	return FALSE;

loading_succeeded:
	gnome_print_layout_selector_schedule_update_preview (cs);
	return TRUE;
}

static void
gnome_print_layout_selector_get_property (GObject *object, guint n, GValue *v,
		GParamSpec *cspec)
{
	GnomePrintLayoutSelector *cs = GNOME_PRINT_LAYOUT_SELECTOR (object);

	switch (n) {
	case PROP_TOTAL        : g_value_set_uint   (v, cs->total ); break;
	case PROP_FILTER       : g_value_set_object (v, cs->filter); break;
	case PROP_META         : g_value_set_object (v, cs->meta  ); break;
	case PROP_INPUT_WIDTH  : g_value_set_double (v, cs->iw    ); break;
	case PROP_INPUT_HEIGHT : g_value_set_double (v, cs->ih    ); break;
	case PROP_OUTPUT_WIDTH : g_value_set_double (v, cs->ow    ); break;
	case PROP_OUTPUT_HEIGHT: g_value_set_double (v, cs->ow    ); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, cspec);
	}
}

static guint
gnome_print_layout_selector_get_layout (GnomePrintLayoutSelector *cs,
		guint n, guint *nx, guint *ny, gboolean *rot)
{
	gdouble ba = 0.;
	guint i, x, y, nxi, nyi;
	gboolean roti;

	g_return_val_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs), 0);
	g_return_val_if_fail (n > 0, 0);

	if (!nx) nx = &nxi;
	if (!ny) ny = &nyi;
	if (!rot) rot = &roti;

	*nx = *ny = 1;
	*rot = FALSE;
	for (i = 0; i <= 1; i++)
		for (x = 1; x <= n; x++) {
			gdouble w, h, a;
			guint ax, ay;

			y = ceil ((gdouble) n / (gdouble) x);
			gnome_print_layout_selector_get_dim (cs, x, y, i > 0, &w, &h);
			a = w * h;
			for (ax = 0; ; ax++) {
				gnome_print_layout_selector_get_dim (cs, x+ax+1, y, (i>0), &w, &h);
				if (w * h < a - 1e-6)
					break;
				a = w * h;
			}
			for (ay = 0; ; ay++) {
				gnome_print_layout_selector_get_dim (cs, x+ax, y+ay+1, (i>0), &w, &h);
				if (w * h < a - 1e-6)
					break;
				a = w * h;
			}
			if (!ba ||
					(((x+ax) * (y+ay) < *nx * *ny) && ((x+ax) * (y+ay) >= n) && (a >= ba + 1e-6)) ||
					((fabs (a - ba) < 1e-6) && ((x+ax) * (y+ay) <= *nx * *ny))) {
				*nx = x + ax;
				*ny = y + ay;
				*rot = (i > 0);
				ba = a;
			}
		}
	return *nx * *ny;
}

static void
_gnome_print_context_gnome (GnomePrintContext *c, gdouble w, gdouble h)
{
	gdouble z;

	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (c));
	g_return_if_fail (w > 0);
	g_return_if_fail (h > 0);

	gnome_print_gsave (c);
	z = ((w > h) ? h : w) * 0.9 / 120.;
	gnome_print_translate (c, (w + 120. * z) / 2., (h + 120. * z) / 2.);
	gnome_print_rotate (c, 180.);
	gnome_print_scale (c, z, z);
	gnome_print_moveto (c, 86.068, 0.);
	gnome_print_curveto (c, 61.466, 0., 56.851, 35.041, 70.691, 35.041);
	gnome_print_curveto (c, 84.529, 35.041, 110.671, 0, 86.068, 0.);
	gnome_print_closepath (c);
	gnome_print_moveto (c, 45.217, 30.699);
	gnome_print_curveto (c, 52.586, 31.149, 60.671, 2.577, 46.821, 4.374);
	gnome_print_curveto (c, 32.976, 6.171, 37.845, 30.249, 45.217, 30.699);
	gnome_print_closepath (c);
	gnome_print_moveto (c, 11.445, 48.453);
	gnome_print_curveto (c, 16.686, 46.146, 12.12, 23.581, 3.208, 29.735);
	gnome_print_curveto (c, -5.7, 35.89, 6.204, 50.759, 11.445, 48.453);
	gnome_print_closepath (c);
	gnome_print_moveto (c, 26.212, 36.642);
	gnome_print_curveto (c, 32.451, 35.37, 32.793, 9.778, 21.667, 14.369);
	gnome_print_curveto (c, 10.539, 18.961, 19.978, 37.916, 26.212, 36.642);
	gnome_print_closepath (c);
	gnome_print_moveto (c, 58.791, 93.913);
	gnome_print_curveto (c, 59.898, 102.367, 52.589, 106.542, 45.431, 101.092);
	gnome_print_curveto (c, 22.644, 83.743, 83.16, 75.089, 79.171, 51.386);
	gnome_print_curveto (c, 75.86, 31.712, 15.495, 37.769, 8.621, 68.552);
	gnome_print_curveto (c, 3.968, 89.374, 27.774, 118.26, 52.614, 118.26);
	gnome_print_curveto (c, 64.834, 118.26, 78.929, 107.226, 81.566, 93.248);
	gnome_print_curveto (c, 83.58, 82.589, 57.867, 86.86, 58.791, 93.913);
	gnome_print_fill (c);
	gnome_print_grestore (c);
}

static void
gnome_print_layout_selector_update_spin_buttons (GnomePrintLayoutSelector *cs)
{
	guint n;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	if (!cs->ow || !cs->oh || !cs->iw || !cs->ih)
		return;

	n = gnome_print_layout_selector_get_layout (cs,
			(guint) cs->o_1_to_n.a->value, &cs->o_1_to_n.nx, &cs->o_1_to_n.ny,
			&cs->o_1_to_n.rot);
	if (n != (guint) cs->o_1_to_n.a->value)
		gtk_adjustment_set_value (cs->o_1_to_n.a, (gdouble) n);
	n = gnome_print_layout_selector_get_layout (cs,
			(guint) cs->o_n_to_1.a->value, &cs->o_n_to_1.nx, &cs->o_n_to_1.ny,
			&cs->o_n_to_1.rot);
	if (n != (guint) cs->o_n_to_1.a->value)
		gtk_adjustment_set_value (cs->o_n_to_1.a, (gdouble) n);
}

static gboolean
update_spin_buttons (gpointer data)
{
	GnomePrintLayoutSelector *cs = GNOME_PRINT_LAYOUT_SELECTOR (data);

	gnome_print_layout_selector_update_spin_buttons (cs);
	cs->needs_update_spin_buttons = FALSE;
	return FALSE;
}

static void
gnome_print_layout_selector_schedule_update_spin_buttons (GnomePrintLayoutSelector *cs)
{
	if (!cs->needs_update_spin_buttons)
		g_idle_add (update_spin_buttons, cs);
}

#define DEFAULT_PAGES 10.

static void
gnome_print_layout_selector_input_changed (GnomePrintLayoutSelector *cs)
{
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	if (!cs->iw || !cs->ih) {
		g_object_set (G_OBJECT (cs->filter), "left", -G_MAXDOUBLE,
			"right", G_MAXDOUBLE, "bottom", -G_MAXDOUBLE, "top", G_MAXDOUBLE, NULL);
		return;
	}
	g_object_set (G_OBJECT (cs->filter), "left", 0., "right", cs->iw,
			"bottom", 0., "top", cs->ih, NULL);
	gnome_print_layout_selector_schedule_update_spin_buttons (cs);
	if (cs->meta)
		return;
	if (cs->meta_default)
		g_object_unref (G_OBJECT (cs->meta_default));
	cs->meta_default = g_object_new (GNOME_TYPE_PRINT_META, NULL);
	for (i = 0; i < (cs->total ? cs->total : DEFAULT_PAGES); i++) {
		gchar *txt;

		gnome_print_beginpage (cs->meta_default, (const guchar *) "test");
		if (!cs->total)
			gnome_print_setrgbcolor (cs->meta_default,
					((gdouble) i) / DEFAULT_PAGES,
          ((gdouble) i) / DEFAULT_PAGES,
					((gdouble) i) / DEFAULT_PAGES);
		gnome_print_rect_stroked (cs->meta_default, 0., 0., cs->iw, cs->ih);
		_gnome_print_context_gnome (cs->meta_default, cs->iw, cs->ih);
		gnome_print_moveto (cs->meta_default, 10., 10.);
		gnome_print_scale (cs->meta_default, 20., 20.);
		txt = g_strdup_printf ("%i", i + 1);
		gnome_print_show (cs->meta_default, (const guchar *) txt);
		g_free (txt);
		gnome_print_showpage (cs->meta_default);
	}
	gnome_print_layout_selector_schedule_update_preview (cs);
}

static void
gnome_print_layout_selector_set_total (GnomePrintLayoutSelector *cs,
		guint total)
{
	guint n;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	if (cs->total == total)
		return;

	cs->total = total;
	n = gnome_print_layout_selector_get_layout (cs, total, NULL, NULL, NULL);
	if ((guint) cs->o_n_to_1.a->upper != n) {
		cs->o_n_to_1.a->upper = n;
		gtk_adjustment_changed (cs->o_n_to_1.a);
	}

	if (cs->total)
		gtk_widget_show (cs->r_leaflet_stapled);
	else
		gtk_widget_hide (cs->r_leaflet_stapled);
	gnome_print_layout_selector_input_changed (cs);
}

static void
on_filter_notify (GObject *object, GParamSpec *pspec,
		GnomePrintLayoutSelector *cs)
{
	if (!strcmp (pspec->name, "context"))
		return;
	gnome_print_layout_selector_load_filter (cs, GNOME_PRINT_FILTER (object));
}

static void
gnome_print_layout_selector_output_changed (GnomePrintLayoutSelector *cs)
{
	gdouble a[6];
	gdouble zoom;

	g_return_if_fail (GNOME_IS_PRINT_LAYOUT_SELECTOR (cs));

	if (!cs->ow || !cs->oh)
		return;

	zoom = MIN (200. / cs->ow, 200. / cs->oh);
	gnome_canvas_set_scroll_region (GNOME_CANVAS (cs->canvas),
			0, 0, cs->ow, cs->oh);
	gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (cs->canvas), zoom);
	a[0] = 1.; a[1] = 0.; a[2] = 0.; a[3] = -1.; a[4] = 0.; a[5] = cs->oh;
	gnome_canvas_item_affine_absolute (cs->group, a);
	g_object_set (G_OBJECT (cs->page), "x2", cs->ow, "y2", cs->oh, NULL);

	gnome_print_layout_selector_schedule_update_spin_buttons (cs);
	gnome_print_layout_selector_schedule_update_preview (cs);
}

static void
gnome_print_layout_selector_set_property (GObject *object, guint n,
		const GValue *v, GParamSpec *cspec)
{
	GnomePrintLayoutSelector *cs = GNOME_PRINT_LAYOUT_SELECTOR (object);

	switch (n) {
	case PROP_TOTAL:
		gnome_print_layout_selector_set_total (cs, g_value_get_uint (v));
		break;
	case PROP_FILTER:
		if (gnome_print_layout_selector_load_filter (cs, g_value_get_object (v))) {
			if (cs->filter) {
				g_signal_handler_disconnect (G_OBJECT (cs->filter), cs->signal);
				g_object_unref (G_OBJECT (cs->filter));
			}
			cs->filter = g_value_get_object (v);
			g_object_ref (G_OBJECT (cs->filter));
			cs->signal = g_signal_connect (G_OBJECT (cs->filter), "notify",
					G_CALLBACK (on_filter_notify), cs);
			g_object_set (G_OBJECT (cs->preview), "filter", cs->filter, NULL);
		}
		break;
	case PROP_META:
		if (cs->meta != g_value_get_object (v)) {
			if (cs->meta)
				g_object_unref (G_OBJECT (cs->meta));
			cs->meta = g_value_get_object (v);
			if (cs->meta)
				g_object_ref (G_OBJECT (cs->meta));
			gnome_print_layout_selector_schedule_update_preview (cs);
		}
		break;
	case PROP_INPUT_WIDTH:
		if (cs->iw != g_value_get_double (v)) {
			cs->iw = g_value_get_double (v);
			gnome_print_layout_selector_input_changed (cs);
		}
		break;
	case PROP_INPUT_HEIGHT:
		if (cs->ih != g_value_get_double (v)) {
			cs->ih = g_value_get_double (v);
			gnome_print_layout_selector_input_changed (cs);
		}
		break;
	case PROP_OUTPUT_WIDTH:
		if (cs->ow != g_value_get_double (v)) {
			cs->ow = g_value_get_double (v);
			gnome_print_layout_selector_output_changed (cs);
		}
		break;
	case PROP_OUTPUT_HEIGHT:
		if (cs->oh != g_value_get_double (v)) {
			cs->oh = g_value_get_double (v);
			gnome_print_layout_selector_output_changed (cs);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, cspec);
	}
}

static void
gnome_print_layout_selector_finalize (GObject *object)
{
	GnomePrintLayoutSelector *cs = GNOME_PRINT_LAYOUT_SELECTOR (object);

	if (cs->filter) {
		g_signal_handler_disconnect (G_OBJECT (cs->filter), cs->signal);
		g_object_unref (G_OBJECT (cs->filter));
		cs->filter = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

struct {
	GParamSpec parent_instance;
} GnomePrintLayoutSelectorParamFilter;

static void
param_filter_set_default (GParamSpec *pspec, GValue *v)
{
	GnomePrintFilter *f;

	f = gnome_print_filter_new_from_description (
			"GnomePrintFilterClip [ GnomePrintFilterMultipage ]", NULL);
	g_value_set_object (v, f);
	g_object_unref (G_OBJECT (f));
}

static GType
gnome_print_layout_selector_param_filter_get_type (void)
{
	static GType type;
	if (G_UNLIKELY (type) == 0) {
		static const GParamSpecTypeInfo pspec_info = {
			sizeof (GnomePrintLayoutSelectorParamFilter), 0, NULL,
			G_TYPE_OBJECT, NULL, param_filter_set_default, NULL, NULL
		};
		type = g_param_type_register_static ("GnomePrintLayoutSelectorParamFilter", &pspec_info);
	}
	return type;
}

static void
gnome_print_layout_selector_class_init (GnomePrintLayoutSelectorClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	GParamSpec *pspec;

	parent_class = g_type_class_peek_parent (klass);

	object_class->get_property = gnome_print_layout_selector_get_property;
	object_class->set_property = gnome_print_layout_selector_set_property;
	object_class->finalize     = gnome_print_layout_selector_finalize;

	g_object_class_install_property (object_class, PROP_TOTAL,
			g_param_spec_uint ("total", _("Number of pages"), _("Number of pages"),
				0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_OUTPUT_WIDTH,
			g_param_spec_double ("output_width", _("Output width"),
				_("Output width"), 0., G_MAXDOUBLE, 0.,
				G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_OUTPUT_HEIGHT,
			g_param_spec_double ("output_height", _("Output height"),
				_("Output height"), 0., G_MAXDOUBLE, 0.,
				G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_INPUT_WIDTH,
			g_param_spec_double ("input_width", _("Input width"),
				_("Input width"), 0., G_MAXDOUBLE, 0.,
				G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_INPUT_HEIGHT,
			g_param_spec_double ("input_height", _("Input height"),
				_("Input height"), 0., G_MAXDOUBLE, 0.,
				G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_META,
			g_param_spec_object ("meta", "Metadata to be printed",
				"Metadata to be printed", GNOME_TYPE_PRINT_META, G_PARAM_READWRITE));

	pspec = g_param_spec_internal (
			gnome_print_layout_selector_param_filter_get_type (),
			"filter", _("Filter"), _("Filter"), G_PARAM_READWRITE);
	pspec->value_type = GNOME_TYPE_PRINT_FILTER;
	g_object_class_install_property (object_class, PROP_FILTER, pspec);
}

static void
on_1_to_n_focus_in_event (GtkWidget *widget, GdkEventFocus *event,
		GnomePrintLayoutSelector *cs)
{
	if (!GTK_TOGGLE_BUTTON (cs->o_1_to_n.r)->active)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->o_1_to_n.r), TRUE);
}

static void
on_n_to_1_focus_in_event (GtkWidget *widget, GdkEventFocus *event,
		GnomePrintLayoutSelector *cs)
{
	if (!GTK_TOGGLE_BUTTON (cs->o_n_to_1.r)->active)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->o_n_to_1.r), TRUE);
}

static void
on_n_to_1_value_changed (GtkAdjustment *a, GnomePrintLayoutSelector *cs)
{
	guint n, n_old = cs->o_n_to_1.nx * cs->o_n_to_1.ny;

	g_return_if_fail (a->value > 0.);

	if ((guint) a->value >= n_old)
		n = gnome_print_layout_selector_get_layout (cs, (guint) a->value,
				&cs->o_n_to_1.nx, &cs->o_n_to_1.ny, &cs->o_n_to_1.rot);
	else while (n_old ==
			(n = gnome_print_layout_selector_get_layout (cs, (guint) a->value,
				&cs->o_n_to_1.nx, &cs->o_n_to_1.ny, &cs->o_n_to_1.rot)))
		a->value -= 1.;
	if (n != n_old) {
		a->value = (gdouble) n;
		gtk_adjustment_value_changed (a);
	}
	gnome_print_layout_selector_save (cs);
}

static void
on_1_to_n_value_changed (GtkAdjustment *a, GnomePrintLayoutSelector *cs)
{
	guint n, n_old = cs->o_1_to_n.nx * cs->o_1_to_n.ny;

	g_return_if_fail (a->value > 0.);

	if ((guint) a->value >= n_old)
		n = gnome_print_layout_selector_get_layout (cs, (guint) a->value,
				&cs->o_1_to_n.nx, &cs->o_1_to_n.ny, &cs->o_1_to_n.rot);
	else while (n_old ==
			(n = gnome_print_layout_selector_get_layout (cs, (guint) a->value,
				&cs->o_1_to_n.nx, &cs->o_1_to_n.ny, &cs->o_1_to_n.rot)))
		a->value -= 1.;
	if (n != n_old) {
		a->value = (gdouble) n;
		gtk_adjustment_value_changed (a);
	}
	gnome_print_layout_selector_save (cs);
}

static void
on_toggled (GtkToggleButton *b, GnomePrintLayoutSelector *cs)
{
	if (b->active)
		gnome_print_layout_selector_save (cs);
}

static void
gnome_print_layout_selector_init (GnomePrintLayoutSelector *cs)
{
	GtkWidget *mhb, *hb, *w, *vb;
	GValue v = {0,};

	mhb = g_object_new (GTK_TYPE_HBOX, NULL);
	gtk_widget_show (mhb);
	gtk_box_pack_start (GTK_BOX (cs), mhb, TRUE, TRUE, 0);

	vb = g_object_new (GTK_TYPE_VBOX, NULL);
	gtk_widget_show (vb);
	gtk_box_pack_start (GTK_BOX (mhb), vb, FALSE, FALSE, 0);

	cs->r_plain = gtk_radio_button_new_with_mnemonic (NULL, _("_Plain"));
	gtk_widget_show (cs->r_plain);
	gtk_box_pack_start (GTK_BOX (vb), cs->r_plain, FALSE, FALSE, 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cs->r_plain), TRUE);
	g_signal_connect (cs->r_plain, "toggled", G_CALLBACK (on_toggled), cs);

	hb = g_object_new (GTK_TYPE_HBOX, NULL);
	gtk_widget_show (hb);
	gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	cs->o_n_to_1.r = GTK_RADIO_BUTTON (
			gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (cs->r_plain), _("_Handout: ")));
	gtk_widget_show (GTK_WIDGET (cs->o_n_to_1.r));
	gtk_box_pack_start (GTK_BOX (hb), GTK_WIDGET (cs->o_n_to_1.r), FALSE, FALSE, 0);
	g_signal_connect (cs->o_n_to_1.r, "toggled", G_CALLBACK (on_toggled), cs);
	cs->o_n_to_1.a = g_object_new (GTK_TYPE_ADJUSTMENT, "lower", 2.,
			"upper", G_MAXDOUBLE,
			"value", 2., "step-increment", 1., "page_increment", 10., NULL);
	w = g_object_new (GTK_TYPE_SPIN_BUTTON, "adjustment", cs->o_n_to_1.a,
			"digits", 0, "numeric", TRUE, "value", 2., "snap-to-ticks", TRUE, NULL);
	gtk_widget_show (w);
	gtk_box_pack_start (GTK_BOX (hb), w, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (cs->o_n_to_1.a), "value_changed", G_CALLBACK (on_n_to_1_value_changed), cs);
	g_signal_connect (G_OBJECT (w), "focus_in_event", G_CALLBACK (on_n_to_1_focus_in_event), cs);
	w = g_object_new (GTK_TYPE_LABEL, "label", _(" pages on 1 page"), NULL);
	gtk_widget_show (w);
	gtk_box_pack_start (GTK_BOX (hb), w, FALSE, FALSE, 0);

	hb = g_object_new (GTK_TYPE_HBOX, NULL);
	gtk_widget_show (hb);
	gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	cs->o_1_to_n.r = GTK_RADIO_BUTTON (gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (cs->r_plain), _("1 page _spread on ")));
	gtk_widget_show (GTK_WIDGET (cs->o_1_to_n.r));
	gtk_box_pack_start (GTK_BOX (hb), GTK_WIDGET (cs->o_1_to_n.r), FALSE, FALSE, 0);
	g_signal_connect (cs->o_1_to_n.r, "toggled", G_CALLBACK (on_toggled), cs);
	cs->o_1_to_n.a = g_object_new (GTK_TYPE_ADJUSTMENT, "lower", 2.,
			"upper", G_MAXDOUBLE,
			"value", 2., "step-increment", 1., "page_increment", 10., NULL);
	w = g_object_new (GTK_TYPE_SPIN_BUTTON, "adjustment", cs->o_1_to_n.a,
			"digits", 0, "numeric", TRUE, "value", 2., "snap-to-ticks", TRUE, NULL);
	gtk_widget_show (w);
	gtk_box_pack_start (GTK_BOX (hb), w, FALSE, FALSE, 0);
	g_signal_connect (cs->o_1_to_n.a, "value_changed", G_CALLBACK (on_1_to_n_value_changed), cs);
	g_signal_connect (w, "focus_in_event", G_CALLBACK (on_1_to_n_focus_in_event), cs);
	w = g_object_new (GTK_TYPE_LABEL, "label", _(" pages"), NULL);
	gtk_widget_show (w);
	gtk_box_pack_start (GTK_BOX (hb), w, FALSE, FALSE, 0);

	cs->r_leaflet_stapled = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (cs->r_plain), _("Leaflet, folded once and _stapled"));
	gtk_box_pack_start (GTK_BOX (vb), cs->r_leaflet_stapled, FALSE, FALSE, 0);
	g_signal_connect (cs->r_leaflet_stapled, "toggled", G_CALLBACK (on_toggled), cs);

	cs->r_leaflet_folded = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (cs->r_plain), _("Leaflet, _folded twice"));
	gtk_widget_show (cs->r_leaflet_folded);
	gtk_box_pack_start (GTK_BOX (vb), cs->r_leaflet_folded, FALSE, FALSE, 0);
	g_signal_connect (cs->r_leaflet_folded, "toggled", G_CALLBACK (on_toggled), cs);

	/* Preview */
	cs->canvas = g_object_new (GNOME_TYPE_CANVAS, "aa", TRUE, NULL);
	gtk_widget_set_size_request (cs->canvas, 200., 200.);
	gtk_widget_show (cs->canvas);
	gtk_box_pack_end (GTK_BOX (mhb), cs->canvas, FALSE, FALSE, 0);
	gnome_canvas_set_center_scroll_region (GNOME_CANVAS (cs->canvas), FALSE);
	cs->page = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (gnome_canvas_root (GNOME_CANVAS (cs->canvas))),
		GNOME_TYPE_CANVAS_RECT,
		"outline_color", "black", "fill_color", "white", "x1", 0., "y1", 0.,
		"x2", 0., "y2", 0., NULL);
	cs->group = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (gnome_canvas_root (GNOME_CANVAS (cs->canvas))),
		GNOME_TYPE_CANVAS_GROUP, NULL);
	cs->preview = g_object_new (GNOME_TYPE_PRINT_PREVIEW,
			"group", cs->group, "only_first", TRUE, NULL);

	/* Load default values */
	g_value_init (&v, G_TYPE_OBJECT);
	param_filter_set_default (NULL, &v);
	g_object_set_property (G_OBJECT (cs), "filter", &v);
	g_value_unset (&v);
	gnome_print_layout_selector_input_changed (cs);
	gnome_print_layout_selector_output_changed (cs);
}

GType
gnome_print_layout_selector_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePrintLayoutSelectorClass), NULL, NULL,
			(GClassInitFunc) gnome_print_layout_selector_class_init,
			NULL, NULL, sizeof (GnomePrintLayoutSelector), 0,
			(GInstanceInitFunc) gnome_print_layout_selector_init
		};
		type = g_type_register_static (GTK_TYPE_VBOX,
				"GnomePrintLayoutSelector", &info, 0);
	}
	return type;
}
