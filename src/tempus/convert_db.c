/*
 * convert_db.c - Convert db from old tokyocabinet format to sqlite
 *
 * Copyright (C) 2020		Andrew Clayton <andrew@digital-domain.net>
 *
 * Licensed under the GNU General Public License V2
 * See COPYING
 */

#define _POSIX_C_SOURCE	200809L		/* strdup(3), *at(2) */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <linux/limits.h>

#include <sqlite3.h>

#include <tcutil.h>
#include <tctdb.h>

#include "tempus.h"

#define TEMPUS_TDB		"tempus.tdb"
#define TEMPUS_SQLITE		"tempus.sqlite"

#define DB_SCHEMA \
	"CREATE TABLE tempus (id INTEGER PRIMARY KEY, date TEXT, " \
	"entity TEXT, project TEXT, sub_project TEXT, duration INT, " \
	"description TEXT)"

static int opendir_containing(const char *file)
{
	char *dird;
	char *dname;
	int dfd;

	dird = strdup(file);
	dname = dirname(dird);

	dfd = open(dname, O_RDONLY);
	free(dird);

	return dfd;
}

/*
 * Rename tempus.tdb -> tempus.tdb.bak and chmod 0400 it
 */
static int backup_tdb(int dfd)
{
	int err;

	err = renameat(dfd, TEMPUS_TDB, dfd, TEMPUS_TDB ".bak");
	if (err)
		perror("renameat");

	err = fchmodat(dfd, TEMPUS_TDB ".bak", 0400, 0);
	if (err)
		perror("fchmodat");

	return err;
}

static int cleanup_err(int dfd)
{
	struct stat sb;
	int err;

	err = fstatat(dfd, TEMPUS_SQLITE, &sb, 0);
	if (err)
		return 0;

	err = unlinkat(dfd, TEMPUS_SQLITE, 0);
	if (err)
		perror("unlinkat");

	return err;
}

static void get_sql_tmp_fname(const char *tdb, char *sql)
{
	char *path = strdup(tdb);
	char *ptr = strrchr(path, '/');

	if (ptr)
		*ptr = '\0';
	snprintf(sql, NAME_MAX + 1, "%s/.%s", path, TEMPUS_SQLITE);
	free(path);
}

static int dur_to_secs(const char *duration)
{
	int hours = atoi(duration);
	int minutes = atoi(duration + 3);
	int seconds = atoi(duration + 6);

	return (hours*3600) + (minutes*60) + seconds;
}

static int populate_db(const char *tc, sqlite3 *db, bool *do_rename)
{
	sqlite3_stmt *stmt;
	TCTDB *tdb;
	TDBQRY *qry;
	TCLIST *res;
	int i;
	int nr;
	int rc;
	int ret = -1;

	tdb = tctdbnew();
	rc = tctdbopen(tdb, tc, TDBOREADER);
	if (!rc) {
		tctdbdel(tdb);
		*do_rename = false;
		return 0; /* OK, no TCTDB to convert... */
	}

	rc = sqlite3_prepare_v2(db, SQL_INSERT, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "sqlite prepare failed: %s\n",
			sqlite3_errmsg(db));
		return ret;
	}

	qry = tctdbqrynew(tdb);
	tctdbqrysetorder(qry, "date", TDBQOSTRASC);
	res = tctdbqrysearch(qry);
	nr = tclistnum(res);
	for (i = 0; i < nr; i++) {
		int rc;
		int duration;
		int rsize;
		const char *pkbuf = tclistval(res, i, &rsize);
		TCMAP *cols = tctdbget(tdb, pkbuf, rsize);

		tcmapiterinit(cols);

		sqlite3_bind_text(stmt, 1, tcmapget2(cols, "date"), -1, NULL);
		sqlite3_bind_text(stmt, 2, tcmapget2(cols, "company"), -1,
				  NULL);
		sqlite3_bind_text(stmt, 3, tcmapget2(cols, "project"), -1,
				  NULL);
		sqlite3_bind_text(stmt, 4, tcmapget2(cols, "sub_project"), -1,
				  NULL);
		duration = dur_to_secs(tcmapget2(cols, "hours"));
		sqlite3_bind_int(stmt, 5, duration);
		sqlite3_bind_text(stmt, 6, tcmapget2(cols, "description"), -1,
				  NULL);

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "sqlite execution failed: %s\n",
				sqlite3_errmsg(db));
			tcmapdel(cols);
			goto out_cleanup;

		}
		sqlite3_reset(stmt);

		tcmapdel(cols);
	}

	ret = 0;

out_cleanup:
	sqlite3_finalize(stmt);

	tclistdel(res);
	tctdbqrydel(qry);
	tctdbclose(tdb);
	tctdbdel(tdb);

	return ret;
}

int convert_db(const char *tdb)
{
	char sql_file[NAME_MAX + 1];
	struct stat sb;
	sqlite3 *db;
	bool do_rename = true;
	int dfd;
	int err;
	int rc;
	int ret = -1;

	dfd = opendir_containing(tdb);
	/*
	 * If we already have a tempus.sqlite everything is good
	 */
	err = fstatat(dfd, TEMPUS_SQLITE, &sb, 0);
	if (!err) {
		close(dfd);
		return 0;
	}

	/*
	 * Check for a temporary .tempus.sqlite file and remove it
	 * if it exists
	 */
	err = fstatat(dfd, "." TEMPUS_SQLITE, &sb, 0);
	if (!err) {
		err = unlinkat(dfd, "." TEMPUS_SQLITE, 0);
		if (err) {
			perror("unlinkat");
			close(dfd);
			return -1;
		}
	}

	get_sql_tmp_fname(tdb, sql_file); /* no sqlite3_openat() ... */
	rc = sqlite3_open(sql_file, &db);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot open database: %s\n",
			sqlite3_errmsg(db));
		goto out_close;
	}

	/* Create DB schema... */
	rc = sqlite3_exec(db, DB_SCHEMA, 0, 0, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot create database schema: %s\n",
			sqlite3_errmsg(db));
		goto out_close;
	}

	fprintf(stdout,
		"Converting tempus.tdb -> tempus.sqlite "
		"(Backing up tempus.tdb -> tempus.tdb.bak)\n");

	err = populate_db(tdb, db, &do_rename);
	if (err)
		goto out_close;

	err = renameat(dfd, "." TEMPUS_SQLITE, dfd, TEMPUS_SQLITE);
	if (err) {
		perror("renameat");
		goto out_close;
	}

	ret = 0;

out_close:
	sqlite3_close(db);

	if (ret == 0 && do_rename)
		ret = backup_tdb(dfd);
	else if (ret == -1)
		ret = cleanup_err(dfd);

	close(dfd);

	return ret;
}
