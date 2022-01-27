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
 *   Copyright (C) 2022 David Bauer <mail@david-bauer.net> 
 */

#include "usteer.h"

bool
usteer_scan_sm_active(struct sta_info *si)
{
	return !!si->scan_data.scan_requests;
}

bool
usteer_scan_sm_request_source_active(struct sta_info *si, enum scan_request_source rs)
{
	return si->scan_data.scan_requests & (1 << rs);
}

void
usteer_scan_sm_request_source_start(struct sta_info *si, enum scan_request_source rs)
{
	si->scan_data.scan_requests |= (1 << rs);
}

void
usteer_scan_sm_request_source_stop(struct sta_info *si, enum scan_request_source rs)
{
	si->scan_data.scan_requests &= ~(1 << rs);
}

static void
usteer_scan_sm_request_source_clear(struct sta_info *si)
{
	si->scan_data.scan_requests = 0;
}

const char *
usteer_scan_state_name(enum scan_state ss)
{
	switch (ss) {
		case SCAN_IDLE:
			return "IDLE";
		case SCAN_START:
			return "START";
		case SCAN_ACTIVE_2_GHZ:
			return "ACTIVE_2_GHZ";
		case SCAN_ACTIVE_5_GHZ:
			return "ACTIVE_5_GHZ";
		case SCAN_PASSIVE_5_GHZ:
			return "PASSIVE_5_GHZ";
		case SCAN_PASSIVE_CURRENT:
			return "PASSIVE_CURRENT";
		case SCAN_DONE:
			return "DONE";
	}

	return "UNKNOWN";
}

enum scan_state
usteer_scan_sm(struct sta_info *si)
{
	uint8_t max_passive_nodes = 5;
	struct usteer_node *n = NULL;
	uint32_t i;

	if (current_time - si->scan_data.event < config.roam_scan_interval)
		return si->scan_data.state;
	
	if (!si->scan_data.scan_requests) {
		si->scan_data.state = SCAN_IDLE;
		return si->scan_data.state;
	}

	switch (si->scan_data.state) {
		case SCAN_IDLE:
			si->scan_data.last_passive_scan_idx = 0;
			si->scan_data.state++;
		case SCAN_START:
			si->scan_data.state++;
		case SCAN_ACTIVE_2_GHZ:
			usteer_ubus_send_beacon_request(si, BEACON_MEASUREMENT_ACTIVE, 81, 0);
			si->scan_data.state++;
			si->scan_data.event = current_time;
			break;
		case SCAN_ACTIVE_5_GHZ:
			usteer_ubus_send_beacon_request(si, BEACON_MEASUREMENT_ACTIVE, 115, 0);
			si->scan_data.state++;
			si->scan_data.event = current_time;
			break;
		case SCAN_PASSIVE_5_GHZ:
			/* Perform a passive scan on 5GHz. Scan the channels of the 5 most active 5GHz nodes. */
			for (i = 0; i <= si->scan_data.last_passive_scan_idx; i++) {
				n = usteer_node_get_next_neighbor(si->node, n);
				if (!n)
					break;
			}

			if (n)
				usteer_ubus_send_beacon_request(si, BEACON_MEASUREMENT_PASSIVE, n->op_class, n->channel);

			si->scan_data.last_passive_scan_idx++;

			/* Event on every scan (Tracked by scan interval) */
			si->scan_data.event = current_time;

			/* Next state if all nodes scanned */
			if (!n || si->scan_data.last_passive_scan_idx >= max_passive_nodes)
				si->scan_data.state++;
			break;
		case SCAN_PASSIVE_CURRENT:
			/* Acquire own neighbor report */
			usteer_ubus_send_beacon_request(si, BEACON_MEASUREMENT_PASSIVE, si->node->op_class, si->node->channel);
			si->scan_data.event = current_time;
			si->scan_data.state++;
			break;
		case SCAN_DONE:
			/* Clear all requests & enter IDLE state */
			usteer_scan_sm_request_source_clear(si);
			si->scan_data.state = SCAN_IDLE;
			break;
	}
	return si->scan_data.state;
}
