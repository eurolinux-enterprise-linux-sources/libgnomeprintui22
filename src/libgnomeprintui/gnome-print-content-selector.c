/*
 *  gnome-print-content-selector.c:
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
#include <libgnomeprintui/gnome-print-content-selector.h>
#include <libgnomeprintui/gnome-print-i18n.h>

#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/gnome-print-filter.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <string.h>

struct _GnomePrintContentSelectorPrivate {
	guint total, current;
};

static GtkFrameClass *parent_class = NULL;

enum {
	PROP_0,
	PROP_CURRENT,
	PROP_TOTAL
};

static void
gnome_print_content_selector_get_property (GObject *object, guint n, GValue *v,
		GParamSpec *cspec)
{
	GnomePrintContentSelector *cs = GNOME_PRINT_CONTENT_SELECTOR (object);

	switch (n) {
	case PROP_TOTAL:
		g_value_set_uint (v, cs->priv->total);
		break;
	case PROP_CURRENT:
		g_value_set_uint (v, cs->priv->current);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, cspec);
	}
}

static void
gnome_print_content_selector_set_property (GObject *object, guint n,
		const GValue *v, GParamSpec *cspec)
{
	GnomePrintContentSelector *cs = GNOME_PRINT_CONTENT_SELECTOR (object);

	switch (n) {
	case PROP_TOTAL:
		cs->priv->total = g_value_get_uint (v);
		break;
	case PROP_CURRENT:
		cs->priv->current = g_value_get_uint (v);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, cspec);
	}
}

static void
gnome_print_content_selector_finalize (GObject *object)
{
	GnomePrintContentSelector *cs = GNOME_PRINT_CONTENT_SELECTOR (object);

	if (cs->priv) {
		g_free (cs->priv);
		cs->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gnome_print_content_selector_class_init (GnomePrintContentSelectorClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;

	parent_class = g_type_class_peek_parent (klass);

	object_class->get_property = gnome_print_content_selector_get_property;
	object_class->set_property = gnome_print_content_selector_set_property;
	object_class->finalize     = gnome_print_content_selector_finalize;

	g_object_class_install_property (object_class, PROP_TOTAL,
			g_param_spec_uint ("total", _("Number of pages"), _("Number of pages"),
				0, G_MAXUINT, 0, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_CURRENT,
			g_param_spec_uint ("current", _("Current page"), _("Current page"),
				0, G_MAXUINT, 0, G_PARAM_READWRITE));
}

static void
gnome_print_content_selector_init (GnomePrintContentSelector *cs)
{
	gchar *s;

	cs->priv = g_new0 (GnomePrintContentSelectorPrivate, 1);

	/* To translators: 'Print Content' can be a specific sheet, line x to y... */
	s = g_strdup_printf ("<b>%s</b>", _("Print Content"));
	g_object_set (G_OBJECT (cs), "label", s,
			"shadow-type", GTK_SHADOW_NONE, NULL);
	g_free (s);
	g_object_set (G_OBJECT (GTK_FRAME (cs)->label_widget),
			"use-markup", TRUE, "use-underline", TRUE, NULL);
}

GType
gnome_print_content_selector_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePrintContentSelectorClass), NULL, NULL,
			(GClassInitFunc) gnome_print_content_selector_class_init,
			NULL, NULL, sizeof (GnomePrintContentSelector), 0,
			(GInstanceInitFunc) gnome_print_content_selector_init
		};
		type = g_type_register_static (GTK_TYPE_FRAME,
				"GnomePrintContentSelector", &info, 0);
	}
	return type;
}
