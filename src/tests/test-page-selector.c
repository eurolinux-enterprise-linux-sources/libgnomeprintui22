#include <libgnomeprintui/gnome-print-page-selector.h>

#include <libgnomeprint/gnome-print-filter.h>

#include <gtk/gtkwindow.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkmain.h>

static void
notify (GObject *o) {
	gchar *description = gnome_print_filter_description (o);

	g_message ("Filter: %s", description);
	g_free (description);
}

int
main (int argc, char **argv)
{
	GnomePrintFilter *filter = NULL;
	GtkWidget *d, *vb, *ps;

	gtk_init (&argc, &argv);

	d = g_object_new (GTK_TYPE_WINDOW, NULL);
	vb = g_object_new (GTK_TYPE_VBOX, NULL);
	gtk_widget_show (vb);
	gtk_container_add (GTK_CONTAINER (d), vb);
	ps = g_object_new (GNOME_TYPE_PRINT_PAGE_SELECTOR, "current", 15, NULL);
	gtk_widget_show (ps);
	gtk_box_pack_start (GTK_BOX (vb), ps, FALSE, FALSE, 0);
	g_object_get (G_OBJECT (ps), "filter", &filter, NULL);
	g_signal_connect (G_OBJECT (filter), "notify", G_CALLBACK (notify), NULL);
	ps = g_object_new (GNOME_TYPE_PRINT_PAGE_SELECTOR, "filter", filter, NULL);
	gtk_widget_show (ps);
	gtk_box_pack_start (GTK_BOX (vb), ps, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (d), "delete-event",
			G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_show (d);
	gtk_main ();

	return 0;
}
