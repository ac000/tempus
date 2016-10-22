/*
 * timer.c -	A simple timer counting upwards from 0 split out into
 *		hours, minutes & seconds.
 *
 * Copyright (C) 2016	Andrew Clayton <andrew@digital-domain.net>
 *
 * Licensed under the GNU General Public License V2
 * See COPYING
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <glib.h>

#include <gtk/gtk.h>

#define APP_NAME	"Tempus - timer"

struct widgets {
	GtkWidget *window;
	GtkWidget *start;
	GtkWidget *stop;
	GtkWidget *hours;
	GtkWidget *minutes;
	GtkWidget *seconds;
};

enum timer_states { TIMER_STOPPED = 0, TIMER_RUNNING };

static int timer_state = TIMER_STOPPED;
static double elapsed_seconds;

static void seconds_to_hms(double *hours, double *minutes, double *seconds)
{
	int secs = (int)elapsed_seconds;

	*seconds = secs % 60;
	secs /= 60;
	*minutes = secs % 60;
	*hours = secs / 60;
}

static void update_window_title(struct widgets *w)
{
	double hours;
	double minutes;
	double seconds;
	char title[32];

	seconds_to_hms(&hours, &minutes, &seconds);

	snprintf(title, sizeof(title), "%s [%s%02d:%02d:%02d]", APP_NAME,
			(timer_state == TIMER_RUNNING) ? "Rec - " : "",
			(int)hours, (int)minutes, (int)seconds);
	gtk_window_set_title(GTK_WINDOW(w->window), title);
}

static bool do_timer(struct widgets *w)
{
	double hours;
	double minutes;
	double seconds;

	if (timer_state == TIMER_STOPPED)
		return false;

	elapsed_seconds++;

	seconds_to_hms(&hours, &minutes, &seconds);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->seconds), seconds);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->minutes), minutes);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->hours), hours);

	update_window_title(w);

	return true;
}

static void cb_stop_timer(GtkButton *button, struct widgets *w)
{
	gtk_widget_set_sensitive(w->start, true);
	gtk_widget_set_sensitive(w->stop, false);

	gtk_editable_set_editable(GTK_EDITABLE(w->hours), true);
	gtk_editable_set_editable(GTK_EDITABLE(w->minutes), true);
	gtk_editable_set_editable(GTK_EDITABLE(w->seconds), true);

	timer_state = TIMER_STOPPED;
}

static void cb_start_timer(GtkButton *button, struct widgets *w)
{
	gtk_widget_set_sensitive(w->start, false);
	gtk_widget_set_sensitive(w->stop, true);

	gtk_editable_set_editable(GTK_EDITABLE(w->hours), false);
	gtk_editable_set_editable(GTK_EDITABLE(w->minutes), false);
	gtk_editable_set_editable(GTK_EDITABLE(w->seconds), false);

	/* Take into account a possibly adjusted value */
	elapsed_seconds =
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hours)) * 3600 +
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->minutes)) * 60 +
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->seconds));

	timer_state = TIMER_RUNNING;
	g_timeout_add(1000, (GSourceFunc)do_timer, w);
}

static void get_widgets(struct widgets *widgets, GtkBuilder *builder)
{
	widgets->window = GTK_WIDGET(gtk_builder_get_object(builder,
				"window"));
	widgets->hours = GTK_WIDGET(gtk_builder_get_object(builder, "hours"));
	widgets->minutes = GTK_WIDGET(gtk_builder_get_object(builder,
				"minutes"));
	widgets->seconds = GTK_WIDGET(gtk_builder_get_object(builder,
				"seconds"));
	widgets->start = GTK_WIDGET(gtk_builder_get_object(builder, "start"));
	widgets->stop = GTK_WIDGET(gtk_builder_get_object(builder, "stop"));

	g_signal_connect(G_OBJECT(widgets->start), "clicked", G_CALLBACK(
				cb_start_timer), widgets);
	g_signal_connect(G_OBJECT(widgets->stop), "clicked", G_CALLBACK(
				cb_stop_timer), widgets);
}

int main(int argc, char **argv)
{
	GtkBuilder *builder;
	GError *error = NULL;
	struct widgets *widgets;

	gtk_init(&argc, &argv);

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "timer.glade", &error)) {
		g_warning("%s", error->message);
		exit(EXIT_FAILURE);
	}

	widgets = g_slice_new(struct widgets);
	get_widgets(widgets, builder);
	gtk_builder_connect_signals(builder, widgets);
	g_object_unref(G_OBJECT(builder));

	update_window_title(widgets);
	gtk_widget_set_sensitive(widgets->stop, false);
	gtk_widget_show(widgets->window);
	gtk_main();

	g_slice_free(struct widgets, widgets);

	exit(EXIT_SUCCESS);
}
