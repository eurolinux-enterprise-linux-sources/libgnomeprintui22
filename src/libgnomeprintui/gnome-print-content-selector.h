/*
 *  gnome-print-content-selector.h:
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
#ifndef __GNOME_PRINT_CONTENT_SELECTOR_H__
#define __GNOME_PRINT_CONTENT_SELECTOR_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GNOME_TYPE_PRINT_CONTENT_SELECTOR (gnome_print_content_selector_get_type ())
#define GNOME_PRINT_CONTENT_SELECTOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_PRINT_CONTENT_SELECTOR, GnomePrintContentSelector))
#define GNOME_PRINT_CONTENT_SELECTOR_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNOME_TYPE_PRINT_CONTENT_SELECTOR, GnomePrintContentSelectorClass))
#define GNOME_IS_PRINT_CONTENT_SELECTOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_PRINT_CONTENT_SELECTOR))
#define GNOME_IS_PRINT_CONTENT_SELECTOR_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_PRINT_CONTENT_SELECTOR))
#define GNOME_PRINT_CONTENT_SELECTOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GNOME_TYPE_PRINT_CONTENT_SELECTOR, GnomePrintContentSelectorClass))

typedef struct _GnomePrintContentSelector GnomePrintContentSelector;
typedef struct _GnomePrintContentSelectorPrivate GnomePrintContentSelectorPrivate;
typedef struct _GnomePrintContentSelectorClass GnomePrintContentSelectorClass;

struct _GnomePrintContentSelector {
	GtkFrame parent;

	GnomePrintContentSelectorPrivate *priv;
};

struct _GnomePrintContentSelectorClass {
	GtkFrameClass parent_class;
};

GType gnome_print_content_selector_get_type (void);

G_END_DECLS

#endif /* __GNOME_PRINT_CONTENT_SELECTOR_H__ */
