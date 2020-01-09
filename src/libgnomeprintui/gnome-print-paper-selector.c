/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  gnome-print-paper-selector.c: A paper selector widget
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
 *    James Henstridge <james@daa.com.au>
 *
 *  Copyright (C) 1998 James Henstridge <james@daa.com.au>
 *
 */

#undef GTK_DISABLE_DEPRECATED

#include <config.h>

#include <string.h>
#include <math.h>
#include <atk/atk.h>
#include <gtk/gtk.h>
#include <libgnomeprint/gnome-print-paper.h>
#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/private/gpa-node.h>
#include <libgnomeprint/private/gpa-utils.h>
#include <libgnomeprint/private/gnome-print-private.h>
#include <libgnomeprint/private/gnome-print-config-private.h>

#include "gnome-print-i18n.h"
#include "gnome-print-dialog.h"
#include "gnome-print-paper-selector.h"
#include "gnome-print-unit-selector.h"

/* This two have to go away */
#include "gnome-print-paper-preview.h"
#include "gpaui/gpa-paper-preview-item.h"

#include "gpaui/gpa-option-menu.h"
#include "gpaui/gpa-spinbutton.h"

#define MM(v) ((v) * 72.0 / 25.4)
#define CM(v) ((v) * 72.0 / 2.54)
#define M(v)  ((v) * 72.0 / 0.0254)
#define DEFAULT_WIDTH  (210. * 72. / 25.4)
#define DEFAULT_HEIGHT (297. * 72. / 25.4)

#define PAD 6

/*
 * GnomePaperSelector widget
 */

struct _GnomePaperSelector {
	GtkHBox box;

	GnomePrintConfig *config;

	GtkWidget *t_paper, *t_margins, *preview;

	GtkWidget *l_paper, *l_porient, *l_forient, *l_source, *l_width, *l_height;
	GtkWidget *f_preview, *f_margins;

	GtkWidget *pmenu, *pomenu, *lomenu, *lymenu, *trmenu;
	GnomePrintUnitSelector *us;

	gdouble mt, mb, ml, mr; /* Values for margins   */
	gdouble pw, ph;         /* Values for page size */
	gboolean rotate;

	struct {GPASpinbutton *t, *b, *l, *r;} m; /* Spins for margins   */
	struct {GPASpinbutton *w, *h;} p;         /* Spins for page size */

	guint handler_printer, handler_unit;
};

struct _GnomePaperSelectorClass {
	GtkHBoxClass parent_class;
};

static void gnome_paper_selector_class_init (GnomePaperSelectorClass *klass);
static void gnome_paper_selector_init (GnomePaperSelector *selector);
static void gnome_paper_selector_finalize (GObject *object);

static GtkHBoxClass *selector_parent_class;

enum {
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_CONFIG
};

GType
gnome_paper_selector_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePaperSelectorClass),
			NULL, NULL,
			(GClassInitFunc) gnome_paper_selector_class_init,
			NULL, NULL,
			sizeof (GnomePaperSelector),
			0,
			(GInstanceInitFunc) gnome_paper_selector_init
		};
		type = g_type_register_static (GTK_TYPE_HBOX, "GnomePaperSelector", &info, 0);
	}
	
	return type;
}

static void
gnome_paper_selector_get_property (GObject *object, guint n, GValue *v,
								GParamSpec *pspec)
{
	GnomePaperSelector *ps = GNOME_PAPER_SELECTOR (object);

	switch (n) {
	case PROP_WIDTH:
		g_value_set_double (v, ps->rotate ? ps->ph : ps->pw);
		break;
	case PROP_HEIGHT:
		g_value_set_double (v, ps->rotate ? ps->pw : ps->ph);
		break;
	case PROP_CONFIG:
		g_value_set_object (v, ps->config);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_print_paper_selector_update_spin_units (GnomePaperSelector *ps)
{
	const GnomePrintUnit *unit;

	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	unit = gnome_print_unit_selector_get_unit (ps->us);
	if (!unit) return;

	gpa_spinbutton_set_unit (ps->m.t, (const gchar *) unit->abbr);
	gpa_spinbutton_set_unit (ps->m.b, (const gchar *) unit->abbr);
	gpa_spinbutton_set_unit (ps->m.r, (const gchar *) unit->abbr);
	gpa_spinbutton_set_unit (ps->m.l, (const gchar *) unit->abbr);
	gpa_spinbutton_set_unit (ps->p.h, (const gchar *) unit->abbr);
	gpa_spinbutton_set_unit (ps->p.w, (const gchar *) unit->abbr);
}

static void
gnome_paper_selector_unit_changed_cb (GnomePrintUnitSelector *sel,
	GnomePaperSelector *ps)
{
	const GnomePrintUnit *unit = gnome_print_unit_selector_get_unit (sel);

	if (unit)
		gnome_print_config_set (ps->config,
			(const guchar *) GNOME_PRINT_KEY_PREFERED_UNIT, unit->abbr);
	gnome_print_paper_selector_update_spin_units (ps);
}

static void
gnome_paper_unit_selector_request_update_cb (GPANode *node, guint flags,
								GnomePaperSelector *ps)
{
	guchar *unit_txt;

	unit_txt = gnome_print_config_get (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PREFERED_UNIT);
	if (!unit_txt) {
		g_warning ("Could not get GNOME_PRINT_KEY_PREFERED_UNIT");
		return;
	}

	gnome_print_unit_selector_set_unit (ps->us,
		gnome_print_unit_get_by_abbreviation (unit_txt));
	g_free (unit_txt);
	gnome_print_paper_selector_update_spin_units (ps);
}

static void
gnome_paper_selector_stop_monitoring (GnomePaperSelector *ps)
{
	if (ps->handler_unit) {
		g_signal_handler_disconnect (G_OBJECT (
			gnome_print_config_get_node (ps->config)), ps->handler_unit);
		ps->handler_unit = 0;
	}
	if (ps->handler_unit) {
		g_signal_handler_disconnect (G_OBJECT (
			gpa_node_get_child_from_path (GNOME_PRINT_CONFIG_NODE (ps->config),
				(const guchar *) "Printer")), ps->handler_unit);
		ps->handler_unit = 0;
	}
}

static void
gnome_paper_selector_update_spin_limits (GnomePaperSelector *ps)
{
	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	ps->m.t->upper = ps->ph - ps->mb; gpa_spinbutton_update (ps->m.t);
	ps->m.b->upper = ps->ph - ps->mt; gpa_spinbutton_update (ps->m.b);
	ps->m.r->upper = ps->pw - ps->ml; gpa_spinbutton_update (ps->m.r);
	ps->m.l->upper = ps->pw - ps->mr; gpa_spinbutton_update (ps->m.l);
}

static void
gnome_paper_selector_set_width (GnomePaperSelector *ps, gdouble d)
{
	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	if (fabs (ps->pw - d) < 0.1)
		return;
	ps->pw = d;
	g_object_notify (G_OBJECT (ps), ps->rotate ? "height" : "width");
}

static void
gnome_paper_selector_set_height (GnomePaperSelector *ps, gdouble d)
{
	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	if (fabs (ps->ph - d) < 0.1)
		return;
	ps->ph = d;
	g_object_notify (G_OBJECT (ps), ps->rotate ? "width" : "height");
}

static void
gnome_paper_selector_load_paper_size (GnomePaperSelector *ps)
{
	gchar *id;
	gdouble d;
	const GnomePrintUnit *unit;

	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	gnome_print_config_get_length (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_WIDTH, &d, &unit);
	gnome_print_convert_distance (&d, unit, GNOME_PRINT_PS_UNIT);
	gnome_paper_selector_set_width (ps, d);
	gnome_print_config_get_length (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_HEIGHT, &d, &unit);
	gnome_print_convert_distance (&d, unit, GNOME_PRINT_PS_UNIT);
	gnome_paper_selector_set_height (ps, d);

	id = (gchar *) gnome_print_config_get (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_SIZE);
	if (id && !strcmp (id, "Custom")) {
		gtk_widget_set_sensitive (GTK_WIDGET (ps->p.w), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (ps->p.h), TRUE);
	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (ps->p.w), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (ps->p.h), FALSE);
	}
	if (id)
		g_free (id);
	gnome_paper_selector_update_spin_limits (ps);
}

static void
gnome_paper_selector_load_orientation (GnomePaperSelector *ps)
{
	gchar *id;

	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	id = (gchar *) gnome_print_config_get (ps->config,
		(const guchar *) GNOME_PRINT_KEY_ORIENTATION);
	if (id && (!strcmp (id, "R90") || !strcmp (id, "R270")) && !ps->rotate) {
		ps->rotate = TRUE;
		g_object_notify (G_OBJECT (ps), "width");
		g_object_notify (G_OBJECT (ps), "height");
	} else if (!id || (strcmp (id, "R90") && strcmp (id, "R270")) || ps->rotate) {
		ps->rotate = FALSE;
		g_object_notify (G_OBJECT (ps), "width");
		g_object_notify (G_OBJECT (ps), "height");
	}
}

static void
paper_size_modified_cb (GtkOptionMenu *menu, GnomePaperSelector *ps)
{
	gnome_paper_selector_load_paper_size (ps);
}

static void
orientation_modified_cb (GtkOptionMenu *menu, GnomePaperSelector *ps)
{
	gnome_paper_selector_load_orientation (ps);
}

static void
gnome_paper_selector_printer_changed_cb (GPANode *node, guint flags,
	GnomePaperSelector *ps)
{
	gnome_paper_selector_load_paper_size (ps);
}

static void
gps_width_value_changed (GtkAdjustment *adj, GnomePaperSelector *ps)
{
	gnome_paper_selector_set_width (ps, adj->value);
	gnome_paper_selector_update_spin_limits (ps);
}

static void
gps_height_value_changed (GtkAdjustment *adj, GnomePaperSelector *ps)
{
	gnome_paper_selector_set_height (ps, adj->value);
	gnome_paper_selector_update_spin_limits (ps);
}

static GpaPaperPreviewItem *
gps_get_preview (GnomePaperSelector *ps)
{
	return GPA_PAPER_PREVIEW_ITEM (GNOME_PAPER_PREVIEW (ps->preview)->item);
}

static void
gps_m_size_value_changed (GtkAdjustment *adj, GnomePaperSelector *ps)
{
	/* Did the value really change? */
	if ((fabs (ps->mt - ps->m.t->value) < 0.1) &&
	    (fabs (ps->mb - ps->m.b->value) < 0.1) &&
			(fabs (ps->ml - ps->m.l->value) < 0.1) &&
			(fabs (ps->mr - ps->m.r->value) < 0.1))
		return;
	ps->ml = ps->m.l->value;
	ps->mr = ps->m.r->value;
	ps->mt = ps->m.t->value;
	ps->mb = ps->m.b->value;

	gpa_paper_preview_item_set_logical_margins (gps_get_preview (ps),
		ps->ml, ps->mr, ps->mt, ps->mb);
	gnome_paper_selector_update_spin_limits (ps);
}

static gboolean
lmargin_top_unit_activated (GtkSpinButton *spin_button, GdkEventFocus *event,
								GnomePaperSelector *ps)
{
	gpa_paper_preview_item_set_lm_highlights (gps_get_preview (ps),
		TRUE, FALSE, FALSE, FALSE);
	return FALSE;
}

static gboolean
lmargin_bottom_unit_activated (GtkSpinButton *spin_button,
	GdkEventFocus *event, GnomePaperSelector *ps)
{
	gpa_paper_preview_item_set_lm_highlights (gps_get_preview (ps),
		FALSE, TRUE, FALSE, FALSE);
	return FALSE;
}

static gboolean
lmargin_right_unit_activated (GtkSpinButton *spin_button,
	GdkEventFocus *event, GnomePaperSelector *ps)
{
	gpa_paper_preview_item_set_lm_highlights (gps_get_preview (ps),
		FALSE, FALSE, FALSE, TRUE);
	return FALSE;
}

static gboolean
lmargin_unit_deactivated (GtkSpinButton *spin_button, GdkEventFocus *event,
	GnomePaperSelector *ps)
{
	gpa_paper_preview_item_set_lm_highlights (gps_get_preview (ps),
		FALSE, FALSE, FALSE, FALSE);
	return FALSE;
}

static gboolean
lmargin_left_unit_activated (GtkSpinButton *spin_button, GdkEventFocus *event,
	GnomePaperSelector *ps)
{
	gpa_paper_preview_item_set_lm_highlights (gps_get_preview (ps),
		FALSE, FALSE, TRUE, FALSE);
	return FALSE;
}

static void
gnome_paper_selector_set_config (GnomePaperSelector *ps,
								GnomePrintConfig *config)
{
	GtkWidget *s;
	AtkObject *atko;

	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));
	g_return_if_fail (!config || GNOME_IS_PRINT_CONFIG (config));

	gnome_paper_selector_stop_monitoring (ps);
	if (ps->pmenu) {
		gtk_object_destroy (GTK_OBJECT (ps->pmenu));
		ps->pmenu = NULL;
	}
	if (ps->pomenu) {
		gtk_object_destroy (GTK_OBJECT (ps->pomenu));
		ps->pomenu = NULL;
	}
	if (ps->lomenu) {
		gtk_object_destroy (GTK_OBJECT (ps->lomenu));
		ps->lomenu = NULL;
	}
	if (ps->trmenu) {
		gtk_object_destroy (GTK_OBJECT (ps->trmenu));
		ps->trmenu = NULL;
	}
	if (ps->p.w) {
		gtk_object_destroy (GTK_OBJECT (ps->p.w));
		ps->p.w = NULL;
	}
	if (ps->p.h) {
		gtk_object_destroy (GTK_OBJECT (ps->p.h));
		ps->p.h = NULL;
	}
	if (ps->m.t) {
		gtk_object_destroy (GTK_OBJECT (ps->m.t));
		ps->m.t = NULL;
	}
	if (ps->m.b) {
		gtk_object_destroy (GTK_OBJECT (ps->m.b));
		ps->m.b = NULL;
	}
	if (ps->m.r) {
		gtk_object_destroy (GTK_OBJECT (ps->m.r));
		ps->m.r = NULL;
	}
	if (ps->m.l) {
		gtk_object_destroy (GTK_OBJECT (ps->m.l));
		ps->m.l = NULL;
	}
	if (ps->preview) {
		gtk_object_destroy (GTK_OBJECT (ps->preview));
		ps->preview = NULL;
	}

	if (ps->config)
		g_object_unref (G_OBJECT (ps->config));
	ps->config = config;
	if (!ps->config)
		return;
	g_object_ref (G_OBJECT (ps->config));

	/* Load current values */
	gnome_print_config_get_length (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_LEFT, &ps->ml, NULL);
	gnome_print_config_get_length (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT, &ps->mr, NULL);
	gnome_print_config_get_length (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_TOP, &ps->mt, NULL);
	gnome_print_config_get_length (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM, &ps->mb, NULL);

	ps->pmenu = gpa_option_menu_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_SIZE);
	gtk_widget_show (ps->pmenu);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->pmenu, 1, 4, 0, 1);
	gnome_print_set_atk_relation (ps->l_paper,
		GTK_WIDGET (GPA_OPTION_MENU (ps->pmenu)->menu));
	gtk_label_set_mnemonic_widget (GTK_LABEL (ps->l_paper),
		GPA_OPTION_MENU (ps->pmenu)->menu);
	g_signal_connect (G_OBJECT (GPA_OPTION_MENU (ps->pmenu)->menu),
		"changed", G_CALLBACK (paper_size_modified_cb), ps);

	ps->pomenu = gpa_option_menu_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_ORIENTATION);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->pomenu, 1, 4, 3, 4);
	gnome_print_set_atk_relation (ps->l_forient,
		GTK_WIDGET (GPA_OPTION_MENU (ps->pomenu)->menu));
	gtk_label_set_mnemonic_widget (GTK_LABEL (ps->l_forient),
		GPA_OPTION_MENU (ps->pomenu)->menu);

	ps->lomenu = gpa_option_menu_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_ORIENTATION);
	gtk_widget_show (ps->lomenu);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->lomenu, 1, 4, 4, 5);
	gnome_print_set_atk_relation (ps->l_porient,
		GTK_WIDGET (GPA_OPTION_MENU (ps->lomenu)->menu));
	gtk_label_set_mnemonic_widget (GTK_LABEL (ps->l_porient),
		GPA_OPTION_MENU (ps->lomenu)->menu);
	g_signal_connect (G_OBJECT (GPA_OPTION_MENU (ps->lomenu)->menu),
		"changed", G_CALLBACK (orientation_modified_cb), ps);

	ps->trmenu = gpa_option_menu_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_SOURCE);
	gtk_widget_show(ps->trmenu);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->trmenu, 1, 4, 6, 7);
	gnome_print_set_atk_relation (ps->l_source,
		GTK_WIDGET (GPA_OPTION_MENU (ps->trmenu)->menu));
	gtk_label_set_mnemonic_widget (GTK_LABEL (ps->l_source),
		GPA_OPTION_MENU (ps->trmenu)->menu);

	s = gpa_spinbutton_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_WIDTH,
		0.0001, 10000, 1, 10, 10, 1, 2);
	ps->p.w = GPA_SPINBUTTON (s);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (ps->p.w->spinbutton), TRUE);
	gtk_widget_show (s);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), s, 2, 3, 1, 2);
	gnome_print_set_atk_relation (ps->l_width, GTK_WIDGET (ps->p.w->spinbutton));
	gtk_label_set_mnemonic_widget (GTK_LABEL (ps->l_width), ps->p.w->spinbutton);
	s = gpa_spinbutton_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAPER_HEIGHT,
		0.0001, 10000, 1, 10, 10, 1, 2);
	ps->p.h = GPA_SPINBUTTON (s);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (ps->p.h->spinbutton), TRUE);
	gtk_widget_show (s);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), s, 2, 3, 2, 3);
	gnome_print_set_atk_relation (ps->l_height, GTK_WIDGET (ps->p.h->spinbutton));
	gtk_label_set_mnemonic_widget (GTK_LABEL (ps->l_height), ps->p.h->spinbutton);

	s = gpa_spinbutton_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_TOP, 0., ps->ph,
		1., 10., 10., 1., 2.);
	gtk_widget_show (s);
	ps->m.t = GPA_SPINBUTTON (s);
	gtk_table_attach (GTK_TABLE (ps->t_margins), s,
		0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
	s = gpa_spinbutton_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM, 0., ps->ph,
		1, 10, 10, 1., 2.);
	gtk_widget_show (s);
	ps->m.b = GPA_SPINBUTTON (s);
	gtk_table_attach (GTK_TABLE (ps->t_margins), s,
		0, 1, 7, 8,  GTK_FILL, GTK_FILL, 0, 0);
	s = gpa_spinbutton_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_LEFT, 0., ps->pw,
		1, 10, 10, 1., 2.);
	gtk_widget_show (s);
	ps->m.l = GPA_SPINBUTTON (s);
	gtk_table_attach (GTK_TABLE (ps->t_margins), s,
		0, 1, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
	s = gpa_spinbutton_new (ps->config,
		(const guchar *) GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT, 0., ps->pw,
		1, 10, 10, 1., 2.);
	gtk_widget_show (s);
	ps->m.r = GPA_SPINBUTTON (s);
	gtk_table_attach (GTK_TABLE (ps->t_margins), s,
		0, 1, 5, 6, GTK_FILL, GTK_FILL, 0, 0);
	g_signal_connect (
		G_OBJECT (GTK_SPIN_BUTTON (ps->p.w->spinbutton)->adjustment),
		"value_changed", G_CALLBACK (gps_width_value_changed), ps);
	g_signal_connect (
		G_OBJECT (GTK_SPIN_BUTTON (ps->p.h->spinbutton)->adjustment),
		"value_changed", G_CALLBACK (gps_height_value_changed), ps);
	g_signal_connect (
		G_OBJECT (GTK_SPIN_BUTTON (ps->m.t->spinbutton)->adjustment),
		"value_changed", G_CALLBACK (gps_m_size_value_changed), ps);
	g_signal_connect (
		G_OBJECT (GTK_SPIN_BUTTON (ps->m.b->spinbutton)->adjustment),
		"value_changed", G_CALLBACK (gps_m_size_value_changed), ps);
	g_signal_connect (
		G_OBJECT (GTK_SPIN_BUTTON (ps->m.l->spinbutton)->adjustment),
		"value_changed", G_CALLBACK (gps_m_size_value_changed), ps);
	g_signal_connect (
		G_OBJECT (GTK_SPIN_BUTTON (ps->m.r->spinbutton)->adjustment),
		"value_changed", G_CALLBACK (gps_m_size_value_changed), ps);
	g_signal_connect (G_OBJECT (ps->m.t), "focus_in_event",
		G_CALLBACK (lmargin_top_unit_activated), ps);
	g_signal_connect (G_OBJECT (ps->m.t->spinbutton), "focus_out_event",
		G_CALLBACK (lmargin_unit_deactivated), ps);
	g_signal_connect (G_OBJECT (ps->m.l->spinbutton), "focus_in_event",
		G_CALLBACK (lmargin_left_unit_activated), ps);
	g_signal_connect (G_OBJECT (ps->m.l->spinbutton), "focus_out_event",
		G_CALLBACK (lmargin_unit_deactivated), ps);
	g_signal_connect (G_OBJECT (ps->m.r->spinbutton), "focus_in_event",
		G_CALLBACK (lmargin_right_unit_activated), ps);
	g_signal_connect (G_OBJECT (ps->m.r->spinbutton), "focus_out_event",
		G_CALLBACK (lmargin_unit_deactivated), ps);
	g_signal_connect (G_OBJECT (ps->m.b->spinbutton), "focus_in_event",
		G_CALLBACK (lmargin_bottom_unit_activated), ps);
	g_signal_connect (G_OBJECT (ps->m.b->spinbutton), "focus_out_event",
		G_CALLBACK (lmargin_unit_deactivated), ps);

	ps->preview = gnome_paper_preview_new (ps->config);
	gtk_widget_set_size_request (ps->preview, 160, 160);
	gtk_widget_show (ps->preview);
	gtk_container_add (GTK_CONTAINER (ps->f_preview), ps->preview);
	atko = gtk_widget_get_accessible (ps->preview);
	atk_object_set_name (atko, _("Preview"));
	atk_object_set_description (atko, _("Preview of the page size, "
		"orientation and layout"));

	gnome_paper_selector_load_orientation (ps);
	gnome_paper_selector_load_paper_size (ps);

	/* Set up the unit selector */
	gnome_paper_unit_selector_request_update_cb (NULL, 0, ps);
	g_signal_connect (G_OBJECT (ps->us), "modified",
		G_CALLBACK (gnome_paper_selector_unit_changed_cb), ps);

	/* Watch out for changes */
	ps->handler_printer = g_signal_connect (G_OBJECT (
		gpa_node_get_child_from_path (GNOME_PRINT_CONFIG_NODE (ps->config),
			(const guchar *) "Printer")),
		"modified", G_CALLBACK (gnome_paper_selector_printer_changed_cb), ps);
	ps->handler_unit = g_signal_connect (G_OBJECT (
		gnome_print_config_get_node (ps->config)),
		"modified", G_CALLBACK (gnome_paper_unit_selector_request_update_cb), ps);
}

static void
gnome_paper_selector_set_property (GObject *object, guint n, const GValue *v,
								GParamSpec *pspec)
{
	GnomePaperSelector *ps = GNOME_PAPER_SELECTOR (object);

	switch (n) {
	case PROP_CONFIG:
		gnome_paper_selector_set_config (ps, g_value_get_object (v));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_paper_selector_class_init (GnomePaperSelectorClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS (klass);
	widget_class = GTK_WIDGET_CLASS (klass);

	selector_parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = gnome_paper_selector_finalize;
	object_class->get_property = gnome_paper_selector_get_property;
	object_class->set_property = gnome_paper_selector_set_property;

	g_object_class_install_property (object_class, PROP_WIDTH,
		g_param_spec_double ("width", _("Width"), _("Width"),
			1., G_MAXDOUBLE, DEFAULT_WIDTH, G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_HEIGHT,
		g_param_spec_double ("height", _("Height"), _("Height"),
			1., G_MAXDOUBLE, DEFAULT_HEIGHT, G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_CONFIG,
		g_param_spec_object ("config", _("Configuration"), _("Configuration"),
			GNOME_TYPE_PRINT_CONFIG, G_PARAM_READWRITE));
}

static void
gnome_paper_selector_init (GnomePaperSelector *ps)
{
	GtkWidget *vb, *f, *label, *s, *l;
	gchar *text;
	AtkObject *atko;

	gtk_box_set_spacing (GTK_BOX (ps), PAD);

	/* VBox for controls */
	vb = gtk_vbox_new (FALSE, PAD);
	gtk_widget_show (vb);
	gtk_box_pack_start (GTK_BOX (ps), vb, FALSE, FALSE, 0);

	/* Create frame for selection menus */
	f = gtk_frame_new ("");
	gtk_frame_set_shadow_type (GTK_FRAME (f), GTK_SHADOW_NONE);
	label = gtk_label_new ("");
	text = g_strdup_printf ("<b>%s</b>", _("Paper"));
	gtk_label_set_markup (GTK_LABEL (label), text);
	g_free (text);
	gtk_frame_set_label_widget (GTK_FRAME (f), label);
	gtk_widget_show (label);
	gtk_widget_show (f);
	gtk_box_pack_start (GTK_BOX (vb), f, FALSE, FALSE, 0);

	/* Create table for packing menus */
	ps->t_paper = gtk_table_new (4, 6, FALSE);
	gtk_widget_show (ps->t_paper);
	gtk_container_set_border_width (GTK_CONTAINER (ps->t_paper), PAD);
	gtk_table_set_row_spacings (GTK_TABLE (ps->t_paper), 2);
	gtk_table_set_col_spacings (GTK_TABLE (ps->t_paper), 4);
	gtk_container_add (GTK_CONTAINER (f), ps->t_paper);

	/* Paper size selector */
	ps->l_paper = gtk_label_new_with_mnemonic (_("Paper _size:"));
	gtk_widget_show (ps->l_paper);
	gtk_misc_set_alignment (GTK_MISC (ps->l_paper), 1.0, 0.5);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->l_paper, 0, 1, 0, 1);

	ps->l_width = gtk_label_new_with_mnemonic (_("_Width:"));
	gtk_widget_show (ps->l_width);
	gtk_misc_set_alignment (GTK_MISC (ps->l_width), 1.0, 0.5);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->l_width, 1, 2, 1, 2);
	ps->l_height = gtk_label_new_with_mnemonic (_("_Height:"));
	gtk_widget_show (ps->l_height);
	gtk_misc_set_alignment (GTK_MISC (ps->l_height), 1.0, 0.5);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->l_height, 1, 2, 2, 3);

	/* Custom paper Unit selector */
	s = gnome_print_unit_selector_new (GNOME_PRINT_UNIT_ABSOLUTE);
	gtk_widget_show (s);
	ps->us = GNOME_PRINT_UNIT_SELECTOR (s);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), s, 3, 4, 1, 2);
	atko = gtk_widget_get_accessible (s);
	atk_object_set_name (atko, _("Metric selector"));
	atk_object_set_description (atko,
		_("Specifies the metric to use when setting "
		  "the width and height of the paper"));

	/* Feed orientation */
	ps->l_forient = gtk_label_new_with_mnemonic (_("_Feed orientation:"));
	gtk_misc_set_alignment (GTK_MISC (ps->l_forient), 1.0, 0.5);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->l_forient, 0, 1, 3, 4);

	/* Page orientation */
	ps->l_porient = gtk_label_new_with_mnemonic (_("Page _orientation:"));
	gtk_widget_show (ps->l_porient);
	gtk_misc_set_alignment (GTK_MISC (ps->l_porient), 1.0, 0.5);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->l_porient, 0, 1, 4, 5);

	/* Paper source */
	ps->l_source = gtk_label_new_with_mnemonic (_("Paper _tray:"));
	gtk_widget_show (ps->l_source);
	gtk_misc_set_alignment (GTK_MISC (ps->l_source), 1.0, 0.5);
	gtk_table_attach_defaults (GTK_TABLE (ps->t_paper), ps->l_source, 0, 1, 6, 7);

	/* Preview frame */
	ps->f_preview = gtk_frame_new ("");
	gtk_frame_set_shadow_type (GTK_FRAME (ps->f_preview), GTK_SHADOW_NONE);
	l = gtk_label_new ("");
	text = g_strdup_printf ("<b>%s</b>", _("Preview"));
	gtk_label_set_markup (GTK_LABEL (l), text);
	g_free (text);
	gtk_frame_set_label_widget (GTK_FRAME (ps->f_preview), l);
	gtk_widget_show (l);
	gtk_widget_show (ps->f_preview);
	gtk_box_pack_start (GTK_BOX (ps), ps->f_preview, TRUE, TRUE, 0);

	/* Margins */
	ps->f_margins = gtk_frame_new ("");
	gtk_frame_set_shadow_type (GTK_FRAME (ps->f_margins), GTK_SHADOW_NONE);
	gtk_box_pack_start (GTK_BOX (ps), ps->f_margins, FALSE, FALSE, 0);
	l = gtk_label_new ("");
	text = g_strdup_printf ("<b>%s</b>", _("Margins"));
	gtk_label_set_markup (GTK_LABEL (l), text);
	g_free (text);
	gtk_frame_set_label_widget (GTK_FRAME (ps->f_margins), l);
	gtk_widget_show (l);
	ps->t_margins = gtk_table_new (8, 1, TRUE);
	gtk_widget_show (ps->t_margins);
	gtk_container_set_border_width (GTK_CONTAINER (ps->t_margins), 4);
	gtk_container_add (GTK_CONTAINER (ps->f_margins), ps->t_margins);
	l = gtk_label_new (_("Top"));
	gtk_widget_show (l);
	gtk_table_attach (GTK_TABLE (ps->t_margins), l,
		0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	l = gtk_label_new (_("Bottom"));
	gtk_widget_show (l);
	gtk_table_attach (GTK_TABLE (ps->t_margins), l,
		0, 1, 6, 7, GTK_FILL, GTK_FILL, 0, 0);
	l = gtk_label_new (_("Left"));
	gtk_widget_show (l);
	gtk_table_attach (GTK_TABLE (ps->t_margins), l,
		0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
	l = gtk_label_new (_("Right"));
	gtk_widget_show (l);
	gtk_table_attach (GTK_TABLE (ps->t_margins), l,
		0, 1, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
}

static void
gnome_paper_selector_finalize (GObject *object)
{
	GnomePaperSelector *ps = GNOME_PAPER_SELECTOR (object);

	gnome_paper_selector_stop_monitoring (ps);
	if (ps->config) {
		g_object_unref (G_OBJECT (ps->config));
		ps->config = NULL;
	}

	G_OBJECT_CLASS (selector_parent_class)->finalize (object);
}

static void
gnome_paper_selector_set_flags (GnomePaperSelector *ps, gint flags)
{
	g_return_if_fail (GNOME_IS_PAPER_SELECTOR (ps));

	if (flags && GNOME_PAPER_SELECTOR_MARGINS)
		gtk_widget_show (GTK_WIDGET (ps->f_margins));
	else
		gtk_widget_hide (GTK_WIDGET (ps->f_margins));

	if (flags && GNOME_PAPER_SELECTOR_FEED_ORIENTATION) {
		gtk_widget_show (GTK_WIDGET (ps->l_forient));
		gtk_widget_show (GTK_WIDGET (ps->pomenu));
	} else {
		gtk_widget_hide (GTK_WIDGET (ps->l_forient));
		gtk_widget_hide (GTK_WIDGET (ps->pomenu));
	}
}

GtkWidget *
gnome_paper_selector_new_with_flags (GnomePrintConfig *config, gint flags)
{
	GnomePrintConfig *c = config ? config : gnome_print_config_default ();
	GnomePaperSelector *ps;

	ps = g_object_new (GNOME_TYPE_PAPER_SELECTOR, "config", c, NULL);
	if (!config)
		g_object_unref (G_OBJECT (c));

	gnome_paper_selector_set_flags (ps, flags);

	return GTK_WIDGET (ps);
}

GtkWidget *
gnome_paper_selector_new (GnomePrintConfig *config)
{
	return gnome_paper_selector_new_with_flags (config, 0);
}
