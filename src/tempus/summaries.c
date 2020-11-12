#include <stdio.h>
#include <strings.h>

#include <gtk/gtk.h>

#include <sqlite3.h>

#include "tempus.h"

enum summaries_column {
	COL_PERIOD = 0,
	COL_ENTITY,
	COL_PROJECT,
	COL_SUB_PROJECT,
	COL_DURATION,
};

static void liststore_insert(GtkListStore *ls, const char *entity,
			     const char *project, const char *sub_project,
			     const char *start, const char *end, int duration)
{
	GtkTreeIter iter;
	char period[32];
	char dbuf[16];

	if (!*start)
		return;

	snprintf(period, sizeof(period), "%s -- %s", start, end);
	secs_to_dur(duration, dbuf, sizeof(dbuf), "%u:%02u:%02u");

	gtk_list_store_append(ls, &iter);
	gtk_list_store_set(ls, &iter,
			   COL_PERIOD, period,
			   COL_ENTITY, entity,
			   COL_PROJECT, project,
			   COL_SUB_PROJECT, sub_project,
			   COL_DURATION, dbuf,
			   -1);
}

void do_summaries(struct widgets *w, const char *tempi_store)
{
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sdate[11] = "\0";
	char edate[11];
	char last_entity[256] = "\0";
	char last_project[256] = "\0";
	char last_sub_project[256] = "\0";
	int duration = 0;
	const char *sql =
		"SELECT * FROM tempus ORDER BY entity COLLATE NOCASE, "
		"project COLLATE NOCASE, sub_project COLLATE NOCASE, date";

	gtk_list_store_clear(w->summaries_ls);
	/* Sort the list with the most recent entries at the top. */
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(w->summaries_tms),
					     COL_PERIOD, GTK_SORT_DESCENDING);
	gtk_widget_show(w->sum_win);

	sqlite3_open(tempi_store, &db);
	sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const char *entity;
		const char *project;
		const char *sub_project;

		entity = (char *)sqlite3_column_text(stmt, SQL_COL_ENTITY);
		project = (char *)sqlite3_column_text(stmt, SQL_COL_PROJECT);
		sub_project = (char *)sqlite3_column_text(stmt,
							  SQL_COL_SUB_PROJECT);

		if (!*sdate)
			snprintf(sdate, sizeof(sdate), "%s",
				 (char *)sqlite3_column_text(stmt,
							     SQL_COL_DATE));
		if ((*last_entity && strcasecmp(last_entity, entity) != 0) ||
		    (*last_project &&
		     strcasecmp(last_project, project) != 0) ||
		    (*last_sub_project &&
		     strcasecmp(last_sub_project, sub_project) != 0)) {
			liststore_insert(w->summaries_ls, last_entity,
					 last_project, last_sub_project, sdate,
					 edate, duration);
			duration = 0;
			snprintf(sdate, sizeof(sdate), "%s",
				 (char *)sqlite3_column_text(stmt,
							     SQL_COL_DATE));
		}
		snprintf(edate, sizeof(edate), "%s",
			 (char *)sqlite3_column_text(stmt, SQL_COL_DATE));
		snprintf(last_entity, sizeof(last_entity), "%s", entity);
		snprintf(last_project, sizeof(last_project), "%s", project);
		snprintf(last_sub_project, sizeof(last_sub_project), "%s",
			 sub_project);
		duration += sqlite3_column_int(stmt, SQL_COL_DURATION);
	}
	/*
	 * Catch the last record (if there was one, or maybe it's
	 * the _only_ one).
	 */
	liststore_insert(w->summaries_ls, last_entity, last_project,
			 last_sub_project, sdate, edate, duration);


	sqlite3_finalize(stmt);
	sqlite3_close(db);
}
