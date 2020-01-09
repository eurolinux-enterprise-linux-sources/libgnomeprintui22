/*
 *  gnome-print-page-selector.h:
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
#ifndef __GNOME_PRINT_PAGE_SELECTOR_H__
#define __GNOME_PRINT_PAGE_SELECTOR_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GNOME_TYPE_PRINT_PAGE_SELECTOR (gnome_print_page_selector_get_type ())
#define GNOME_PRINT_PAGE_SELECTOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_PRINT_PAGE_SELECTOR, GnomePrintPageSelector))
#define GNOME_PRINT_PAGE_SELECTOR_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNOME_TYPE_PRINT_PAGE_SELECTOR, GnomePrintPageSelectorClass))
#define GNOME_IS_PRINT_PAGE_SELECTOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_PRINT_PAGE_SELECTOR))
#define GNOME_IS_PRINT_PAGE_SELECTOR_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_PRINT_PAGE_SELECTOR))
#define GNOME_PRINT_PAGE_SELECTOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GNOME_TYPE_PRINT_PAGE_SELECTOR, GnomePrintPageSelectorClass))

typedef struct _GnomePrintPageSelector GnomePrintPageSelector;
typedef struct _GnomePrintPageSelectorClass GnomePrintPageSelectorClass;

GType gnome_print_page_selector_get_type (void);

G_END_DECLS

#endif /* __GNOME_PRINT_PAGE_SELECTOR_H__ */
