/*
 *  gnome-print-private.c: Private variables of the library
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
 *    Ivan, Wong Yat Cheung <email@ivanwong.info>
 *
 *  Copyright (C) 2005 Novell Inc. and authors
 *
 */

#include <config.h>
#include <glib.h>
#include "gnome-print-ui-private.h"

gchar *gnome_printui_locale_dir = GNOMELOCALEDIR;
gchar *gnome_printui_job_preview_data_dir = GNOME_PRINT_JOB_PREVIEW_DATADIR;

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL APIENTRY DllMain (HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		{
			gchar *gnome_printui_prefix, dll_name[MAX_PATH];

			GetModuleFileName (hModule, dll_name, MAX_PATH);
			gnome_printui_prefix = g_win32_get_package_installation_directory (NULL, 
				dll_name);
			if (gnome_printui_prefix) {
				gnome_printui_locale_dir = g_build_filename (
					gnome_printui_prefix, "share", "locale", NULL);
				gnome_printui_job_preview_data_dir = g_build_filename (
					gnome_printui_prefix, "share", "libgnomeprintui", VERSION, NULL);
				g_free (gnome_printui_prefix);
			}

			break;
		}
		case DLL_PROCESS_DETACH:
			g_free (gnome_printui_locale_dir);
			g_free (gnome_printui_job_preview_data_dir);

			break;
	}

	return TRUE;
}
#endif
