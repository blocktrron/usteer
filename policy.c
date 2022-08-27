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
 */

#include "usteer.h"
#include "node.h"
#include "event.h"

bool
usteer_policy_node_below_max_assoc(struct usteer_node *node)
{
	return !node->max_assoc || node->n_assoc < node->max_assoc;
}

bool
usteer_policy_is_better_candidate(struct usteer_candidate *c_ref, struct usteer_candidate *c_test)
{
	return c_ref->score * (1.0 + config.candidate_acceptance_factor * 0.01) < c_test->score;
}

struct usteer_candidate *
usteer_policy_find_better_candidate(struct sta_info *si_ref)
{
	struct usteer_candidate *c, *best_candidate = NULL, *current_candidate = NULL;
	struct sta *sta = si_ref->sta;

	list_for_each_entry(c, &sta->candidates, sta_list) {
		if (strcmp(c->node->ssid, si_ref->node->ssid))
			continue;

		if (c->node == si_ref->node) {
			current_candidate = c;
		} else if (!best_candidate || best_candidate->score < c->score) {
			best_candidate = c;
		}
	}

	if (!current_candidate || !best_candidate)
		return NULL;

	if (usteer_policy_is_better_candidate(current_candidate, best_candidate))
		return best_candidate;

	return NULL;
}

int
usteer_snr_to_signal(struct usteer_node *node, int snr)
{
	int noise = -95;

	if (snr < 0)
		return snr;

	if (node->noise)
		noise = node->noise;

	return noise + snr;
}

int
usteer_signal_to_snr(struct usteer_node *node, int signal)
{
	int noise = -95;

	if (signal > 0)
		return noise;

	if (node->noise)
		noise = node->noise;

	if (signal < noise)
		return 3;

	return abs(noise - signal);
}

bool
usteer_check_request(struct sta_info *si, enum usteer_event_type type)
{
	struct usteer_candidate *best_candidate;
	struct uevent ev = {
		.si_cur = si,
	};
	int min_signal;
	bool ret = true;

	if (type == EVENT_TYPE_PROBE && !config.probe_steering)
		goto out;

	if (type == EVENT_TYPE_AUTH)
		goto out;

	if (type == EVENT_TYPE_ASSOC) {
		/* Check if assoc request has lower signal than min_signal.
		 * If this is the case, block assoc even when assoc steering is enabled.
		 *
		 * Otherwise, the client potentially ends up in a assoc - kick loop.
		 */
		if (config.min_snr && si->signal < usteer_snr_to_signal(si->node, config.min_snr)) {
			ev.reason = UEV_REASON_LOW_SIGNAL;
			ev.threshold.cur = si->signal;
			ev.threshold.ref = usteer_snr_to_signal(si->node, config.min_snr);
			ret = false;
			goto out;
		} else if (!config.assoc_steering) {
			goto out;
		}
	}

	min_signal = usteer_snr_to_signal(si->node, config.min_connect_snr);
	if (si->signal < min_signal) {
		ev.reason = UEV_REASON_LOW_SIGNAL;
		ev.threshold.cur = si->signal;
		ev.threshold.ref = min_signal;
		ret = false;
		goto out;
	}

	if (current_time - si->created < config.initial_connect_delay) {
		ev.reason = UEV_REASON_CONNECT_DELAY;
		ev.threshold.cur = current_time - si->created;
		ev.threshold.ref = config.initial_connect_delay;
		ret = false;
		goto out;
	}

	usteer_sta_generate_candidate_list(si);

	best_candidate = usteer_policy_find_better_candidate(si);
	if (!best_candidate)
		goto out;

	ev.reason = UEV_REASON_BETTER_CANDIDATE;
	ev.node_cur = si->node;
	ev.candidate_cur = usteer_candidate_get(si->sta, si->node, false);
	ev.candidate_other = best_candidate;
	ev.threshold.cur = ev.candidate_cur->score;
	ev.threshold.ref = ev.candidate_other->score;
	ret = false;

out:
	switch (type) {
	case EVENT_TYPE_PROBE:
		ev.type = ret ? UEV_PROBE_REQ_ACCEPT : UEV_PROBE_REQ_DENY;
		break;
	case EVENT_TYPE_ASSOC:
		ev.type = ret ? UEV_ASSOC_REQ_ACCEPT : UEV_ASSOC_REQ_DENY;
		break;
	case EVENT_TYPE_AUTH:
		ev.type = ret ? UEV_AUTH_REQ_ACCEPT : UEV_AUTH_REQ_DENY;
		break;
	default:
		break;
	}

	if (!ret && si->stats[type].blocked_cur >= config.max_retry_band) {
		ev.reason = UEV_REASON_RETRY_EXCEEDED;
		ev.threshold.cur = si->stats[type].blocked_cur;
		ev.threshold.ref = config.max_retry_band;
	}
	usteer_event(&ev);

	return ret;
}

static bool
is_more_kickable(struct sta_info *si_cur, struct sta_info *si_new)
{
	if (!si_cur)
		return true;

	if (si_new->kick_count > si_cur->kick_count)
		return false;

	return si_cur->signal > si_new->signal;
}

bool usteer_policy_can_perform_steer(struct sta_info *si)
{
	/* Only trigger for connected STAs */
	if (si->connected != STA_CONNECTED)
		return false;

	/* Skip on pending kick */
	if (si->kick_time)
		return false;

	/* Skip on rejected transition */
	if (si->bss_transition_response.status_code && current_time - si->bss_transition_response.timestamp < config.steer_reject_timeout)
		return false;

	/* Skip on previous kick attempt */
	if (current_time - si->roam_kick < config.steer_trigger_interval)
		return false;

	/* Skip on previous steering attempt */
	if (current_time - si->last_steer < config.steer_trigger_interval)
		return false;

	/* Skip if connection is established shorter than the trigger-interval */
	if (current_time - si->connected_since < config.steer_trigger_interval)
		return false;

	/* Also skip in case we've not yet retrieved the connection time */
	if (!si->connected_since)
		return false;
	
	return true;
}

static void
usteer_local_node_snr_kick(struct usteer_local_node *ln)
{
	unsigned int min_count = DIV_ROUND_UP(config.min_snr_kick_delay, config.local_sta_update);
	struct uevent ev = {
		.node_local = &ln->node,
	};
	struct sta_info *si;
	int min_signal;

	if (!config.min_snr)
		return;

	min_signal = usteer_snr_to_signal(&ln->node, config.min_snr);
	ev.threshold.ref = min_signal;

	list_for_each_entry(si, &ln->node.sta_info, node_list) {
		if (si->connected != STA_CONNECTED)
			continue;

		if (si->signal >= min_signal) {
			si->below_min_snr = 0;
			continue;
		} else {
			si->below_min_snr++;
		}

		if (si->below_min_snr <= min_count)
			continue;

		ev.type = UEV_SIGNAL_KICK;
		ev.threshold.cur = si->signal;
		ev.count = si->kick_count;
		usteer_event(&ev);

		usteer_ubus_kick_client(si);
		return;
	}
}

static void
usteer_local_node_load_kick(struct usteer_local_node *ln)
{
	struct usteer_node *node = &ln->node;
	struct sta_info *kick1 = NULL, *kick2 = NULL;
	struct sta_info *si;
	struct uevent ev = {
		.node_local = &ln->node,
	};
	unsigned int min_count = DIV_ROUND_UP(config.load_kick_delay, config.local_sta_update);

	if (!config.load_kick_enabled || !config.load_kick_threshold ||
	    !config.load_kick_delay)
		return;

	if (node->load < config.load_kick_threshold) {
		if (!ln->load_thr_count)
			return;

		ln->load_thr_count = 0;
		ev.type = UEV_LOAD_KICK_RESET;
		ev.threshold.cur = node->load;
		ev.threshold.ref = config.load_kick_threshold;
		goto out;
	}

	if (++ln->load_thr_count <= min_count) {
		if (ln->load_thr_count > 1)
			return;

		ev.type = UEV_LOAD_KICK_TRIGGER;
		ev.threshold.cur = node->load;
		ev.threshold.ref = config.load_kick_threshold;
		goto out;
	}

	ln->load_thr_count = 0;
	if (node->n_assoc < config.load_kick_min_clients) {
		ev.type = UEV_LOAD_KICK_MIN_CLIENTS;
		ev.threshold.cur = node->n_assoc;
		ev.threshold.ref = config.load_kick_min_clients;
		goto out;
	}

	list_for_each_entry(si, &ln->node.sta_info, node_list) {
		struct usteer_candidate *tmp;

		if (si->connected != STA_CONNECTED)
			continue;

		if (is_more_kickable(kick1, si))
			kick1 = si;

		tmp = usteer_policy_find_better_candidate(si);
		if (!tmp)
			continue;

		if (is_more_kickable(kick2, si)) {
			kick2 = si;
		}
	}

	if (!kick1) {
		ev.type = UEV_LOAD_KICK_NO_CLIENT;
		goto out;
	}

	if (kick2)
		kick1 = kick2;

	kick1->kick_count++;

	ev.type = UEV_LOAD_KICK_CLIENT;
	ev.si_cur = kick1;
	ev.count = kick1->kick_count;

	usteer_ubus_kick_client(kick1);

out:
	usteer_event(&ev);
}

static void
usteer_local_node_perform_kick(struct usteer_local_node *ln)
{
	struct sta_info *si;

	list_for_each_entry(si, &ln->node.sta_info, node_list) {
		if (!si->kick_time || si->kick_time > current_time)
			continue;

		usteer_ubus_kick_client(si);
	}
}

void
usteer_local_node_kick(struct usteer_local_node *ln)
{
	usteer_local_node_perform_kick(ln);

	usteer_local_node_snr_kick(ln);
	usteer_local_node_load_kick(ln);
	usteer_roam_check(ln);
}
