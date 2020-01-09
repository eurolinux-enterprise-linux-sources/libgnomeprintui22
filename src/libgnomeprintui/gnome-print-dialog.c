/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  gnome-print-dialog.c: A system print dialog
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
 *    Michael Zucchi <notzed@helixcode.com>
 *    Chema Celorio <chema@celorio.com>
 *    Lauris Kaplinski <lauris@ximian.com>
 *    Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 *  Copyright (C) 2000-2002 Ximian Inc.
 *  Copyright (C) 2004 Andreas J. Guelzow
 *
 */

#define GNOME_PRINT_UNSTABLE_API

#include <config.h>

#include <time.h>
#include <string.h>

#include <atk/atk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/private/gpa-node.h>
#include <libgnomeprint/private/gpa-node-private.h>
#include <libgnomeprint/private/gpa-utils.h>
#include <libgnomeprint/private/gpa-key.h>
#include <libgnomeprint/private/gnome-print-private.h>
#include <libgnomeprint/private/gnome-print-config-private.h>

#include <libgnomeprintui/gnome-print-i18n.h>
#include <libgnomeprintui/gnome-printer-selector.h>
#include <libgnomeprintui/gnome-print-content-selector.h>
#include <libgnomeprintui/gnome-print-layout-selector.h>
#include <libgnomeprintui/gnome-print-page-selector.h>
#include <libgnomeprintui/gnome-print-paper-selector.h>
#include <libgnomeprintui/gnome-print-copies.h>
#include <libgnomeprintui/gnome-print-dialog.h>

#define PAD 6

enum {
	PROP_0,
	PROP_TITLE,
	PROP_FLAGS,
	PROP_PRINT_CONFIG,
	PROP_PRINTER_SELECTOR,
	PROP_CONTENT_SELECTOR,
	PROP_PAGE_SELECTOR,
	PROP_NOTEBOOK
};

static void gnome_print_dialog_class_init (GnomePrintDialogClass *class);
static void gnome_print_dialog_init (GnomePrintDialog *dialog);
static void gnome_print_dialog_watch_filter (GnomePrintDialog *, GnomePrintFilter *);

static GtkDialogClass *parent_class;

struct _GnomePrintDialog {
	GtkDialog dialog;

	GnomePrintConfig *config;
	GnomePrintFilter *filter;

	GPANode *node_filter, *node_source, *node_printer;
	guint signal_filter, signal_source, signal_printer;

	GtkWidget *notebook;
	GtkWidget *l_layout, *l_job, *l_paper;

	/* Containers for selectors */
	GtkWidget *e_content;
	GtkWidget *e_range; /* Old API */

	/* Selectors */
	GtkWidget *s_content;
	GtkWidget *s_page;
	GtkWidget *s_paper;
	GtkWidget *s_copies;
	GtkWidget *s_layout;

	GtkWidget *job;
	GtkWidget *printer;

	gint flags;
	gboolean needs_save_filter;
};

struct _GnomePrintDialogClass {
	GtkDialogClass parent_class;
};

GType
gnome_print_dialog_get_type (void)
{	static GType type = 0;
	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePrintDialogClass),
			NULL, NULL,
			(GClassInitFunc) gnome_print_dialog_class_init,
			NULL, NULL,
			sizeof (GnomePrintDialog),
			0,
			(GInstanceInitFunc) gnome_print_dialog_init,
			NULL
		};
		type = g_type_register_static (GTK_TYPE_DIALOG, "GnomePrintDialog", &info, 0);
	}
	return type;
}

static void
on_paper_selector_notify (GObject *object, GParamSpec *pspec,
		GnomePrintDialog *gpd)
{
	GValue v = {0,};

	g_value_init (&v, pspec->value_type);
	if (!strcmp (pspec->name, "width")) {
		g_object_get_property (object, "width", &v);
		g_object_set_property (G_OBJECT (gpd->s_layout), "input_width", &v);
		g_object_set_property (G_OBJECT (gpd->s_layout), "output_width", &v);
	}
	if (!strcmp (pspec->name, "height")) {
		g_object_get_property (object, "height", &v);
		g_object_set_property (G_OBJECT (gpd->s_layout), "input_height", &v);
		g_object_set_property (G_OBJECT (gpd->s_layout), "output_height", &v);
	}
	g_value_unset (&v);
}

static void
on_content_selector_notify (GObject *object, GParamSpec *pspec,
		GnomePrintDialog *gpd)
{
	GValue v = {0,};

	g_value_init (&v, pspec->value_type);
	if (!strcmp (pspec->name, "total") || !strcmp (pspec->name, "current")) {
		g_object_get_property (object, pspec->name, &v);
		g_object_set_property (G_OBJECT (gpd->s_page), pspec->name, &v);
	}
	g_value_unset (&v);
}

static GPANode *
_gnome_print_config_ensure_key (GnomePrintConfig *config, const gchar *key)
{
	GPANode *node;
	const gchar *p;
	gchar *q;

	g_return_val_if_fail (GNOME_IS_PRINT_CONFIG (config), NULL);
	g_return_val_if_fail (key, NULL);

	node = gpa_node_lookup (GNOME_PRINT_CONFIG_NODE (config),
			(const guchar *) key);
	if (node)
		return node;

	for (p = key + strlen (key) - 1; (p > key) && (*p != '.'); p--);
	if (*p == '.') {
		q = g_strndup (key, p - key);
		node = gpa_node_lookup (GNOME_PRINT_CONFIG_NODE (config),
				(const guchar *) q);
		gpa_key_insert (node, (const guchar *) (p + 1), (const guchar *) "");
	}
	return gpa_node_lookup (GNOME_PRINT_CONFIG_NODE (config),
			(const guchar *) key);
}

static GtkWidget *
get_page (GtkWidget *n, GtkWidget *tab)
{
	gint i;

	g_return_val_if_fail (GTK_IS_NOTEBOOK (n), NULL);
	g_return_val_if_fail (GTK_IS_WIDGET (tab), NULL);

	for (i = gtk_notebook_get_n_pages (GTK_NOTEBOOK (n)); i > 0; i--) {
		GtkWidget *w = gtk_notebook_get_nth_page (GTK_NOTEBOOK (n), i - 1);
		if (gtk_notebook_get_tab_label (GTK_NOTEBOOK (n), w) == tab)
			return w;
	}
	return NULL;
}

static void
gnome_print_dialog_set_has_source (GnomePrintDialog *gpd, gboolean source)
{
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));

	gtk_widget_set_sensitive (gpd->l_layout, !source);
	gtk_widget_set_sensitive (gpd->l_job, !source);
	gtk_widget_set_sensitive (gpd->l_paper, !source);
	gtk_widget_set_sensitive (get_page (gpd->notebook, gpd->l_layout), !source);
	gtk_widget_set_sensitive (get_page (gpd->notebook, gpd->l_job), !source);
	gtk_widget_set_sensitive (get_page (gpd->notebook, gpd->l_paper), !source);
}

static void
gnome_print_dialog_check_source (GnomePrintDialog *gpd)
{
	gchar *source;

	source = (gchar *) gnome_print_config_get (gpd->config,
		(const guchar *) "Settings.Document.Source");
	gnome_print_dialog_set_has_source (gpd, source && strcmp (source, ""));
	if (source)
		g_free (source);
}

static void
gnome_print_dialog_save_filter (GnomePrintDialog *gpd)
{
	gchar *d = NULL;

	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));

	if (gpd->filter)
		d = gnome_print_filter_description (gpd->filter);
	if (gpd->node_filter)
		g_signal_handler_block (G_OBJECT (gpd->node_filter), gpd->signal_filter);
	gnome_print_config_set (gpd->config,
			(const guchar *) "Settings.Document.Filter",
			(const guchar *) (d ? d : ""));
	if (gpd->node_filter)
		g_signal_handler_unblock (G_OBJECT (gpd->node_filter), gpd->signal_filter);
	if (d)
		g_free (d);
}

static gboolean
save_filter (gpointer data)
{
	GnomePrintDialog *gpd = GNOME_PRINT_DIALOG (data);

	gnome_print_dialog_save_filter (gpd);
	gpd->needs_save_filter = FALSE;
	return FALSE;
}

static void
gnome_print_dialog_schedule_save_filter (GnomePrintDialog *gpd)
{
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));

	if (!gpd->needs_save_filter) {
		gpd->needs_save_filter = TRUE;
		g_idle_add (save_filter, gpd);
	}
}

static void
gnome_print_dialog_check_filter (GnomePrintDialog *gpd)
{
	gchar *d_new, *d_old = NULL;
	GnomePrintFilter *f = NULL;
	guint i;
	struct {
		GObject *o;
		gboolean b;
	} o[3];

	d_new = (gchar *) gnome_print_config_get (gpd->config,
			(const guchar *) "Settings.Document.Filter");
	if (gpd->filter)
		d_old = gnome_print_filter_description (gpd->filter);
	if ((!d_new && !d_old) || (d_new && d_old && !strcmp (d_new, d_old))) {
		if (d_new)
			g_free (d_new);
		if (d_old)
			g_free (d_old);
		return;
	}
	if (d_old)
		g_free (d_old);
	if (gpd->filter) {
		g_object_unref (G_OBJECT (gpd->filter));
		gpd->filter = NULL;
	}
	if (d_new)
		gpd->filter = gnome_print_filter_new_from_description (d_new, NULL);

	o[0].o = G_OBJECT (gpd->s_page);
	o[1].o = G_OBJECT (gpd->s_copies);
	o[2].o = G_OBJECT (gpd->s_layout);
	o[0].b = FALSE;
	o[1].b = FALSE;
	o[2].b = FALSE;
	for (f = gpd->filter; f; ) {
		guint n = gnome_print_filter_count_successors (f);

		for (i = 0; i < G_N_ELEMENTS (o); i++) {
			GnomePrintFilter *fn = NULL;

			if (o[i].b)
				break;
			g_object_set (o[i].o, "filter", f, NULL);
			g_object_get (o[i].o, "filter", &fn, NULL);
			o[i].b = f == fn;
		}
		if (!n || n > 1)
			break;
		f = gnome_print_filter_get_successor (f, 0);
	}
	for (i = 0; i < G_N_ELEMENTS (o); i++) {
		GnomePrintFilter *fn = NULL;

		if (!o[i].b) {
			GParamSpec *ps = g_object_class_find_property (
					G_OBJECT_GET_CLASS (o[i].o), "filter");
			GValue v = {0,};

			g_value_init (&v, G_PARAM_SPEC_VALUE_TYPE (ps));
			g_param_value_set_default (ps, &v);
			g_object_set_property (o[i].o, "filter", &v);
			fn = g_value_get_object (&v);
			g_object_ref (G_OBJECT (fn));
			g_value_unset (&v);
			if (gpd->filter) {
				gnome_print_filter_append_predecessor (gpd->filter, fn);
				g_object_unref (G_OBJECT (gpd->filter));
			}
			gpd->filter = fn;
		}
		g_object_get (o[i].o, "filter", &fn, NULL);
	}
	gnome_print_dialog_schedule_save_filter (gpd);
	if (gpd->filter)
		gnome_print_dialog_watch_filter (gpd, gpd->filter);
}

static void
on_node_source_modified (GPANode *node, guint flags, GnomePrintDialog *gpd)
{
	gnome_print_dialog_check_source (gpd);
}

static void
on_filter_notify (GObject *object, GParamSpec *pspec, GnomePrintDialog *gpd)
{
	gnome_print_dialog_schedule_save_filter (gpd);

	if (!strcmp (pspec->name, "filters")) {
		GValueArray *va = NULL;
		guint i;

		g_object_get (object, "filters", &va, NULL);
		for (i = 0; i < va->n_values; i++)
			gnome_print_dialog_watch_filter (gpd, GNOME_PRINT_FILTER (
						g_value_get_object (g_value_array_get_nth (va, i))));
	}
}

static void
gnome_print_dialog_watch_filter (GnomePrintDialog *gpd, GnomePrintFilter *f)
{
	GClosure *closure;
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));
	g_return_if_fail (GNOME_IS_PRINT_FILTER (f));

	closure = g_cclosure_new (G_CALLBACK (on_filter_notify), gpd, NULL);
	g_object_watch_closure (G_OBJECT (gpd), closure);
	g_signal_connect_closure (G_OBJECT (f), "notify", closure, FALSE);
	for (i = gnome_print_filter_count_filters (f); i > 0; i--)
		gnome_print_dialog_watch_filter (gpd, gnome_print_filter_get_filter (f, i - 1));
	for (i = gnome_print_filter_count_successors (f); i > 0; i--)
		gnome_print_dialog_watch_filter (gpd, gnome_print_filter_get_successor (f, i - 1));
}

static void
on_node_filter_modified (GPANode *node, guint flags, GnomePrintDialog *gpd)
{
	gnome_print_dialog_check_filter (gpd);
}

static void
on_node_printer_modified (GPANode *node, guint flags, GnomePrintDialog *gpd)
{
	/* The filter gets reset in the config. We need to figure out why. */
	gnome_print_dialog_schedule_save_filter (gpd);
}

static void
gnome_print_dialog_set_config (GnomePrintDialog *gpd,
		GnomePrintConfig *config)
{
	gint n, copies = 1;
	gboolean collate = FALSE;
	GtkWidget *hb, *l;

	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));
	g_return_if_fail (!config || GNOME_IS_PRINT_CONFIG (config));

	if (gpd->config == config)
		return;

	if (gpd->node_source) {
		if (gpd->signal_source) {
			g_signal_handler_disconnect (G_OBJECT (gpd->node_source),
					gpd->signal_source);
			gpd->signal_source = 0;
		}
		g_object_unref (G_OBJECT (gpd->node_source));
		gpd->node_source = NULL;
	}
	if (gpd->node_filter) {
		if (gpd->signal_filter) {
			g_signal_handler_disconnect (G_OBJECT (gpd->node_filter),
					gpd->signal_filter);
			gpd->signal_filter = 0;
		}
		g_object_unref (G_OBJECT (gpd->node_filter));
		gpd->node_filter = NULL;
	}
	if (gpd->node_printer) {
		if (gpd->signal_printer) {
			g_signal_handler_disconnect (G_OBJECT (gpd->node_printer),
					gpd->signal_printer);
			gpd->signal_printer = 0;
		}
		g_object_unref (G_OBJECT (gpd->node_printer));
		gpd->node_printer = NULL;
	}
	if (gpd->config)
		g_object_unref (G_OBJECT (gpd->config));
	gpd->config = config;
	if (config)
		g_object_ref (G_OBJECT (config));

	if (gpd->s_paper)
		g_object_set (G_OBJECT (gpd->s_paper), "config", config, NULL);

	if (!config)
		return;

	/* Copies & Collate */
	gnome_print_config_get_int (gpd->config,
		(const guchar *) GNOME_PRINT_KEY_NUM_COPIES, &copies);
	gnome_print_config_get_boolean (gpd->config,
		(const guchar *) GNOME_PRINT_KEY_COLLATE, &collate);
	gnome_print_copies_selector_set_copies (
		GNOME_PRINT_COPIES_SELECTOR (gpd->s_copies), copies, collate);
	gnome_print_dialog_set_copies (gpd, copies, collate);

	/* Add the printers page */
	n = gtk_notebook_page_num (GTK_NOTEBOOK (gpd->notebook), gpd->printer);
	if (n >= 0)
		gtk_notebook_remove_page (GTK_NOTEBOOK (gpd->notebook), n);
	hb = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hb);
	l = gtk_label_new_with_mnemonic (_("Printer"));
	gtk_widget_show (l);
	gtk_notebook_insert_page (GTK_NOTEBOOK (gpd->notebook), hb, l, MAX (0, n));
	gpd->printer = gnome_printer_selector_new (gpd->config);
	gtk_container_set_border_width (GTK_CONTAINER (hb), 4);
	gtk_widget_show (gpd->printer);
	gtk_box_pack_start (GTK_BOX (hb), gpd->printer, TRUE, TRUE, 0);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (gpd->notebook),
			gtk_notebook_page_num (GTK_NOTEBOOK (gpd->notebook), hb));

	gnome_print_dialog_check_source (gpd);
	gnome_print_dialog_check_filter (gpd);

	/* Watch out for changes */
	gpd->node_source = _gnome_print_config_ensure_key (config,
			"Settings.Document.Source");
	g_object_ref (G_OBJECT (gpd->node_source));
	gpd->signal_source = g_signal_connect (G_OBJECT (gpd->node_source),
			"modified", G_CALLBACK (on_node_source_modified), gpd);
	gpd->node_filter = _gnome_print_config_ensure_key (config,
			"Settings.Document.Source");
	g_object_ref (G_OBJECT (gpd->node_filter));
	gpd->signal_filter = g_signal_connect (G_OBJECT (gpd->node_filter),
			"modified", G_CALLBACK (on_node_filter_modified), gpd);
	gpd->node_printer = gpa_node_lookup (GNOME_PRINT_CONFIG_NODE (gpd->config),
			(const guchar *) "Printer");
	g_object_ref (G_OBJECT (gpd->node_printer));
	gpd->signal_printer = g_signal_connect (G_OBJECT (gpd->node_printer),
			"modified", G_CALLBACK (on_node_printer_modified), gpd);
}

static void
gnome_print_dialog_finalize (GObject *object)
{
	GnomePrintDialog *gpd = GNOME_PRINT_DIALOG (object);

	/* Widgets got already destroyed */
	gpd->s_paper = NULL;

	gnome_print_dialog_set_config (gpd, NULL);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gnome_print_dialog_set_property (GObject *object, guint prop_id,
		GValue const *value, GParamSpec *pspec)
{
	GnomePrintDialog *gpd = GNOME_PRINT_DIALOG (object);

	switch (prop_id) {
	case PROP_TITLE:
		gtk_window_set_title (GTK_WINDOW (gpd), g_value_get_string (value));
		break;
	case PROP_FLAGS:
		gpd->flags = g_value_get_int (value);
		g_object_set (G_OBJECT (gpd->e_range), "visible",
				(gpd->flags & GNOME_PRINT_DIALOG_RANGE) > 0, NULL);
		g_object_set (G_OBJECT (gpd->s_copies), "visible",
				(gpd->flags & GNOME_PRINT_DIALOG_COPIES) > 0, NULL);
		break;
	case PROP_PRINT_CONFIG:
		gnome_print_dialog_set_config (gpd, g_value_get_object (value));
		break;
	case PROP_CONTENT_SELECTOR:
		if (gpd->s_content)
			gtk_container_remove (GTK_CONTAINER (gpd->e_content),
					gpd->s_content);
		gpd->s_content = g_value_get_object (value);
		if (gpd->s_content) {
			GValue v = {0,};

			gtk_widget_show (gpd->e_content);
			gtk_container_add (GTK_CONTAINER (gpd->e_content), gpd->s_content);
			gtk_widget_show (gpd->s_content);
			g_object_get_property (G_OBJECT (gpd->s_content), "current", &v);
			g_object_set_property (G_OBJECT (gpd->s_page), "current", &v);
			g_object_get_property (G_OBJECT (gpd->s_content), "total", &v);
			g_object_set_property (G_OBJECT (gpd->s_page), "total", &v);
			g_signal_connect (gpd->s_content, "notify",
					G_CALLBACK (on_content_selector_notify), gpd);
		} else
			gtk_widget_hide (gpd->e_content);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
gnome_print_dialog_get_property (GObject *object, guint prop_id,
		GValue *value, GParamSpec *pspec)
{
	GnomePrintDialog *gpd = GNOME_PRINT_DIALOG (object);

	switch (prop_id) {
	case PROP_TITLE:
		g_value_set_string (value, gtk_window_get_title (GTK_WINDOW (gpd)));
		break;
	case PROP_FLAGS:
		g_value_set_int (value, gpd->flags);
		break;
	case PROP_PRINTER_SELECTOR:
		g_value_set_object (value, gpd->printer);
		break;
	case PROP_NOTEBOOK:
		g_value_set_object (value, gpd->notebook);
		break;
	case PROP_CONTENT_SELECTOR:
		g_value_set_object (value, gpd->s_content);
		break;
	case PROP_PAGE_SELECTOR:
		g_value_set_object (value, gpd->s_page);
		break;
	case PROP_PRINT_CONFIG:
		g_value_set_object (value, gpd->config);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
gnome_print_dialog_class_init (GnomePrintDialogClass *class)
{
	GObjectClass *gobject_class = (GObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_DIALOG);

	gobject_class->set_property = gnome_print_dialog_set_property;
	gobject_class->get_property = gnome_print_dialog_get_property;
	gobject_class->finalize     = gnome_print_dialog_finalize;

	g_object_class_install_property (gobject_class, PROP_PRINT_CONFIG,
			g_param_spec_object ("print_config", "Print Config",
				"Printing Configuration to be used",
				GNOME_TYPE_PRINT_CONFIG, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PRINTER_SELECTOR,
			g_param_spec_object ("printer_selector", "Printer selector",
				"Printer selector", GNOME_TYPE_PRINTER_SELECTOR, G_PARAM_READABLE));
	g_object_class_install_property (gobject_class, PROP_NOTEBOOK,
			g_param_spec_object ("notebook", "Notebook", "Notebook",
				GTK_TYPE_NOTEBOOK, G_PARAM_READABLE));
	g_object_class_install_property (gobject_class, PROP_CONTENT_SELECTOR,
			g_param_spec_object ("content_selector", "Content selector",
				"Content selector", GNOME_TYPE_PRINT_CONTENT_SELECTOR,
				G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PAGE_SELECTOR,
			g_param_spec_object ("page_selector", "Page selector",
				"Page selector", GNOME_TYPE_PRINT_PAGE_SELECTOR,
				G_PARAM_READABLE));
	g_object_class_install_property (gobject_class, PROP_TITLE,
			g_param_spec_string ("title", "Title",
				"Title", _("Gnome Print Dialog"), G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_FLAGS,
			g_param_spec_int ("flags", "Flags", "Flags", -G_MAXINT, G_MAXINT, 0,
				G_PARAM_READWRITE));
}

static void
gpd_copies_set (GnomePrintCopiesSelector *gpc, gint copies,
		GnomePrintDialog *gpd)
{
	if (gpd->config)
		gnome_print_config_set_int (gpd->config,
				(const guchar *) GNOME_PRINT_KEY_NUM_COPIES, copies);
}

static void
gpd_collate_set (GnomePrintCopiesSelector *gpc, gboolean collate,
		GnomePrintDialog *gpd)
{
	if (gpd->config)
		gnome_print_config_set_boolean (gpd->config,
				(const guchar *) GNOME_PRINT_KEY_COLLATE, collate);
}

static void
gnome_print_dialog_response_cb (GtkDialog *dialog, gint response_id,
		GnomePrintDialog *gpd)
{
	if (response_id != GNOME_PRINT_DIALOG_RESPONSE_PRINT)
		return;

	if (!gnome_printer_selector_check_consistency
			(GNOME_PRINTER_SELECTOR (gpd->printer)))
		g_signal_stop_emission_by_name (dialog, "response");
}

static void
on_page_selector_notify (GObject *object, GParamSpec *pspec,
		GnomePrintDialog *gpd)
{
	GValue v = {0,};

	g_value_init (&v, pspec->value_type);
	if (!strcmp (pspec->name, "total")) {
		g_object_get_property (object, "total", &v);
		g_object_set_property (G_OBJECT (gpd->s_layout), "total", &v);
	}
	g_value_unset (&v);
}

static void
gnome_print_dialog_init (GnomePrintDialog *gpd)
{
	GtkWidget *vb, *l, *b;
	gchar *text;

	/* Set up the dialog */
	gtk_window_set_title (GTK_WINDOW (gpd), _("Gnome Print Dialog"));
	gtk_dialog_set_has_separator (GTK_DIALOG (gpd), FALSE);
	gtk_dialog_add_buttons (GTK_DIALOG (gpd),
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_PRINT, GNOME_PRINT_DIALOG_RESPONSE_PRINT,
			NULL);
	b = gtk_dialog_add_button (GTK_DIALOG (gpd),
			GTK_STOCK_PRINT_PREVIEW, GNOME_PRINT_DIALOG_RESPONSE_PREVIEW);
	gtk_button_box_set_child_secondary (
			GTK_BUTTON_BOX (GTK_DIALOG (gpd)->action_area), b, TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (gpd),
		GNOME_PRINT_DIALOG_RESPONSE_PRINT);
	g_signal_connect (gpd, "response",
		G_CALLBACK (gnome_print_dialog_response_cb), gpd);

	gpd->notebook = g_object_new (GTK_TYPE_NOTEBOOK, "border-width", 4, NULL);
	gtk_widget_show (gpd->notebook);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (gpd)->vbox), gpd->notebook);

	/* Job page */
	gpd->job = gtk_hbox_new (FALSE, 5);
	gtk_widget_show (gpd->job);
	gtk_container_set_border_width (GTK_CONTAINER (gpd->job), 4);
	gpd->l_job = gtk_label_new_with_mnemonic (_("Job"));
	gtk_widget_show (gpd->l_job);
	gtk_notebook_append_page (GTK_NOTEBOOK (gpd->notebook), gpd->job,
			gpd->l_job);
	vb = gtk_vbox_new (FALSE, PAD);
	gtk_widget_show (vb);
	gtk_box_pack_start (GTK_BOX (gpd->job), vb, FALSE, FALSE, 0);

	/* Content selector */
	gpd->e_content = g_object_new (GTK_TYPE_HBOX, NULL);
	gtk_box_pack_start (GTK_BOX (vb), gpd->e_content, FALSE, FALSE, 0);

	/* Print range, old API */
	gpd->e_range = gtk_frame_new ("");
	gtk_frame_set_shadow_type (GTK_FRAME (gpd->e_range), GTK_SHADOW_NONE);
	l = gtk_label_new ("");
	text = g_strdup_printf ("<b>%s</b>", _("Print Range"));
	gtk_label_set_markup (GTK_LABEL (l), text);
	g_object_set_data (G_OBJECT (gpd->e_range), "label", l);
	g_free (text);
	gtk_frame_set_label_widget (GTK_FRAME (gpd->e_range), l);
	gtk_widget_show (l);
	gtk_widget_hide (gpd->e_range);
	gtk_box_pack_start (GTK_BOX (vb), gpd->e_range, FALSE, FALSE, 0);
	g_object_set_data (G_OBJECT (gpd->job), "range", gpd->e_range);

	/* Print range, new API */
	gpd->s_page = g_object_new (GNOME_TYPE_PRINT_PAGE_SELECTOR, NULL);
	gtk_widget_show (gpd->s_page);
	gtk_box_pack_start (GTK_BOX (vb), gpd->s_page, FALSE, FALSE, 0);
	g_signal_connect (gpd->s_page, "notify", G_CALLBACK (on_page_selector_notify), gpd);

	/* Copies */
	gpd->s_copies = g_object_new (GNOME_TYPE_PRINT_COPIES_SELECTOR, NULL);
	g_signal_connect (G_OBJECT (gpd->s_copies), "copies_set", (GCallback) gpd_copies_set, gpd);
	g_signal_connect (G_OBJECT (gpd->s_copies), "collate_set", (GCallback) gpd_collate_set, gpd);
	gtk_widget_hide (gpd->s_copies);
	gtk_box_pack_start (GTK_BOX (vb), gpd->s_copies, FALSE, FALSE, 0);
	g_object_set_data (G_OBJECT (gpd->job), "copies", gpd->s_copies);

	/* Paper page */
	gpd->s_paper = g_object_new (GNOME_TYPE_PAPER_SELECTOR, NULL);
	gtk_container_set_border_width (GTK_CONTAINER (gpd->s_paper), 4);
	gtk_widget_show (gpd->s_paper);
	gpd->l_paper = gtk_label_new_with_mnemonic (_("Paper"));
	gtk_widget_show (gpd->l_paper);
	gtk_notebook_append_page (GTK_NOTEBOOK (gpd->notebook), gpd->s_paper,
			gpd->l_paper);
	g_signal_connect (G_OBJECT (gpd->s_paper), "notify",
			G_CALLBACK (on_paper_selector_notify), gpd);

	/* Layout page */
	gpd->l_layout = gtk_label_new_with_mnemonic (_("Layout"));
	gtk_widget_show (gpd->l_layout);
	gpd->s_layout = g_object_new (GNOME_TYPE_PRINT_LAYOUT_SELECTOR,
			"border-width", 4, NULL);
	gtk_widget_show (gpd->s_layout);
	gtk_notebook_append_page (GTK_NOTEBOOK (gpd->notebook), gpd->s_layout,
			gpd->l_layout);
}

static void
update_range_sensitivity (GtkWidget *button, 
			  GtkWidget *range)
{
	gtk_widget_set_sensitive (range, 
				  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)));
}

static GtkWidget *
gpd_create_range (gint flags, GtkWidget *range, const guchar *clabel, const guchar *rlabel)
{
	GtkWidget *t, *rb;
	GSList *group;
	gint row;

	t = gtk_table_new (4, 2, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (t), 6);

	group = NULL;
	row = 0;

	if (flags & GNOME_PRINT_RANGE_CURRENT) {
		rb = gtk_radio_button_new_with_mnemonic (group, (const gchar *) clabel);
		g_object_set_data (G_OBJECT (t), "current", rb);
		gtk_widget_show (rb);
		gtk_table_attach (GTK_TABLE (t), rb, 0, 1, row, row + 1, GTK_FILL | GTK_EXPAND, 
				  GTK_FILL, 0, 0);
		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (rb));
		row += 1;
	}

	if (flags & GNOME_PRINT_RANGE_ALL) {
		rb = gtk_radio_button_new_with_mnemonic(group, _("_All"));
		g_object_set_data (G_OBJECT (t), "all", rb);
		gtk_widget_show (rb);
		gtk_table_attach (GTK_TABLE (t), rb, 0, 1, row, row + 1, GTK_FILL | GTK_EXPAND, 
				  GTK_FILL, 0, 0);
		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (rb));
		row += 1;
	}

	if (flags & GNOME_PRINT_RANGE_RANGE) {
		rb = gtk_radio_button_new_with_mnemonic (group, (const gchar *) rlabel);
		g_object_set_data (G_OBJECT (t), "range", rb);
		gtk_widget_show (rb);
		gtk_table_attach (GTK_TABLE (t), rb, 0, 1, row, row + 1, GTK_FILL | GTK_EXPAND, 
				  GTK_FILL, 0, 0);
		g_object_set_data (G_OBJECT (t), "range-widget", range);
		gtk_table_attach (GTK_TABLE (t), range, 1, 2, row, row + 1, GTK_FILL, 0, 0, 0);
		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (rb));
		row += 1;
		g_signal_connect (rb, "toggled", G_CALLBACK (update_range_sensitivity), range);
		update_range_sensitivity (rb, range);
	}

	if ((flags & GNOME_PRINT_RANGE_SELECTION) || (flags & GNOME_PRINT_RANGE_SELECTION_UNSENSITIVE)) {
		rb = gtk_radio_button_new_with_mnemonic (group, _("_Selection"));
		g_object_set_data (G_OBJECT (t), "selection", rb);
		gtk_widget_show (rb);
		gtk_widget_set_sensitive (rb, !(flags & GNOME_PRINT_RANGE_SELECTION_UNSENSITIVE));
		gtk_table_attach (GTK_TABLE (t), rb, 0, 1, row, row + 1, GTK_FILL | GTK_EXPAND, 
				  GTK_FILL, 0, 0);
		group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (rb));
		row += 1;
	}

	return t;
}

/**
 * gnome_print_dialog_new:
 * @gpj: GnomePrintJob
 * @title: Title of window.
 * @flags: Options for created widget.
 * 
 * Create a new gnome-print-dialog window.
 *
 * The following options flags are available:
 * GNOME_PRINT_DIALOG_RANGE: A range widget container will be created.
 * A range widget must be created separately, using one of the
 * gnome_print_dialog_construct_* functions.
 * GNOME_PRINT_DIALOG_COPIES: A copies widget will be created.
 * 
 * Return value: A newly created and initialised widget.
 **/
GtkWidget *
gnome_print_dialog_new (GnomePrintJob *gpj, const guchar *title, gint flags)
{
	GnomePrintConfig *config;
	GnomePrintDialog *gpd;

	config = gnome_print_job_get_config (gpj);
	if (!config)
		config = gnome_print_config_default ();
	gpd = g_object_new (GNOME_TYPE_PRINT_DIALOG, "print-config", config,
			"title", title, "flags", flags, NULL);
	g_object_unref (G_OBJECT (config));

	return GTK_WIDGET (gpd);
}

/**
 * gnome_print_dialog_construct:
 * @gpd: A created GnomePrintDialog.
 * @title: Title of the window.
 * @flags: Initialisation options, see gnome_print_dialog_new().
 * 
 * Used for language bindings to post-initialise an object instantiation.
 *
 */
void
gnome_print_dialog_construct (GnomePrintDialog *gpd, const guchar *title, gint flags)
{
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));

	g_object_set (G_OBJECT (gpd), "title", title, "flags", flags, NULL);
}

/**
 * gnome_print_dialog_construct_range_custom:
 * @gpd: A GnomePrintDialog for which a range was requested.
 * @custom: A widget which will be placed in a "Range" frame in the
 * main display.
 * 
 * Install a custom range specification widget.
 **/
void
gnome_print_dialog_construct_range_custom (GnomePrintDialog *gpd, GtkWidget *custom)
{
	GtkWidget *f, *r;

	g_return_if_fail (gpd != NULL);
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));
	g_return_if_fail (custom != NULL);
	g_return_if_fail (GTK_IS_WIDGET (custom));

	gtk_widget_hide (gpd->s_page);

	f = g_object_get_data (G_OBJECT (gpd->job), "range");
	g_return_if_fail (f != NULL);
	r = g_object_get_data (G_OBJECT (f), "range");
	if (r)
		gtk_container_remove (GTK_CONTAINER (f), r);

	gtk_widget_show (custom);
	gtk_widget_show (gpd->job);
	gtk_container_add (GTK_CONTAINER (f), custom);
	g_object_set_data (G_OBJECT (f), "range", custom);
}

/**
 * gnome_print_dialog_construct_range_any:
 * @gpd: An initialise GnomePrintDialog, which can contain a range.
 * @flags: Options flags, which ranges are displayed.
 * @range_widget: Widget to display for the range option.
 * @currentlabel: Label to display next to the 'current page' button.
 * @rangelabel: Label to display next to the 'range' button.
 * 
 * Create a generic range area within the print range dialogue.  The flags
 * field contains a mask of which options you wish displayed:
 *
 * GNOME_PRINT_RANGE_CURRENT: A label @currentlabel will be displayed.
 * GNOME_PRINT_RANGE_ALL: A label "All" will be displayed.
 * GNOME_PRINT_RANGE_RANGE: A label @rangelabel will be displayed, next
 * to the range specification widget @range_widget.
 * GNOME_PRINT_RANGE_SELECTION: A label "Selection" will be displayed.
 * 
 **/
void
gnome_print_dialog_construct_range_any (GnomePrintDialog *gpd, gint flags, GtkWidget *range_widget,
					const guchar *currentlabel, const guchar *rangelabel)
{
	GtkWidget *f, *r, *l, *b;

	g_return_if_fail (gpd != NULL);
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));
	g_return_if_fail (!range_widget || GTK_IS_WIDGET (range_widget));
	g_return_if_fail (!(range_widget && !(flags & GNOME_PRINT_RANGE_RANGE)));
	g_return_if_fail (!(!range_widget && (flags & GNOME_PRINT_RANGE_RANGE)));
	g_return_if_fail (!((flags & GNOME_PRINT_RANGE_SELECTION) && (flags & GNOME_PRINT_RANGE_SELECTION_UNSENSITIVE)));

	gtk_widget_hide (gpd->s_page);

	f = g_object_get_data (G_OBJECT (gpd->job), "range");
	g_return_if_fail (f != NULL);
	r = g_object_get_data (G_OBJECT (f), "range");
	if (r)
		gtk_container_remove (GTK_CONTAINER (f), r);

	r = gpd_create_range (flags, range_widget, currentlabel, rangelabel);

	if (r) {
		gtk_widget_show (r);
		gtk_widget_show (gpd->job);
		gtk_container_add (GTK_CONTAINER (f), r);

		l = g_object_get_data (G_OBJECT (f), "label");
		b = g_object_get_data (G_OBJECT (r), "current");
		if (b !=NULL)
			gnome_print_set_atk_relation (l, GTK_WIDGET (b));
		b = g_object_get_data (G_OBJECT (r), "all");
		if (b !=NULL)
			gnome_print_set_atk_relation (l, GTK_WIDGET (b));
		b = g_object_get_data (G_OBJECT (r), "range");
		if (b !=NULL)
			gnome_print_set_atk_relation (l, GTK_WIDGET (b));
		b = g_object_get_data (G_OBJECT (r), "selection");
		if (b !=NULL)
			gnome_print_set_atk_relation (l, GTK_WIDGET (b));
	}

	g_object_set_data (G_OBJECT (f), "range", r);
}

/**
 * gnome_print_dialog_construct_range_page:
 * @gpd: An initialise GnomePrintDialog, which can contain a range.
 * @flags: Option flags.  See gnome_print_dialog_construct_any().
 * @start: First page which may be printed.
 * @end: Last page which may be printed.
 * @currentlabel: Label text for current option.
 * @rangelabel: Label text for range option.
 * 
 * Construct a generic page/sheet range area.
 **/
void
gnome_print_dialog_construct_range_page (GnomePrintDialog *gpd, gint flags, gint start, gint end,
					 const guchar *currentlabel, const guchar *rangelabel)
{
	GtkWidget *hbox = NULL;

	gtk_widget_hide (gpd->s_page);

	if (flags & GNOME_PRINT_RANGE_RANGE) {
		GtkWidget *l, *sb;
		GtkObject *a;
		AtkObject *atko;

		hbox = gtk_hbox_new (FALSE, 3);
		gtk_widget_show (hbox);

		l = gtk_label_new_with_mnemonic (_("_From:"));
		gtk_widget_show (l);
		gtk_box_pack_start (GTK_BOX (hbox), l, FALSE, FALSE, 0);

		a = gtk_adjustment_new (start, start, end, 1, 10, 10);
		g_object_set_data (G_OBJECT (hbox), "from", a);
		sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 1, 0.0);
		gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (sb), TRUE);
		gtk_widget_show (sb);
		gtk_box_pack_start (GTK_BOX (hbox), sb, FALSE, FALSE, 0);
		gtk_label_set_mnemonic_widget ((GtkLabel *) l, sb);

		atko = gtk_widget_get_accessible (sb);
		atk_object_set_description (atko, _("Sets the start of the range of pages to be printed"));

		l = gtk_label_new_with_mnemonic (_("_To:"));
		gtk_widget_show (l);
		gtk_box_pack_start (GTK_BOX (hbox), l, FALSE, FALSE, 0);

		a = gtk_adjustment_new (end, start, end, 1, 10, 10);
		g_object_set_data (G_OBJECT (hbox), "to", a);
		sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 1, 0.0);
		gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (sb), TRUE);
		gtk_widget_show (sb);
		gtk_box_pack_start (GTK_BOX (hbox), sb, FALSE, FALSE, 0);
		gtk_label_set_mnemonic_widget ((GtkLabel *) l, sb);

		atko = gtk_widget_get_accessible (sb);
		atk_object_set_description (atko, _("Sets the end of the range of pages to be printed"));
	}

	gnome_print_dialog_construct_range_any (gpd, flags, hbox, currentlabel, rangelabel);
}

/**
 * gnome_print_dialog_get_range:
 * @gpd: A GnomePrintDialog with a range display.
 * 
 * Return the range option selected by the user.  This is a bitmask
 * with only 1 bit set, out of:
 *
 * GNOME_PRINT_RANGE_CURRENT: The current option selected.
 * GNOME_PRINT_RANGE_ALL: The all option selected.
 * GNOME_PRINT_RANGE_RANGE The range option selected.
 * GNOME_PRINT_RANGE_SELECTION: The selection option selected.
 * 
 * Return value: A bitmask with one option set.
 **/
GnomePrintRangeType
gnome_print_dialog_get_range (GnomePrintDialog *gpd)
{
	GtkWidget *f, *r, *b;

	g_return_val_if_fail (gpd != NULL, 0);
	g_return_val_if_fail (GNOME_IS_PRINT_DIALOG (gpd), 0);

	f = g_object_get_data (G_OBJECT (gpd->job), "range");
	g_return_val_if_fail (f != NULL, 0);
	r = g_object_get_data (G_OBJECT (f), "range");
	g_return_val_if_fail (r != NULL, 0);

	b = g_object_get_data (G_OBJECT (r), "current");
	if (b && GTK_IS_TOGGLE_BUTTON (b) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (b))) 
		return GNOME_PRINT_RANGE_CURRENT;
	
	b = g_object_get_data (G_OBJECT (r), "all");
	if (b && GTK_IS_TOGGLE_BUTTON (b) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (b))) 
		return GNOME_PRINT_RANGE_ALL;

	b = g_object_get_data (G_OBJECT (r), "range");
	if (b && GTK_IS_TOGGLE_BUTTON (b) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (b))) 
		return GNOME_PRINT_RANGE_RANGE;
	
	b = g_object_get_data (G_OBJECT (r), "selection");
	if (b && GTK_IS_TOGGLE_BUTTON (b) && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (b))) 
		return GNOME_PRINT_RANGE_SELECTION;

	return 0;
}

/**
 * gnome_print_dialog_get_range_page:
 * @gpd: A GnomePrintDialog with a page range display.
 * @start: Return for the user-specified start page.
 * @end: Return for the user-specified end page.
 * 
 * Retrieves the user choice for range type and range, if the user
 * has requested a range of pages to print.
 * 
 * Return value: A bitmask with the user-selection set.  See
 * gnome_print_dialog_get_range().
 **/
gint
gnome_print_dialog_get_range_page (GnomePrintDialog *gpd, gint *start, gint *end)
{
	gint mask;

	g_return_val_if_fail (gpd != NULL, 0);
	g_return_val_if_fail (GNOME_IS_PRINT_DIALOG(gpd), 0);

	mask = gnome_print_dialog_get_range (gpd);

	if (mask & GNOME_PRINT_RANGE_RANGE) {
		GtkObject *f, *r, *w, *a;
		f = g_object_get_data (G_OBJECT (gpd->job), "range");
		g_return_val_if_fail (f != NULL, 0);
		r = g_object_get_data (G_OBJECT (f), "range");
		g_return_val_if_fail (r != NULL, 0);
		w = g_object_get_data (G_OBJECT (r), "range-widget");
		g_return_val_if_fail (w != NULL, 0);
		a = g_object_get_data (G_OBJECT (w), "from");
		g_return_val_if_fail (a && GTK_IS_ADJUSTMENT (a), 0);
		if (start)
			*start = (gint) gtk_adjustment_get_value (GTK_ADJUSTMENT (a));
		a = g_object_get_data (G_OBJECT (w), "to");
		g_return_val_if_fail (a && GTK_IS_ADJUSTMENT (a), 0);
		if (end)
			*end = (gint) gtk_adjustment_get_value (GTK_ADJUSTMENT (a));
	}

	return mask;
}

/**
 * gnome_print_dialog_get_copies:
 * @gpd: A GnomePrintDialog with a copies display.
 * @copies: Return for the number of copies.
 * @collate: Return for collation flag.
 * 
 * Retrieves the number of copies and collation indicator from
 * the print dialogue.  If the print dialogue does not have a
 * copies indicator, then a default of 1 copy is returned.
 **/
void
gnome_print_dialog_get_copies (GnomePrintDialog *gpd, gint *copies, gboolean *collate)
{
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));

	if (copies)
		*copies = gnome_print_copies_selector_get_copies (GNOME_PRINT_COPIES_SELECTOR (gpd->s_copies));
	if (collate)
		*collate = gnome_print_copies_selector_get_collate (GNOME_PRINT_COPIES_SELECTOR (gpd->s_copies));
}

/**
 * gnome_print_dialog_set_copies:
 * @gpd: A GnomePrintDialog with a copies display.
 * @copies: New number of copies.
 * @collate: New collation status.
 * 
 * Sets the print copies and collation status in the print dialogue.
 **/
void
gnome_print_dialog_set_copies (GnomePrintDialog *gpd, gint copies, gboolean collate)
{
	g_return_if_fail (GNOME_IS_PRINT_DIALOG (gpd));

	gnome_print_copies_selector_set_copies (
			GNOME_PRINT_COPIES_SELECTOR (gpd->s_copies), copies, collate);
}

/**
 * gnome_print_dialog_get_printer:
 * @gpd: An initialised GnomePrintDialog.
 * 
 * Retrieve the user-requested printer from the printer area of
 * the print dialogue.
 * 
 * Return value: The user-selected printer.
 **/
GnomePrintConfig *
gnome_print_dialog_get_config (GnomePrintDialog *gpd)
{
	g_return_val_if_fail (gpd != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_PRINT_DIALOG (gpd), NULL);

	return gnome_print_config_ref (gpd->config);
}


/**
 * gnome_print_dialog_run:
 * @gpd: 
 * 
 * Runs a gnome-print dialog. Use it instead of gtk_dialog_run
 * in the future this function will handle more stuff like opening
 * a file selector when printing to a file.
 *
 * Note: this routine does not destroy the dialog!
 * 
 * Return Value: the user response
 **/
gint
gnome_print_dialog_run (GnomePrintDialog const *gpd)
{
	gint response;

	response = gtk_dialog_run (GTK_DIALOG (gpd));

	return response;
}

/**
 * gnome_print_set_atk_relation:
 *
 * Sets Atk Relation
 *
 **/
void
gnome_print_set_atk_relation (GtkWidget *label, GtkWidget *widget)
{
	AtkRelationSet *relation_set;
	AtkRelation *relation;
	AtkObject *relation_targets[1];
	AtkObject *atk_widget, *atk_label;

	atk_label = gtk_widget_get_accessible (label);
	atk_widget = gtk_widget_get_accessible (widget);

	/* Add a LABEL_FOR relation from the label to the label_for
	widget. */

	relation_set = atk_object_ref_relation_set (atk_label);
	relation_targets[0] = atk_widget;
	relation = atk_relation_new (relation_targets, 1,
				     ATK_RELATION_LABEL_FOR);
	atk_relation_set_add (relation_set, relation);
	g_object_unref (G_OBJECT (relation));
	g_object_unref (G_OBJECT (relation_set));

	/* Add a LABELLED_BY relation from the mnemonic widget to the
	label. */

	relation_set = atk_object_ref_relation_set (atk_widget);
	relation_targets[0] = atk_label;
	relation = atk_relation_new (relation_targets, 1,
				     ATK_RELATION_LABELLED_BY);
	atk_relation_set_add (relation_set, relation);
	g_object_unref (G_OBJECT (relation));
	g_object_unref (G_OBJECT (relation_set));
}

