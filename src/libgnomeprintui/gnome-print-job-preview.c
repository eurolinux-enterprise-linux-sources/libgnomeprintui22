/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  gnome-print-job-preview.c: print preview window
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
 *    Miguel de Icaza <miguel@gnu.org>
 *    Lauris Kaplinski <lauris@ximian.com>
 *
 *  Copyright (C) 2000-2002 Ximian Inc.
 *
 */

#undef GTK_DISABLE_DEPRECATED

#include <config.h>

#include <math.h>
#include <string.h>
#include <libart_lgpl/art_affine.h>
#include <atk/atk.h>
#include <gdk/gdkkeysyms.h>

#include <gtk/gtk.h>

#include <libgnomeprint/private/gnome-print-private.h>
#include <libgnomeprint/gnome-print-meta.h>
#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/private/gpa-node.h>
#include <libgnomeprint/private/gnome-print-config-private.h>

#include <libgnomeprintui/gnome-print-i18n.h>
#include <libgnomeprintui/gnome-print-job-preview.h>
#include <libgnomeprintui/gnome-print-ui-private.h>

#define GPP_COLOR_RGBA(color, ALPHA) \
	((guint32) (ALPHA              | \
	 (((color).red   / 256) << 24) | \
	 (((color).green / 256) << 16) | \
	 (((color).blue  / 256) <<  8)))

#define GPMP_ZOOM_IN_FACTOR M_SQRT2
#define GPMP_ZOOM_OUT_FACTOR M_SQRT1_2
#define GPMP_ZOOM_MIN 0.0625
#define GPMP_ZOOM_MAX 16.0

#ifndef GTK_STOCK_EDIT
#define GTK_STOCK_EDIT "gtk-edit"
#endif

typedef struct {
	GnomeCanvasItem *page_fg, *page_bg, *group;
	GnomePrintPreview *preview;
	guint n;
} GnomePrintJobPreviewPage;

typedef enum {
	GNOME_PRINT_JOB_PREVIEW_STATE_NORMAL = 0,
	GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING,
	GNOME_PRINT_JOB_PREVIEW_STATE_EDITING
} GnomePrintJobPreviewState;

typedef enum {
	GNOME_PRINT_JOB_PREVIEW_CMD_INSERT,
	GNOME_PRINT_JOB_PREVIEW_CMD_MOVE,
	GNOME_PRINT_JOB_PREVIEW_CMD_DELETE
} GnomePrintJobPreviewCmd;

typedef struct {
	GArray *selection;
	GnomePrintMeta *meta;
	guint position;
} GnomePrintJobPreviewCmdInsert;

typedef struct {
	GArray *selection;
	guint position;
} GnomePrintJobPreviewCmdMove;

typedef struct {
	GArray *selection;
	GnomePrintMeta *meta;
} GnomePrintJobPreviewCmdDelete;

typedef struct {
	GnomePrintJobPreviewCmd cmd;
	union {
		GnomePrintJobPreviewCmdInsert insert;
		GnomePrintJobPreviewCmdMove   move;
		GnomePrintJobPreviewCmdDelete delete;
	} params;
} GnomePrintJobPreviewCmdData;

typedef enum {
	GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_NONE          = 0,
	GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY  = 1 << 1,
	GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP = 1 << 2
} GnomePrintJobPreviewPointerType;

struct _GnomePrintJobPreview {
	GtkWindow window;
	GtkWidget *scrolled_window;

	/* User interface */
	GtkUIManager *ui_manager;
	struct { GtkActionGroup *group; GtkAction *print, *close; } main;
	struct { GtkActionGroup *group; GtkAction *undo, *redo; } u_r;
	struct { GtkActionGroup *group; GtkAction *cut, *copy, *paste; } ccp;
	struct { GtkActionGroup *group; GtkAction *f, *p, *n, *l; } nav;
	struct { GtkActionGroup *group; GtkAction *z1, *zf, *zi, *zo; } zoom;
	struct {
		GtkActionGroup *group;
		GtkToggleAction *edit, *theme;
		GtkAction *multi;
	} other;

	/* Zoom factor */
	gdouble zoom_factor;

	/* Physical area dimensions */
	gdouble paw, pah;
	/* Calculated Physical Area -> Layout */
	gdouble pa2ly[6];

	/* State */
	GnomePrintJobPreviewState state;
	gint anchorx, anchory;
	gint offsetx, offsety;

	/* Our GnomePrintJob */
	GnomePrintJob *job;
	gulong notify_id;

	GPANode *node_paper_size, *node_page_orient;
	gulong handler_paper_size, handler_page_orient;

	GtkWidget *page_entry;
	GtkWidget *last;
	GnomeCanvas *canvas;
	GnomePrintConfig *config;

	guint current_page, current_offset;

	/* Strict theme compliance [#96802] */
	gboolean theme_compliance;

	/* Number of pages displayed together */
	gboolean nx_auto, ny_auto;
	gulong nx, ny;

	GArray *pages;

	/* Undo, redo */
	GArray *undo;
	GArray *redo;

	/* Drag & drop and clipboard */
	GnomePrintJobPreviewPointerType pointer_t;
	GnomeCanvasItem *pointer_l, *pointer_r;
	GArray *selection;
	GnomePrintContext *clipboard;
	GdkEvent *event;
};

struct _GnomePrintJobPreviewClass {
	GtkWindowClass parent_class;
};

static void gnome_print_job_preview_get_targets (GnomePrintJobPreview *jp,
						 GtkTargetEntry      **t,
						 guint                *nt);
static void target_entries_free (GtkTargetEntry *t, guint nt);

static void
gnome_print_job_preview_close (GnomePrintJobPreview *jp)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	/* Store the clipboard before we quit. */
	if (jp->clipboard) {
		GdkDisplay *d;
		GtkClipboard *c;
		GtkTargetEntry *t = NULL;
		guint nt = 0;

		d = gtk_widget_get_display (GTK_WIDGET (jp));
		c = gtk_clipboard_get_for_display (d, GDK_SELECTION_CLIPBOARD);
		gnome_print_job_preview_get_targets (jp, &t, &nt);
		gtk_clipboard_set_can_store (c, t, nt);
		target_entries_free (t, nt);
		gtk_clipboard_store (c);
	}
	gtk_widget_destroy (GTK_WIDGET (jp));
}

static guint
gnome_print_job_preview_count_selected (GnomePrintJobPreview *jp)
{
	guint i, n;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), 0);

	for (n = i = 0; i < jp->selection->len; i++)
		if (g_array_index (jp->selection, gboolean, i)) n++;
	return n;
}

static gboolean
gnome_print_job_preview_has_next_screen (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewPage p;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), FALSE);

	p = g_array_index (jp->pages, GnomePrintJobPreviewPage, 0);
	return (p.n + jp->nx * jp->ny < jp->selection->len);
}

static gboolean
gnome_print_job_preview_has_previous_screen (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewPage p;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), FALSE);

	p = g_array_index (jp->pages, GnomePrintJobPreviewPage, 0);
	return (p.n > 0);
}

static void
gnome_print_job_preview_clear_undo_redo (GnomePrintJobPreview *jp,
					 gboolean undo)
{
	GArray *a;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	a = undo ? jp->undo : jp->redo;
	while (a->len) {
		GnomePrintJobPreviewCmdData cd;
		
		cd = g_array_index (a, GnomePrintJobPreviewCmdData, 0);
		switch (cd.cmd) {
		case GNOME_PRINT_JOB_PREVIEW_CMD_INSERT:
			g_object_unref (G_OBJECT (cd.params.insert.meta));
			g_array_free (cd.params.insert.selection, TRUE);
			break;
		case GNOME_PRINT_JOB_PREVIEW_CMD_DELETE:
			g_object_unref (G_OBJECT (cd.params.delete.meta));
			g_array_free (cd.params.delete.selection, TRUE);
			break;
		case GNOME_PRINT_JOB_PREVIEW_CMD_MOVE:
			g_array_free (cd.params.move.selection, TRUE);
			break;
		default:
			break;
		}
		g_array_remove_index (a, 0);
	}
	g_object_set (G_OBJECT (undo ? jp->u_r.undo : jp->u_r.redo), "sensitive", FALSE, NULL);
}

static void
gnome_print_job_preview_clear_redo (GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_clear_undo_redo (jp, FALSE);
}

static void
gnome_print_job_preview_clear_undo (GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_clear_undo_redo (jp, TRUE);
}

static GtkTargetEntry target_table[] = {
	{ "GNOME_PRINT_META", 0, 0 }
};

/*
 * Padding in points around the simulated page
 */

#define PAGE_PAD 4

static void
gnome_print_job_preview_update_pointer (GnomePrintJobPreview *jp, guint n)
{
	guint col, row;
	gdouble px, py, dx, dy;
	GnomePrintJobPreviewPage p;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->nx == 0 || jp->ny == 0)
		return;

	n = MIN (jp->selection->len, n);
	p = g_array_index (jp->pages, GnomePrintJobPreviewPage, 0);
	col = (n - p.n) % jp->nx;
	row = (n - p.n) / jp->nx;

	/* Pointer open to the right */
	gnome_canvas_item_raise_to_top (jp->pointer_r);
	if ((n == jp->selection->len) || (row == jp->ny)) {
		gnome_canvas_item_hide (jp->pointer_r);
	} else {
		g_object_get (jp->pointer_r, "x", &px, "y", &py, NULL);
		dx = col * (jp->paw + PAGE_PAD * 2) - px;
		dy = row * (jp->pah + PAGE_PAD * 2) - py;
		gnome_canvas_item_move (jp->pointer_r, dx, dy);
		gnome_canvas_item_show (jp->pointer_r);
	}

	/* Pointer open to the left */
	gnome_canvas_item_raise_to_top (jp->pointer_l);
	if (!col && !row)
		gnome_canvas_item_hide (jp->pointer_l);
	else {
		if (!col) { row -= 1; col = jp->nx; }
		g_object_get (jp->pointer_l, "x", &px, "y", &py, NULL);
		dx = col * (jp->paw + PAGE_PAD * 2) - px;
		dy = row * (jp->pah + PAGE_PAD * 2) - py;
		gnome_canvas_item_move (jp->pointer_l, dx, dy);
		gnome_canvas_item_show (jp->pointer_l);
	}
}

static void
gnome_print_job_preview_selection_changed (GnomePrintJobPreview *jp)
{
	GtkStyle *style = gtk_widget_get_style (GTK_WIDGET (jp));
	gint32 ca = GPP_COLOR_RGBA (style->text[GTK_STATE_ACTIVE  ], 0xff);
	gint32 cs = GPP_COLOR_RGBA (style->text[GTK_STATE_SELECTED], 0xff);
	gint32 cn = GPP_COLOR_RGBA (style->text[GTK_STATE_NORMAL  ], 0xff);
	guint i;

	i = gnome_print_job_preview_count_selected (jp);
	g_object_set (G_OBJECT (jp->ccp.cut) , "sensitive", i > 0, NULL);
	g_object_set (G_OBJECT (jp->ccp.copy), "sensitive", i > 0, NULL);

	for (i = 0; i < jp->pages->len; i++) {
		GnomePrintJobPreviewPage p;
		guint32 c;

		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, i);
		if (jp->state != GNOME_PRINT_JOB_PREVIEW_STATE_EDITING)
			c = cn;
		else if (p.n == MIN (jp->current_page, jp->selection->len - 1))
			c = ca;
		else if (g_array_index (jp->selection, gboolean, p.n))
			c = cs;
		else
			c = cn;
		g_object_set (p.page_fg, "outline_color_rgba", c, NULL);
	}
}

static void
gnome_print_job_preview_deselect_all (GnomePrintJobPreview *jp)
{
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	for (i = 0; i < jp->selection->len; i++)
		g_array_index (jp->selection, gboolean, i) = FALSE;
	gnome_print_job_preview_selection_changed (jp);
}

static void
gnome_print_job_preview_select_all_none (GnomePrintJobPreview *jp,
					 gboolean all)
{
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	for (i = 0; i < jp->selection->len; i++)
		g_array_index (jp->selection, gboolean, i) = all;
	gnome_print_job_preview_selection_changed (jp);
}

static void
gnome_print_job_preview_select_page (GnomePrintJobPreview *jp, guint n)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	n = MIN (n, jp->selection->len - 1);
	if ((gnome_print_job_preview_count_selected (jp) == 1) &&
	    g_array_index (jp->selection, gboolean, n)) return;

	gnome_print_job_preview_select_all_none (jp, FALSE);
	g_array_index (jp->selection, gboolean, n) = TRUE;
	gnome_print_job_preview_selection_changed (jp);
}

static void
gnome_print_job_preview_update_navigation (GnomePrintJobPreview *jp)
{

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	g_object_set (G_OBJECT (jp->nav.f), "sensitive", gnome_print_job_preview_has_previous_screen (jp), NULL);
	g_object_set (G_OBJECT (jp->nav.p), "sensitive", gnome_print_job_preview_has_previous_screen (jp), NULL);
	g_object_set (G_OBJECT (jp->nav.n), "sensitive", gnome_print_job_preview_has_next_screen (jp), NULL);
	g_object_set (G_OBJECT (jp->nav.l), "sensitive", gnome_print_job_preview_has_next_screen (jp), NULL);
}

static gboolean
gnome_print_job_preview_page_is_visible (GnomePrintJobPreview *jp, guint n)
{
	GnomePrintJobPreviewPage p;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), FALSE);
	g_return_val_if_fail (n < jp->pages->len, FALSE);

	p = g_array_index (jp->pages, GnomePrintJobPreviewPage, n);
	return (p.group->object.flags & GNOME_CANVAS_ITEM_VISIBLE);
}

static void
gnome_print_job_preview_hide_page (GnomePrintJobPreview *jp, guint n)
{
	GnomePrintJobPreviewPage p;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (n < jp->pages->len);

	if (!gnome_print_job_preview_page_is_visible (jp, n)) return;
	p = g_array_index (jp->pages, GnomePrintJobPreviewPage, n);
	gnome_canvas_item_hide (p.group);
}

static void
gnome_print_job_preview_update_page (GnomePrintJobPreview *jp, GnomePrintJobPreviewPage *p)
{
	GnomeCanvasItem *group;
	gdouble t[6];

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (p);

	if (p->n >= jp->selection->len) {
		gnome_canvas_item_hide (p->group);
		return;
	}
	gnome_canvas_item_show (p->group);

	g_object_set (G_OBJECT (p->page_bg),
			"x2", jp->paw + 3., "y2", jp->pah + 3., NULL);
	g_object_set (G_OBJECT (p->page_fg), "x2", jp->paw, "y2", jp->pah, NULL);
	g_object_get (G_OBJECT (p->preview), "group", &group, NULL);
	t[0] =  1.; t[1] =  0.; t[2] =  0.;
	t[3] = -1.; t[4] =  0.; t[5] =  jp->pah;
	gnome_canvas_item_affine_absolute (group, t);
	gnome_print_preview_reset (p->preview);
	gnome_print_job_render_page (jp->job, GNOME_PRINT_CONTEXT (p->preview),
			p->n, TRUE);
}

static void
gnome_print_job_preview_show_page (GnomePrintJobPreview *jp, guint n,
		guint page)
{
	GnomePrintJobPreviewPage *p;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (page < jp->selection->len);
	g_return_if_fail (n < jp->pages->len);

	p = &(g_array_index (jp->pages, GnomePrintJobPreviewPage, n));
	p->n = page;
	gnome_print_job_preview_update_page (jp, p);
}

static void
gnome_print_job_preview_show_pages (GnomePrintJobPreview *jp, guint page)
{
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	if (jp->selection->len == 0)
		return;

	g_return_if_fail (page < jp->selection->len);

	for (i = 0; i < jp->pages->len; i++)
		if (page + i < jp->selection->len)
			gnome_print_job_preview_show_page (jp, i, page + i);
		else
			gnome_print_job_preview_hide_page (jp, i);
}

static void
gnome_print_job_preview_goto_page (GnomePrintJobPreview *jp, guint page)
{
	gchar c[32];
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (page <= jp->selection->len);

	if (jp->pages->len && (page == jp->current_page)) return;

	if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING)
		if ((gnome_print_job_preview_count_selected (jp) == 1) &&
		    g_array_index (jp->selection, gboolean,
			    MIN (jp->current_page, jp->selection->len - 1)))
				gnome_print_job_preview_select_page (jp, page);
	jp->current_page = page;
	page = MIN (page, jp->selection->len - 1);

	/* Check if we need to go to another screen or show additional pages. */
	for (i = 0; i < jp->pages->len; i++) {
		GnomePrintJobPreviewPage p;

		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, i);
		if (!i && (p.n > page)) {
			gnome_print_job_preview_show_pages (jp, page);
			break;
		}
		if ((p.n == page) && gnome_print_job_preview_page_is_visible (jp, i))
			break;
	}
	if (i == jp->pages->len)
		gnome_print_job_preview_show_pages (jp, page);

	gnome_print_job_preview_update_navigation (jp);
	gnome_print_job_preview_selection_changed (jp);

	g_snprintf (c, 32, "%d", MIN (page + 1, jp->selection->len));
	gtk_entry_set_text (GTK_ENTRY (jp->page_entry), c);

	if (jp->pointer_t)
		gnome_print_job_preview_update_pointer (jp, jp->current_page);
}

static void
gnome_print_job_preview_number_of_pages_changed (GnomePrintJobPreview *jp)
{
	gchar *text;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	g_array_set_size (jp->selection,
			  MAX (0, gnome_print_job_get_pages (jp->job)));
	if (!jp->selection->len) {
		text = g_strdup_printf ("<markup>%d   "
				        "<span foreground=\"red\" "
					"weight=\"ultrabold\" "
					"background=\"white\">"
					"%s</span></markup>", 1,
					_("No visible output was created."));
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (jp->last), text);
	} else {
		text = g_strdup_printf ("%i", jp->selection->len);
		gtk_label_set_text (GTK_LABEL (jp->last), text);
	}
	g_free (text);

	/* Update pages */
	if (jp->current_page > jp->selection->len) {
		gnome_print_job_preview_goto_page (jp, jp->selection->len);
	} else if (jp->pages->len) {
		GnomePrintJobPreviewPage p;

		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, 0);
		gnome_print_job_preview_show_pages (jp, p.n);
	}
}

static gint
change_page_cmd (GtkEntry *entry, GnomePrintJobPreview *jp)
{
	const gchar *text = gtk_entry_get_text (entry);
	gint page;

	page = CLAMP (atoi (text), 1, jp->selection->len) - 1;

	gnome_print_job_preview_goto_page (jp, page);

	return TRUE;
}

static void
gnome_print_job_preview_cmd_insert_real (GnomePrintJobPreview *jp,
					 GnomePrintMeta *meta, guint n)
{
	GnomePrintMeta *m_old;
	GnomePrintContext *m_new;
	guint i, n_new;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	g_object_get (G_OBJECT (jp->job), "context", &m_old, NULL);
	m_new = g_object_new (GNOME_TYPE_PRINT_META, NULL);
	for (i = 0; i < n; i++)
		gnome_print_meta_render_page (m_old, m_new, i, TRUE);
	gnome_print_meta_render (meta, m_new);
	for (; i < gnome_print_meta_get_pages (m_old); i++)
		gnome_print_meta_render_page (m_old, m_new, i, TRUE);
	g_object_set (jp->job, "context", m_new, NULL);
	g_object_unref (G_OBJECT (m_new));

	/* Select the inserted pages and show the first one. */
	gnome_print_job_preview_deselect_all (jp);
	n_new = gnome_print_meta_get_pages (meta);
	for (i = n; i < n + n_new; i++)
		g_array_index (jp->selection, gboolean, i) = TRUE;
	gnome_print_job_preview_selection_changed (jp);
	gnome_print_job_preview_goto_page (jp, n);
}

static void
gnome_print_job_preview_cmd_insert (GnomePrintJobPreview *jp,
				     guint n, GnomePrintMeta *meta)
{
	GnomePrintJobPreviewCmdData cd;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (GNOME_IS_PRINT_META (meta));

	gnome_print_job_preview_clear_redo (jp);
	cd.cmd = GNOME_PRINT_JOB_PREVIEW_CMD_INSERT;
	cd.params.insert.meta = meta;
	g_object_ref (G_OBJECT (meta));
	cd.params.insert.position = n;
	cd.params.insert.selection = g_array_new (TRUE, TRUE, sizeof (gboolean));
	g_array_append_vals (cd.params.insert.selection, jp->selection->data,
							 jp->selection->len);

	g_array_prepend_val (jp->undo, cd);
	g_object_set (G_OBJECT (jp->u_r.undo), "sensitive", TRUE, NULL);
	gnome_print_job_preview_cmd_insert_real (jp, meta, n);
}

static void
gnome_print_job_preview_cmd_delete_real (GnomePrintJobPreview *jp,
					 GnomePrintMeta *meta)
{
	GnomePrintMeta *m_old;
	GnomePrintContext *m_new;
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	g_object_get (G_OBJECT (jp->job), "context", &m_old, NULL);
	m_new = gnome_print_meta_new ();
	for (i = 0; i < jp->selection->len; i++) {
		if (!g_array_index (jp->selection, gboolean, i))
			gnome_print_meta_render_page (m_old, m_new, i, TRUE);
		else if (meta)
			gnome_print_meta_render_page (m_old,
				GNOME_PRINT_CONTEXT (meta), i, TRUE);
	}
	g_object_set (G_OBJECT (jp->job), "context", m_new, NULL);
	gnome_print_job_preview_select_page (jp, 0);
}

static void
gnome_print_job_preview_cmd_delete (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewCmdData cd;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (!gnome_print_job_preview_count_selected (jp)) return;

	gnome_print_job_preview_clear_redo (jp);
	cd.cmd = GNOME_PRINT_JOB_PREVIEW_CMD_DELETE;
	cd.params.delete.selection = g_array_new (TRUE, TRUE, sizeof (gboolean));
	g_array_append_vals (cd.params.delete.selection, jp->selection->data,
							 jp->selection->len);
	cd.params.delete.meta = GNOME_PRINT_META (gnome_print_meta_new ());
	g_array_prepend_val (jp->undo, cd);
	g_object_set (G_OBJECT (jp->u_r.undo), "sensitive", TRUE, NULL);
	gnome_print_job_preview_cmd_delete_real (jp, cd.params.delete.meta);
}

static void
gnome_print_job_preview_cmd_move_real (GnomePrintJobPreview *jp, guint n)
{
	GnomePrintMeta *meta;
	guint i, j;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	n = MIN (n, jp->selection->len);
	for (j = i = 0; i < n; i++)
		if (g_array_index (jp->selection, gboolean, i)) j++;
	meta = GNOME_PRINT_META (gnome_print_meta_new ());
	gnome_print_job_preview_cmd_delete_real (jp, meta);
	gnome_print_job_preview_cmd_insert_real (jp, meta, n - j);
	g_object_unref (G_OBJECT (meta));
}

static void
gnome_print_job_preview_cmd_move (GnomePrintJobPreview *jp, guint n)
{
	GnomePrintJobPreviewCmdData cd;
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	/* Do we really need to do anything? */
	if (!gnome_print_job_preview_count_selected (jp)) return;
	n = MIN (n, jp->selection->len);
	for (i = 0; !g_array_index (jp->selection, gboolean, i) && (i<n); i++);
	for (     ;  g_array_index (jp->selection, gboolean, i) && (i<n); i++);
	if (i == n) {
		for (;  g_array_index (jp->selection, gboolean, i) && (i < jp->selection->len); i++);
		for (; !g_array_index (jp->selection, gboolean, i) && (i < jp->selection->len); i++);
		if (i == jp->selection->len) return;
	}

	gnome_print_job_preview_clear_redo (jp);
	cd.cmd = GNOME_PRINT_JOB_PREVIEW_CMD_MOVE;
	cd.params.move.position = n;
	cd.params.move.selection = g_array_new (TRUE, TRUE, sizeof (gboolean));
	g_array_append_vals (cd.params.move.selection, jp->selection->data,
						       jp->selection->len);
	g_array_prepend_val (jp->undo, cd);
	g_object_set (G_OBJECT (jp->u_r.undo), "sensitive", TRUE, NULL);
	gnome_print_job_preview_cmd_move_real (jp, n);
}

static gboolean
gnome_print_job_preview_undo (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewCmdData cd;
	guint i, n, pos;
	GnomePrintContext *meta;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), FALSE);

	if (!jp->undo->len) return FALSE;
	cd = g_array_index (jp->undo, GnomePrintJobPreviewCmdData, 0);
	switch (cd.cmd) {
	case GNOME_PRINT_JOB_PREVIEW_CMD_INSERT:
		n = gnome_print_meta_get_pages (cd.params.insert.meta);
		for (i = 0; i < jp->selection->len; i++)
			g_array_index (jp->selection, gboolean, i) =
				((i >= cd.params.insert.position) &&
				 (i < cd.params.insert.position + n));
		gnome_print_job_preview_cmd_delete_real (jp, NULL);
		break;
	case GNOME_PRINT_JOB_PREVIEW_CMD_DELETE:
		for (n = i = 0; i < cd.params.delete.selection->len; i++) {
			if (!g_array_index (cd.params.delete.selection,
							gboolean, i))
				continue;
			meta = gnome_print_meta_new ();
			gnome_print_meta_render_page (cd.params.delete.meta,
						      meta, n, TRUE);
			gnome_print_job_preview_cmd_insert_real (jp,
						GNOME_PRINT_META (meta), i);
			g_object_unref (G_OBJECT (meta));
			n++;
		}
		memcpy (jp->selection->data, cd.params.delete.selection->data,
			jp->selection->len * sizeof (gboolean));
		gnome_print_job_preview_selection_changed (jp);
		break;
	case GNOME_PRINT_JOB_PREVIEW_CMD_MOVE:

		/* Count the moved pages. */
		pos = cd.params.move.position;
		for (n = i = 0; i < cd.params.move.selection->len; i++)
			if (g_array_index (cd.params.move.selection, gboolean, i)) {
				n++;
				if (i < cd.params.move.position) pos--;
			}

		/* Put the pages that got moved temporarily away. */
		for (i = 0; i < jp->selection->len; i++)
			g_array_index (jp->selection, gboolean, i) = (i >= pos) && (i < pos + n);
		meta = gnome_print_meta_new ();
		gnome_print_job_preview_cmd_delete_real (jp, GNOME_PRINT_META (meta));

		/* Put the moved pages on their original position. */
		for (n = i = 0; i < cd.params.move.selection->len; i++) {
			GnomePrintContext *m_new;

			if (!g_array_index (cd.params.move.selection, gboolean, i)) continue;
			m_new = gnome_print_meta_new ();
			gnome_print_meta_render_page (GNOME_PRINT_META (meta),
						      m_new, n++, TRUE);
			gnome_print_job_preview_cmd_insert_real (jp, GNOME_PRINT_META (m_new), i);
			g_object_unref (G_OBJECT (m_new));
		}
		g_object_unref (G_OBJECT (meta));
		memcpy (jp->selection->data, cd.params.move.selection->data,
			jp->selection->len * sizeof (gboolean));
		gnome_print_job_preview_selection_changed (jp);
		break;
	default:
		break;
	}
	g_array_prepend_val (jp->redo, cd);
	g_array_remove_index (jp->undo, 0);
	g_object_set (G_OBJECT (jp->u_r.undo), "sensitive", jp->undo->len > 0, NULL);
	g_object_set (G_OBJECT (jp->u_r.redo), "sensitive", TRUE, NULL);

	return (jp->undo->len);
}

static gboolean
gnome_print_job_preview_redo (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewCmdData cd;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), FALSE);

	if (!jp->redo->len) return FALSE;
	cd = g_array_index (jp->redo, GnomePrintJobPreviewCmdData, 0);
	switch (cd.cmd) {
	case GNOME_PRINT_JOB_PREVIEW_CMD_INSERT:
		gnome_print_job_preview_cmd_insert_real (jp,
			cd.params.insert.meta, cd.params.insert.position);
		break;
	case GNOME_PRINT_JOB_PREVIEW_CMD_DELETE:
		memcpy (jp->selection->data, cd.params.delete.selection->data,
			jp->selection->len * sizeof (gboolean));
		gnome_print_job_preview_cmd_delete_real (jp, NULL);
		break;
	case GNOME_PRINT_JOB_PREVIEW_CMD_MOVE:
		memcpy (jp->selection->data, cd.params.move.selection->data,
			jp->selection->len * sizeof (gboolean));
		gnome_print_job_preview_cmd_move_real (jp,
				cd.params.move.position);
		break;
	default:
		break;
	}
	g_array_prepend_val (jp->undo, cd);
	g_array_remove_index (jp->redo, 0);
	g_object_set (G_OBJECT (jp->u_r.undo), "sensitive", TRUE, NULL);
	g_object_set (G_OBJECT (jp->u_r.redo), "sensitive", jp->redo->len > 0, NULL);

	return (jp->redo->len);
}

#define CLOSE_ENOUGH(a,b) (fabs (a - b) < 1e-6)

static void
gnome_print_job_preview_zoom (GnomePrintJobPreview *jp, gdouble factor)
{
	gdouble	     zoom;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	/* No pages -> no zoom. */
	if (!jp->nx || !jp->ny) return;

	if (factor <= 0.) {
		gint width = GTK_WIDGET (jp->canvas)->allocation.width;
		gint height = GTK_WIDGET (jp->canvas)->allocation.height;
		gdouble zoomx = width  / ((jp->paw + PAGE_PAD * 2) * jp->nx + PAGE_PAD * 2);
		gdouble zoomy = height / ((jp->pah + PAGE_PAD * 2) * jp->ny + PAGE_PAD * 2.);

		zoom = MIN (zoomx, zoomy);
	} else
		zoom = jp->zoom_factor * factor;

	jp->zoom_factor = CLAMP (zoom, GPMP_ZOOM_MIN, GPMP_ZOOM_MAX);
	gnome_canvas_set_pixels_per_unit (jp->canvas, jp->zoom_factor);

	g_object_set (G_OBJECT (jp->zoom.z1), "sensitive", (!CLOSE_ENOUGH (jp->zoom_factor, 1.0)), NULL);
	g_object_set (G_OBJECT (jp->zoom.zi), "sensitive", (!CLOSE_ENOUGH (jp->zoom_factor, GPMP_ZOOM_MAX)), NULL);
	g_object_set (G_OBJECT (jp->zoom.zo), "sensitive", (!CLOSE_ENOUGH (jp->zoom_factor, GPMP_ZOOM_MIN)), NULL);
}

static guint
gnome_print_job_preview_get_page_at (GnomePrintJobPreview *jp, guint x, guint y)
{
	gint ox, oy;
	guint row, col;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), 0);

	gnome_canvas_get_scroll_offsets (jp->canvas, &ox, &oy);
	col = (x / jp->canvas->pixels_per_unit - ox) /
	      (jp->paw + PAGE_PAD * 2);
	row = (y / jp->canvas->pixels_per_unit - oy) /
	      (jp->pah + PAGE_PAD * 2);

	return MIN (row * jp->nx + col, jp->pages->len);
}

static GdkPixbuf *
gnome_print_job_preview_get_pixbuf_for_meta (GnomePrintJobPreview *jp,
		GnomePrintMeta *meta, guint page, gdouble factor)
{
	GnomePrintContext *rbuf;
	GdkPixbuf *pixbuf;
	gdouble translate[6], scale[6], page2buf[6];

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), NULL);
	g_return_val_if_fail (GNOME_IS_PRINT_META (meta), NULL);

	rbuf = gnome_print_context_new_from_module_name ("rbuf");
	if (!rbuf) return NULL;
	
	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
			jp->paw * factor, jp->pah * factor);
	gdk_pixbuf_fill (pixbuf, 0);
	art_affine_translate (translate, 0, -jp->pah);
	art_affine_scale (scale, factor, -factor);
	art_affine_multiply (page2buf, translate, scale);
	g_object_set (G_OBJECT (rbuf),
			"pixels",    gdk_pixbuf_get_pixels    (pixbuf),
			"width",     gdk_pixbuf_get_width     (pixbuf),
			"height",    gdk_pixbuf_get_height    (pixbuf),
			"rowstride", gdk_pixbuf_get_rowstride (pixbuf),
			"alpha",     gdk_pixbuf_get_has_alpha (pixbuf),
			"page2buf", page2buf,
			NULL);
	gnome_print_meta_render_page (meta, rbuf, page, TRUE);
	g_object_unref (G_OBJECT (rbuf));
	return pixbuf;
}

static GdkPixbuf *
gnome_print_job_preview_get_pixbuf_for_selection (GnomePrintJobPreview *jp,
		gdouble factor)
{
	GnomePrintMeta *meta;
	guint i;

	g_return_val_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp), NULL);

	g_object_get (jp->job, "context", &meta, NULL);
	for (i = 0; i < jp->selection->len; i++)
		if (g_array_index (jp->selection, gboolean, i))
			return gnome_print_job_preview_get_pixbuf_for_meta (jp, meta, i, factor);
	return NULL;
}

static void
on_drag_data_get (GtkWidget *widget, GdkDragContext *context,
		  GtkSelectionData *selection_data,
		  guint info, guint time_, GnomePrintJobPreview *jp)
{
	if (selection_data->target == gdk_atom_intern ("GNOME_PRINT_META", FALSE)) {
		GnomePrintContext *meta = gnome_print_meta_new ();
		guint i;

		for (i = 0; i < jp->selection->len; i++)
			if (g_array_index (jp->selection, gboolean, i))
				gnome_print_job_render_page (jp->job, meta, i, TRUE);
		gtk_selection_data_set (selection_data, selection_data->target, 8,
				gnome_print_meta_get_buffer (GNOME_PRINT_META (meta)),
				gnome_print_meta_get_length (GNOME_PRINT_META (meta)));
		g_object_unref (G_OBJECT (meta));
	} else {
		GdkPixbuf *p = gnome_print_job_preview_get_pixbuf_for_selection (jp, 1.);
		gtk_selection_data_set_pixbuf (selection_data, p);
		g_object_unref (G_OBJECT (p));
	}
}

static void
gnome_print_job_preview_set_state_editing (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewState s;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) return;
	s = jp->state; jp->state = GNOME_PRINT_JOB_PREVIEW_STATE_EDITING;

	/* Clean up previous states */
	if (s == GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING) {
		GdkDisplay *d = gtk_widget_get_display (GTK_WIDGET (jp));

		gdk_display_pointer_ungrab (d, jp->event->button.time);
		gnome_print_job_preview_select_page (jp,
			gnome_print_job_preview_get_page_at (jp,
				jp->event->button.x, jp->event->button.y));
	} else
		gnome_print_job_preview_select_page (jp, jp->current_page);

	/* Enable drag & drop */
	gtk_drag_source_set (GTK_WIDGET (jp->canvas),
		GDK_BUTTON1_MASK | GDK_BUTTON3_MASK, target_table,
		G_N_ELEMENTS (target_table),
		GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_source_add_image_targets (GTK_WIDGET (jp->canvas));
	if (s == GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING) {
		gtk_drag_begin (GTK_WIDGET (jp->canvas),
			gtk_drag_source_get_target_list (GTK_WIDGET (jp->canvas)),
			(jp->event->button.state & GDK_SHIFT_MASK) ?
				GDK_ACTION_MOVE : GDK_ACTION_COPY,
			jp->event->button.button, jp->event);
	}

	if (!gtk_toggle_action_get_active (jp->other.edit))
		gtk_toggle_action_set_active (jp->other.edit, TRUE);
	gtk_widget_grab_focus (GTK_WIDGET (jp->canvas));
}

static void
gnome_print_job_preview_set_pointer_type (GnomePrintJobPreview *jp,
					  GnomePrintJobPreviewPointerType t)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->pointer_t & t) return;
	jp->pointer_t |= t;
	if (!jp->pointer_t) return;
	if ((jp->pointer_l->object.flags & GNOME_CANVAS_ITEM_VISIBLE) ||
			(jp->pointer_r->object.flags & GNOME_CANVAS_ITEM_VISIBLE)) return;
	
	gnome_print_job_preview_set_state_editing (jp);
	g_object_set (G_OBJECT (jp->ccp.paste), "sensitive", TRUE, NULL);
	gnome_print_job_preview_update_pointer (jp, jp->current_page);
}

static void
gnome_print_job_preview_unset_pointer_type (GnomePrintJobPreview *jp,
					    GnomePrintJobPreviewPointerType t)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (!(jp->pointer_t & t)) return;

	jp->pointer_t &= ~t;
	if (jp->pointer_t) return;
	if (jp->pointer_l->object.flags & GNOME_CANVAS_ITEM_VISIBLE)
		gnome_canvas_item_hide (jp->pointer_l);
	if (jp->pointer_r->object.flags & GNOME_CANVAS_ITEM_VISIBLE)
		gnome_canvas_item_hide (jp->pointer_r);
	g_object_set (G_OBJECT (jp->ccp.paste), "sensitive", FALSE, NULL);
}

static void
on_drag_begin (GtkWidget *widget, GdkDragContext *context,
	       GnomePrintJobPreview *jp)
{
	GdkPixbuf *pixbuf;

	gnome_print_job_preview_set_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP);

	/* Show a preview if possible */
	pixbuf = gnome_print_job_preview_get_pixbuf_for_selection (jp, 0.1);
	gtk_drag_set_icon_pixbuf (context, pixbuf, 0, 0);
	g_object_unref (G_OBJECT (pixbuf));
}

static gboolean
on_drag_motion (GtkWidget *widget, GdkDragContext *context,
		gint x, gint y, guint time, GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_set_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP);
	gnome_print_job_preview_update_pointer (jp,
			gnome_print_job_preview_get_page_at (jp, x, y));
	return TRUE;
}

static void
on_drag_leave (GtkWidget *widget, GdkDragContext *context,
	       guint time_, GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_unset_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP);
}

static void
on_drag_end (GtkWidget *widget, GdkDragContext *context,
	     GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_unset_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP);
}

static void
on_drag_data_delete (GtkWidget *widget, GdkDragContext *context,
		     GnomePrintJobPreview *jp)
{
	if (context->is_source) return;
	gnome_print_job_preview_cmd_delete (jp);
}

static void
gnome_print_job_preview_set_state_normal (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewState s;
	GdkDisplay *d = gtk_widget_get_display (GTK_WIDGET (jp));

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_NORMAL) return;
	s = jp->state; jp->state = GNOME_PRINT_JOB_PREVIEW_STATE_NORMAL;

	/* Clean up previous states. */
	gnome_print_job_preview_select_all_none (jp, FALSE);
	if (jp->pointer_t) {
		gnome_print_job_preview_unset_pointer_type (jp,
				GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY |
				GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP);
	}
	if (s == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING)
		gtk_drag_source_unset (GTK_WIDGET (jp->canvas));
	if (s == GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING)
		gdk_display_pointer_ungrab (d, jp->event->button.time);
	if (jp->event) { gdk_event_free (jp->event); jp->event = NULL; }
	if (gtk_toggle_action_get_active (jp->other.edit))
		gtk_toggle_action_set_active (jp->other.edit, FALSE);
}

static void
gnome_print_job_preview_set_state_dragging (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewState s;
	GdkDisplay *d = gtk_widget_get_display (GTK_WIDGET (jp));
	GdkCursor *c;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING) return;
	s = jp->state; jp->state = GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING;

	/* Clean up previous states */
	gnome_print_job_preview_select_all_none (jp, FALSE);
	if (s == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING)
		gtk_drag_source_unset (GTK_WIDGET (jp->canvas));
	if (gtk_toggle_action_get_active (jp->other.edit))
		gtk_toggle_action_set_active (jp->other.edit, FALSE);

	/* Set up new state. */
	gnome_canvas_get_scroll_offsets (jp->canvas,
					 &jp->offsetx, &jp->offsety);
	jp->anchorx = jp->event->button.x - jp->offsetx;
	jp->anchory = jp->event->button.y - jp->offsety;
	c = gdk_cursor_new_for_display (d, GDK_FLEUR);
	gdk_pointer_grab (GTK_WIDGET (jp->canvas)->window, FALSE,
		GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
		GDK_BUTTON_RELEASE_MASK, NULL, c, jp->event->button.time);
	gdk_cursor_unref (c);
}

static gint
on_canvas_button_press_event (GtkWidget *widget, GdkEventButton *event,
			      GnomePrintJobPreview *jp)
{
	guint n;

	if (jp->event) gdk_event_free (jp->event);
	jp->event = gdk_event_copy ((GdkEvent *) event);

	if (event->button == 1) {
		switch (jp->state) {
		case GNOME_PRINT_JOB_PREVIEW_STATE_NORMAL:
			gnome_print_job_preview_set_state_dragging (jp);
			return TRUE;
		case GNOME_PRINT_JOB_PREVIEW_STATE_EDITING:

			/* Figure out the page */
			n = MIN (jp->selection->len - 1,
				gnome_print_job_preview_get_page_at (jp,
							event->x, event->y));

			if (event->state & GDK_CONTROL_MASK) {
				g_array_index (jp->selection, gboolean, n) =
					!g_array_index (jp->selection,
							gboolean, n);
				gnome_print_job_preview_selection_changed (jp);
			} else if (event->state & GDK_SHIFT_MASK) {
				guint i;
				
				for (i = 1; i < n; i++)
					g_array_index (jp->selection,
							gboolean, i) |=
						g_array_index (jp->selection,
							gboolean, i - 1);
				g_array_index (jp->selection, gboolean, n) = TRUE;
				gnome_print_job_preview_selection_changed (jp);
			} else
				gnome_print_job_preview_select_page (jp, n);
			if (g_array_index (jp->selection, gboolean, n))
				gnome_print_job_preview_goto_page (jp, n);

			return FALSE;
		default:
			return FALSE;
		}
	}

	return FALSE;
}

static gint
on_canvas_motion_notify_event (GtkWidget *widget, GdkEventMotion *event,
			       GnomePrintJobPreview *jp)
{
	gint x, y, dx, dy;
	gint right, left, top, bottom, width, height;
	GdkModifierType mod;

	switch (jp->state) {
	case GNOME_PRINT_JOB_PREVIEW_STATE_EDITING:
	case GNOME_PRINT_JOB_PREVIEW_STATE_NORMAL:
		return FALSE;

	case GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING:

		if (event->is_hint) {
			gdk_window_get_pointer (widget->window, &x, &y, &mod);
		} else {
			x = event->x;
			y = event->y;
		}

		dx = jp->anchorx - x;
		dy = jp->anchory - y;

		if (!dx && !dy) return TRUE;
		width  = jp->canvas->layout.width;
		height = jp->canvas->layout.height;
		left = jp->offsetx;
		top  = jp->offsety;
		right  = MIN (width,  left + GTK_WIDGET (jp->canvas)->allocation.width);
		bottom = MIN (height, top  + GTK_WIDGET (jp->canvas)->allocation.height);
		if ((dx < -left) || (right  + dx > width) ||
		    (dy < -top ) || (bottom + dy > height)) {
			guint p_old, p_new;
			GtkAllocation a = GTK_WIDGET (jp->canvas)->allocation;

			p_old = gnome_print_job_preview_get_page_at (jp, jp->anchorx, jp->anchory);
			p_new = gnome_print_job_preview_get_page_at (jp, x, y);

			/*
			 * Start dragging? Only if the user moves over another page or 
			 * out of the canvas.
			 */
			if (((p_old != p_new) && (p_old + 1 != p_new)) ||
					(x < 0) || (x > a.width) ||
					(y < 0) || (y > a.height)) {
				gnome_print_job_preview_set_state_editing (jp);
				return FALSE;
			}
			dx = MIN (width  - right , MAX (dx, -left));
			dy = MIN (height - bottom, MAX (dy, -top ));
		}
		if (!dx && !dy) return TRUE;

		/* Move the canvas and get new anchor and offset. */
		gnome_canvas_scroll_to (jp->canvas, jp->offsetx + dx, jp->offsety + dy);
		jp->anchorx = event->x;
		jp->anchory = event->y;
		gnome_canvas_get_scroll_offsets (jp->canvas, &jp->offsetx, &jp->offsety);

		return TRUE;
	default:
		return FALSE;
	}
}

static gint
on_canvas_button_release_event (GtkWidget *widget, GdkEventButton *event,
				GnomePrintJobPreview *jp)
{
	if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_DRAGGING) {
		gnome_print_job_preview_set_state_normal (jp);
		return TRUE;
	}

	return FALSE;
}


static gboolean
on_delete_event (GtkWidget *widget, GdkEventAny *event,
		 GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_close (jp);
	return TRUE;
}

static void
gnome_print_job_preview_goto_next_screen (GnomePrintJobPreview *jp)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	gnome_print_job_preview_goto_page (jp,
		MIN (jp->current_page,
		     jp->selection->len - 1) + jp->nx * jp->ny);
}

static void
gnome_print_job_preview_goto_previous_screen (GnomePrintJobPreview *jp)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	gnome_print_job_preview_goto_page (jp, MAX (jp->nx * jp->ny, 
				MIN (jp->current_page, jp->selection->len - 1)) - jp->nx * jp->ny);
}

static void
clipboard_get_func (GtkClipboard *c, GtkSelectionData *sd,
		    guint info, GnomePrintJobPreview *jp)
{
	if (sd->target == gdk_atom_intern ("GNOME_PRINT_META", FALSE)) {
		gtk_selection_data_set (sd, sd->target, 8,
				gnome_print_meta_get_buffer (GNOME_PRINT_META (jp->clipboard)),
				gnome_print_meta_get_length (GNOME_PRINT_META (jp->clipboard)));
	} else {
		GdkPixbuf *p = gnome_print_job_preview_get_pixbuf_for_meta (jp, 
				GNOME_PRINT_META (jp->clipboard), 0, 1.);
		gtk_selection_data_set_pixbuf (sd, p);
		g_object_unref (G_OBJECT (p));
	}
}

static void
clipboard_received_func (GtkClipboard *c, GtkSelectionData *sd,
			 GnomePrintJobPreview *jp)
{
	if (sd->target == gdk_atom_intern ("GNOME_PRINT_META", FALSE)) {
		GnomePrintContext *meta = gnome_print_meta_new (); 

		gnome_print_meta_render_data (meta, sd->data, sd->length);
		gnome_print_job_preview_cmd_insert (jp, jp->current_page,
				GNOME_PRINT_META (meta));
		g_object_unref (G_OBJECT (meta));
	}
}

static void
clipboard_clear_func (GtkClipboard *c, GnomePrintJobPreview *jp)
{
	g_object_unref (G_OBJECT (jp->clipboard));
	jp->clipboard = NULL;
	gnome_print_job_preview_unset_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY);
}

static void
gnome_print_job_preview_paste (GnomePrintJobPreview *jp)
{
	GdkDisplay *d = gtk_widget_get_display (GTK_WIDGET (jp));
	GtkClipboard *c = gtk_clipboard_get_for_display (d, GDK_SELECTION_CLIPBOARD);

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	gtk_clipboard_request_contents (c, 
			gdk_atom_intern ("GNOME_PRINT_META", FALSE),
			(GtkClipboardReceivedFunc) clipboard_received_func, jp);
}

static void
target_entries_free (GtkTargetEntry *t, guint nt)
{
	guint i;

	for (i = 0; i < nt; i++) g_free (t[i].target);
	g_free (t);
}

static void
gnome_print_job_preview_get_targets (GnomePrintJobPreview *jp,
		GtkTargetEntry **t, guint *nt)
{
	GtkTargetList *list = NULL;
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (t);
	g_return_if_fail (nt);

	list = gtk_target_list_new (target_table, G_N_ELEMENTS (target_table));
	gtk_target_list_add_image_targets (list, 0, FALSE);
	*nt = g_list_length (list->list);
	*t = g_new (GtkTargetEntry, *nt);
	for (i = 0; i < *nt; i++) {
		GtkTargetPair *pair = g_list_nth_data (list->list, i);
		(*t)[i].target = gdk_atom_name (pair->target);
		(*t)[i].flags = pair->flags;
		(*t)[i].info = pair->info;
	}
	gtk_target_list_unref (list);
}

static void
gnome_print_job_preview_cut_copy (GnomePrintJobPreview *jp, gboolean cut)
{
	GdkDisplay *d;
	GtkClipboard *c;
	guint i;
	GnomePrintMeta *meta;
	GtkTargetEntry *t = NULL;
	guint nt = 0;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (!g_array_index (jp->selection, gboolean,
			MIN (jp->current_page, jp->selection->len - 1)))
		gnome_print_job_preview_select_page (jp, jp->current_page);
	if (jp->clipboard)
		g_object_unref (G_OBJECT (jp->clipboard));
	jp->clipboard = gnome_print_meta_new ();
	g_object_get (G_OBJECT (jp->job), "context", &meta, NULL);
	for (i = 0; i < jp->selection->len; i++)
		if (g_array_index (jp->selection, gboolean, i))
			gnome_print_meta_render_page (meta,
						jp->clipboard, i, TRUE);

	gnome_print_job_preview_get_targets (jp, &t, &nt);
	d = gtk_widget_get_display (GTK_WIDGET (jp));
	c = gtk_clipboard_get_for_display (d, GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_with_owner (c, t, nt, 
			(GtkClipboardGetFunc  ) clipboard_get_func,
			(GtkClipboardClearFunc) clipboard_clear_func, G_OBJECT (jp));
	target_entries_free (t, nt);
	if (cut) gnome_print_job_preview_cmd_delete (jp);

	gnome_print_job_preview_set_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY);
	gnome_print_job_preview_update_pointer (jp, jp->current_page);
}

static void
gnome_print_job_preview_suggest_nx_and_ny (GnomePrintJobPreview *jp,
					   gulong *nx, gulong *ny)
{
	gulong x, y, n;
	
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (!nx) nx = &x;
	if (!ny) ny = &y;

	/* No pages -> no suggestion. */
	n = jp->selection->len;
	if (!n) { *nx = 0; *ny = 0; return; }

	*nx = MAX (1, jp->nx);
	*ny = MAX (1, jp->ny);

	if (jp->nx_auto && jp->ny_auto) {
		guint w = GTK_WIDGET (jp->canvas)->allocation.width;
		guint h = GTK_WIDGET (jp->canvas)->allocation.height;

		for (*nx = 1; *nx * *nx < n * w / h; (*nx)++);
		for (*ny = 1; *nx * *ny < n; (*ny)++);

		/* Can we use one column less? */
		if ((*nx - 1) * *ny >= jp->selection->len) (*nx)--;
	} else if (jp->nx_auto) for (*nx = 1; *nx * *ny < n; (*nx)++);
	  else if (jp->ny_auto) for (*ny = 1; *nx * *ny < n; (*ny)++);
}

static void
gnome_print_job_preview_nx_and_ny_changed (GnomePrintJobPreview *jp)
{
	GnomePrintJobPreviewPage p;
	guint i, col, row;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	gnome_print_job_preview_suggest_nx_and_ny (jp, &jp->nx, &jp->ny);

	/* Remove unnecessary pages */
	while (jp->pages->len > MIN (jp->nx * jp->ny, jp->selection->len)) {
		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, 0);
		gtk_object_destroy (GTK_OBJECT (p.group));
		g_object_unref (G_OBJECT (p.preview));
		g_array_remove_index (jp->pages, 0);
	}

	/* Create pages if needed */
	if (jp->pages->len < jp->nx * jp->ny) {
		GtkStyle *style;
		gdouble transform[6];
		gint32 c;

		style = gtk_widget_get_style (GTK_WIDGET (jp->canvas));
		c = GPP_COLOR_RGBA (style->text[GTK_STATE_NORMAL], 0xff);

		transform[0] =  1.; transform[1] =  0.;
		transform[2] =  0.; transform[3] = -1.;
		transform[4] =  0.; transform[5] =  jp->pah;
		art_affine_multiply (transform, jp->pa2ly, transform);

		while (jp->pages->len < jp->nx * jp->ny) {
			p.group = gnome_canvas_item_new (gnome_canvas_root (jp->canvas),
				GNOME_TYPE_CANVAS_GROUP, NULL);
			gnome_canvas_item_hide (p.group);
			p.page_fg = gnome_canvas_item_new (GNOME_CANVAS_GROUP (p.group),
					GNOME_TYPE_CANVAS_RECT, "fill_color", "white",
					"outline_color_rgba", c, "width_pixels", 1, NULL);
			gnome_canvas_item_lower_to_bottom (p.page_fg);
			p.page_bg = gnome_canvas_item_new (GNOME_CANVAS_GROUP (p.group),
					GNOME_TYPE_CANVAS_RECT, "x1", 3.0, "y1", 3.0,
					"fill_color", "black", "outline_color", "black", NULL);
			gnome_canvas_item_lower_to_bottom (p.page_bg);

			/* Create the group that holds the preview. */
			p.preview = g_object_new (GNOME_TYPE_PRINT_PREVIEW,
				"group", gnome_canvas_item_new (
					GNOME_CANVAS_GROUP (p.group), GNOME_TYPE_CANVAS_GROUP, NULL),
				"theme_compliance", jp->theme_compliance,
				NULL);
			if (!jp->pages || !jp->pages->len)
				p.n = 0;
			else {
				GnomePrintJobPreviewPage pp;

				pp = g_array_index (jp->pages, GnomePrintJobPreviewPage,
						jp->pages->len - 1);
				p.n = pp.n + 1;
			}
			g_array_append_val (jp->pages, p);
			gnome_print_job_preview_update_page (jp, &p);
		}
	}

	/* Position the pages */
	for (i = 0; i < jp->pages->len; i++) {
		GnomePrintJobPreviewPage p;
		
		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, i);
		if (jp->nx > 0) {
			col = i % jp->nx;
			row = i / jp->nx;
		} else
			col = row = 0;
		g_object_set (p.group,
			"x", (gdouble) col * (jp->paw + PAGE_PAD * 2),
			"y", (gdouble) row * (jp->pah + PAGE_PAD * 2), NULL);
	}

	gnome_print_job_preview_zoom (jp, -1.);
	gnome_print_job_preview_update_navigation (jp);
	gnome_canvas_set_scroll_region (jp->canvas, 0 - PAGE_PAD, 0 - PAGE_PAD,
			(jp->paw + PAGE_PAD * 2) * jp->nx + PAGE_PAD,
			(jp->pah + PAGE_PAD * 2) * jp->ny + PAGE_PAD);
}

static void
on_1x1_clicked (GtkMenuItem *i, GnomePrintJobPreview *jp)
{
	if (!jp->nx_auto && !jp->ny_auto && (jp->nx == 1) && (jp->ny == 1))
		return;
	jp->nx_auto = jp->ny_auto = FALSE;
	jp->nx = jp->ny = 1;
	gnome_print_job_preview_nx_and_ny_changed (jp);
}

static void
on_1x2_clicked (GtkMenuItem *i, GnomePrintJobPreview *jp)
{
	if (!jp->nx_auto && !jp->ny_auto && (jp->nx == 2) && (jp->ny == 1))
		return;
	jp->nx_auto = jp->ny_auto = FALSE;
	jp->nx = 2;
	jp->ny = 1;
	gnome_print_job_preview_nx_and_ny_changed (jp);
}

static void
on_2x1_clicked (GtkMenuItem *i, GnomePrintJobPreview *jp)
{
	if (!jp->nx_auto && !jp->ny_auto && (jp->nx == 1) && (jp->ny == 2))
		return;
	jp->nx_auto = jp->ny_auto = FALSE;
	jp->nx = 1;
	jp->ny = 2;
	gnome_print_job_preview_nx_and_ny_changed (jp);
}

static void
on_2x2_clicked (GtkMenuItem *i, GnomePrintJobPreview *jp)
{
	if (!jp->nx_auto && !jp->ny_auto && (jp->nx == 2) && (jp->ny == 2))
		return;
	jp->nx_auto = jp->ny_auto = FALSE;
	jp->nx = jp->ny = 2;
	gnome_print_job_preview_nx_and_ny_changed (jp);
}

static void
on_all_clicked (GtkMenuItem *i, GnomePrintJobPreview *jp)
{
	if (jp->nx_auto && jp->ny_auto) return;
	jp->nx_auto = jp->ny_auto = TRUE;
	gnome_print_job_preview_nx_and_ny_changed (jp);
	gnome_print_job_preview_show_pages (jp, 0);
}

static void
gnome_print_job_preview_show_multi_popup (GnomePrintJobPreview *jp)
{
	GtkWidget *m, *i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	m = gtk_menu_new ();
	gtk_widget_show (m);
	g_signal_connect (m, "selection_done", G_CALLBACK (gtk_widget_destroy),
			  m);

        i = gtk_menu_item_new_with_label ("1x1");
        gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (m), i, 0, 1, 0, 1);
        g_signal_connect (i, "activate", G_CALLBACK (on_1x1_clicked), jp);

        i = gtk_menu_item_new_with_label ("2x1");
        gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (m), i, 0, 1, 1, 2);
        g_signal_connect (i, "activate", G_CALLBACK (on_2x1_clicked), jp);

        i = gtk_menu_item_new_with_label ("1x2");
        gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (m), i, 1, 2, 0, 1);
        g_signal_connect (i, "activate", G_CALLBACK (on_1x2_clicked), jp);

        i = gtk_menu_item_new_with_label ("2x2");
        gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (m), i, 1, 2, 1, 2);
        g_signal_connect (i, "activate", G_CALLBACK (on_2x2_clicked), jp);

	i = gtk_menu_item_new_with_label (_("all"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (m), i, 2, 3, 2, 3);
	g_signal_connect (i, "activate", G_CALLBACK (on_all_clicked), jp);

	gtk_menu_popup (GTK_MENU (m), NULL, NULL, NULL, jp, 0,
			GDK_CURRENT_TIME);
}

static void
on_action_activate (GtkAction *a, GnomePrintJobPreview *jp)
{
	const gchar *name = gtk_action_get_name (a);

	if (!strcmp (name, "zoom_fit"))
		gnome_print_job_preview_zoom (jp, -1.);
	else if (!strcmp (name, "zoom_100")) 
		gnome_print_job_preview_zoom (jp, 1. / jp->zoom_factor);
	else if (!strcmp (name, "zoom_in"))
		gnome_print_job_preview_zoom (jp, GPMP_ZOOM_IN_FACTOR);
	else if (!strcmp (name, "zoom_out"))
		gnome_print_job_preview_zoom (jp, GPMP_ZOOM_OUT_FACTOR);
	else if (!strcmp (name, "first"))
		gnome_print_job_preview_goto_page (jp, 0);
	else if (!strcmp (name, "previous"))
		gnome_print_job_preview_goto_previous_screen (jp);
	else if (!strcmp (name, "next"))
		gnome_print_job_preview_goto_next_screen (jp);
	else if (!strcmp (name, "last"))
		gnome_print_job_preview_goto_page (jp, jp->selection->len - 1);
	else if (!strcmp (name, "undo"))
		gnome_print_job_preview_undo (jp);
	else if (!strcmp (name, "redo"))
		gnome_print_job_preview_redo (jp);
	else if (!strcmp (name, "print"))
		gnome_print_job_print (jp->job);
	else if (!strcmp (name, "close"))
		gnome_print_job_preview_close (jp);
	else if (!strcmp (name, "cut"))
		gnome_print_job_preview_cut_copy (jp, TRUE);
	else if (!strcmp (name, "copy"))
		gnome_print_job_preview_cut_copy (jp, FALSE);
	else if (!strcmp (name, "paste"))
		gnome_print_job_preview_paste (jp);
	else if (!strcmp (name, "multi"))
		gnome_print_job_preview_show_multi_popup (jp);
}

static gint
on_canvas_key_press_event (GtkWidget *widget, GdkEventKey *event,
			   GnomePrintJobPreview *jp)
{
	gint x,y;
	gint height, width;
	gint domove = 0;

	if (gtk_accel_groups_activate (G_OBJECT (jp), event->keyval,
				       event->state)) return TRUE;

	gnome_canvas_get_scroll_offsets (jp->canvas, &x, &y);
	height = GTK_WIDGET (jp->canvas)->allocation.height;
	width = GTK_WIDGET (jp->canvas)->allocation.width;

	switch (event->keyval) {
	case '1':
		gnome_print_job_preview_zoom (jp, 1. / jp->zoom_factor);
		break;
	case '+':
	case '=':
	case GDK_KP_Add:
		gnome_print_job_preview_zoom (jp, GPMP_ZOOM_IN_FACTOR);
		break;
	case '-':
	case '_':
	case GDK_KP_Subtract:
		gnome_print_job_preview_zoom (jp, GPMP_ZOOM_OUT_FACTOR);
		break;
	case GDK_KP_Right:
	case GDK_Right:
		if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) {
			if (event->state & GDK_SHIFT_MASK) {
				g_array_index (jp->selection, gboolean, MIN (jp->current_page + 1,
							jp->selection->len)) = TRUE;
				gnome_print_job_preview_selection_changed (jp);
			}
			gnome_print_job_preview_goto_page (jp,
						MIN (jp->current_page + 1, jp->selection->len));
			return TRUE;
		}
		if (event->state & GDK_SHIFT_MASK)
			x += width;
		else
			x += 10;
		domove = 1;
		break;
	case GDK_KP_Left:
	case GDK_Left:
		if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) {
			if (!jp->current_page) return TRUE;
			if ((jp->current_page == jp->selection->len) &&
			    !jp->clipboard)
				gnome_print_job_preview_goto_page (jp,
						jp->selection->len - 2);
			else gnome_print_job_preview_goto_page (jp,
						MAX (1, jp->current_page) - 1);
			return TRUE;
		}
		if (event->state & GDK_SHIFT_MASK)
			x -= width;
		else
			x -= 10;
		domove = 1;
		break;
	case GDK_KP_Up:
	case GDK_Up:
		if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) {
			guint i = MIN (jp->current_page, jp->selection->len - 1);

			while ((event->state & GDK_CONTROL_MASK) &&
			       (i > 2 * jp->nx)) i -= jp->nx;
			if (i >= jp->nx)
				gnome_print_job_preview_goto_page (jp, i - jp->nx);
			return TRUE;
		}
		if (event->state & GDK_SHIFT_MASK)
			goto page_up;
		y -= 10;
		domove = 1;
		break;
	case GDK_KP_Down:
	case GDK_Down:
		if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) {
			guint i = jp->current_page + jp->nx; 
			
			while ((event->state & GDK_CONTROL_MASK) && 
			       (i + jp->nx < jp->selection->len)) i += jp->nx;
			if (i < jp->selection->len)
				gnome_print_job_preview_goto_page (jp, i);
			return TRUE;
		}
		if (event->state & GDK_SHIFT_MASK)
			goto page_down;
		y += 10;
		domove = 1;
		break;
	case GDK_Delete:
		if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) {
			if (!gnome_print_job_preview_count_selected (jp))
				gnome_print_job_preview_select_page (jp,
							jp->current_page);
			gnome_print_job_preview_cmd_delete (jp);
			return TRUE;
		}
		goto page_up;
		break;
	case GDK_KP_Page_Up:
	case GDK_Page_Up:
	case GDK_KP_Delete:
	case GDK_BackSpace:
	page_up:
		if (y <= 0) {
			gnome_print_job_preview_goto_previous_screen (jp);
			return TRUE;
		} else {
			y -= height;
		}
		domove = 1;
		break;
	case GDK_KP_Page_Down:
	case GDK_Page_Down:
	page_down:
		if (y >= GTK_LAYOUT (jp->canvas)->height - height) {
			gnome_print_job_preview_goto_next_screen (jp);
			return TRUE;
		} else {
			y += height;
		}
		domove = 1;
		break;
	case GDK_KP_Home:
	case GDK_Home:
		gnome_print_job_preview_goto_page (jp, 0);
		return TRUE;
		break;
	case GDK_KP_End:
	case GDK_End:
		gnome_print_job_preview_goto_page (jp, jp->selection->len - 1);
		return TRUE;
		break;
	case GDK_Escape:
		if (jp->clipboard) {
			GdkDisplay *d = gtk_widget_get_display (widget);
			GtkClipboard *c = gtk_clipboard_get_for_display (d, GDK_SELECTION_CLIPBOARD);
			
			gtk_clipboard_clear (c);
			return TRUE;
		}
		gnome_print_job_preview_close (jp);
		return TRUE;
	case GDK_q:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_close (jp);
			return TRUE;		
		}
		return FALSE;
	case GDK_z:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_undo (jp);
			return TRUE;
		}
		break;
	case GDK_y:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_redo (jp);
			return TRUE;
		}
		break;
	case GDK_a:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_select_all_none (jp, TRUE);
			return TRUE;
		}
		break;
	case GDK_c:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_set_state_editing (jp);
			gnome_print_job_preview_cut_copy (jp, FALSE);
			return TRUE;
		}
		break;
	case GDK_x:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_set_state_editing (jp);
			gnome_print_job_preview_cut_copy (jp, TRUE);
			return TRUE;
		}
		break;
	case GDK_v:
		if (event->state & GDK_CONTROL_MASK) {
			gnome_print_job_preview_set_state_editing (jp);
			gnome_print_job_preview_paste (jp);
			return TRUE;
		}
		break;
	case GDK_space:
		if (jp->state == GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) {
			if (gnome_print_job_preview_count_selected (jp) > 1) {
				g_array_index (jp->selection, gboolean,
					MIN (jp->current_page,
					jp->selection->len - 1)) =
					!g_array_index (jp->selection,
						gboolean,
						MIN (jp->current_page,
						jp->selection->len - 1));
				gnome_print_job_preview_selection_changed (jp);
			}
			return TRUE;
		}
		goto page_down;
		break;
	default:
		return FALSE;
	}

	if (domove)
		gnome_canvas_scroll_to (jp->canvas, x, y);

	g_signal_stop_emission (G_OBJECT (widget), g_signal_lookup ("key_press_event", G_OBJECT_TYPE (widget)), 0);
	
	return TRUE;
}

static void
entry_insert_text_cb (GtkEditable *editable, const gchar *text, gint length, gint *position)
{
	const gchar *p = text;

	while (p != text + length) {
		const gchar *next = g_utf8_next_char (p);

		if (!g_unichar_isdigit (g_utf8_get_char (p))) {
			g_signal_stop_emission_by_name (editable, "insert_text");
			break;
		}
		p = next;
	}
}

static gboolean 
entry_focus_out_event_cb (GtkWidget *widget, GdkEventFocus *event, GnomePrintJobPreview *jp)
{
	const gchar *text;
	gint page;

	text = gtk_entry_get_text (GTK_ENTRY(widget));
	page = atoi (text) - 1;
	
	/* Reset the page number only if really needed */
	if (page != MIN (jp->current_page, jp->selection->len - 1) + 1) {
		gchar *str;

		str = g_strdup_printf ("%d", MIN (jp->current_page,
						  jp->selection->len - 1) + 1);
		gtk_entry_set_text (GTK_ENTRY (widget), str);
		g_free (str);
	}
	return FALSE;
}

static void
on_action_toggled (GtkToggleAction *a, GnomePrintJobPreview *jp)
{
	const gchar *name = gtk_action_get_name (GTK_ACTION (a));

	if (!strcmp (name, "edit")) {
		if (gtk_toggle_action_get_active (a))
			gnome_print_job_preview_set_state_editing (jp);
		else
			gnome_print_job_preview_set_state_normal (jp);
	} else if (!strcmp (name, "theme")) {
		gboolean use_theme = gtk_toggle_action_get_active (a);
		guint i;
		
		jp->theme_compliance = use_theme;
		for (i = 0; i < jp->pages->len; i++) {
			GnomePrintJobPreviewPage p;
			
			p = g_array_index (jp->pages, GnomePrintJobPreviewPage, i);
			g_object_set (p.preview, "use_theme", use_theme, NULL);
			if (gnome_print_job_preview_page_is_visible (jp, i))
				gnome_print_job_preview_show_page (jp, i, p.n);
		}
	}
}

static void
on_drag_data_received (GtkWidget *widget, GdkDragContext *context,
		       gint x, gint y, GtkSelectionData *data,
		       guint info, guint time_, GnomePrintJobPreview *jp)
{
	guint n, no;
	GtkWidget *w, *wo;
	GnomePrintContext *meta;

	n = gnome_print_job_preview_get_page_at (jp, x, y);
	no = gnome_print_job_preview_get_page_at (jp, jp->event->button.x,
			jp->event->button.y);

	w = widget;
	wo = gtk_drag_get_source_widget (context);

	if ((w == wo) && (n == no)) {
		gtk_drag_finish (context, FALSE, FALSE, time_);
		return;
	}

	if ((w == wo) && (context->action & GDK_ACTION_MOVE)) {
		gnome_print_job_preview_cmd_move (jp, n);
		return;
	}

	meta = gnome_print_meta_new ();
	gnome_print_meta_render_data (meta, data->data, data->length);
	gnome_print_job_preview_cmd_insert (jp, n, GNOME_PRINT_META (meta));
	g_object_unref (G_OBJECT (meta));
}

static gboolean
on_drag_drop (GtkWidget *widget, GdkDragContext *context, gint x, gint y,
	      guint time_, GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_unset_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_DRAG_DROP);
	return TRUE;
}

static void
on_style_set (GtkWidget *widget, GtkStyle *previous_style,
	      GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_selection_changed (jp);
}

static GtkWindowClass *parent_class;

static void
gnome_print_job_preview_check_number_of_pages (GnomePrintJobPreview *jp)
{
	gulong nx, ny;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->selection->len == MAX (0, gnome_print_job_get_pages (jp->job)))
		return;
	gnome_print_job_preview_number_of_pages_changed (jp); 
	gnome_print_job_preview_suggest_nx_and_ny (jp, &nx, &ny);
	if ((jp->nx != nx) || (jp->ny != ny)) {
		jp->nx = nx;
		jp->ny = ny;
		gnome_print_job_preview_nx_and_ny_changed (jp);
		if (jp->nx * jp->ny >= jp->selection->len)
			gnome_print_job_preview_show_pages (jp, 0);
	}
}

static void
gnome_print_job_preview_width_height_changed (GnomePrintJobPreview *jp)
{
	GdkDisplay *d = gtk_widget_get_display (GTK_WIDGET (jp));
	GdkScreen *s = gdk_display_get_screen (d, 0);
	gint w, h;
	guint i;
	GnomeCanvasPoints *points;
	GnomeCanvasItem *item;
	GdkGeometry hints;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->pointer_l) gtk_object_destroy (GTK_OBJECT (jp->pointer_l));
	if (jp->pointer_r) gtk_object_destroy (GTK_OBJECT (jp->pointer_r));

	/* Pointer that opens to the left */
	jp->pointer_l = gnome_canvas_item_new (gnome_canvas_root (jp->canvas),
		GNOME_TYPE_CANVAS_GROUP, "x", 0., "y", 0., NULL);
	points = gnome_canvas_points_new (4);
	points->coords[0] = -jp->paw / 10.; points->coords[1] = 0.;
	points->coords[2] =             0.; points->coords[3] = 0.;
	points->coords[4] =             0.; points->coords[5] = jp->pah;
	points->coords[6] = -jp->paw / 10.; points->coords[7] = jp->pah;
	item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (jp->pointer_l),
			GNOME_TYPE_CANVAS_LINE, "width_pixels", 2,
			"points", points, "fill_color", "red", NULL);

	/* Pointer that opens to the right */
	jp->pointer_r = gnome_canvas_item_new (gnome_canvas_root (jp->canvas),
		GNOME_TYPE_CANVAS_GROUP, "x", 0., "y", 0., NULL);
	points->coords[0] = jp->paw / 10.;
	points->coords[6] = jp->paw / 10.;
	item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (jp->pointer_r),
			GNOME_TYPE_CANVAS_LINE, "width_pixels", 2,
			"points", points, "fill_color", "red", NULL);
	gnome_canvas_points_free (points);
	if (!jp->pointer_t) {
		gnome_canvas_item_hide (jp->pointer_r);
		gnome_canvas_item_hide (jp->pointer_l);
	}

	hints.base_width = jp->paw;
	hints.base_height = jp->pah;
	hints.min_width = 150;
	hints.min_height = 150;
	gtk_window_set_geometry_hints (GTK_WINDOW (jp), jp->scrolled_window,
			&hints, GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);
	w = gdk_screen_get_width (s);
	h = gdk_screen_get_height (s);
	gtk_window_set_default_size (GTK_WINDOW (jp),
			MIN (jp->paw + PAGE_PAD * 3, w * 3 / 4),
			MIN (jp->pah + PAGE_PAD * 3, h * 3 / 4));

	for (i = 0; i < (jp->pages ? jp->pages->len : 0); i++) {
		GnomePrintJobPreviewPage p;

		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, i);
		gnome_print_job_preview_update_page (jp, &p);
	}
}

static void
gnome_print_job_preview_set_width (GnomePrintJobPreview *jp, gdouble width)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->paw == width)
		return;
	jp->paw = width;
	gnome_print_job_preview_width_height_changed (jp);
}

static void
gnome_print_job_preview_set_height (GnomePrintJobPreview *jp, gdouble height)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->pah == height)
		return;
	jp->pah = height;
	gnome_print_job_preview_width_height_changed (jp);
}

static void
gnome_print_job_preview_check_paper_size (GnomePrintJobPreview *jp)
{
	gdouble a_page[6];
	const GnomePrintUnit *unit = NULL;
	ArtPoint p1, p2;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	gnome_print_config_get_length (jp->config,
			(const guchar *) GNOME_PRINT_KEY_PAPER_WIDTH, &p1.x, &unit);
	gnome_print_convert_distance (&p1.x, unit, GNOME_PRINT_PS_UNIT);
	gnome_print_config_get_length (jp->config,
			(const guchar *) GNOME_PRINT_KEY_PAPER_HEIGHT, &p1.y, &unit);
	gnome_print_convert_distance (&p1.y, unit, GNOME_PRINT_PS_UNIT);
	gnome_print_config_get_transform (jp->config,
			(const guchar *) GNOME_PRINT_KEY_PAGE_ORIENTATION_MATRIX, a_page);
	art_affine_point (&p2, &p1, a_page);
	gnome_print_job_preview_set_width (jp, fabs (p2.x));
	gnome_print_job_preview_set_height (jp, fabs (p2.y));
}

static void
on_paper_size_modified (GPANode *node, guint flags, GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_check_paper_size (jp);
}

static void
on_page_orient_modified (GPANode *node, guint flags, GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_check_paper_size (jp);
}

static void
gnome_print_job_preview_set_config (GnomePrintJobPreview *jp,
		GnomePrintConfig *config)
{
	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (!config || GNOME_IS_PRINT_CONFIG (config));

	if (jp->config == config)
		return;
	if (jp->node_paper_size) {
		if (jp->handler_paper_size) {
			g_signal_handler_disconnect (G_OBJECT (jp->node_paper_size),
					jp->handler_paper_size);
			jp->handler_paper_size = 0;
		}
		jp->node_paper_size = NULL;
	}
	if (jp->node_page_orient) {
		if (jp->handler_page_orient) {
			g_signal_handler_disconnect (G_OBJECT (jp->node_page_orient),
					jp->handler_page_orient);
			jp->handler_page_orient = 0;
		}
		jp->node_page_orient = NULL;
	}
	if (jp->config)
		g_object_unref (G_OBJECT (jp->config));
	jp->config = config;
	if (!jp->config)
		return;
	g_object_ref (G_OBJECT (jp->config));
  jp->node_paper_size = gpa_node_get_child_from_path (
			GNOME_PRINT_CONFIG_NODE (jp->config),
			(const guchar *) GNOME_PRINT_KEY_PAPER_SIZE);
  jp->handler_paper_size = g_signal_connect (G_OBJECT (jp->node_paper_size),
			"modified", G_CALLBACK (on_paper_size_modified), jp);
	jp->node_page_orient = gpa_node_get_child_from_path (
			GNOME_PRINT_CONFIG_NODE (jp->config),
			(const guchar *) GNOME_PRINT_KEY_PAGE_ORIENTATION);
	jp->handler_page_orient = g_signal_connect (G_OBJECT (jp->node_page_orient),
			"modified", G_CALLBACK (on_page_orient_modified), jp);
	gnome_print_job_preview_check_paper_size (jp);
}

static void
on_job_notify (GObject *object, GParamSpec *pspec, GnomePrintJobPreview *jp)
{
	guint i;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (!strcmp (pspec->name, "config")) {
		GnomePrintConfig *config;

		g_object_get (object, "config", &config, NULL);
		gnome_print_job_preview_set_config (jp, config);
	}

	/* Check number of pages. */
	gnome_print_job_preview_check_number_of_pages (jp);

	/* Because we don't know if the content has changed, redraw everything. */
	for (i = 0; i < jp->pages->len; i++) {
		GnomePrintJobPreviewPage p;

		if (!gnome_print_job_preview_page_is_visible (jp, i)) continue;
		p = g_array_index (jp->pages, GnomePrintJobPreviewPage, i);
		gnome_print_job_preview_show_page (jp, i, p.n);
	}
}

static void
gnome_print_job_preview_parse_layout (GnomePrintJobPreview *jp)
{
	GnomePrintConfig *config;
	GnomePrintLayoutData *lyd;
	guint w = (210.0 * 72.0 / 25.4), h = (297.0 * 72.0 / 2.54);

	/* Calculate layout-compensated page dimensions */
	art_affine_identity (jp->pa2ly);
	config = gnome_print_job_get_config (jp->job);
	lyd = gnome_print_config_get_layout_data (config, NULL, NULL, NULL, NULL);
	gnome_print_config_unref (config);
	if (lyd) {
		GnomePrintLayout *ly;
		ly = gnome_print_layout_new_from_data (lyd);
		if (ly) {
			gdouble pp2lyI[6], pa2pp[6];
			gdouble expansion;
			ArtDRect pp, ap, tp;
			/* Find paper -> layout transformation */
			art_affine_invert (pp2lyI, ly->LYP[0].matrix);
			/* Find out, what the page dimensions should be */
			expansion = art_affine_expansion (pp2lyI);
			if (expansion > 1e-6) {
				/* Normalize */
				pp2lyI[0] /= expansion;
				pp2lyI[1] /= expansion;
				pp2lyI[2] /= expansion;
				pp2lyI[3] /= expansion;
				pp2lyI[4] = 0.0;
				pp2lyI[5] = 0.0;
				/* Find page dimensions relative to layout */
				pp.x0 = 0.0;
				pp.y0 = 0.0;
				pp.x1 = lyd->pw;
				pp.y1 = lyd->ph;
				art_drect_affine_transform (&tp, &pp, pp2lyI);
				/* Compensate with expansion */
				w = tp.x1 - tp.x0;
				h = tp.y1 - tp.y0;
			}
			/* Now compensate with feed orientation */
			art_affine_invert (pa2pp, ly->PP2PA);
			art_affine_multiply (jp->pa2ly, pa2pp, pp2lyI);
			/* Finally we need translation factors */
			/* Page box in normalized layout */
			pp.x0 = 0.0;
			pp.y0 = 0.0;
			pp.x1 = lyd->pw;
			pp.y1 = lyd->ph;
			art_drect_affine_transform (&ap, &pp, ly->PP2PA);
			art_drect_affine_transform (&tp, &ap, jp->pa2ly);
			jp->pa2ly[4] -= tp.x0;
			jp->pa2ly[5] -= tp.y0;
			/* Now, if job does PA2LY LY2PA concat it ends with scaled identity */
			gnome_print_layout_free (ly);
		}
		gnome_print_layout_data_free (lyd);
	}
	gnome_print_job_preview_set_width (jp, w);
	gnome_print_job_preview_set_height (jp, h);
}

static void
gnome_print_job_preview_set_job (GnomePrintJobPreview *jp, GnomePrintJob *job)
{
	GnomePrintConfig *config = NULL;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));
	g_return_if_fail (!job || GNOME_IS_PRINT_JOB (job));

	if (jp->job) {
		if (jp->notify_id) {
			g_signal_handler_disconnect (G_OBJECT (jp->job), jp->notify_id);
			jp->notify_id = 0;
		}
		g_object_unref (G_OBJECT (jp->job));
		jp->job = NULL;
		g_array_set_size (jp->selection, 0);
	}
	if (!job) return;

	jp->job = job;
	g_object_ref (G_OBJECT (job));
	jp->notify_id = g_signal_connect (G_OBJECT (jp->job), "notify",
			G_CALLBACK (on_job_notify), jp);
	gnome_print_job_preview_parse_layout (jp);
	gnome_print_job_preview_check_number_of_pages (jp);

	g_object_get (G_OBJECT (jp->job), "config", &config, NULL);
	gnome_print_job_preview_set_config (jp, config);
}

enum {
	PROP_0,
	PROP_NX,
	PROP_NY,
	PROP_JOB
};

static void
gnome_print_job_preview_get_property (GObject *object, guint n, GValue *v, GParamSpec *pspec)
{
	GnomePrintJobPreview *jp = GNOME_PRINT_JOB_PREVIEW (object);

	switch (n) {
	case PROP_NX:
		g_value_set_ulong (v, jp->nx_auto ? 0 : jp->nx);
		break;
	case PROP_NY:
		g_value_set_ulong (v, jp->ny_auto ? 0 : jp->ny);
		break;
	case PROP_JOB:
		g_value_set_object (v, jp->job);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_print_job_preview_set_property (GObject *object, guint n,
				      const GValue *v, GParamSpec *pspec)
{
	GnomePrintJobPreview *jp = GNOME_PRINT_JOB_PREVIEW (object);
	gulong l;

	switch (n) {
	case PROP_NX:
		l = g_value_get_ulong (v);
		if ((!l &&  jp->nx_auto) ||
		    ( l && !jp->nx_auto && (l == jp->nx)))
			return;
		jp->nx = l;
		jp->nx_auto = !jp->nx;
		gnome_print_job_preview_nx_and_ny_changed (jp);
		break;
	case PROP_NY:
		l = g_value_get_ulong (v);
		if ((!l &&  jp->ny_auto) ||
		    ( l && !jp->ny_auto && (l == jp->ny)))
			return;
		jp->ny = l;
		jp->ny_auto = !jp->ny;
		gnome_print_job_preview_nx_and_ny_changed (jp);
		break;
	case PROP_JOB:
		gnome_print_job_preview_set_job (jp, g_value_get_object (v));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, n, pspec);
	}
}

static void
gnome_print_job_preview_finalize (GObject *object)
{
	GnomePrintJobPreview *jp = GNOME_PRINT_JOB_PREVIEW (object);

	gnome_print_job_preview_set_config (jp, NULL);
	gnome_print_job_preview_set_job (jp, NULL);

	if (jp->selection != NULL) {
		g_array_free (jp->selection, TRUE);
		jp->selection = NULL;
	}

	if (jp->clipboard) {
		g_object_unref (G_OBJECT (jp->clipboard));
		jp->clipboard = NULL;
	}

	if (jp->undo) {
		gnome_print_job_preview_clear_undo (jp);
		g_array_free (jp->undo, TRUE);
		jp->undo = NULL;
	}
	if (jp->redo) {
		gnome_print_job_preview_clear_redo (jp);
		g_array_free (jp->redo, TRUE);
		jp->redo = NULL;
	}

	if (jp->pages != NULL) {
		unsigned i;
		for (i = jp->pages->len; i-- > 0 ; )
			g_object_unref (g_array_index (jp->pages, GnomePrintJobPreviewPage, i).preview);
		g_array_free (jp->pages, TRUE);
		jp->pages = NULL;
	}

	if (jp->event) {
		gdk_event_free (jp->event);
		jp->event = NULL;
	}

	if (jp->ui_manager) {
		g_object_unref (G_OBJECT (jp->ui_manager));
		jp->ui_manager = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gnome_print_job_preview_class_init (GnomePrintJobPreviewClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	parent_class = gtk_type_class (GTK_TYPE_WINDOW);

	gobject_class->finalize     = gnome_print_job_preview_finalize;
	gobject_class->set_property = gnome_print_job_preview_set_property;
	gobject_class->get_property = gnome_print_job_preview_get_property;

	g_object_class_install_property (gobject_class, PROP_NX,
		g_param_spec_ulong ("nx", _("Number of pages horizontally"),
			_("Number of pages horizontally"), 0, 0xffff, 1,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NY,
		g_param_spec_ulong ("ny", _("Number of pages vertically"),
			_("Number of pages vertically"), 0, 0xffff, 1,
			G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_JOB,
		g_param_spec_object ("job", _("Job"), _("Print job"),
			GNOME_TYPE_PRINT_JOB,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
on_realize (GtkWidget *widget, GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_zoom (jp, -1.);
}

static void
cb_clipboard_targets_changed (GtkClipboard *clipboard, GdkAtom *targets,
		gint n_targets, gpointer data)
{
	GnomePrintJobPreview *jp = GNOME_PRINT_JOB_PREVIEW (data);
	guint i;
	
	for (i = 0; (gint) i < n_targets; i++)
		if (targets[i] == gdk_atom_intern ("GNOME_PRINT_META", TRUE)) {
			gnome_print_job_preview_set_pointer_type (jp,
					GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY);
			return;
		}
	gnome_print_job_preview_unset_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY);
}

static void
gnome_print_job_preview_check_clipboard (GnomePrintJobPreview *jp)
{
	GdkDisplay *d;
	GtkClipboard *c;

	g_return_if_fail (GNOME_IS_PRINT_JOB_PREVIEW (jp));

	if (jp->state != GNOME_PRINT_JOB_PREVIEW_STATE_EDITING) return;
	d = gtk_widget_get_display (GTK_WIDGET (jp));
	c = gtk_clipboard_get_for_display (d, GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_request_targets (c, cb_clipboard_targets_changed, jp);
}

static gboolean
on_focus_in_event (GtkWidget *widget, GdkEventFocus *event)
{
	GnomePrintJobPreview *jp = GNOME_PRINT_JOB_PREVIEW (widget);

	gnome_print_job_preview_check_clipboard (jp);

	return FALSE;
}

static gboolean
on_focus_out_event (GtkWidget *widget, GdkEventFocus *event)
{
	GnomePrintJobPreview *jp = GNOME_PRINT_JOB_PREVIEW (widget);

	/* Hide the pointer */
	gnome_print_job_preview_unset_pointer_type (jp,
			GNOME_PRINT_JOB_PREVIEW_POINTER_TYPE_CUT_COPY);
	return FALSE;
}

static void
gnome_print_job_preview_init (GnomePrintJobPreview *jp)
{
	GtkWidget *vbox, *tb = NULL, *l, *status;
	const gchar *env_theme_variable;
	gchar *text, *ui_file;
	AtkObject *atko;
	GList *action_groups, *actions, *g_ptr, *a_ptr;
	GError *e = NULL;

	jp->ui_manager = g_object_new (GTK_TYPE_UI_MANAGER, NULL);
	gtk_window_add_accel_group (GTK_WINDOW (jp),
			gtk_ui_manager_get_accel_group (jp->ui_manager));

	env_theme_variable = g_getenv("GP_PREVIEW_STRICT_THEME");
	if (env_theme_variable && env_theme_variable [0])
		jp->theme_compliance = TRUE;

	jp->zoom_factor = 1.;

	jp->pages = g_array_new (TRUE, TRUE, sizeof (GnomePrintJobPreviewPage));
	jp->selection = g_array_new (TRUE, TRUE, sizeof (gboolean));

	jp->undo = g_array_new (TRUE, TRUE, sizeof (GnomePrintJobPreviewCmdData));
	jp->redo = g_array_new (TRUE, TRUE, sizeof (GnomePrintJobPreviewCmdData));

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (jp), vbox);

	/* Main */
	jp->main.group = gtk_action_group_new ("main");
	jp->main.print = gtk_action_new ("print", _("Print"), _("Prints the current file"), GTK_STOCK_PRINT);
	jp->main.close = gtk_action_new ("close", _("Close"), _("Closes print preview window"), GTK_STOCK_CLOSE);
	gtk_action_group_add_action_with_accel (jp->main.group, jp->main.print, NULL);
	gtk_action_group_add_action_with_accel (jp->main.group, jp->main.close, NULL);
	gtk_ui_manager_insert_action_group (jp->ui_manager, jp->main.group, -1);

	/* Cut, copy and paste */
	jp->ccp.group = gtk_action_group_new ("cut_copy_paste");
	jp->ccp.cut = gtk_action_new ("cut", _("Cut"), _("Cut"), GTK_STOCK_CUT);
	jp->ccp.copy = gtk_action_new ("copy", _("Copy"), _("Copy"), GTK_STOCK_COPY);
	jp->ccp.paste = gtk_action_new ("paste", _("Paste"),  _("Paste"), GTK_STOCK_PASTE);
	g_object_set (G_OBJECT (jp->ccp.cut), "sensitive", FALSE, NULL);
	g_object_set (G_OBJECT (jp->ccp.copy), "sensitive", FALSE, NULL);
	g_object_set (G_OBJECT (jp->ccp.paste), "sensitive", FALSE, NULL);
	gtk_action_group_add_action_with_accel (jp->ccp.group, jp->ccp.cut, NULL);
	gtk_action_group_add_action_with_accel (jp->ccp.group, jp->ccp.copy, NULL);
	gtk_action_group_add_action_with_accel (jp->ccp.group, jp->ccp.paste, NULL);
	gtk_ui_manager_insert_action_group (jp->ui_manager, jp->ccp.group, -1);

	/* Undo, redo */
	jp->u_r.group = gtk_action_group_new ("undo_redo");
	jp->u_r.undo = gtk_action_new ("undo", _("Undo"), _("Undo the last action"), GTK_STOCK_UNDO);
	jp->u_r.redo = gtk_action_new ("redo", _("Redo"), _("Redo the undone action"), GTK_STOCK_REDO);
	g_object_set (G_OBJECT (jp->u_r.undo), "sensitive", FALSE, NULL);
	g_object_set (G_OBJECT (jp->u_r.redo), "sensitive", FALSE, NULL);
	gtk_action_group_add_action_with_accel (jp->u_r.group, jp->u_r.undo, NULL);
	gtk_action_group_add_action_with_accel (jp->u_r.group, jp->u_r.redo, NULL);
	gtk_ui_manager_insert_action_group (jp->ui_manager, jp->u_r.group, -1);

	/* Navigation */
	jp->nav.group = gtk_action_group_new ("navigation");
	jp->nav.f = gtk_action_new ("first", _("First"), _("Show the first page"), GTK_STOCK_GOTO_FIRST);
	jp->nav.p = gtk_action_new ("previous", _("Previous"),  _("Show previous page"), GTK_STOCK_GO_BACK);
	jp->nav.n = gtk_action_new ("next", _("Next"), _("Show the next page"), GTK_STOCK_GO_FORWARD);
	jp->nav.l = gtk_action_new ("last", _("Last"), _("Show the last page"), GTK_STOCK_GOTO_LAST);
	gtk_action_group_add_action_with_accel (jp->nav.group, jp->nav.f, NULL);
	gtk_action_group_add_action_with_accel (jp->nav.group, jp->nav.p, NULL);
	gtk_action_group_add_action_with_accel (jp->nav.group, jp->nav.n, NULL);
	gtk_action_group_add_action_with_accel (jp->nav.group, jp->nav.l, NULL);
	gtk_ui_manager_insert_action_group (jp->ui_manager, jp->nav.group, -1);

	/* Zoom */
	jp->zoom.group = gtk_action_group_new ("zoom");
	/* xgettext:no-c-format */
	jp->zoom.z1 = gtk_action_new ("zoom_100", _("100%"), _("Zoom 1:1"), GTK_STOCK_ZOOM_100);
	jp->zoom.zf = gtk_action_new ("zoom_fit", _("Zoom to fit"), _("Zoom to fit the whole page"), GTK_STOCK_ZOOM_FIT);
	jp->zoom.zi = gtk_action_new ("zoom_in", _("Zoom in"), _("Zoom the page in"), GTK_STOCK_ZOOM_IN);
	jp->zoom.zo = gtk_action_new ("zoom_out", _("Zoom out"), _("Zoom the page out"), GTK_STOCK_ZOOM_OUT);
	gtk_action_group_add_action_with_accel (jp->zoom.group, jp->zoom.z1, NULL);
	gtk_action_group_add_action_with_accel (jp->zoom.group, jp->zoom.zf, NULL);
	gtk_action_group_add_action_with_accel (jp->zoom.group, jp->zoom.zi, NULL);
	gtk_action_group_add_action_with_accel (jp->zoom.group, jp->zoom.zo, NULL);
	gtk_ui_manager_insert_action_group (jp->ui_manager, jp->zoom.group, -1);

	/* Other */
	jp->other.group = gtk_action_group_new ("other");
	jp->other.multi = gtk_action_new ("multi", _("Show multiple pages"), _("Show multiple pages"), GTK_STOCK_DND_MULTIPLE);
	jp->other.edit = gtk_toggle_action_new ("edit", _("Edit"), _("Edit"), GTK_STOCK_EDIT);
	jp->other.theme = gtk_toggle_action_new ("theme", _("Use theme"), _("Use _theme colors for content"), GTK_STOCK_COLOR_PICKER);
	gtk_action_group_add_action (jp->other.group, jp->other.multi);
	gtk_action_group_add_action (jp->other.group, GTK_ACTION (jp->other.edit));
	gtk_action_group_add_action (jp->other.group, GTK_ACTION (jp->other.theme));
	gtk_ui_manager_insert_action_group (jp->ui_manager, jp->other.group, -1);

	/* Canvas */
	jp->scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_box_pack_end (GTK_BOX (vbox), jp->scrolled_window, TRUE, TRUE, 0);
	gtk_scrolled_window_set_policy (
			GTK_SCROLLED_WINDOW (jp->scrolled_window),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show (jp->scrolled_window);
	gtk_widget_push_colormap (
		gdk_screen_get_rgb_colormap (
		    gtk_widget_get_screen (jp->scrolled_window)));
	jp->canvas = GNOME_CANVAS (gnome_canvas_new_aa ());
	gtk_widget_show (GTK_WIDGET (jp->canvas));
	gtk_container_add (GTK_CONTAINER (jp->scrolled_window),
			   GTK_WIDGET (jp->canvas));
	gnome_canvas_set_center_scroll_region (jp->canvas, FALSE);
	gtk_widget_pop_colormap ();
	atko = gtk_widget_get_accessible (GTK_WIDGET (jp->canvas));
	atk_object_set_name (atko, _("Page Preview"));
	atk_object_set_description (atko, _("The preview of a page in the document to be printed"));
	gtk_widget_grab_focus (GTK_WIDGET (jp->canvas));
	gtk_drag_dest_set (GTK_WIDGET (jp->canvas),
		GTK_DEST_DEFAULT_ALL, target_table,
		G_N_ELEMENTS (target_table),
		GDK_ACTION_COPY | GDK_ACTION_MOVE);

	/* Toolbar */
	ui_file = g_build_filename (gnome_printui_job_preview_data_dir, "gnome-print-job-preview.xml", NULL);
	gtk_ui_manager_add_ui_from_file (jp->ui_manager, ui_file, &e);
	g_free (ui_file);
	if (!e)
		tb = gtk_ui_manager_get_widget (jp->ui_manager, "/StandardToolbar");
	if (e || !tb) {
		gchar *txt;

		/* Should that error message be translated? */
		txt = g_strdup_printf ("The toolbar can not be displayed: %s",
				e ? e->message : "Path '/StandardToolbar' not found");
		tb = gtk_label_new (txt);
		g_free (txt);
		gtk_label_set_line_wrap (GTK_LABEL (tb), TRUE);
		if (e) {
			g_error_free (e);
			e = NULL;
		}
	} else {
		gtk_toolbar_set_style (GTK_TOOLBAR (tb), GTK_TOOLBAR_ICONS);
		gtk_toolbar_set_orientation (GTK_TOOLBAR (tb), GTK_ORIENTATION_HORIZONTAL);
	}
	gtk_widget_show (tb);
	gtk_box_pack_start (GTK_BOX (vbox), tb, FALSE, FALSE, 0);

	/* Status ('Page x of y') */
	status = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), status, FALSE, FALSE, 3);
	l = gtk_label_new_with_mnemonic (_("_Page: "));
	gtk_box_pack_start (GTK_BOX (status), l, FALSE, FALSE, 4);
	jp->page_entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (jp->page_entry), "1");
	gtk_widget_set_size_request (jp->page_entry, 40, -1);
	gtk_box_pack_start (GTK_BOX (status), jp->page_entry, FALSE, FALSE, 0);
        gtk_label_set_mnemonic_widget (GTK_LABEL (l), jp->page_entry);
	/* xgettext : There are a set of labels and a GtkEntry of the form _Page: <entry> of {total pages} */
	gtk_box_pack_start (GTK_BOX (status), gtk_label_new (_("of")),
			    FALSE, FALSE, 8);
	text = g_strdup_printf ("%i", jp->selection->len);
	jp->last = gtk_label_new (text);
	g_free (text);
	gtk_box_pack_start (GTK_BOX (status), jp->last, FALSE, FALSE, 0);
	atko = gtk_widget_get_accessible (jp->last);
	atk_object_set_name (atko, _("Page total"));
	atk_object_set_description (atko, _("The total number of pages in the document"));
	gtk_widget_show_all (status);

	action_groups = gtk_ui_manager_get_action_groups (jp->ui_manager);
        for (g_ptr = action_groups; g_ptr != NULL ; g_ptr = g_ptr->next) {
		GtkActionGroup *ag = g_ptr->data;

		actions = gtk_action_group_list_actions (ag);
		for (a_ptr = actions ; a_ptr != NULL ; a_ptr = a_ptr->next) {
			GtkAction *a = a_ptr->data;

			gtk_action_connect_accelerator (a);
			if (GTK_IS_TOGGLE_ACTION (a))
				g_signal_connect (G_OBJECT (a), "toggled", G_CALLBACK (on_action_toggled), jp);
			else
				g_signal_connect (G_OBJECT (a), "activate", G_CALLBACK (on_action_activate), jp);
		}
		g_list_free (actions);
	}

	/* Connect the signals */
	g_signal_connect (G_OBJECT (jp->canvas), "button_press_event", G_CALLBACK (on_canvas_button_press_event), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "button_release_event", G_CALLBACK (on_canvas_button_release_event), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "motion_notify_event", G_CALLBACK (on_canvas_motion_notify_event), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "key_press_event", G_CALLBACK (on_canvas_key_press_event), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "style_set", G_CALLBACK (on_style_set), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "realize", G_CALLBACK (on_realize), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_data_received", G_CALLBACK (on_drag_data_received), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_drop", G_CALLBACK (on_drag_drop), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_data_delete", G_CALLBACK (on_drag_data_delete), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_begin", G_CALLBACK (on_drag_begin), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_end", G_CALLBACK (on_drag_end), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_data_get", G_CALLBACK (on_drag_data_get), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_motion", G_CALLBACK (on_drag_motion), jp);
	g_signal_connect (G_OBJECT (jp->canvas), "drag_leave", G_CALLBACK (on_drag_leave), jp);
	g_signal_connect (G_OBJECT (jp->page_entry), "activate", G_CALLBACK (change_page_cmd), jp);
	g_signal_connect (G_OBJECT (jp->page_entry), "insert_text", G_CALLBACK (entry_insert_text_cb), jp);
	g_signal_connect (G_OBJECT (jp->page_entry), "focus_out_event", G_CALLBACK (entry_focus_out_event_cb), jp);
	g_signal_connect (G_OBJECT (jp), "delete_event", G_CALLBACK (on_delete_event), jp);
	g_signal_connect (G_OBJECT (jp), "focus_in_event", G_CALLBACK (on_focus_in_event), jp);
	g_signal_connect (G_OBJECT (jp), "focus_out_event", G_CALLBACK (on_focus_out_event), jp);
}

GType
gnome_print_job_preview_get_type (void)
{
	static GType type;
	if (!type) {
		static const GTypeInfo info = {
			sizeof (GnomePrintJobPreviewClass),
			NULL, NULL,
			(GClassInitFunc) gnome_print_job_preview_class_init,
			NULL, NULL,
			sizeof (GnomePrintJobPreview),
			0,
			(GInstanceInitFunc) gnome_print_job_preview_init,
			NULL
		};
		type = g_type_register_static (GTK_TYPE_WINDOW, "GnomePrintJobPreview", &info, 0);
	}
	return type;
}

static gboolean
cb_clipboard_owner_changed (GtkClipboard *clipboard,
			    G_GNUC_UNUSED GdkEventOwnerChange *event,
			    GnomePrintJobPreview *jp)
{
	gnome_print_job_preview_check_clipboard (jp);
	return TRUE;
}

GtkWidget *
gnome_print_job_preview_new (GnomePrintJob *job, const guchar *title)
{
	GnomePrintJobPreview *jp;
	GtkClipboard *clipboard;

	g_return_val_if_fail (job != NULL, NULL);
	g_return_val_if_fail (GNOME_IS_PRINT_JOB (job), NULL);

	jp = g_object_new (GNOME_TYPE_PRINT_JOB_PREVIEW, "job", job, NULL);

	gtk_window_set_title (GTK_WINDOW (jp),
		title ? (gchar const *)title : _("Gnome Print Preview"));

	/* Start monitoring the clipboard. */
	clipboard = gtk_clipboard_get_for_display (
		gtk_widget_get_display (GTK_WIDGET (jp)),
		GDK_SELECTION_CLIPBOARD);
	g_signal_connect_object (G_OBJECT (clipboard), "owner_change",
		G_CALLBACK (cb_clipboard_owner_changed), jp, 0);

	return GTK_WIDGET (jp);
}
