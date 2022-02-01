/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *   Copyright (C) 2020 embedd.ch 
 *   Copyright (C) 2020 Felix Fietkau <nbd@nbd.name> 
 *   Copyright (C) 2020 John Crispin <john@phrozen.org> 
 *   Copyright (C) 2021 David Bauer <mail@david-bauer.net> 
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "element.h"
#include "node.h"
#include "remote.h"
#include "usteer.h"
#include "neighbor_report.h"
 
#define MAX_NR_SIZE	256
#define MAX_NR_STRLEN	(MAX_NR_SIZE * 2)

char *
usteer_rrm_get_neighbor_report_data_for_candidate(struct usteer_candidate *c)
{
	struct blobmsg_policy policy[3] = {
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[3];
	char nr_buf[MAX_NR_SIZE] = {};
	static char nr_str[MAX_NR_STRLEN + 1];
	struct usteer_node *node = c->node;
	uint8_t priority = c->priority;

	if (!node->rrm_nr)
		goto out_fail;

	blobmsg_parse_array(policy, ARRAY_SIZE(tb), tb,
			    blobmsg_data(node->rrm_nr),
			    blobmsg_data_len(node->rrm_nr));
	if (!tb[2])
		goto out_fail;

	/* Add Candidate preference subelement */
	if (blobmsg_data_len(tb[2]) > MAX_NR_STRLEN + 1)
		goto out_fail;

	if (usteer_load_hex(blobmsg_get_string(tb[2]), nr_buf, MAX_NR_SIZE) < 0)
		goto out_fail;

	usteer_nr_set_subelement((uint8_t *)nr_buf, MAX_NR_SIZE, 3, &priority, sizeof(priority));

	memset(nr_str, 0, sizeof(nr_str));

	usteer_dump_hex(nr_buf, usteer_nr_len((uint8_t *)nr_buf, MAX_NR_SIZE), nr_str);
	return nr_str;

out_fail:
	return NULL;
}
