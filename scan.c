/* SPDX-License-Identifier: GPL-2.0-only */

#include "usteer.h"


#define OP_CLASS_2G_1_13		81
#define OP_CLASS_5G_36_48		115
#define OP_CLASS_5G_52_64		118
#define OP_CLASS_5G_100_144		121
#define OP_CLASS_5G_149_169		125

static const uint8_t op_classes_5g[] = {
		OP_CLASS_5G_36_48,
		OP_CLASS_5G_52_64,
		OP_CLASS_5G_100_144,
		OP_CLASS_5G_149_169,
};

bool usteer_scan_start(struct sta_info *si)
{
	if (si->scan.state != SCAN_STATE_IDLE)
		return true;
	
	if (si->scan.end && current_time - si->scan.end < config.roam_scan_timeout)
		return false;
	
	si->scan.state = SCAN_STATE_TABLE;
	si->scan.start = current_time;
	si->scan.end = 0;
	return true;
}

void usteer_scan_stop(struct sta_info *si)
{
	if (si->scan.state == SCAN_STATE_IDLE)
		return;

	si->scan.state = SCAN_STATE_IDLE;
	si->scan.end = current_time;
	si->scan.op_class_idx = 0;
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
	switch (si->scan.state) {
		case SCAN_STATE_TABLE:
			usteer_scan_trigger(si, BEACON_MEASUREMENT_TABLE, 0, 0);
			si->scan.state++;
			break;
		case SCAN_STATE_ACTIVE_2G:
			usteer_scan_trigger(si, BEACON_MEASUREMENT_ACTIVE, OP_CLASS_2G_1_13, 0);
			si->scan.state++;
			break;
		case SCAN_STATE_PASSIVE_5G:
			usteer_scan_trigger(si, BEACON_MEASUREMENT_PASSIVE, op_classes_5g[si->scan.op_class_idx], 0);
			si->scan.op_class_idx++;
			if (si->scan.op_class_idx == ARRAY_SIZE(op_classes_5g)) {
				usteer_scan_stop(si);
			}
			break;
		default:
	}
}