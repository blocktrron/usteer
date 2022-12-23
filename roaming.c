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
#include "node.h"
#include "event.h"

static int usteer_scan_requester = -1;


static void
usteer_roam_set_state(struct sta_info *si, enum roam_trigger_state state,
		      struct uevent *ev)
{
	if (state != ROAM_TRIGGER_SCAN && usteer_scan_requester)
		usteer_scan_cancel(si, usteer_scan_requester_id);

	si->roam_state = state;
	usteer_event(ev);
}

static void
usteer_roam_sm_start_scan(struct sta_info *si, struct uevent *ev)
{
	bool inserted;
	if (!usteer_scan_requester)
		return;

	if (usteer_scan_timeout_active(si))
		return;

	if (usteer_scan_list_add_table(si, usteer_scan_requester) ||
	    usteer_scan_list_add_remote(si, 5, usteer_scan_requester))
		inserted = true;
	
	if (!inserted)
		return;

	/* Start scanning in case we are not timeout-constrained or timeout has expired */
	if (usteer_scan_start(si)) {
		usteer_roam_set_state(si, ROAM_TRIGGER_SCAN, ev);
		return;
	}

	/* We are currently in scan timeout / cooldown. */
}

static struct usteer_candidate *
usteer_roam_sm_found_better_node(struct sta_info *si, struct uevent *ev, enum roam_trigger_state next_state)
{
	struct usteer_candidate *candidate;

	candidate = usteer_policy_find_better_candidate(si);
	if (candidate)
		usteer_roam_set_state(si, next_state, ev);

	return candidate;
}

static bool
usteer_roam_trigger_sm(struct usteer_local_node *ln, struct sta_info *si)
{
	struct usteer_candidate *candidate;
	struct uevent ev = {
		.si_cur = si,
	};

	switch (si->roam_state) {
	case ROAM_TRIGGER_SCAN:
		/* Check if we've found a better node regardless of the scan-interval */
		if (usteer_roam_sm_found_better_node(si, &ev, ROAM_TRIGGER_SCAN_DONE))
			break;
		
		/* Kick back to idle state in case scan finished */
		if (si->roam_scan_finished) {
			si->roam_scan_finished = false;
			if (si->signal <= config.roam_trigger_snr)
				si->roam_tries++;

			/* Kick client in case it exceeded the max roam-attempts */
			if (config.roam_scan_tries && si->roam_tries >= config.roam_scan_tries)
				usteer_ubus_kick_client(si);
			
			usteer_roam_set_state(si, ROAM_TRIGGER_SEARCHING, &ev);
		}
		break;

	case ROAM_TRIGGER_IDLE:
		break;
	
	case ROAM_TRIGGER_SEARCHING:
		/* Check if we've found a better node regardless of the scan-interval */
		usteer_roam_sm_found_better_node(si, &ev, ROAM_TRIGGER_SCAN_DONE);

		/* Start Scan if possible */
		usteer_roam_sm_start_scan(si, &ev);
		break;

	case ROAM_TRIGGER_SCAN_DONE:
		candidate = usteer_roam_sm_found_better_node(si, &ev, ROAM_TRIGGER_SCAN_DONE);

		/* Kick back in case no better node is found */
		if (!candidate) {
			usteer_roam_set_state(si, ROAM_TRIGGER_SEARCHING, &ev);
			break;
		}
	
		if (si->signal <= config.roam_trigger_snr)
			break;

		usteer_ubus_bss_transition_request(si, 1, false, false, 100);

		if (config.roam_kick_delay)
			si->kick_time = current_time + config.roam_kick_delay;

        si->last_steer = current_time;
		usteer_roam_set_state(si, ROAM_TRIGGER_IDLE, &ev);
		break;
	}

	return false;
}

static bool
usteer_roam_sm_active(struct sta_info *si, int min_signal)
{
	if (!usteer_policy_can_perform_steer(si))
		return false;

	/* Signal has to be below scan / roam threshold */
	if (si->signal >= min_signal)
		return false;

	return true;
}

static void usteer_roam_sm_activate(struct sta_info *si, struct uevent *ev)
{
	if (si->roam_state == ROAM_TRIGGER_IDLE) {
		usteer_roam_set_state(si, ROAM_TRIGGER_SEARCHING, ev);
	}
}

void
usteer_roam_check(struct usteer_local_node *ln)
{
    struct uevent event = {
		.node_local = &ln->node,
	};
    struct uevent *ev = &event;

	struct sta_info *si;
	int min_signal;

	if (config.roam_scan_snr)
		min_signal = config.roam_scan_snr;
	else if (config.roam_trigger_snr)
		min_signal = config.roam_trigger_snr;
	else
		return;

	usteer_update_time();
	min_signal = usteer_snr_to_signal(&ln->node, min_signal);

	list_for_each_entry(si, &ln->node.sta_info, node_list) {
		if (!usteer_roam_sm_active(si, min_signal)) {
			usteer_roam_set_state(si, ROAM_TRIGGER_IDLE, ev);
			continue;
		}

		usteer_roam_sm_activate(si, ev);

		/*
		 * If the state machine kicked a client, other clients should wait
		 * until the next turn
		 */
		if (usteer_roam_trigger_sm(ln, si))
			return;
	}
}

static void usteer_roam_scan_finished_cb(struct sta_info *si)
{
	si->roam_scan_finished = true;
}

static void __usteer_init usteer_roaming_init(void) 
{
	usteer_scan_requester = usteer_scan_requester_register("ROAMING", &usteer_roam_scan_finished_cb);
}