#include "test-common.h"

#include <gtk/gtk.h>

#include <libgnomeprint/gnome-print-config.h>
#include <libgnomeprint/gnome-print-job.h>

#include <libgnomeprintui/gnome-print-layout-selector.h>

#define NUM_PAGES 8

int
main (int argc, char **argv)
{
	const GnomePrintUnit *unit;
	GnomePrintConfig *config;
	GnomePrintJob *job;
	GnomePrintContext *pc;
	GtkWidget *w, *s;
	guint i;
	gdouble width, height;
	GnomePrintFilter *f, *f_orig;
	gchar *description;

	gtk_init (&argc, &argv);

	w = g_object_new (GTK_TYPE_WINDOW, NULL);
	gtk_widget_show (w);

	config = gnome_print_config_default ();
	gnome_print_config_set (config, (const guchar *) "Printer",
			(const guchar *) "GENERIC");
	gnome_print_config_set (config, (const guchar *) GNOME_PRINT_KEY_PAPER_SIZE,
			(const guchar *) "A4");

	gnome_print_config_get_length (config,
			(const guchar *) GNOME_PRINT_KEY_PAPER_WIDTH, &width, &unit);
	gnome_print_convert_distance (&width, unit, GNOME_PRINT_PS_UNIT);
	gnome_print_config_get_length (config,
			(const guchar *) GNOME_PRINT_KEY_PAPER_HEIGHT, &height, &unit);
	gnome_print_convert_distance (&height, unit, GNOME_PRINT_PS_UNIT);

	s = g_object_new (GNOME_TYPE_PRINT_LAYOUT_SELECTOR,
			"input_width", width, "input_height", height,
			"output_width", width, "output_height", height,
			"total", NUM_PAGES, NULL);
	gtk_widget_show (s);
	gtk_container_add (GTK_CONTAINER (w), s);
	g_signal_connect (G_OBJECT (w), "delete_event",
			G_CALLBACK (gtk_main_quit), NULL);
	g_object_get (G_OBJECT (s), "filter", &f, NULL);
	g_object_ref (G_OBJECT (f));
	gtk_main ();
	description = gnome_print_filter_description (f);
	g_message ("Selected filter: %s", description);
	g_free (description);
	gnome_print_config_dump (config);

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
	description = (gchar *) gnome_print_config_get (config,
			(const guchar *) "Settings.Output.Job.Filter");
	f_orig = gnome_print_filter_new_from_description (description, NULL);
	g_free (description);
	gnome_print_filter_append_predecessor (f_orig, f);
	g_object_unref (G_OBJECT (f_orig));
	description = gnome_print_filter_description (f);
	g_message ("Using filter '%s'...", description);
	g_object_unref (G_OBJECT (f));
	gnome_print_config_set (config,
			(const guchar *) "Settings.Output.Job.Filter",
			(const guchar *) description);
	g_free (description);
	job = gnome_print_job_new (config);
	g_object_get (G_OBJECT (job), "context", &pc, NULL);
	for (i = 0; i < NUM_PAGES; i++)
		test_print_page (pc, i + 1);
	gnome_print_job_close (job);
	g_object_unref (config);
	gnome_print_job_print_to_file (job, "o.ps");
	gnome_print_job_print (job);
	g_object_unref (G_OBJECT (job));

	return 0;
}
