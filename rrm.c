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

#include "node.h"
#include "remote.h"
#include "usteer.h"
#include "neighbor_report.h"
 
#define MAX_NR_SIZE	256
#define MAX_NR_STRLEN	(MAX_NR_SIZE * 2)

static uint8_t
usteer_rrm_determine_node_priority(struct usteer_node *current_node, struct usteer_node *node, struct sta_info *si)
{
	uint8_t priority;
	uint8_t penalty;
	int min_signal = 0;


	/* Check if client should stay connected to the current node */
	if (si && current_node == node) {
		if (config.roam_scan_snr)
			min_signal = usteer_snr_to_signal(current_node, config.roam_scan_snr);
		else if (config.roam_trigger_snr)
			min_signal = usteer_snr_to_signal(current_node, config.roam_trigger_snr);
		else if (config.min_snr)
			min_signal = usteer_snr_to_signal(current_node, config.min_snr);

		/* Indicate client should stay connected if load & signal level is fine */
		if ((!min_signal || si->signal > min_signal) &&
			(current_node->load < config.load_kick_threshold || current_node->n_assoc < config.load_kick_min_clients))
				return 255;
	}
	

	priority = 128;
	penalty = (node->load / config.nr_priority_interval) * config.nr_priority_interval;

	/* Increase priority for 5 GHz nodes */
	if (node->freq > 4000)
		priority += 1;

	return priority - penalty;
}

char *
usteer_rrm_get_nr_data(struct usteer_node *current_node, struct usteer_node *node, struct sta_info *si)
{
	struct blobmsg_policy policy[3] = {
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[3];
	char nr[MAX_NR_SIZE] = {};
	static char nr_str[MAX_NR_STRLEN + 1];
	uint8_t priority = usteer_rrm_determine_node_priority(current_node, node, si);

	if (!node->rrm_nr)
		goto out_fail;

	/* Remote node only adds same SSID. Required for local-node. */
	if (strcmp(current_node->ssid, node->ssid) != 0)
		goto out_fail;

	blobmsg_parse_array(policy, ARRAY_SIZE(tb), tb,
			    blobmsg_data(node->rrm_nr),
			    blobmsg_data_len(node->rrm_nr));
	if (!tb[2])
		goto out_fail;

	/* Add Candidate preference subelement */
	if (blobmsg_data_len(tb[2]) > MAX_NR_STRLEN + 1)
		goto out_fail;

	if (usteer_load_hex(blobmsg_get_string(tb[2]), nr, MAX_NR_SIZE) < 0)
		goto out_fail;

	usteer_nr_set_subelement((uint8_t *)nr, MAX_NR_SIZE, 3, &priority, sizeof(priority));

	memset(nr_str, 0, sizeof(nr_str));

	usteer_dump_hex(nr, usteer_nr_len((uint8_t *)nr, MAX_NR_SIZE), nr_str);
	return nr_str;

out_fail:
	return NULL;
}
