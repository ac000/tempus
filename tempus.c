/*
 * tempus.c -	Time tracking tool
 *
 * Copyright (C) 2016	Andrew Clayton <andrew@digital-domain.net>
 *
 * Licensed under the GNU General Public License V2
 * See COPYING
 */

#define	_XOPEN_SOURCE	500	/* strdup(3) */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <linux/limits.h>

#include <tcutil.h>
#include <tctdb.h>

#include <uuid/uuid.h>

#include <glib.h>

#include <gtk/gtk.h>

#define APP_NAME	"Tempus"

struct widgets {
	GtkWidget *window;
	GtkWidget *notebook;
	GtkWidget *list_box;
	GtkWidget *start;
	GtkWidget *stop;
	GtkWidget *save;
	GtkWidget *new;
	GtkWidget *hours;
	GtkWidget *minutes;
	GtkWidget *seconds;
	GtkWidget *company;
	GtkWidget *project;
	GtkWidget *sub_project;
};

struct list_w {
	GtkWidget *hbox;
	GtkWidget *date;
	GtkWidget *company;
	GtkWidget *project;
	GtkWidget *sub_project;
	GtkWidget *hours;
	GtkWidget *description;
	GtkWidget *edit;
};

enum timer_states { TIMER_STOPPED = 0, TIMER_RUNNING };

/* Set this to the number of seconds past midnight a new day should start */
static const int new_day_offset = 16200; /* 0430 */

static int timer_state = TIMER_STOPPED;
static double elapsed_seconds;
static GTree *tempi;
static char tempi_store[PATH_MAX];
static char tempus_id[37];	/* 36 char UUID + '\0' */

static void seconds_to_hms(double *hours, double *minutes, double *seconds)
{
	int secs = (int)elapsed_seconds;

	*seconds = secs % 60;
	secs /= 60;
	*minutes = secs % 60;
	*hours = secs / 60;
}

static bool is_editable(const char *date)
{
	time_t now = time(NULL) - new_day_offset;
	struct tm *tm = localtime(&now);

	if (atoi(date) != tm->tm_year + 1900 ||
	    atoi(date + 5) != tm->tm_mon + 1 ||
	    atoi(date + 8) != tm->tm_mday)
		return false;
	else
		return true;
}

static void free_lw(gpointer data)
{
	struct list_w *lw = data;

	gtk_widget_destroy(lw->hbox);
	free(lw);
}

static void update_window_title(struct widgets *w)
{
	double hours;
	double minutes;
	double seconds;
	char title[128];

	seconds_to_hms(&hours, &minutes, &seconds);

	snprintf(title, sizeof(title), "%s [%s%02g:%02g:%02g - %s / %s / %s]",
			APP_NAME,
			(timer_state == TIMER_RUNNING) ? "Rec - " : "",
			hours, minutes, seconds,
			gtk_entry_get_text(GTK_ENTRY(w->company)),
			gtk_entry_get_text(GTK_ENTRY(w->project)),
			gtk_entry_get_text(GTK_ENTRY(w->sub_project)));

	gtk_window_set_title(GTK_WINDOW(w->window), title);
}

static bool do_timer(struct widgets *w)
{
	double hours;
	double minutes;
	double seconds;

	if (timer_state == TIMER_STOPPED) {
		update_window_title(w);
		return false;
	}

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
	timer_state = TIMER_STOPPED;

	gtk_widget_set_sensitive(w->start, true);
	gtk_widget_set_sensitive(w->stop, false);
	gtk_widget_set_sensitive(w->save, true);
	gtk_widget_set_sensitive(w->new, true);

	gtk_editable_set_editable(GTK_EDITABLE(w->hours), true);
	gtk_editable_set_editable(GTK_EDITABLE(w->minutes), true);
	gtk_editable_set_editable(GTK_EDITABLE(w->seconds), true);
}

static void cb_start_timer(GtkButton *button, struct widgets *w)
{
	timer_state = TIMER_RUNNING;
	g_timeout_add(1000, (GSourceFunc)do_timer, w);

	/* Take into account a possibly adjusted value */
	elapsed_seconds =
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hours)) * 3600 +
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->minutes)) * 60 +
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->seconds));

	gtk_editable_set_editable(GTK_EDITABLE(w->hours), false);
	gtk_editable_set_editable(GTK_EDITABLE(w->minutes), false);
	gtk_editable_set_editable(GTK_EDITABLE(w->seconds), false);

	gtk_widget_set_sensitive(w->start, false);
	gtk_widget_set_sensitive(w->stop, true);
	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);
}

static void cb_edit(GtkButton *button, struct widgets *w)
{
	const char *id = gtk_widget_get_name(GTK_WIDGET(button));
	struct list_w *lw = g_tree_lookup(tempi, id);
	const char *time_str = gtk_entry_get_text(GTK_ENTRY(lw->hours));
	int hours;
	int minutes;
	int seconds;

	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);

	gtk_entry_set_text(GTK_ENTRY(w->company), gtk_entry_get_text(
				GTK_ENTRY(lw->company)));
	gtk_entry_set_text(GTK_ENTRY(w->project), gtk_entry_get_text(
				GTK_ENTRY(lw->project)));
	gtk_entry_set_text(GTK_ENTRY(w->sub_project), gtk_entry_get_text(
				GTK_ENTRY(lw->sub_project)));

	hours = atoi(time_str);
	minutes = atoi(time_str + 3);
	seconds = atoi(time_str + 6);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->hours), hours);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->minutes), minutes);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->seconds), seconds);

	elapsed_seconds = hours*3600 + minutes*60 + seconds;
	update_window_title(w);
	snprintf(tempus_id, sizeof(tempus_id), "%s", id);
}

static void cb_new(GtkButton *button, struct widgets *w)
{
	uuid_t uuid;

	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);

	gtk_entry_set_text(GTK_ENTRY(w->company), "");
	gtk_entry_set_text(GTK_ENTRY(w->project), "");
	gtk_entry_set_text(GTK_ENTRY(w->sub_project), "");
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->hours), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->minutes), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->seconds), 0.0);

	elapsed_seconds = 0;

	uuid_generate(uuid);
	uuid_unparse(uuid, tempus_id);
}

static struct list_w *create_list_widget(struct widgets *w,
					 const char *tempus_id)
{
	struct list_w *lw = malloc(sizeof(struct list_w));

	lw->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	lw->date = gtk_label_new("");
	lw->company = gtk_entry_new();
	lw->project = gtk_entry_new();
	lw->sub_project = gtk_entry_new();
	lw->hours = gtk_entry_new();
	lw->edit = gtk_button_new_with_label("Edit");

	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->date, false, false, 0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->company, false, false, 0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->project, false, false, 0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->sub_project, false, false,
			0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->hours, false, false, 0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->edit, false, false, 0);
	gtk_widget_set_name(lw->edit, tempus_id);
	g_signal_connect(G_OBJECT(lw->edit), "clicked", G_CALLBACK(cb_edit),
			w);

	return lw;
}

static void cb_save(GtkButton *button, struct widgets *w)
{
	time_t now = time(NULL) - new_day_offset;
	struct tm *tm = localtime(&now);
	struct list_w *lw = create_list_widget(w, tempus_id);
	double h;
	double m;
	double s;
	int pksize;
	char pkbuf[256];
	char hours[10];
	char date[11];
	TCTDB *tdb;
	TCMAP *cols;

	snprintf(date, sizeof(date), "%04d-%02d-%02d", tm->tm_year + 1900,
			tm->tm_mon + 1, tm->tm_mday);
	gtk_label_set_text(GTK_LABEL(lw->date), date);

	gtk_entry_set_text(GTK_ENTRY(lw->company), gtk_entry_get_text(
				GTK_ENTRY(w->company)));
	gtk_entry_set_text(GTK_ENTRY(lw->project), gtk_entry_get_text(
				GTK_ENTRY(w->project)));
	gtk_entry_set_text(GTK_ENTRY(lw->sub_project), gtk_entry_get_text(
				GTK_ENTRY(w->sub_project)));

	seconds_to_hms(&h, &m, &s);
	snprintf(hours, sizeof(hours), "%02g:%02g:%02g", h, m, s);
	gtk_entry_set_text(GTK_ENTRY(lw->hours), hours);

	g_tree_replace(tempi, strdup(tempus_id), lw);

	if (!is_editable(date))
		gtk_widget_set_no_show_all(lw->edit, true);

	gtk_container_add(GTK_CONTAINER(w->list_box), lw->hbox);
	gtk_box_reorder_child(GTK_BOX(w->list_box), lw->hbox, 0);
	gtk_widget_show_all(lw->hbox);

	tdb = tctdbnew();
	tctdbopen(tdb, tempi_store, TDBOWRITER | TDBOCREAT);
	pksize = snprintf(pkbuf, sizeof(pkbuf), "%s", tempus_id);
	cols = tcmapnew3("date", date,
			 "company", gtk_entry_get_text(GTK_ENTRY(w->company)),
			 "project", gtk_entry_get_text(GTK_ENTRY(w->project)),
			 "sub_project", gtk_entry_get_text(GTK_ENTRY(
					 w->sub_project)),
			 "hours", hours,
			 NULL);
	tctdbput(tdb, pkbuf, pksize, cols);
	tcmapdel(cols);
	tctdbclose(tdb);
	tctdbdel(tdb);
}

static void get_widgets(struct widgets *w, GtkBuilder *builder)
{
	w->window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
	w->notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
	w->list_box = GTK_WIDGET(gtk_builder_get_object(builder, "list_box"));
	w->save = GTK_WIDGET(gtk_builder_get_object(builder, "save"));
	w->new = GTK_WIDGET(gtk_builder_get_object(builder, "new"));
	w->hours = GTK_WIDGET(gtk_builder_get_object(builder, "hours"));
	w->minutes = GTK_WIDGET(gtk_builder_get_object(builder, "minutes"));
	w->seconds = GTK_WIDGET(gtk_builder_get_object(builder, "seconds"));
	w->start = GTK_WIDGET(gtk_builder_get_object(builder, "start"));
	w->stop = GTK_WIDGET(gtk_builder_get_object(builder, "stop"));
	w->company = GTK_WIDGET(gtk_builder_get_object(builder, "company"));
	w->project = GTK_WIDGET(gtk_builder_get_object(builder, "project"));
	w->sub_project = GTK_WIDGET(gtk_builder_get_object(builder,
				"sub_project"));

	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);

	g_signal_connect(G_OBJECT(w->start), "clicked", G_CALLBACK(
				cb_start_timer), w);
	g_signal_connect(G_OBJECT(w->stop), "clicked", G_CALLBACK(
				cb_stop_timer), w);
	g_signal_connect(G_OBJECT(w->save), "clicked", G_CALLBACK(cb_save), w);
	g_signal_connect(G_OBJECT(w->new), "clicked", G_CALLBACK(cb_new), w);
}

static void set_tempi_store(void)
{
	char tempi_dir[PATH_MAX];

	snprintf(tempi_dir, sizeof(tempi_dir), "%s/.local/share/tempus",
			getenv("HOME"));
	g_mkdir_with_parents(tempi_dir, 0777);

	snprintf(tempi_store, sizeof(tempi_store), "%s/tempus.tdb", tempi_dir);
}

static void load_tempi(struct widgets *w)
{
	TDBQRY *qry;
	TCTDB *tdb;
	TCLIST *res;
	int nr_items;
	int i;

	set_tempi_store();

	tdb = tctdbnew();
	tctdbopen(tdb, tempi_store, TDBOREADER);
	qry = tctdbqrynew(tdb);
	res = tctdbqrysearch(qry);

	nr_items = tclistnum(res);
	for (i = 0; i < nr_items; i++) {
		int rsize;
		struct list_w *lw;
		const char *date;
		const char *pkbuf = tclistval(res, i, &rsize);
		TCMAP *cols = tctdbget(tdb, pkbuf, rsize);

		tcmapiterinit(cols);
		date = tcmapget2(cols, "date");

		lw = create_list_widget(w, pkbuf);
		gtk_label_set_text(GTK_LABEL(lw->date), date);
		gtk_entry_set_text(GTK_ENTRY(lw->company), tcmapget2(cols,
					"company"));
		gtk_entry_set_text(GTK_ENTRY(lw->project), tcmapget2(cols,
					"project"));
		gtk_entry_set_text(GTK_ENTRY(lw->sub_project), tcmapget2(cols,
					"sub_project"));
		gtk_entry_set_text(GTK_ENTRY(lw->hours), tcmapget2(cols,
					"hours"));

		tcmapdel(cols);
		g_tree_replace(tempi, strdup(pkbuf), lw);

		if (!is_editable(date))
			gtk_widget_set_no_show_all(lw->edit, true);

		gtk_container_add(GTK_CONTAINER(w->list_box), lw->hbox);
		gtk_box_reorder_child(GTK_BOX(w->list_box), lw->hbox, 0);
		gtk_widget_show_all(lw->hbox);
	}

	tclistdel(res);
	tctdbqrydel(qry);
	tctdbclose(tdb);
	tctdbdel(tdb);
}

int main(int argc, char **argv)
{
	GtkBuilder *builder;
	GError *error = NULL;
	struct widgets *widgets;
	uuid_t uuid;

	gtk_init(&argc, &argv);

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "tempus.glade", &error)) {
		g_warning("%s", error->message);
		exit(EXIT_FAILURE);
	}

	widgets = g_slice_new(struct widgets);
	get_widgets(widgets, builder);
	gtk_builder_connect_signals(builder, widgets);
	g_object_unref(G_OBJECT(builder));

	tempi = g_tree_new_full((GCompareDataFunc)g_ascii_strcasecmp, NULL,
			free, free_lw);
	load_tempi(widgets);

	uuid_generate(uuid);
	uuid_unparse(uuid, tempus_id);

	update_window_title(widgets);
	gtk_widget_show(widgets->window);
	gtk_main();

	g_slice_free(struct widgets, widgets);

	exit(EXIT_SUCCESS);
}
