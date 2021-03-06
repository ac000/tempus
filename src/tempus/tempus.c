/*
 * tempus.c -	Time tracking tool
 *
 * Copyright (C) 2016 - 2020	Andrew Clayton <andrew@digital-domain.net>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <linux/limits.h>

#include <sqlite3.h>

#include <glib.h>

#include <gtk/gtk.h>

#include "short_types.h"
#include "tempus.h"
#include "summaries.h"
#include "convert_db.h"

#define APP_NAME	"Tempus"

#define REC_BTN		"\342\217\272" /* U+23FA BLACK CIRCLE FOR RECORD */

struct _data {
	char *description;
};

struct list_w {
	int id;

	GtkWidget *hbox;
	GtkWidget *date;
	GtkWidget *company;
	GtkWidget *project;
	GtkWidget *sub_project;
	GtkWidget *hours;
	GtkWidget *description;
	GtkWidget *edit;

	struct _data data;
};

enum timer_states { TIMER_STOPPED = 0, TIMER_RUNNING };

struct d_o_w {
	const char *day_full;
	const char *day_abr;
};

static const struct d_o_w days_of_week[] = {
	{ (const char *)NULL, (const char *)NULL },
	{ "Monday",	"Mo" },
	{ "Tuesday",	"Tu" },
	{ "Wednesday",	"We" },
	{ "Thursday",	"Th" },
	{ "Friday",	"Fr" },
	{ "Saturday",	"Sa" },
	{ "Sunday",	"Su"}
};

/* Number of days to show history for */
#define HISTORY_LIMIT	180

/* Set this to the number of seconds past midnight a new day should start */
static const int new_day_offset = 16200; /* 0430 */

static bool show_all;
static bool unsaved_recording;
static bool todays_date_hdr_displayed;
static int timer_state = TIMER_STOPPED;
static u32 elapsed_seconds;
static GTree *tempi;
static char tempi_store[PATH_MAX];
static long long tempus_id = -1;
static char last_date[11];	/* YYYY-MM-DD + '\0' */

static void disp_usage(void)
{
	printf("Usage: tempus [-a]\n\n");
	printf("Pass -a to show all log entries. Otherwise only the last 90 "
			"days are shown.\n");
}

static void update_elapased_seconds(const struct widgets *w)
{
	elapsed_seconds =
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->hours)) * 3600 +
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->minutes)) * 60 +
		gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->seconds));
}

static int int_cmp(gconstpointer a, gconstpointer b,
		   gpointer user_data __attribute__((unused)))
{
	long aa = GPOINTER_TO_INT(a);
	long bb = GPOINTER_TO_INT(b);

	if (aa < bb)
		return -1;
	else if (aa > bb)
		return 1;

	return 0;
}

static void seconds_to_hms(int seconds, u32 *h, u32 *m, u32 *s)
{
	u32 secs = seconds;

	*s = secs % 60;
	secs /= 60;
	*m = secs % 60;
	*h = secs / 60;
}

char *secs_to_dur(int seconds, char *buf, size_t len, const char *format)
{
	u32 secs;
	u32 minutes;
	u32 hours;
	const char *fmt = format ? format : "%02u:%02u:%02u";

	seconds_to_hms(seconds, &hours, &minutes, &secs);
	snprintf(buf, len, fmt, hours, minutes, secs);

	return buf;
}

static const char *get_day_of_week_abr(const char *date)
{
	GTimeZone *tz = g_time_zone_new_local();
	GDateTime *dt = g_date_time_new(tz, atoi(date), atoi(date+5),
			atoi(date+8), 0, 0, 0.0);

	return days_of_week[g_date_time_get_day_of_week(dt)].day_abr;
}

static bool is_today(const char *date)
{
	time_t now = time(NULL) - new_day_offset;
	struct tm *tm = localtime(&now);
	struct tm then;

	memset(&then, 0, sizeof(struct tm));
	strptime(date, "%F", &then);

	if (then.tm_year != tm->tm_year ||
	    then.tm_mon != tm->tm_mon ||
	    then.tm_mday != tm->tm_mday)
		return false;

	return true;
}

static bool entry_show(const char *date)
{
	GDate *today;
	GDate *then;
	struct tm tm;
	int days;
	bool ret = true;

	memset(&tm, 0, sizeof(struct tm));
	strptime(date, "%F", &tm);

	today = g_date_new();
	g_date_set_time_t(today, time(NULL));
	then = g_date_new_dmy(tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);

	days = g_date_days_between(then, today);
	/* Don't show log entries older than HISTORY_LIMIT days */
	if (days > HISTORY_LIMIT)
		ret = false;

	g_date_free(today);
	g_date_free(then);

	return ret;
}

static bool override_unsaved_recording(struct widgets *w)
{
	int ret = true;

	if (unsaved_recording) {
		int response = gtk_dialog_run(GTK_DIALOG(w->dialog));

		if (response == GTK_RESPONSE_CANCEL)
			ret = false;

		gtk_widget_hide(w->dialog);
	}

	return ret;
}

static void free_lw(gpointer data)
{
	struct list_w *lw = data;

	gtk_widget_destroy(lw->hbox);
	free(lw->data.description);
	free(lw);
}

static void update_window_title(struct widgets *w)
{
	u32 hours;
	u32 minutes;
	u32 seconds;
	char title[128];

	seconds_to_hms(elapsed_seconds, &hours, &minutes, &seconds);

	snprintf(title, sizeof(title), "%s%s [%s%02u:%02u:%02u - %s / %s / %s]",
			(timer_state == TIMER_RUNNING) ? REC_BTN: "", APP_NAME,
			(timer_state == TIMER_RUNNING) ? "Rec - " : "",
			hours, minutes, seconds,
			gtk_entry_get_text(GTK_ENTRY(w->company)),
			gtk_entry_get_text(GTK_ENTRY(w->project)),
			gtk_entry_get_text(GTK_ENTRY(w->sub_project)));

	gtk_window_set_title(GTK_WINDOW(w->window), title);
}

static gboolean do_timer(struct widgets *w)
{
	u32 hours;
	u32 minutes;
	u32 seconds;

	if (timer_state == TIMER_STOPPED) {
		update_window_title(w);
		return false;
	}

	elapsed_seconds++;

	seconds_to_hms(elapsed_seconds, &hours, &minutes, &seconds);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->seconds), seconds);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->minutes), minutes);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->hours), hours);

	update_window_title(w);

	return true;
}

void cb_quit(GtkButton *button __attribute__((unused)), struct widgets *w)
{
	if (override_unsaved_recording(w))
		gtk_main_quit();
}

static void cb_stop_timer(GtkButton *button __attribute__((unused)),
			  struct widgets *w)
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

static void cb_start_timer(GtkButton *button __attribute__((unused)),
			   struct widgets *w)
{
	timer_state = TIMER_RUNNING;
	g_timeout_add(1000, (GSourceFunc)do_timer, w);

	/* Take into account a possibly adjusted value */
	update_elapased_seconds(w);

	gtk_editable_set_editable(GTK_EDITABLE(w->hours), false);
	gtk_editable_set_editable(GTK_EDITABLE(w->minutes), false);
	gtk_editable_set_editable(GTK_EDITABLE(w->seconds), false);

	gtk_widget_set_sensitive(w->start, false);
	gtk_widget_set_sensitive(w->stop, true);
	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);

	unsaved_recording = true;
}

static void cb_edit(GtkButton *button, struct widgets *w)
{
	const char *s_id = gtk_widget_get_name(GTK_WIDGET(button));
	struct list_w *lw;
	const char *time_str;
	GtkTextBuffer *desc_buf;
	int hours;
	int minutes;
	int seconds;

	if (!override_unsaved_recording(w))
		return;

	unsaved_recording = false;

	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, true);

	tempus_id = atol(s_id);
	lw = g_tree_lookup(tempi, GINT_TO_POINTER(tempus_id));

	gtk_entry_set_text(GTK_ENTRY(w->company), gtk_entry_get_text(
				GTK_ENTRY(lw->company)));
	gtk_entry_set_text(GTK_ENTRY(w->project), gtk_entry_get_text(
				GTK_ENTRY(lw->project)));
	gtk_entry_set_text(GTK_ENTRY(w->sub_project), gtk_entry_get_text(
				GTK_ENTRY(lw->sub_project)));

	desc_buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(desc_buf, (lw->data.description) ?
			lw->data.description : "\0", -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(w->description), desc_buf);

	time_str = gtk_entry_get_text(GTK_ENTRY(lw->hours));
	hours = atoi(time_str);
	minutes = atoi(time_str + 3);
	seconds = atoi(time_str + 6);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->hours), hours);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->minutes), minutes);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->seconds), seconds);

	elapsed_seconds = hours*3600 + minutes*60 + seconds;
	update_window_title(w);
}

static void cb_new(GtkButton *button __attribute__((unused)),
		   struct widgets *w)
{
	GtkTextBuffer *desc_buf;

	if (!override_unsaved_recording(w))
		return;
	else
		unsaved_recording = false;

	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);

	gtk_entry_set_text(GTK_ENTRY(w->company), "");
	gtk_entry_set_text(GTK_ENTRY(w->project), "");
	gtk_entry_set_text(GTK_ENTRY(w->sub_project), "");
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->hours), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->minutes), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->seconds), 0.0);

	desc_buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(desc_buf, "", -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(w->description), desc_buf);

	elapsed_seconds = 0;
	tempus_id = -1;
}

static void create_date_hdr(struct widgets *w, const char *date, bool reorder)
{
	GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	GtkWidget *date_hdr = gtk_label_new(date);
	const char *dow = get_day_of_week_abr(date);
	char *markup;

	if (is_today(date)) {
		const char *date_fmt = "<span weight=\"bold\">\%s</span> <span size=\"small\">(\%s)</span>";

		markup = g_markup_printf_escaped(date_fmt, date, dow);
		gtk_label_set_markup(GTK_LABEL(date_hdr), markup);

		todays_date_hdr_displayed = true;
	} else {
		const char *date_fmt = "\%s <span size=\"small\">(\%s)</span>";

		markup = g_markup_printf_escaped(date_fmt, date, dow);
		gtk_label_set_markup(GTK_LABEL(date_hdr), markup);
	}
	gtk_widget_set_halign(date_hdr, GTK_ALIGN_START);
	gtk_widget_set_margin_start(date_hdr, 10);
	g_free(markup);

	gtk_box_pack_start(GTK_BOX(w->list_box), sep, false, false, 5);
	gtk_box_pack_start(GTK_BOX(w->list_box), date_hdr, false, false, 0);

	if (reorder) {
		gtk_box_reorder_child(GTK_BOX(w->list_box), sep, 0);
		gtk_box_reorder_child(GTK_BOX(w->list_box), date_hdr, 1);
	}

	gtk_widget_show(sep);
	gtk_widget_show(date_hdr);

	snprintf(last_date, sizeof(last_date), "%s", date);
}

static struct list_w *create_list_widget(struct widgets *w, int id)
{
	struct list_w *lw = malloc(sizeof(struct list_w));
	char s_id[22];

	lw->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	lw->company = gtk_entry_new();
	lw->project = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(lw->project), 25);
	lw->sub_project = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(lw->sub_project), 25);
	lw->hours = gtk_entry_new();
	lw->edit = gtk_button_new_with_label("Edit");

	lw->data.description = NULL;

	gtk_editable_set_editable(GTK_EDITABLE(lw->company), false);
	gtk_editable_set_editable(GTK_EDITABLE(lw->project), false);
	gtk_editable_set_editable(GTK_EDITABLE(lw->sub_project), false);
	gtk_editable_set_editable(GTK_EDITABLE(lw->hours), false);

	gtk_widget_set_can_focus(lw->company, false);
	gtk_widget_set_can_focus(lw->project, false);
	gtk_widget_set_can_focus(lw->sub_project, false);
	gtk_widget_set_can_focus(lw->hours, false);

	gtk_entry_set_has_frame(GTK_ENTRY(lw->company), false);
	gtk_entry_set_has_frame(GTK_ENTRY(lw->project), false);
	gtk_entry_set_has_frame(GTK_ENTRY(lw->sub_project), false);
	gtk_entry_set_has_frame(GTK_ENTRY(lw->hours), false);

	gtk_entry_set_width_chars(GTK_ENTRY(lw->hours), 10);

	gtk_widget_set_margin_start(lw->company, 48);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->company, false, false, 0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->project, false, false, 0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->sub_project, false, false,
			0);
	gtk_box_pack_start(GTK_BOX(lw->hbox), lw->hours, false, false, 0);
	gtk_box_pack_end(GTK_BOX(lw->hbox), lw->edit, false, false, 5);

	snprintf(s_id, sizeof(s_id), "%d", id);
	gtk_widget_set_name(lw->edit, s_id);
	g_signal_connect(G_OBJECT(lw->edit), "clicked", G_CALLBACK(cb_edit),
			w);

	return lw;
}

static void cb_summaries(GtkButton *button __attribute__((unused)),
			 struct widgets *w)
{
	do_summaries(w, tempi_store);
}

void cb_close_sum_win(GtkButton *button __attribute__((unused)),
		      GtkWidget *sum_win)
{
	gtk_widget_hide(sum_win);
}

static void cb_save(GtkButton *button __attribute__((unused)),
		    struct widgets *w)
{
	sqlite3_stmt *stmt;
	sqlite3 *db;
	time_t now = time(NULL) - new_day_offset;
	struct tm *tm = localtime(&now);
	struct list_w *lw;
	GtkTextBuffer *desc_buf;
	GtkTextIter start;
	GtkTextIter end;
	int rc;
	char hours[14];
	char date[11];
	const char *desc;
	const char *sql = tempus_id == -1 ? SQL_INSERT : SQL_UPDATE;

	sqlite3_open(tempi_store, &db);
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		fprintf(stderr, "sqlite prepare failed: %s\n",
			sqlite3_errmsg(db));

	strftime(date, sizeof(date), "%F", tm);	/* YYYY-MM-DD */
	if (!todays_date_hdr_displayed || strcmp(last_date, date) != 0)
		create_date_hdr(w, date, true);

	sqlite3_bind_text(stmt, 1, date, -1, NULL);
	sqlite3_bind_text(stmt, 2, gtk_entry_get_text(GTK_ENTRY(w->company)),
			  -1, NULL);
	sqlite3_bind_text(stmt, 3, gtk_entry_get_text(GTK_ENTRY(w->project)),
			  -1, NULL);
	sqlite3_bind_text(stmt, 4,
			  gtk_entry_get_text(GTK_ENTRY(w->sub_project)), -1,
			  NULL);
	sqlite3_bind_int(stmt, 5, elapsed_seconds);

	desc_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->description));
	gtk_text_buffer_get_start_iter(desc_buf, &start);
	gtk_text_buffer_get_end_iter(desc_buf, &end);
	desc = gtk_text_buffer_get_text(desc_buf, &start, &end, true);
	sqlite3_bind_text(stmt, 6, desc, -1, NULL);

	if (tempus_id > -1)
		sqlite3_bind_int(stmt, 7, tempus_id);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE)
		fprintf(stderr, "sqlite execution failed: %s\n",
			sqlite3_errmsg(db));
	sqlite3_finalize(stmt);
	if (tempus_id == -1)
		tempus_id = sqlite3_last_insert_rowid(db);
	sqlite3_close(db);

	lw = create_list_widget(w, tempus_id);
	gtk_entry_set_text(GTK_ENTRY(lw->company), gtk_entry_get_text(
				GTK_ENTRY(w->company)));
	gtk_entry_set_text(GTK_ENTRY(lw->project), gtk_entry_get_text(
				GTK_ENTRY(w->project)));
	gtk_entry_set_text(GTK_ENTRY(lw->sub_project), gtk_entry_get_text(
				GTK_ENTRY(w->sub_project)));

	if (desc && strlen(desc) > 0)
		lw->data.description = strdup(desc);
	gtk_widget_set_tooltip_text(lw->hbox, desc);

	update_elapased_seconds(w);
	secs_to_dur(elapsed_seconds, hours, sizeof(hours), NULL);
	gtk_entry_set_text(GTK_ENTRY(lw->hours), hours);

	g_tree_replace(tempi, GINT_TO_POINTER(tempus_id), lw);

	gtk_container_add(GTK_CONTAINER(w->list_box), lw->hbox);
	/*
	 * 2 for the position to take into account the GtkSeparator
	 * and GtkLabel
	 */
	gtk_box_reorder_child(GTK_BOX(w->list_box), lw->hbox, 2);
	gtk_widget_show_all(lw->hbox);

	update_window_title(w);
	unsaved_recording = false;
}

static int store_sub_project_name(gpointer key,
				  gpointer value __attribute__((unused)),
				  gpointer data)
{
	struct widgets *w = (struct widgets *)data;
	GtkTreeIter iter;

	gtk_list_store_append(w->sub_projects, &iter);
	gtk_list_store_set(w->sub_projects, &iter, 0, (char *)key, -1);

	return 0;
}

static int store_project_name(gpointer key,
			      gpointer value __attribute__((unused)),
			      gpointer data)
{
	struct widgets *w = (struct widgets *)data;
	GtkTreeIter iter;

	gtk_list_store_append(w->projects, &iter);
	gtk_list_store_set(w->projects, &iter, 0, (char *)key, -1);

	return 0;
}

static int store_company_name(gpointer key,
			      gpointer value __attribute__((unused)),
			      gpointer data)
{
	struct widgets *w = (struct widgets *)data;
	GtkTreeIter iter;

	gtk_list_store_append(w->companies, &iter);
	gtk_list_store_set(w->companies, &iter, 0, (char *)key, -1);

	return 0;
}

static int set_tempi_store(void)
{
	char tempi_dir[PATH_MAX - 14];	/* - length of "/tempus.sqlite" */
	struct stat sb;
	int err;

	snprintf(tempi_dir, sizeof(tempi_dir), "%s/.local/share/tempus",
		 getenv("HOME"));
	g_mkdir_with_parents(tempi_dir, 0777);

	/* Check if we need to do the tokyocabinet -> sqlite conversion */
	snprintf(tempi_store, sizeof(tempi_store), "%s/tempus.sqlite",
		 tempi_dir);
	err = stat(tempi_store, &sb);
	if (!err)
		return  0;

	/* Convert the db from tokyocabinet to sqlite */
	snprintf(tempi_store, sizeof(tempi_store), "%s/tempus.tdb", tempi_dir);
	err = convert_db(tempi_store);
	if (err)
		return -1;

	/* We are now using sqlite */
	snprintf(tempi_store, sizeof(tempi_store), "%s/tempus.sqlite",
		 tempi_dir);

	return 0;
}

static void load_tempi(struct widgets *w)
{
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char prev_date[11] = "\0";
	GTree *companies;
	GTree *projects;
	GTree *sub_projects;

	/*
	 * Get the list of company, project & sub project names to store
	 * for the accompanying entry auto completions. These will be
	 * automatically de-duplicated and sorted.
	 */
	companies = g_tree_new_full((GCompareDataFunc)
				    (void (*)(void))g_ascii_strcasecmp, NULL,
				    free, NULL);
	projects = g_tree_new_full((GCompareDataFunc)
				   (void (*)(void))g_ascii_strcasecmp, NULL,
				   free, NULL);
	sub_projects = g_tree_new_full((GCompareDataFunc)
				       (void (*)(void))g_ascii_strcasecmp,
				       NULL, free, NULL);

	sqlite3_open(tempi_store, &db);
	sqlite3_prepare_v2(db, "SELECT * FROM tempus ORDER by date DESC",
			   -1, &stmt, NULL);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		struct list_w *lw;
		const char *date;
		const char *desc;
		const char *entity;
		const char *proj;
		const char *sub_proj;
		char buf[16];
		int secs;
		int id;

		date = (char *)sqlite3_column_text(stmt, 1);
		if (!show_all && !entry_show(date))
			break;

		if (strcmp(prev_date, date) != 0)
			create_date_hdr(w, date, false);
		snprintf(prev_date, sizeof(prev_date), "%s", date);

		id = sqlite3_column_int(stmt, 0);
		lw = create_list_widget(w, id);
		entity = (char *)sqlite3_column_text(stmt, 2);
		gtk_entry_set_text(GTK_ENTRY(lw->company), entity);
		proj = (char *)sqlite3_column_text(stmt, 3);
		gtk_entry_set_text(GTK_ENTRY(lw->project), proj);
		sub_proj = (char *)sqlite3_column_text(stmt, 4);
		gtk_entry_set_text(GTK_ENTRY(lw->sub_project), sub_proj);
		secs = sqlite3_column_int(stmt, 5);
		gtk_entry_set_text(GTK_ENTRY(lw->hours),
				   secs_to_dur(secs, buf, sizeof(buf), NULL));

		desc = (char *)sqlite3_column_text(stmt, 6);
		if (desc && strlen(desc) > 0) {
			gtk_widget_set_tooltip_text(lw->hbox, desc);
			lw->data.description = strdup(desc);
		}

		if (strlen(entity) > 0)
			g_tree_replace(companies, strdup(entity), NULL);
		if (strlen(proj) > 0)
			g_tree_replace(projects, strdup(proj), NULL);
		if (strlen(sub_proj) > 0)
			g_tree_replace(sub_projects, strdup(sub_proj), NULL);

		g_tree_replace(tempi, GINT_TO_POINTER(id), lw);

		if (!is_today(date))
			gtk_widget_set_no_show_all(lw->edit, true);

		gtk_box_pack_start(GTK_BOX(w->list_box), lw->hbox, false,
				false, 0);
		gtk_widget_show_all(lw->hbox);
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	g_tree_foreach(companies, store_company_name, w);
	g_tree_foreach(projects, store_project_name, w);
	g_tree_foreach(sub_projects, store_sub_project_name, w);
	g_tree_destroy(companies);
	g_tree_destroy(projects);
	g_tree_destroy(sub_projects);
}

static void get_widgets(struct widgets *w, GtkBuilder *builder)
{
	w->window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
	w->list_box = GTK_WIDGET(gtk_builder_get_object(builder, "list_box"));
	w->save = GTK_WIDGET(gtk_builder_get_object(builder, "save"));
	w->new = GTK_WIDGET(gtk_builder_get_object(builder, "new"));
	w->summaries = GTK_WIDGET(gtk_builder_get_object(builder,
							 "summaries"));
	w->hours = GTK_WIDGET(gtk_builder_get_object(builder, "hours"));
	w->minutes = GTK_WIDGET(gtk_builder_get_object(builder, "minutes"));
	w->seconds = GTK_WIDGET(gtk_builder_get_object(builder, "seconds"));
	w->start = GTK_WIDGET(gtk_builder_get_object(builder, "start"));
	w->stop = GTK_WIDGET(gtk_builder_get_object(builder, "stop"));
	w->company = GTK_WIDGET(gtk_builder_get_object(builder, "company"));
	w->project = GTK_WIDGET(gtk_builder_get_object(builder, "project"));
	w->sub_project = GTK_WIDGET(gtk_builder_get_object(builder,
				"sub_project"));
	w->description = GTK_WIDGET(gtk_builder_get_object(builder,
				"description"));
	w->dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog"));
	w->companies = GTK_LIST_STORE(gtk_builder_get_object(builder,
				"companies"));
	w->projects = GTK_LIST_STORE(gtk_builder_get_object(builder,
				"projects"));
	w->sub_projects = GTK_LIST_STORE(gtk_builder_get_object(builder,
				"sub_projects"));

	w->sum_win = GTK_WIDGET(gtk_builder_get_object(builder, "sum_win"));

	w->summaries_ls = GTK_LIST_STORE(gtk_builder_get_object(builder,
								"summaries_ls"));
	w->summaries_tms = GTK_TREE_MODEL_SORT(gtk_builder_get_object(builder,
								      "summaries_tms"));

	gtk_widget_set_sensitive(w->save, false);
	gtk_widget_set_sensitive(w->new, false);

	g_signal_connect(G_OBJECT(w->start), "clicked", G_CALLBACK(
				cb_start_timer), w);
	g_signal_connect(G_OBJECT(w->stop), "clicked", G_CALLBACK(
				cb_stop_timer), w);
	g_signal_connect(G_OBJECT(w->save), "clicked", G_CALLBACK(cb_save), w);
	g_signal_connect(G_OBJECT(w->new), "clicked", G_CALLBACK(cb_new), w);
	g_signal_connect(G_OBJECT(w->summaries), "clicked",
			 G_CALLBACK(cb_summaries), w);
}

int main(int argc, char **argv)
{
	GtkBuilder *builder;
	GError *error = NULL;
	struct widgets *widgets;
	int optind;
	int err;

	while ((optind = getopt(argc, argv, "ah")) != -1) {
		switch (optind) {
		case 'a':
			show_all = true;
			break;
		case 'h':
		default:
			disp_usage();
			exit(EXIT_FAILURE);
		}
	}
	if (optind >= argc) {
		disp_usage();
		exit(EXIT_FAILURE);
	}

	err = set_tempi_store();
	if (err)
		exit(EXIT_FAILURE);

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

	tempi = g_tree_new_full((GCompareDataFunc)int_cmp, NULL, NULL,
				free_lw);
	load_tempi(widgets);

	update_window_title(widgets);
	gtk_widget_show(widgets->window);
	gtk_main();

	g_slice_free(struct widgets, widgets);

	exit(EXIT_SUCCESS);
}
