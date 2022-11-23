/* SPDX-License-Identifier: GPL-2.0-only */

#include "usteer.h"


#define OP_CLASS_2G_1_13		81
#define OP_CLASS_5G_36_48		115
#define OP_CLASS_5G_52_64		118
#define OP_CLASS_5G_100_144		121
#define OP_CLASS_5G_149_169		125

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

static bool usteer_scan_list_contains(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_class, uint8_t channel)
{
	struct usteer_client_scan *s;

	list_for_each_entry(s, &si->scan.queue, list) {
		if (s->mode == mode && s->op_class == op_class && s->channel == channel)
			return true;
	}

	return false;
}

static void usteer_scan_list_add(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_class, uint8_t channel)
{
	struct usteer_client_scan *s;

	if (usteer_scan_list_contains(si, mode, op_class, channel))
		return;

	s = calloc(1, sizeof(*s));
	if (!s)
		return;
	
	s->mode = mode;
	s->op_class = op_class;
	s->channel = channel;

	list_add(&s->list, &si->scan.queue);
}

static void usteer_scan_list_create(struct sta_info *si)
{
	struct usteer_node *node;
	int i;
	/* Check if beacon-table is supported */
	if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_TABLE)) {
		usteer_scan_list_add(si, BEACON_MEASUREMENT_TABLE, 0, 0);
	}

	/* Add Nodes based on the neighbor suitability.
	 * TODO: Score nodes not by roam procedures but instead of how often they've seen identical clients
	 * (Cell coverage overlap).
	 */
	node = usteer_node_get_next_neighbor(si->node, NULL);
	for (i = 0; i < 10 && node; i++) {
		if (node->freq < 3000) {
			if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_ACTIVE)) {
				usteer_scan_list_add(si, BEACON_MEASUREMENT_ACTIVE, OP_CLASS_2G_1_13, node->channel);
			} else if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_PASSIVE)) {
				usteer_scan_list_add(si, BEACON_MEASUREMENT_ACTIVE, OP_CLASS_2G_1_13, node->channel);
			}
		} else {
			/* Only add 2.4 GHz channels for active scanning. Intel prohibits active probing on 5GHz,
			 * this is however not detectable, as active probing is still flagged as supported due to
			 * the STA supporting it on 2.4GHz.
			 */
			if (usteer_sta_supports_beacon_measurement_mode(si, BEACON_MEASUREMENT_PASSIVE)) {
				usteer_scan_list_add(si, BEACON_MEASUREMENT_PASSIVE, usteer_scan_node_to_op_class(node), node->channel);
			}
		}
		node = usteer_node_get_next_neighbor(si->node, node);
	}
}

void usteer_scan_list_clear(struct sta_info *si)
{
	struct usteer_client_scan *s, *tmp;

	list_for_each_entry_safe(s, tmp, &si->scan.queue, list) {
		list_del(&s->list);
		free(s);
	}
}

bool usteer_scan_start(struct sta_info *si)
{
	if (si->scan.state != SCAN_STATE_IDLE)
		return true;
	
	if (si->scan.end && current_time - si->scan.end < config.scan_timeout)
		return false;
	
	usteer_scan_list_create(si);
	si->scan.state = SCAN_STATE_SCANNING;
	si->scan.start = current_time;
	si->scan.end = 0;
	return true;
}

void usteer_scan_stop(struct sta_info *si)
{
	if (si->scan.state == SCAN_STATE_IDLE)
		return;
	
	usteer_scan_list_clear(si);

	si->scan.state = SCAN_STATE_IDLE;
	si->scan.end = current_time;
}

bool usteer_scan_active(struct sta_info *si)
{
	return !(si->scan.state == SCAN_STATE_IDLE);
}

static void usteer_scan_trigger(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_mode, uint8_t channel)
{
	usteer_ubus_trigger_beacon_request(si, mode, op_mode, channel);
	si->scan.last_request = current_time;
}

void usteer_scan_next(struct sta_info *si)
{
	struct usteer_client_scan *s;

	if (list_empty(&si->scan.queue)) {
		usteer_scan_stop(si);
		return;
	}

	s = list_first_entry(&si->scan.queue, struct usteer_client_scan, list);
	usteer_scan_trigger(si, s->mode, s->op_class, s->channel);

	list_del(&s->list);
	free(s);
}