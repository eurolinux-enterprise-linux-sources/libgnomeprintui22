/*
 *  gnome-print-page-selector.c:
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

#include <libgnomeprintui/gnome-print-dialog.h>
#include <libgnomeprintui/gnome-print-page-selector.h>
#include <libgnomeprintui/gnome-print-i18n.h>

#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/gnome-print-filter.h>

#include <libgnomeprint/private/gpa-node.h>
#include <libgnomeprint/private/gpa-utils.h>
#include <libgnomeprint/private/gnome-print-private.h>
#include <libgnomeprint/private/gnome-print-config-private.h>

#include <gtk/gtk.h>

#include <gdk/gdkkeysyms.h>

#include <string.h>

struct _GnomePrintPageSelector {
	GtkFrame parent;

	GnomePrintFilter *filter;
	guint current, total;

	guint signal;

	GtkWidget *r_all, *r_selection, *r_current, *r_even, *r_odd;
	GtkWidget *e_selection;

	gboolean saving, loading;
};

struct _GnomePrintPageSelectorClass {
	GtkFrameClass parent_class;
};

static GtkFrameClass *parent_class = NULL;

enum {
	PROP_0,
	PROP_FILTER,
	PROP_CURRENT,
	PROP_TOTAL_IN,
	PROP_TOTAL_OUT
};

static gboolean
gnome_print_page_selector_load (GnomePrintPageSelector *ps,
		GnomePrintFilter *filter)
{
	guint first = 0, last = 0, skip;
	gboolean collect;
	GValueArray *pages = NULL;

	g_return_val_if_fail (GNOME_IS_PRINT_PAGE_SELECTOR (ps), FALSE);
	g_return_val_if_fail (GNOME_IS_PRINT_FILTER (filter), FALSE);

	if (ps->saving || ps->loading)
		return FALSE;

	if (strcmp ("GnomePrintFilterSelect",
				G_OBJECT_TYPE_NAME (G_OBJECT (filter))))
		return FALSE;
	g_object_get (G_OBJECT (filter), "first", &first, "last", &last,
			"skip", &skip, "collect", &collect, NULL);
	if (collect || (skip > 1))
		return FALSE;

	g_object_get (G_OBJECT (filter), "pages", &pages, NULL);
	gtk_widget_set_sensitive (GTK_WIDGET (ps), TRUE);
	ps->loading = TRUE;
	if (skip && (first <= 1) &&
			((last == G_MAXUINT) || ((ps->total) && (ps->total == last)))) {
		if (!first)
			g_object_set (G_OBJECT (ps->r_odd), "active", TRUE, NULL);
		else
			g_object_set (G_OBJECT (ps->r_even), "active", TRUE, NULL);
	} else if (pages) {
		guint i;
		gchar *s, *str = NULL;
		gboolean all_selected = TRUE;

		for (i = 0; i < pages->n_values; i++) {
			gboolean p, n, c;

			p = i ? g_value_get_boolean (g_value_array_get_nth (pages, i - 1)) : FALSE;
			n = (i + 1 < pages->n_values) ? g_value_get_boolean (g_value_array_get_nth (pages, i + 1)) : FALSE;
			c = g_value_get_boolean (g_value_array_get_nth (pages, i));
			all_selected &= c;

			if ((p && c && n) || (p && !c && !n) ||
					(!p && !c && n) || (!p && !c && !n) || (p && !c && n))
				continue;
			if (p && c && !n) {
				s = g_strdup_printf ("%s-%i", str, i + 1);
				g_free (str);
				str = s;
				continue;
			}
			if (p && !c && n) {
				s = g_strdup_printf ("%s,", str);
				g_free (str);
				str = s;
				continue;
			}
			if ((!p && c && n) || (!p && c && !n)) {
				s = g_strdup_printf ("%s%s%i", str ? str : "", str ? "," : "", i + 1);
				g_free (str);
				str = s;
				continue;
			}
		}
		if (ps->total && (ps->total == pages->n_values) && all_selected)
			g_object_set (G_OBJECT (ps->r_all), "active", TRUE, NULL);
		else
			g_object_set (G_OBJECT (ps->r_selection), "active", TRUE, NULL);
		gtk_entry_set_text (GTK_ENTRY (ps->e_selection), str ? str : "");
		if (str)
			g_free (str);
		g_value_array_free (pages);
	} else if ((first == 0) &&
			((last == G_MAXUINT) || ((ps->total) && (ps->total == last))))
		g_object_set (G_OBJECT (ps->r_all), "active", TRUE, NULL);
	else if (first || (last != G_MAXUINT)) {
		gchar *str;

		if (!first)
			str = g_strdup_printf ("-%i", last);
		else if (last == G_MAXUINT)
			str = g_strdup_printf ("%i-", first);
		else
			str = g_strdup_printf ("%i-%i", first, last);
		gtk_entry_set_text (GTK_ENTRY (ps->e_selection), str);
		g_free (str);
		g_object_set (G_OBJECT (ps->r_selection), "active", TRUE, NULL);
	} else if ((ps->current > 0) &&
			(first == ps->current - 1) &&
			(last == ps->current - 1))
		g_object_set (G_OBJECT (ps->r_current), "active", TRUE, NULL);
	else
		g_warning ("Implement!");
	ps->loading = FALSE;
	return TRUE;
}

typedef enum {
	STATE_BEGIN,
	STATE_NUMBER_FROM,
	STATE_NUMBER_FROM_TO,
	STATE_NUMBER_BEGIN_TO,
	STATE_BEGIN_TO,
	STATE_FROM_TO
} ParseState;

static GArray *
gnome_print_page_selector_get_array (GnomePrintPageSelector *ps)
{
	const gchar *s;
	guint i, n_from = 0, n_to = 0, m;
	ParseState state = STATE_BEGIN;
	gboolean err = FALSE;
	GArray *a;
	GdkColor color;

	g_return_val_if_fail (GNOME_IS_PRINT_PAGE_SELECTOR (ps), NULL);

	/* Total number of pages */
	m = ps->total ? ps->total : 1000;

	a = g_array_new (FALSE, TRUE, sizeof (gboolean));
	s = gtk_editable_get_chars (GTK_EDITABLE (ps->e_selection), 0, -1);
	for (i = 0; !err && (i < strlen (s)); i++) {
		if ((s[i] == '0') || (s[i] == '1') || (s[i] == '2') ||
		    (s[i] == '3') || (s[i] == '4') || (s[i] == '5') ||
		    (s[i] == '6') || (s[i] == '7') || (s[i] == '8') ||
		    (s[i] == '9')) {
			switch (state) {
			case STATE_BEGIN:
				state = STATE_NUMBER_FROM;
				n_from = (s[i]-'0');
				if (!n_from) err = TRUE;
				break;
			case STATE_NUMBER_FROM:
				n_from = n_from * 10 + (s[i]-'0');
				if (n_from > G_MAXUINT16) err = TRUE;
				break;
			case STATE_NUMBER_BEGIN_TO:
			case STATE_NUMBER_FROM_TO:
				n_to = n_to * 10 + (s[i]-'0');
				if (n_to > G_MAXUINT16) err = TRUE;
				break;
			case STATE_FROM_TO:
				state = STATE_NUMBER_FROM_TO;
				n_to = (s[i]-'0');
				if (!n_to) err = TRUE;
				break;
			case STATE_BEGIN_TO:
				state = STATE_NUMBER_BEGIN_TO;
				n_to = (s[i]-'0');
				if (!n_to) err = TRUE;
				break;
			}
		} else if (s[i] == '-') {
			switch (state) {
			case STATE_BEGIN:
				state = STATE_BEGIN_TO;
				break;
			case STATE_NUMBER_FROM_TO:
			case STATE_NUMBER_BEGIN_TO:
			case STATE_BEGIN_TO:
			case STATE_FROM_TO:
				err = TRUE;
				break;
			case STATE_NUMBER_FROM:
				state = STATE_FROM_TO;
				break;
			}
		} else if ((s[i] == ',') || (s[i] == ';')) {
			switch (state) {
			case STATE_BEGIN:
				break;
			case STATE_FROM_TO:
				if (a->len < m)
					g_array_set_size (a, m);
				for (i = n_from - 1; i < m; i++)
					g_array_index (a, gboolean, i) = TRUE;
				break;
			case STATE_BEGIN_TO:
				err = TRUE;
				break;
			case STATE_NUMBER_FROM:
				if (a->len < n_from)
					g_array_set_size (a, n_from);
				g_array_index (a, gboolean, n_from - 1) = TRUE;
				break;
			case STATE_NUMBER_FROM_TO:
				if (a->len < MAX (n_from, n_to))
					g_array_set_size (a, MAX (n_from, n_to));
				for (i = MIN (n_from, n_to) - 1; i < MAX (n_from, n_to); i++)
					g_array_index (a, gboolean, i) = TRUE;
				break;
			case STATE_NUMBER_BEGIN_TO:
				if (a->len < n_to)
					g_array_set_size (a, n_to);
				for (i = 0; i < n_to; i++)
					g_array_index (a, gboolean, i) = TRUE;
				break;
			}
			state = STATE_BEGIN;
		} else
			err = TRUE;
	}

	if (!err) switch (state) {
	case STATE_BEGIN:
		break;
	case STATE_BEGIN_TO:
		err = TRUE;
		break;
	case STATE_FROM_TO:
		if (a->len < m)
			g_array_set_size (a, m);
		for (i = n_from - 1; i < m; i++)
			g_array_index (a, gboolean, i) = TRUE;
		break;
	case STATE_NUMBER_FROM:
		if (a->len < n_from)
			g_array_set_size (a, n_from);
		g_array_index (a, gboolean, n_from - 1) = TRUE;
		break;
	case STATE_NUMBER_FROM_TO:
		if (a->len < MAX (n_from, n_to))
			g_array_set_size (a, MAX (n_from, n_to));
		for (i = MIN (n_from, n_to) - 1; i < MAX (n_from, n_to); i++)
			g_array_index (a, gboolean, i) = TRUE;
		break;
	case STATE_NUMBER_BEGIN_TO:
		if (a->len < n_to)
			g_array_set_size (a, n_to);
		for (i = 0; i < n_to; i++)
			g_array_index (a, gboolean, i) = TRUE;
		break;
	}

	if (err) {
		gdk_color_parse ("red", &color);
		gtk_widget_modify_text (ps->e_selection, GTK_STATE_NORMAL, &color);
	} else {
		gdk_color_parse ("black", &color);
		gtk_widget_modify_text (ps->e_selection, GTK_STATE_NORMAL, &color);
	}

	return a;
}

static void
gnome_print_page_selector_save (GnomePrintPageSelector *ps)
{
	g_return_if_fail (GNOME_IS_PRINT_PAGE_SELECTOR (ps));

	if (!ps->filter || ps->saving || ps->loading)
		return;

	ps->saving = TRUE;
	g_object_set (G_OBJECT (ps->filter), "first", 0, "last", G_MAXUINT,
			"skip", 0, "pages", NULL, NULL);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_even)))
		g_object_set (G_OBJECT (ps->filter), "first", 1, "skip", 1, NULL);
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_odd)))
		g_object_set (G_OBJECT (ps->filter), "skip", 1, NULL);
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_current)))
		g_object_set (G_OBJECT (ps->filter),
				"first", ps->current - 1, "last", ps->current - 1, NULL);
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_selection))) {
		GArray *a = gnome_print_page_selector_get_array (ps);
		GValueArray *va = NULL;
		if (a) {
			GValue v = {0,};
			guint i;

			g_value_init (&v, G_TYPE_BOOLEAN);
			va = g_value_array_new (a->len);
			for (i = 0; i < a->len; i++) {
				g_value_set_boolean (&v, g_array_index (a, gboolean, i));
				g_value_array_append (va, &v);
			}
			g_array_free (a, TRUE);
			g_value_unset (&v);
		}
		g_object_set (G_OBJECT (ps->filter), "pages", va, NULL);
	}
	ps->saving = FALSE;
}

static guint
gnome_print_page_selector_count_pages (GnomePrintPageSelector *ps)
{
	g_return_val_if_fail (GNOME_IS_PRINT_PAGE_SELECTOR (ps), 0);

	if (!ps->total)
		return 0;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_all)))
		return ps->total;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_even)))
		return ps->total / 2;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_odd)))
		return (ps->total + 1) / 2;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_selection))) {
		GArray *a = gnome_print_page_selector_get_array (ps);
		guint i, n = 0;

		for (i = 0; i < a->len; i++) if (g_array_index (a, gboolean, i)) n++;
		g_array_free (a, TRUE);
		return n;
	}
	return 0;
}

static void
gnome_print_page_selector_get_property (GObject *object, guint n, GValue *v,
		GParamSpec *pspec)
{
	GnomePrintPageSelector *ps = GNOME_PRINT_PAGE_SELECTOR (object);

	switch (n) {
	case PROP_FILTER:
		g_value_set_object (v, ps->filter);
		break;
	case PROP_CURRENT:
		g_value_set_uint (v, ps->current);
		break;
	case PROP_TOTAL_IN:
		g_value_set_uint (v, ps->total);
		break;
	case PROP_TOTAL_OUT:
		g_value_set_uint (v, gnome_print_page_selector_count_pages (ps));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
on_filter_notify (GObject *object, GParamSpec *pspec,
		GnomePrintPageSelector *ps)
{
	gnome_print_page_selector_load (ps, GNOME_PRINT_FILTER (object));
}

static void
gnome_print_page_selector_set_property (GObject *object, guint n,
		const GValue *v, GParamSpec *pspec)
{
	GnomePrintPageSelector *ps = GNOME_PRINT_PAGE_SELECTOR (object);

	switch (n) {
	case PROP_FILTER:
		if (gnome_print_page_selector_load (ps, g_value_get_object (v))) {
			if (ps->filter) {
				g_signal_handler_disconnect (G_OBJECT (ps->filter), ps->signal);
				g_object_unref (G_OBJECT (ps->filter));
			}
			ps->filter = g_value_get_object (v);
			g_object_ref (G_OBJECT (ps->filter));
			ps->signal = g_signal_connect (G_OBJECT (ps->filter), "notify",
					G_CALLBACK (on_filter_notify), ps);
		}
		break;
	case PROP_CURRENT:
		ps->current = g_value_get_uint (v);
		if (ps->current)
			gtk_widget_show (ps->r_current);
		else
			gtk_widget_hide (ps->r_current);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_print_page_selector_finalize (GObject *object)
{
	GnomePrintPageSelector *ps = GNOME_PRINT_PAGE_SELECTOR (object);

	if (ps->filter) {
		g_signal_handler_disconnect (G_OBJECT (ps->filter), ps->signal);
		g_object_unref (G_OBJECT (ps->filter));
		ps->filter = NULL;
	}

        G_OBJECT_CLASS (parent_class)->finalize (object);

}

struct {
	GParamSpec parent_instance;
} GnomePrintPageSelectorParamFilter;

static void
param_filter_set_default (GParamSpec *pspec, GValue *v)
{
	GnomePrintFilter *f;

	f = gnome_print_filter_new_from_description ("GnomePrintFilterSelect", NULL);
	g_value_set_object (v, f);
	g_object_unref (G_OBJECT (f));
}

static GType
gnome_print_page_selector_param_filter_get_type (void)
{
	static GType type;
	if (G_UNLIKELY (type) == 0) {
		static const GParamSpecTypeInfo pspec_info = {
			sizeof (GnomePrintPageSelectorParamFilter), 0, NULL,
			G_TYPE_OBJECT, NULL, param_filter_set_default, NULL, NULL};
		type = g_param_type_register_static ("GnomePrintPageSelectorParamFilter",
				&pspec_info);
	}
	return type;
}

static void
gnome_print_page_selector_class_init (GnomePrintPageSelectorClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	GParamSpec *ps;

	parent_class = g_type_class_peek_parent (klass);

	object_class->get_property = gnome_print_page_selector_get_property;
	object_class->set_property = gnome_print_page_selector_set_property;
	object_class->finalize     = gnome_print_page_selector_finalize;

	ps = g_param_spec_internal (
			gnome_print_page_selector_param_filter_get_type (),
			"filter", _("Filter"), _("Filter"), G_PARAM_READWRITE);
	ps->value_type = GNOME_TYPE_PRINT_FILTER;
	g_object_class_install_property (object_class, PROP_FILTER, ps);
	g_object_class_install_property (object_class, PROP_CURRENT,
			g_param_spec_uint ("current", _("Current page"), _("Current page"),
				0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_TOTAL_IN,
			g_param_spec_uint ("total_in", _("Number of pages to select from"),
				_("Number of pages to select from"),
				0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_TOTAL_OUT,
			g_param_spec_uint ("total_out", _("Number of selected pages"),
				_("Number of selected pages"),
				0, G_MAXUINT, 0, G_PARAM_READABLE));
}

static void
on_button_toggled (GtkToggleButton *b, GnomePrintPageSelector *ps)
{
	if (b->active)
		gnome_print_page_selector_save (ps);
}

static void
on_selection_changed (GtkEditable *editable, GnomePrintPageSelector *ps)
{
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_selection)))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ps->r_selection), TRUE);
	gnome_print_page_selector_save (ps);
}

static gboolean
on_selection_key_press_event (GtkWidget *widget, GdkEventKey *event,
		GnomePrintPageSelector *ps)
{
	switch (event->keyval) {
	case GDK_0:
	case GDK_1:
	case GDK_2:
	case GDK_3:
	case GDK_4:
	case GDK_5:
	case GDK_6:
	case GDK_7:
	case GDK_8:
	case GDK_9:
	case GDK_minus:
	case GDK_comma:
	case GDK_semicolon:
	case GDK_Left:
	case GDK_Right:
	case GDK_Return:
	case GDK_Delete:
	case GDK_BackSpace:
		return FALSE;
	default:
		return TRUE;
	}
}

static void
on_selection_focus_in_event (GtkWidget *widget, GdkEventFocus *event,
		GnomePrintPageSelector *ps)
{
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ps->r_selection)))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ps->r_selection), TRUE);
}

static void
gnome_print_page_selector_init (GnomePrintPageSelector *ps)
{
	GtkWidget *hb, *vb, *mhb;
	gchar *s;

	ps->filter = gnome_print_filter_new_from_description (
			"GnomePrintFilterSelect", NULL);
	ps->signal = g_signal_connect (G_OBJECT (ps->filter), "notify",
			G_CALLBACK (on_filter_notify), ps);

	s = g_strdup_printf ("<b>%s</b>", _("Print Range"));
	g_object_set (G_OBJECT (ps), "label", s,
			"shadow-type", GTK_SHADOW_NONE, NULL);
	g_free (s);
	g_object_set (G_OBJECT (GTK_FRAME (ps)->label_widget),
			"use-markup", TRUE, "use-underline", TRUE, NULL);

	mhb = g_object_new (GTK_TYPE_HBOX, "spacing", 5, NULL);
	gtk_widget_show (mhb);
	gtk_container_add (GTK_CONTAINER (ps), mhb);

	vb = g_object_new (GTK_TYPE_VBOX, "spacing", 5, NULL);
	gtk_widget_show (vb);
	gtk_box_pack_start (GTK_BOX (mhb), vb, FALSE, FALSE, 0);

	ps->r_all = g_object_new (GTK_TYPE_RADIO_BUTTON, "label", _("_All pages"),
			"use-underline", TRUE, "active", TRUE, NULL);
	gtk_widget_show (ps->r_all);
	gtk_box_pack_start (GTK_BOX (vb), ps->r_all, FALSE, FALSE, 0);

	ps->r_even = gtk_radio_button_new_with_mnemonic_from_widget (
			GTK_RADIO_BUTTON (ps->r_all), _("_Even pages"));
	gtk_widget_show (ps->r_even);
	gtk_box_pack_start (GTK_BOX (vb), ps->r_even, FALSE, FALSE, 0);
	ps->r_odd = gtk_radio_button_new_with_mnemonic_from_widget (
			GTK_RADIO_BUTTON (ps->r_all), _("_Odd pages"));
	gtk_widget_show (ps->r_odd);
	gtk_box_pack_start (GTK_BOX (vb), ps->r_odd, FALSE, FALSE, 0);
										
	/*
	 * Current page. We don't show that widget unless we know
	 * the current page.
	 */
	ps->r_current = gtk_radio_button_new_with_mnemonic_from_widget (
			GTK_RADIO_BUTTON (ps->r_all), _("_Current page"));
	gtk_box_pack_start (GTK_BOX (vb), ps->r_current, FALSE, FALSE, 0);

	/* Custom page selection */
	hb = g_object_new (GTK_TYPE_HBOX, NULL);
	gtk_widget_show (hb);
	gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	ps->r_selection= g_object_new (GTK_TYPE_RADIO_BUTTON, "group", ps->r_all,
			"label", _("_Page range: "), "use-underline", TRUE, NULL);
	gtk_widget_show (ps->r_selection);
	gtk_box_pack_start (GTK_BOX (hb), ps->r_selection, FALSE, FALSE, 0);
	ps->e_selection = g_object_new (GTK_TYPE_ENTRY, "text", "1-", NULL);
	gtk_widget_show (ps->e_selection);
	gtk_box_pack_start (GTK_BOX (hb), ps->e_selection, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (ps->e_selection), "focus_in_event",
			G_CALLBACK (on_selection_focus_in_event), ps);

	/* Connect some signals */
	g_signal_connect (G_OBJECT (ps->r_all), "toggled", G_CALLBACK (on_button_toggled), ps);
	g_signal_connect (G_OBJECT (ps->r_current), "toggled", G_CALLBACK (on_button_toggled), ps);
	g_signal_connect (G_OBJECT (ps->r_even), "toggled", G_CALLBACK (on_button_toggled), ps);
	g_signal_connect (G_OBJECT (ps->r_odd), "toggled", G_CALLBACK (on_button_toggled), ps);
	g_signal_connect (G_OBJECT (ps->r_selection), "toggled", G_CALLBACK (on_button_toggled), ps);
	g_signal_connect (G_OBJECT (ps->e_selection), "changed", G_CALLBACK (on_selection_changed), ps);
	g_signal_connect (G_OBJECT (ps->e_selection), "key_press_event", G_CALLBACK (on_selection_key_press_event), ps);

	gnome_print_set_atk_relation (GTK_FRAME (ps)->label_widget, ps->r_all);
	gnome_print_set_atk_relation (GTK_FRAME (ps)->label_widget, ps->r_current);
	gnome_print_set_atk_relation (GTK_FRAME (ps)->label_widget, ps->r_selection);
	gnome_print_set_atk_relation (GTK_FRAME (ps)->label_widget, ps->r_even);
	gnome_print_set_atk_relation (GTK_FRAME (ps)->label_widget, ps->r_odd);
}

GType
gnome_print_page_selector_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePrintPageSelectorClass), NULL, NULL,
			(GClassInitFunc) gnome_print_page_selector_class_init,
			NULL, NULL, sizeof (GnomePrintPageSelector), 0,
			(GInstanceInitFunc) gnome_print_page_selector_init
		};
		type = g_type_register_static (GTK_TYPE_FRAME,
				"GnomePrintPageSelector", &info, 0);
	}
	return type;
}
