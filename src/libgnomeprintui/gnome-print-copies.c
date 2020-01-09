/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  gnome-print-copies.c: A system print copies widget
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
 *    Michael Zucchi <notzed@ximian.com>
 *    Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 *  Copyright (C) 2000-2002 Ximian Inc.
 *  Copyright (C) 2003 Andreas J. Guelzow
 *
 */

#include <config.h>

#include <atk/atk.h>
#include <gtk/gtk.h>

#include <string.h>

#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/gnome-print-filter.h>

#include <libgnomeprintui/gnome-print-i18n.h>
#include <libgnomeprintui/gnome-print-copies.h>
#include <libgnomeprintui/gnome-print-dialog.h>
#include <libgnomeprintui/gnome-printui-marshal.h>

enum {
	COPIES_SET, 
	COLLATE_SET, 
	DUPLEX_SET, 
	TUMBLE_SET, 
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_FILTER
};

struct _GnomePrintCopiesSelector {
	GtkVBox vbox;
  
	guint changing : 1;

	GtkWidget *copies;
	GtkWidget *collate, *reverse;
	GtkWidget *collate_image;

	gboolean loading, saving;

	GnomePrintFilter *filter;
	guint signal;
};

struct _GnomePrintCopiesSelectorClass {
	GtkVBoxClass parent_class;

	void (* copies_set) (GnomePrintCopiesSelector * gpc, gint copies);
	void (* collate_set) (GnomePrintCopiesSelector * gpc, gboolean collate);
};

static void gnome_print_copies_selector_class_init (GnomePrintCopiesSelectorClass *class);
static void gnome_print_copies_selector_init       (GnomePrintCopiesSelector *gspaper);

/* again, these images may be here temporarily */

/* XPM */
static const char *collate_xpm[] = {
"65 35 6 1",
" 	c None",
".	c #000000",
"+	c #020202",
"@	c #FFFFFF",
"#	c #010101",
"$	c #070707",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           ..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..",
"           ..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"..+++++++++##++++++$@@@@@@@@@..   ..+++++++++##++++++$@@@@@@@@@..",
"..+++++++++##+++++#+@@@@@@@@@..   ..+++++++++##+++++#+@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@..@@@@..   ..@@@@@@@@@@@@@@@@++@@@..@@@@..",
"..@@@@@@@@@@@@@@@@++@@.@@.@@@..   ..@@@@@@@@@@@@@@@@++@@.@@.@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@.@@@..   ..@@@@@@@@@@@@@@@@++@@@@@.@@@..",
"..@@@@@@@@@@@@@@@@++@@@@.@@@@..   ..@@@@@@@@@@@@@@@@++@@@@.@@@@..",
"..@@@@@@@@@@@@@@@@++@@@.@@@@@..   ..@@@@@@@@@@@@@@@@++@@@.@@@@@..",
"..@@@@@@@@@@@@@@@@++@@.@@@@@@..   ..@@@@@@@@@@@@@@@@++@@.@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@....@@@..   ..@@@@@@@@@@@@@@@@++@@....@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@.@@@@.............   ..@@@@@@@@@@@.@@@@.............",
"..@@@@@@@@@@..@@@@.............   ..@@@@@@@@@@..@@@@.............",
"..@@@@@@@@@@@.@@@@..              ..@@@@@@@@@@@.@@@@..           ",
"..@@@@@@@@@@@.@@@@..              ..@@@@@@@@@@@.@@@@..           ",
"..@@@@@@@@@@@.@@@@..              ..@@@@@@@@@@@.@@@@..           ",
"..@@@@@@@@@@@.@@@@..              ..@@@@@@@@@@@.@@@@..           ",
"..@@@@@@@@@@...@@@..              ..@@@@@@@@@@...@@@..           ",
"..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..           ",
"..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..           ",
"..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..           ",
"....................              ....................           ",
"....................              ....................           "};

/* XPM */
static const char *nocollate_xpm[] = {
"65 35 6 1",
" 	c None",
".	c #000000",
"+	c #FFFFFF",
"@	c #020202",
"#	c #010101",
"$	c #070707",
"           ....................              ....................",
"           ....................              ....................",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"..@@@@@@@@@##@@@@@@$+++++++++..   ..@@@@@@@@@##@@@@@@$+++++++++..",
"..@@@@@@@@@##@@@@@#@+++++++++..   ..@@@@@@@@@##@@@@@#@+++++++++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++++++++@@++++.++++..   ..++++++++++++++++@@+++..++++..",
"..++++++++++++++++@@+++..++++..   ..++++++++++++++++@@++.++.+++..",
"..++++++++++++++++@@++++.++++..   ..++++++++++++++++@@+++++.+++..",
"..++++++++++++++++@@++++.++++..   ..++++++++++++++++@@++++.++++..",
"..++++++++++++++++@@++++.++++..   ..++++++++++++++++@@+++.+++++..",
"..++++++++++++++++@@++++.++++..   ..++++++++++++++++@@++.++++++..",
"..++++++++++++++++@@+++...+++..   ..++++++++++++++++@@++....+++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..+++++++++++.++++.............   ..++++++++++..++++.............",
"..++++++++++..++++.............   ..+++++++++.++.+++.............",
"..+++++++++++.++++..              ..++++++++++++.+++..           ",
"..+++++++++++.++++..              ..+++++++++++.++++..           ",
"..+++++++++++.++++..              ..++++++++++.+++++..           ",
"..+++++++++++.++++..              ..+++++++++.++++++..           ",
"..++++++++++...+++..              ..+++++++++....+++..           ",
"..++++++++++++++++..              ..++++++++++++++++..           ",
"..++++++++++++++++..              ..++++++++++++++++..           ",
"..++++++++++++++++..              ..++++++++++++++++..           ",
"....................              ....................           ",
"....................              ....................           "};

/* XPM */
static const char *collate_reverse_xpm[] = {
"65 35 6 1",
" 	c None",
".	c #000000",
"+	c #020202",
"@	c #FFFFFF",
"#	c #010101",
"$	c #070707",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           ..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..",
"           ..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"           ++@@@@@@@@@@@@@@@@..              ++@@@@@@@@@@@@@@@@..",
"..+++++++++##++++++$@@@@@@@@@..   ..+++++++++##++++++$@@@@@@@@@..",
"..+++++++++##+++++#+@@@@@@@@@..   ..+++++++++##+++++#+@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@.@@@@..   ..@@@@@@@@@@@@@@@@++@@@@.@@@@..",
"..@@@@@@@@@@@@@@@@++@@@..@@@@..   ..@@@@@@@@@@@@@@@@++@@@..@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@.@@@@..   ..@@@@@@@@@@@@@@@@++@@@@.@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@.@@@@..   ..@@@@@@@@@@@@@@@@++@@@@.@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@.@@@@..   ..@@@@@@@@@@@@@@@@++@@@@.@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@.@@@@..   ..@@@@@@@@@@@@@@@@++@@@@.@@@@..",
"..@@@@@@@@@@@@@@@@++@@@...@@@..   ..@@@@@@@@@@@@@@@@++@@@...@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@@@@@@@++@@@@@@@@@..   ..@@@@@@@@@@@@@@@@++@@@@@@@@@..",
"..@@@@@@@@@@..@@@@.............   ..@@@@@@@@@@..@@@@.............",
"..@@@@@@@@@.@@.@@@.............   ..@@@@@@@@@.@@.@@@.............",
"..@@@@@@@@@@@@.@@@..              ..@@@@@@@@@@@@.@@@..           ",
"..@@@@@@@@@@@.@@@@..              ..@@@@@@@@@@@.@@@@..           ",
"..@@@@@@@@@@.@@@@@..              ..@@@@@@@@@@.@@@@@..           ",
"..@@@@@@@@@.@@@@@@..              ..@@@@@@@@@.@@@@@@..           ",
"..@@@@@@@@@....@@@..              ..@@@@@@@@@....@@@..           ",
"..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..           ",
"..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..           ",
"..@@@@@@@@@@@@@@@@..              ..@@@@@@@@@@@@@@@@..           ",
"....................              ....................           ",
"....................              ....................           "};

/* XPM */
static const char *nocollate_reverse_xpm[] = {
"65 35 6 1",
" 	c None",
".	c #000000",
"+	c #FFFFFF",
"@	c #020202",
"#	c #010101",
"$	c #070707",
"           ....................              ....................",
"           ....................              ....................",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           ..++++++++++++++++..              ..++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"           @@++++++++++++++++..              @@++++++++++++++++..",
"..@@@@@@@@@##@@@@@@$+++++++++..   ..@@@@@@@@@##@@@@@@$+++++++++..",
"..@@@@@@@@@##@@@@@#@+++++++++..   ..@@@@@@@@@##@@@@@#@+++++++++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++++++++@@+++..++++..   ..++++++++++++++++@@++++.++++..",
"..++++++++++++++++@@++.++.+++..   ..++++++++++++++++@@+++..++++..",
"..++++++++++++++++@@+++++.+++..   ..++++++++++++++++@@++++.++++..",
"..++++++++++++++++@@++++.++++..   ..++++++++++++++++@@++++.++++..",
"..++++++++++++++++@@+++.+++++..   ..++++++++++++++++@@++++.++++..",
"..++++++++++++++++@@++.++++++..   ..++++++++++++++++@@++++.++++..",
"..++++++++++++++++@@++....+++..   ..++++++++++++++++@@+++...+++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++++++++@@+++++++++..   ..++++++++++++++++@@+++++++++..",
"..++++++++++..++++.............   ..+++++++++++.++++.............",
"..+++++++++.++.+++.............   ..++++++++++..++++.............",
"..++++++++++++.+++..              ..+++++++++++.++++..           ",
"..+++++++++++.++++..              ..+++++++++++.++++..           ",
"..++++++++++.+++++..              ..+++++++++++.++++..           ",
"..+++++++++.++++++..              ..+++++++++++.++++..           ",
"..+++++++++....+++..              ..++++++++++...+++..           ",
"..++++++++++++++++..              ..++++++++++++++++..           ",
"..++++++++++++++++..              ..++++++++++++++++..           ",
"..++++++++++++++++..              ..++++++++++++++++..           ",
"....................              ....................           ",
"....................              ....................           "};

static GtkVBoxClass *parent_class;
static guint gpc_signals[LAST_SIGNAL] = {0};

GType
gnome_print_copies_selector_get_type (void)
{
	static GType type = 0;
	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePrintCopiesSelectorClass),
			NULL, NULL,
			(GClassInitFunc) gnome_print_copies_selector_class_init,
			NULL, NULL,
			sizeof (GnomePrintCopiesSelector),
			0,
			(GInstanceInitFunc) gnome_print_copies_selector_init,
			NULL
		};
		type = g_type_register_static (GTK_TYPE_VBOX, "GnomePrintCopiesSelector", &info, 0);
	}
	return type;
}

static void
gnome_print_copies_selector_save (GnomePrintCopiesSelector *gpc)
{
	GnomePrintFilter *f;

	g_return_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc));

	if (gpc->loading || gpc->saving)
		return;

	gpc->saving = TRUE;
	f = gnome_print_filter_get_filter (gpc->filter, 0);
	if (GTK_TOGGLE_BUTTON (gpc->reverse)->active &&
			!strcmp ("GnomePrintFilter", G_OBJECT_TYPE_NAME (G_OBJECT (f)))) {
		gnome_print_filter_remove_filters (gpc->filter);
		f = gnome_print_filter_new_from_description ("GnomePrintFilterReverse", NULL);
		gnome_print_filter_add_filter (gpc->filter, f);
		g_object_unref (G_OBJECT (f));
	} else if (!GTK_TOGGLE_BUTTON (gpc->reverse)->active &&
			strcmp ("GnomePrintFilter", G_OBJECT_TYPE_NAME (G_OBJECT (f)))) {
		gnome_print_filter_remove_filters (gpc->filter);
		f = g_object_new (GNOME_TYPE_PRINT_FILTER, NULL);
		gnome_print_filter_add_filter (gpc->filter, f);
		g_object_unref (G_OBJECT (f));
	}
	gpc->saving = FALSE;
}

static gboolean
gnome_print_copies_selector_load_filter (GnomePrintCopiesSelector *gpc,
		GnomePrintFilter *f)
{
	g_return_val_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc), FALSE);
	g_return_val_if_fail (GNOME_IS_PRINT_FILTER (f), FALSE);

	if (gpc->loading || gpc->saving)
		return FALSE;

	if (strcmp ("GnomePrintFilter", G_OBJECT_TYPE_NAME (G_OBJECT (f))))
		return FALSE;
	if (gnome_print_filter_count_filters (f) != 1)
		return FALSE;
	f = gnome_print_filter_get_filter (f, 0);
	if (!strcmp ("GnomePrintFilterReverse", G_OBJECT_TYPE_NAME (G_OBJECT (f)))) {
		gpc->loading = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gpc->reverse), TRUE);
		gpc->loading = FALSE;
		gtk_widget_show (gpc->reverse);
		return TRUE;
	} else if (!strcmp ("GnomePrintFilter", G_OBJECT_TYPE_NAME (G_OBJECT (f)))) {
		gpc->loading = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gpc->reverse), FALSE);
		gpc->loading = FALSE;
		gtk_widget_show (gpc->reverse);
		return TRUE;
	} else {
		gtk_widget_hide (gpc->reverse);
		return FALSE;
	}
}

static void
on_filter_notify (GObject *object, GParamSpec *pspec,
		GnomePrintCopiesSelector *cs)
{
	gnome_print_copies_selector_load_filter (cs, GNOME_PRINT_FILTER (object));
}

static void
gnome_print_copies_selector_set_property (GObject *object, guint n,
		const GValue *v, GParamSpec *pspec)
{
	GnomePrintCopiesSelector *gpc = GNOME_PRINT_COPIES_SELECTOR (object);

	switch (n) {
	case PROP_FILTER:
		if (gnome_print_copies_selector_load_filter (gpc, g_value_get_object (v))) {
			if (gpc->filter) {
				g_signal_handler_disconnect (G_OBJECT (gpc->filter), gpc->signal);
				g_object_unref (G_OBJECT (gpc->filter));
			}
			gpc->filter = g_value_get_object (v);
			g_object_ref (G_OBJECT (gpc->filter));
			gpc->signal = g_signal_connect (G_OBJECT (gpc->filter), "notify",
					G_CALLBACK (on_filter_notify), gpc);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_print_copies_selector_get_property (GObject *object, guint n,
		GValue *v, GParamSpec *pspec)
{
	GnomePrintCopiesSelector *gpc = GNOME_PRINT_COPIES_SELECTOR (object);

	switch (n) {
	case PROP_FILTER:
		g_value_set_object (v, gpc->filter);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_print_copies_selector_finalize (GObject *object)
{
	GnomePrintCopiesSelector *gpc = GNOME_PRINT_COPIES_SELECTOR (object);

	if (gpc->filter) {
		g_signal_handler_disconnect (G_OBJECT (gpc->filter), gpc->signal);
		g_object_unref (G_OBJECT (gpc->filter));
		gpc->filter = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

#define DEFAULT_FILTER "GnomePrintFilter [ GnomePrintFilter ]"

struct {
	GParamSpec parent_instance;
} GnomePrintCopiesSelectorParamFilter;

static void
param_filter_set_default (GParamSpec *pspec, GValue *v)
{
	GnomePrintFilter *f;

	f = gnome_print_filter_new_from_description (DEFAULT_FILTER, NULL);
	g_value_set_object (v, f);
	g_object_unref (G_OBJECT (f));
}

static GType
gnome_print_copies_selector_param_filter_get_type (void)
{
	static GType type;
	if (G_UNLIKELY (type) == 0) {
		static const GParamSpecTypeInfo pspec_info = {
			sizeof (GnomePrintCopiesSelectorParamFilter), 0, NULL,
			G_TYPE_OBJECT, NULL, param_filter_set_default, NULL, NULL
		};
		type = g_param_type_register_static ("GnomePrintCopiesSelectorParamFilter", &pspec_info);
	}
	return type;
}

static void
gnome_print_copies_selector_class_init (GnomePrintCopiesSelectorClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	GParamSpec *ps;

	object_class->get_property = gnome_print_copies_selector_get_property;
	object_class->set_property = gnome_print_copies_selector_set_property;
	object_class->finalize     = gnome_print_copies_selector_finalize;

	ps = g_param_spec_internal (
			gnome_print_copies_selector_param_filter_get_type (),
			"filter", _("Filter"), _("Filter"), G_PARAM_READWRITE);
	ps->value_type = GNOME_TYPE_PRINT_FILTER;
	g_object_class_install_property (object_class, PROP_FILTER, ps);

	parent_class = g_type_class_peek_parent (klass);

	gpc_signals[COPIES_SET] = g_signal_new ("copies_set",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_FIRST,
						G_STRUCT_OFFSET (GnomePrintCopiesSelectorClass, copies_set),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT,
						G_TYPE_NONE,
						1,
						G_TYPE_INT);
	
	gpc_signals[COLLATE_SET] = g_signal_new ("collate_set",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_FIRST,
						G_STRUCT_OFFSET (GnomePrintCopiesSelectorClass, collate_set),
						NULL, NULL,
						libgnomeprintui_marshal_VOID__BOOLEAN,
						G_TYPE_NONE,
						1,
						G_TYPE_BOOLEAN);
}

static void
gnome_print_copies_selector_update_image (GnomePrintCopiesSelector *gpc)
{
	GdkPixbuf *p;

	g_return_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc));

	p = gdk_pixbuf_new_from_xpm_data (
			GTK_TOGGLE_BUTTON (gpc->collate)->active ?
			(GTK_TOGGLE_BUTTON (gpc->reverse)->active ? collate_reverse_xpm : collate_xpm) :
			(GTK_TOGGLE_BUTTON (gpc->reverse)->active ? nocollate_reverse_xpm : nocollate_xpm));
	gtk_image_set_from_pixbuf (GTK_IMAGE (gpc->collate_image), p);
	g_object_unref (G_OBJECT (p));
}

static void
collate_toggled (GtkToggleButton *t, GnomePrintCopiesSelector *gpc)
{
	gnome_print_copies_selector_update_image (gpc);

	if (gpc->changing)
		return;

	g_signal_emit (G_OBJECT (gpc), gpc_signals[COLLATE_SET], 0, t->active);
}

static void
gnome_print_copies_selector_update_sensitivity (GnomePrintCopiesSelector *gpc)
{
	gboolean s;

	g_return_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc));

	s = GTK_SPIN_BUTTON (gpc->copies)->adjustment->value > 1.;
	gtk_widget_set_sensitive (gpc->collate, s);
	gtk_widget_set_sensitive (gpc->collate_image, s);
}

static void
copies_changed (GtkAdjustment *adj, GnomePrintCopiesSelector *gpc)
{
	gnome_print_copies_selector_update_sensitivity (gpc);

	if (!gpc->changing)
		g_signal_emit (G_OBJECT (gpc), gpc_signals[COPIES_SET], 0,
				(gint) adj->value);
}

static void
reverse_toggled (GtkToggleButton *button, GnomePrintCopiesSelector *gpc)
{
	gnome_print_copies_selector_update_image (gpc);
	gnome_print_copies_selector_save (gpc);
}

static void
gnome_print_copies_selector_init (GnomePrintCopiesSelector *gpc)
{
	GtkWidget *table, *label, *frame, *l;
	GtkAdjustment *adj;
	GdkPixbuf *pb;
	AtkObject *atko;
	gchar *text;

	gpc->filter = gnome_print_filter_new_from_description (DEFAULT_FILTER, NULL);
	gpc->signal = g_signal_connect (G_OBJECT (gpc->filter), "notify",
			G_CALLBACK (on_filter_notify), gpc);

	frame = gtk_frame_new ("");
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	label = gtk_label_new ("");
	text = g_strdup_printf ("<b>%s</b>", _("Copies"));
	gtk_label_set_markup (GTK_LABEL (label), text);
	g_free (text);
	gtk_frame_set_label_widget (GTK_FRAME (frame), label);
	gtk_widget_show (label);
	gtk_container_add (GTK_CONTAINER (gpc), frame);
	gtk_widget_show (frame);

	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER(table), 6);
	gtk_container_add (GTK_CONTAINER(frame), GTK_WIDGET (table));
	gtk_widget_show (table);

	l = gtk_label_new_with_mnemonic (_("N_umber of copies:"));
	gtk_widget_show (l);
	gtk_table_attach_defaults ((GtkTable *)table, l, 0, 1, 0, 1);

	adj = (GtkAdjustment *) gtk_adjustment_new(1, 1, 1000, 1.0, 10.0, 10.0);
	gpc->copies = gtk_spin_button_new (adj, 1.0, 0);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (gpc->copies), TRUE);
	gtk_widget_show (gpc->copies);
	gtk_table_attach_defaults ((GtkTable *)table, gpc->copies, 1, 2, 0, 1);

	gnome_print_set_atk_relation (label, GTK_WIDGET (gpc->copies));
	gnome_print_set_atk_relation (l, GTK_WIDGET (gpc->copies));
	gtk_label_set_mnemonic_widget ((GtkLabel *) l, gpc->copies);

	pb = gdk_pixbuf_new_from_xpm_data (nocollate_xpm);
	gpc->collate_image = gtk_image_new_from_pixbuf (pb);
	g_object_unref (G_OBJECT (pb));
	gtk_widget_show (gpc->collate_image);
	gtk_table_attach_defaults ((GtkTable *)table, gpc->collate_image, 0, 1, 1, 2);
	atko = gtk_widget_get_accessible (gpc->collate_image);
	atk_image_set_image_description (ATK_IMAGE (atko), _("Image showing the collation sequence when multiple copies of the document are printed"));

	/* Collate */
	gpc->collate = gtk_check_button_new_with_mnemonic (_("C_ollate"));
	gnome_print_set_atk_relation (label, GTK_WIDGET (gpc->collate));
	gtk_widget_show (gpc->collate);
	gtk_table_attach_defaults (GTK_TABLE (table), gpc->collate, 1, 2, 1, 2);
	atko = gtk_widget_get_accessible (gpc->collate);
	atk_object_set_description (atko, _("If copies of the document are printed separately, one after another, rather than being interleaved"));

	/* Reverse */
	gpc->reverse = gtk_check_button_new_with_mnemonic (_("_Reverse"));
	gtk_table_attach_defaults(GTK_TABLE (table), gpc->reverse, 1, 2, 2, 3);
	atko = gtk_widget_get_accessible (gpc->reverse);
	atk_object_set_description (atko, _("Reverse order of pages when printing"));

	gnome_print_copies_selector_update_sensitivity (gpc);

	g_signal_connect (G_OBJECT (adj), "value_changed", (GCallback) copies_changed, gpc);
	g_signal_connect (G_OBJECT (gpc->collate), "toggled", (GCallback) collate_toggled, gpc);
	g_signal_connect (G_OBJECT (gpc->reverse), "toggled", (GCallback) reverse_toggled, gpc);
}

/**
 * gnome_print_copies_selector_new:
 *
 * Create a new GnomePrintCopies widget.
 * 
 * Return value: A new GnomePrintCopies widget.
 **/

GtkWidget *
gnome_print_copies_selector_new (void)
{
	return GTK_WIDGET (g_object_new (GNOME_TYPE_PRINT_COPIES_SELECTOR, NULL));
}

/**
 * gnome_print_copies_set_copies:
 * @gpc: An initialised GnomePrintCopies widget.
 * @copies: New number of copies.
 * @collate: New collation status.
 * 
 * Set the number of copies and collation sequence to be displayed.
 **/

void
gnome_print_copies_selector_set_copies (GnomePrintCopiesSelector *gpc, gint copies, gboolean collate)
{
	g_return_if_fail (gpc != NULL);
	g_return_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc));

	gpc->changing = TRUE;

	gtk_toggle_button_set_active ((GtkToggleButton *) gpc->collate, collate);
	gpc->changing = FALSE;

	gtk_spin_button_set_value ((GtkSpinButton *) gpc->copies, copies);

	gtk_widget_set_sensitive (gpc->collate, (copies != 1));
	gtk_widget_set_sensitive (gpc->collate_image, (copies != 1));
}

/**
 * gnome_print_copies_get_copies:
 * @gpc: An initialised GnomePrintCopies widget.
 * 
 * Retrieve the number of copies set
 *
 * Return value: Number of copies set
 **/

gint
gnome_print_copies_selector_get_copies (GnomePrintCopiesSelector *gpc)
{
	g_return_val_if_fail (gpc != NULL, 0);
	g_return_val_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc), 0);

	return gtk_spin_button_get_value_as_int ((GtkSpinButton *) gpc->copies);
}

/**
 * gnome_print_copies_get_collate:
 * @gpc: An initialised GnomePrintCopies widget.
 * 
 * Retrieve the collation status
 *
 * Return value: Collation status
 **/

gboolean
gnome_print_copies_selector_get_collate (GnomePrintCopiesSelector *gpc)
{
	g_return_val_if_fail (gpc != NULL, FALSE);
	g_return_val_if_fail (GNOME_IS_PRINT_COPIES_SELECTOR (gpc), FALSE);

	return GTK_TOGGLE_BUTTON (gpc->collate)->active?TRUE:FALSE;
}
