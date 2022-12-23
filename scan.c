/* SPDX-License-Identifier: GPL-2.0-only */

#include "usteer.h"


#define OP_CLASS_2G_1_13		81
#define OP_CLASS_5G_36_48		115
#define OP_CLASS_5G_52_64		118
#define OP_CLASS_5G_100_144		121
#define OP_CLASS_5G_149_169		125

static uint8_t next_requester_id = 0;
static LIST_HEAD(scan_requesters);


struct usteer_scan_requester *usteer_scan_requester_register(const char *name, void (*scan_finish_cb)())
{
	struct usteer_scan_requester *requester;

	if (next_requester_id == 32)
		return NULL;

	requester = calloc(1, sizeof(struct usteer_scan_requester));

	if (!requester)
		return NULL;
	
	requester->id = next_requester_id;
	requester->name = name;
	requester->scan_finish_cb = scan_finish_cb;
	next_requester_id++;

	list_add(&requester->list, &scan_requesters);
	return requester;
}

static int usteer_scan_node_to_op_class(struct usteer_node *node)
{
	if (node->freq < 3000)
		return OP_CLASS_2G_1_13;

	if (node->channel <= 48)
		return OP_CLASS_5G_36_48;
	
	if (node->channel <= 64)
		return OP_CLASS_5G_52_64;
	
	if (node->channel <= 144)
		return OP_CLASS_5G_100_144;
	
	return OP_CLASS_5G_149_169;
}

static struct usteer_client_scan *usteer_scan_list_contains(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_class, uint8_t channel)
{
	struct usteer_client_scan *s;

	list_for_each_entry(s, &si->scan.queue, list) {
		if (s->mode == mode && s->op_class == op_class && s->channel == channel)
			return s;
	}

	return NULL;
}

static bool usteer_scan_list_add(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_class, uint8_t channel, struct usteer_scan_requester *requester)
{
	struct usteer_client_scan *s = usteer_scan_list_contains(si, mode, op_class, channel);

	if (!s) {
		s = calloc(1, sizeof(*s));
		if (!s)
			return false;

		list_add(&s->list, &si->scan.queue);
	}
	
	s->mode = mode;
	s->op_class = op_class;
	s->channel = channel;
	s->request_sources |= 1 << requester->id;
	return true;
}

static bool usteer_scan_list_add_node(struct sta_info *si, struct usteer_node *node, struct usteer_scan_requester *requester)
{
	if (node->freq < 3000) {
		if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_ACTIVE)) {
			return usteer_scan_list_add(si, BEACON_MEASUREMENT_ACTIVE, OP_CLASS_2G_1_13, node->channel, requester);
		} else if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_PASSIVE)) {
			return usteer_scan_list_add(si, BEACON_MEASUREMENT_ACTIVE, OP_CLASS_2G_1_13, node->channel, requester);
		}
	} else {
		/* Only add 2.4 GHz channels for active scanning. Intel prohibits active probing on 5GHz,
		 * this is however not detectable, as active probing is still flagged as supported due to
		 * the STA supporting it on 2.4GHz.
		 */
		if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_PASSIVE)) {
			return usteer_scan_list_add(si, BEACON_MEASUREMENT_PASSIVE, usteer_scan_node_to_op_class(node), node->channel, requester);
		}
	}
	return false;
}

bool usteer_scan_list_add_table(struct sta_info *si, struct usteer_scan_requester *requester)
{
	if (!usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_TABLE))
		return false;
	
	return usteer_scan_list_add(si, BEACON_MEASUREMENT_TABLE, 0, 0, requester);
}

bool usteer_scan_list_add_remote(struct sta_info *si, int count, struct usteer_scan_requester *requester)
{
	struct usteer_node *node;
	bool inserted = false;
	int i;

	/* Add Nodes based on the neighbor suitability.
	 * TODO: Score nodes not by roam procedures but instead of how often they've seen identical clients
	 * (Cell coverage overlap).
	 */
	node = usteer_node_get_next_neighbor(si->node, NULL);
	for (i = 0; i < 10 && node; i++) {
		if (usteer_scan_list_add_node(si, node, requester))
			inserted = true;
		node = usteer_node_get_next_neighbor(si->node, node);
	}
	return inserted;
}

void usteer_scan_list_clear(struct sta_info *si)
{
	struct usteer_client_scan *s, *tmp;

	list_for_each_entry_safe(s, tmp, &si->scan.queue, list) {
		list_del(&s->list);
		free(s);
	}
}

bool usteer_scan_timeout_active(struct sta_info *si)
{
	return si->scan.end && current_time - si->scan.end < config.scan_timeout;
}

bool usteer_scan_start(struct sta_info *si)
{
	if (si->scan.state != SCAN_STATE_IDLE)
		return true;
	
	if (usteer_scan_timeout_active(si))
		return false;
	
	if (list_empty(&si->scan.queue))
		return false;
	
	si->scan.state = SCAN_STATE_SCANNING;
	si->scan.start = current_time;
	si->scan.end = 0;
	return true;
}

void usteer_scan_stop(struct sta_info *si)
{
	usteer_scan_list_clear(si);

	if (si->scan.state == SCAN_STATE_IDLE)
		return;

	si->scan.state = SCAN_STATE_IDLE;
	si->scan.end = current_time;
}

void usteer_scan_cancel(struct sta_info *si, struct usteer_scan_requester *requester)
{
	struct usteer_client_scan *s, *tmp;

	list_for_each_entry_safe(s, tmp, &si->scan.queue, list) {
		s->request_sources = s->request_sources & ~(1 << requester->id);
		if (!s->request_sources) {
			list_del(&s->list);
			free(s);
		}
	}
}

static void usteer_scan_trigger(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_mode, uint8_t channel)
{
	usteer_ubus_trigger_beacon_request(si, mode, op_mode, channel);
	si->scan.last_request = current_time;
}

static void usteer_scan_notify_targets(struct sta_info *si, uint32_t notification_targets)
{
	struct usteer_scan_requester *r;

	list_for_each_entry(r, &scan_requesters, list) {
		if (notification_targets & (1 << r->id)) {
			r->scan_finish_cb(si);
		}
	}
}

void usteer_scan_next(struct sta_info *si)
{
	struct usteer_client_scan *scan_job, *s;
	uint32_t notify = 0;

	if (list_empty(&si->scan.queue)) {
		usteer_scan_stop(si);
		return;
	}

	scan_job = list_first_entry(&si->scan.queue, struct usteer_client_scan, list);
	usteer_scan_trigger(si, scan_job->mode, scan_job->op_class, scan_job->channel);

	list_del(&scan_job->list);

	/* Check if this was the last request of a requester */
	notify = scan_job->request_sources;
	list_for_each_entry(s, &si->scan.queue, list) {
		notify = notify & ~(s->request_sources);
	}

	if (notify) {
		usteer_scan_notify_targets(si, notify);
	}
	
	free(scan_job);

	if (list_empty(&si->scan.queue)) {
		usteer_scan_stop(si);
		return;
	}
}