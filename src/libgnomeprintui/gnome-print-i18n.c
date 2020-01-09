/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  gnome-print-i18n.c:
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
 *    Zbigniew Chyla
 *
 *  Copyright (C) Authors
 *
 */

#include <config.h>
#include <glib.h>
#include "gnome-print-i18n.h"
#include "gnome-print-ui-private.h"

char *
libgnomeprintui_gettext (const char *msgid)
{
	static gboolean initialized = FALSE;

	if (!initialized) {
		bindtextdomain (GETTEXT_PACKAGE, gnome_printui_locale_dir);
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
		initialized = TRUE;
	}        

	return dgettext (GETTEXT_PACKAGE, msgid);
}

