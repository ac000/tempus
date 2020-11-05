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

#include <tctdb.h>

#define SQL_INSERT \
	"INSERT INTO tempus " \
	"(date, entity, project, sub_project, duration, description) " \
	"VALUES (?, ?, ?, ?, ?, ?)"

#define SQL_UPDATE \
	"UPDATE tempus SET " \
	"date = ?, entity = ?, project = ?, sub_project = ?, duration = ?, " \
	"description = ? WHERE id = ?"

#endif /* _TEMPUS_H_ */
