/*
 * tempus.h
 *
 * Copyright (C) 2020		Andrew Clayton <andrew@digital-domain.net>
 *
 * Licensed under the GNU General Public License V2
 * See COPYING
 */

#ifndef _TEMPUS_H_
#define _TEMPUS_H_

#include <gtk/gtk.h>

struct widgets {
	GtkWidget *window;
	GtkWidget *list_box;
	GtkWidget *start;
	GtkWidget *stop;
	GtkWidget *save;
	GtkWidget *new;
	GtkWidget *summaries;
	GtkWidget *hours;
	GtkWidget *minutes;
	GtkWidget *seconds;
	GtkWidget *company;
	GtkWidget *project;
	GtkWidget *sub_project;
	GtkWidget *description;
	GtkWidget *dialog;

	GtkListStore *companies;
	GtkListStore *projects;
	GtkListStore *sub_projects;

	GtkWidget *sum_win;

	GtkListStore *summaries_ls;
	GtkTreeModelSort *summaries_tms;
};

enum sql_column {
	SQL_COL_ID = 0,
	SQL_COL_DATE,
	SQL_COL_ENTITY,
	SQL_COL_PROJECT,
	SQL_COL_SUB_PROJECT,
	SQL_COL_DURATION,
	SQL_COL_DESCRIPTION
};

#define SQL_INSERT \
	"INSERT INTO tempus " \
	"(date, entity, project, sub_project, duration, description) " \
	"VALUES (?, ?, ?, ?, ?, ?)"

#define SQL_UPDATE \
	"UPDATE tempus SET " \
	"date = ?, entity = ?, project = ?, sub_project = ?, duration = ?, " \
	"description = ? WHERE id = ?"

extern char *secs_to_dur(int seconds, char *buf, size_t len,
			 const char *format);

#endif /* _TEMPUS_H_ */
