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

#ifndef __APMGR_H
#define __APMGR_H

#include <libubox/avl.h>
#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubox/kvlist.h>
#include <libubus.h>
#include "scan.h"
#include "utils.h"
#include "timeout.h"

#define NO_SIGNAL 0xff

#define __STR(x)		#x
#define _STR(x)			__STR(x)

#define APMGR_V6_MCAST_GROUP	"ff02::4150"

#define APMGR_PORT		16720 /* AP */
#define APMGR_PORT_STR		_STR(APMGR_PORT)
#define APMGR_BUFLEN		(64 * 1024)

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define MIN(a,b)	(a < b ? a : b)
#define MAX(a,b)	(a > b ? a : b)

enum usteer_event_type {
	EVENT_TYPE_PROBE,
	EVENT_TYPE_ASSOC,
	EVENT_TYPE_AUTH,
	__EVENT_TYPE_MAX,
};

enum usteer_node_type {
	NODE_TYPE_LOCAL,
	NODE_TYPE_REMOTE,
};

enum usteer_sta_connection_state {
	STA_NOT_CONNECTED = 0,
	STA_CONNECTED = 1,
	STA_DISCONNECTED = 2,
};

enum usteer_beacon_measurement_mode {
	BEACON_MEASUREMENT_PASSIVE = 0,
	BEACON_MEASUREMENT_ACTIVE = 1,
	BEACON_MEASUREMENT_TABLE = 2,
};

enum usteer_scan_state {
	SCAN_STATE_IDLE = 0,
	SCAN_STATE_SCANNING = 1,
};

struct sta_info;
struct usteer_local_node;
struct usteer_remote_host;

struct usteer_node {
	struct avl_node avl;
	struct list_head sta_info;
	struct list_head measurements;
	struct list_head candidates;

	enum usteer_node_type type;

	struct blob_attr *rrm_nr;
	struct blob_attr *node_info;
	char ssid[33];
	uint8_t bssid[6];

	bool disabled;
	int freq;
	int channel;
	int op_class;
	int noise;
	int n_assoc;
	int max_assoc;
	int load;

	struct {
		int source;
		int target;
	} roam_events;

	uint64_t created;
};

struct usteer_scan_request {
	int n_freq;
	int *freq;

	bool passive;
};

struct usteer_scan_result {
	uint8_t bssid[6];
	char ssid[33];

	int freq;
	int signal;
};

struct usteer_survey_data {
	uint16_t freq;
	int8_t noise;

	uint64_t time;
	uint64_t time_busy;
};

struct usteer_freq_data {
	uint16_t freq;

	uint8_t txpower;
	bool dfs;
};

struct usteer_node_handler {
	struct list_head list;

	void (*init_node)(struct usteer_node *);
	void (*free_node)(struct usteer_node *);
	void (*update_node)(struct usteer_node *);
	void (*update_sta)(struct usteer_node *, struct sta_info *);
	void (*get_survey)(struct usteer_node *, void *,
			   void (*cb)(void *priv, struct usteer_survey_data *d));
	void (*get_freqlist)(struct usteer_node *, void *,
			     void (*cb)(void *priv, struct usteer_freq_data *f));
	int (*scan)(struct usteer_node *, struct usteer_scan_request *,
		    void *, void (*cb)(void *priv, struct usteer_scan_result *r));
};

struct usteer_config {
	bool syslog;
	uint32_t debug_level;

	bool ipv6;
	bool local_mode;

	uint32_t sta_block_timeout;
	uint32_t local_sta_timeout;
	uint32_t local_sta_update;

	uint32_t max_retry_band;
	uint32_t seen_policy_timeout;
	uint32_t measurement_report_timeout;

	bool assoc_steering;
	bool probe_steering;

	uint32_t max_neighbor_reports;

	uint32_t remote_update_interval;
	uint32_t remote_node_timeout;

	int32_t min_snr;
	uint32_t min_snr_kick_delay;
	int32_t min_connect_snr;
	uint32_t signal_diff_threshold;

	uint32_t steer_reject_timeout;

	int32_t roam_scan_snr;
	uint32_t roam_process_timeout;

	uint32_t roam_scan_tries;
	uint32_t scan_timeout;
	uint32_t scan_interval;

	int32_t roam_trigger_snr;
	uint32_t steer_trigger_interval;

	uint32_t roam_kick_delay;

	uint32_t candidate_acceptance_factor;

	uint32_t band_steering_interval;
	int32_t band_steering_min_snr; 

	uint32_t initial_connect_delay;

	bool load_kick_enabled;
	uint32_t load_kick_threshold;
	uint32_t load_kick_delay;
	uint32_t load_kick_min_clients;
	uint32_t load_kick_reason_code;

	const char *node_up_script;
	uint32_t event_log_mask;

	struct blob_attr *ssid_list;
};

struct usteer_bss_tm_query {
	struct list_head list;

	/* Can't use sta_info here, as the STA might already be deleted */
	uint8_t sta_addr[6];
	uint8_t dialog_token;
};

struct sta_info_stats {
	uint32_t requests;
	uint32_t blocked_cur;
	uint32_t blocked_total;
	uint32_t blocked_last_time;
};

enum roam_trigger_state {
	/* Scanning inactive / candidate selection inactive */
	ROAM_TRIGGER_IDLE,
	/* Scanning active / candidate selection active */
	ROAM_TRIGGER_SCAN,
	/* Scanning inactive / candidate selection sctive */
	ROAM_TRIGGER_SEARCHING,
	/* Better candidate found */
	ROAM_TRIGGER_SCAN_DONE,
};

struct usteer_client_scan {
	struct list_head list;
	enum usteer_beacon_measurement_mode mode;
	uint8_t op_class;
	uint8_t channel;
	uint32_t request_sources;
};

struct sta_info {
	struct list_head list;
	struct list_head node_list;

	struct usteer_node *node;
	struct sta *sta;

	struct usteer_timeout timeout;

	struct sta_info_stats stats[__EVENT_TYPE_MAX];
	uint64_t created;
	uint64_t seen;

	uint64_t connected_since;
	uint64_t last_connected;

	int signal;

	uint8_t rrm;
	bool bss_transition;
	bool mbo;

	enum roam_trigger_state roam_state;
	uint8_t roam_tries;
	uint64_t roam_event;
	uint64_t roam_kick;
	bool roam_scan_finished;
	uint64_t last_steer;

	struct {
		uint8_t status_code;
		uint64_t timestamp;
	} bss_transition_response;

	struct {
		bool below_snr;
	} band_steering;

	struct {
		enum usteer_scan_state state;
		struct list_head queue;

		uint64_t last_request;
		uint64_t start;
		uint64_t end;
	} scan;

	uint64_t kick_time;

	int kick_count;

	uint32_t below_min_snr;

	uint8_t scan_band : 1;
	uint8_t connected : 2;
};

struct sta {
	struct avl_node avl;
	struct list_head nodes;
	struct list_head measurements;
	struct list_head candidates;

	uint8_t seen_2ghz : 1;
	uint8_t seen_5ghz : 1;

	uint8_t addr[6];
};

struct usteer_measurement_report {
	struct usteer_timeout timeout;

	struct list_head list;

	struct usteer_node *node;
	struct list_head node_list;

	struct sta *sta;
	struct list_head sta_list;

	uint64_t timestamp;

	uint8_t rcpi;
	uint8_t rsni;
};

enum usteer_candidate_information_source {
	CIS_UNKNOWN,
	CIS_STA_INFO,
	CIS_MEASUREMENT,
};

struct usteer_candidate {
	struct usteer_timeout timeout;

	struct list_head list;

	struct usteer_node *node;
	struct list_head node_list;

	struct sta *sta;
	struct list_head sta_list;

	enum usteer_candidate_information_source information_source;
	uint64_t information_timestamp;


	int signal;
	int snr;

	uint16_t estimated_throughput;
	uint16_t score;
};

extern struct ubus_context *ubus_ctx;
extern struct usteer_config config;
extern struct list_head node_handlers;
extern struct avl_tree stations;
extern struct ubus_object usteer_obj;
extern uint64_t current_time;
extern const char * const event_types[__EVENT_TYPE_MAX];
extern struct blob_attr *host_info_blob;

void usteer_update_time(void);
void usteer_init_defaults(void);
bool usteer_handle_sta_event(struct usteer_node *node, const uint8_t *addr,
			    enum usteer_event_type type, int freq, int signal);

int usteer_snr_to_signal(struct usteer_node *node, int snr);
int usteer_signal_to_snr(struct usteer_node *node, int signal);

void usteer_local_nodes_init(struct ubus_context *ctx);
void usteer_local_node_kick(struct usteer_local_node *ln);

int usteer_local_node_get_beacon_interval(struct usteer_local_node *ln);

struct usteer_candidate *usteer_policy_find_better_candidate(struct sta_info *si_ref);
bool usteer_policy_node_below_max_assoc(struct usteer_node *node);
bool usteer_policy_can_perform_steer(struct sta_info *si);
bool usteer_policy_is_better_candidate(struct usteer_candidate *c_ref, struct usteer_candidate *c_test);

void usteer_band_steering_perform_steer(struct usteer_local_node *ln);
void usteer_band_steering_sta_update(struct sta_info *si);
bool usteer_band_steering_is_target(struct usteer_local_node *ln, struct usteer_node *node);

void usteer_ubus_init(struct ubus_context *ctx);
void usteer_ubus_kick_client(struct sta_info *si);
int usteer_ubus_trigger_beacon_request(struct sta_info *si, enum usteer_beacon_measurement_mode mode, uint8_t op_mode, uint8_t channel);
int usteer_ubus_band_steering_request(struct sta_info *si);
int usteer_ubus_bss_transition_request(struct sta_info *si,
				       uint8_t dialog_token,
				       bool disassoc_imminent,
				       bool abridged,
				       uint8_t validity_period);

struct sta *usteer_sta_get(const uint8_t *addr, bool create);
struct sta_info *usteer_sta_info_get(struct sta *sta, struct usteer_node *node, bool *create);

bool usteer_sta_supports_beacon_measurement_mode(struct sta_info *si, enum usteer_beacon_measurement_mode mode);
bool usteer_sta_supports_link_measurement(struct sta_info *si);

void usteer_sta_disconnected(struct sta_info *si);
void usteer_sta_info_update_timeout(struct sta_info *si, int timeout);
void usteer_sta_info_update(struct sta_info *si, int signal, bool avg);

static inline const char *usteer_node_name(struct usteer_node *node)
{
	return node->avl.key;
}
void usteer_node_set_blob(struct blob_attr **dest, struct blob_attr *val);

struct usteer_local_node *usteer_local_node_by_bssid(uint8_t *bssid);
struct usteer_remote_node *usteer_remote_node_by_bssid(uint8_t *bssid);
struct usteer_node *usteer_node_by_bssid(uint8_t *bssid);

struct usteer_node *usteer_node_get_next_neighbor(struct usteer_node *current_node, struct usteer_node *last);
bool usteer_check_request(struct sta_info *si, enum usteer_event_type type);

void config_set_interfaces(struct blob_attr *data);
void config_get_interfaces(struct blob_buf *buf);

void config_set_node_up_script(struct blob_attr *data);
void config_get_node_up_script(struct blob_buf *buf);

void config_set_ssid_list(struct blob_attr *data);
void config_get_ssid_list(struct blob_buf *buf);

int usteer_interface_init(void);
void usteer_interface_add(const char *name);
void usteer_sta_node_cleanup(struct usteer_node *node);
void usteer_send_sta_update(struct sta_info *si);

int usteer_lua_init(void);
int usteer_lua_ubus_init(void);
void usteer_run_hook(const char *name, const char *arg);

void usteer_dump_node(struct blob_buf *buf, struct usteer_node *node);
void usteer_dump_host(struct blob_buf *buf, struct usteer_remote_host *host);

int usteer_measurement_get_rssi(struct usteer_measurement_report *report);

struct usteer_measurement_report * usteer_measurement_report_get(struct sta *sta, struct usteer_node *node, bool create);
void usteer_measurement_report_node_cleanup(struct usteer_node *node);
void usteer_measurement_report_sta_cleanup(struct sta *sta);
void usteer_measurement_report_del(struct usteer_measurement_report *mr);

struct usteer_measurement_report *
usteer_measurement_report_add(struct sta *sta, struct usteer_node *node, uint8_t rcpi, uint8_t rsni, uint64_t timestamp);

void usteer_candidate_node_cleanup(struct usteer_node *node);
void usteer_candidate_sta_cleanup(struct sta *sta);
struct usteer_candidate *usteer_candidate_get(struct sta *sta, struct usteer_node *node, bool create);
void usteer_candidate_del(struct usteer_candidate *c);
void usteer_candidate_list_sort(struct sta *sta);

void usteer_sta_generate_candidate_list(struct sta_info *si);

int usteer_ubus_trigger_link_measurement(struct sta_info *si);

void usteer_roam_check(struct usteer_local_node *ln);

/* Scanning */
struct usteer_scan_requester {
    struct list_head list; 
    int id;
    const char *name;
    void (*scan_finish_cb)(struct sta_info *si);
};

int usteer_scan_requester_register(const char *name, void (*scan_finish_cb)(struct sta_info *si));

bool usteer_scan_timeout_active(struct sta_info *si);
bool usteer_scan_start(struct sta_info *si);
void usteer_scan_stop(struct sta_info *si);
void usteer_scan_cancel(struct sta_info *si, uint8_t requester);
void usteer_scan_next(struct sta_info *si);

bool usteer_scan_list_add_table(struct sta_info *si, uint8_t requester);
bool usteer_scan_list_add_remote(struct sta_info *si, int count, uint8_t requester);
void usteer_scan_list_clear(struct sta_info *si);

static inline char *
usteer_beacon_measurement_mode_name(enum usteer_beacon_measurement_mode mode)
{
	if (mode == BEACON_MEASUREMENT_ACTIVE)
		return "ACTIVE";
	if (mode == BEACON_MEASUREMENT_PASSIVE)
		return "PASSIVE";
	if (mode == BEACON_MEASUREMENT_TABLE)
		return "TABLE";
	return "N/A";
}

static inline void
usteer_list_swap(struct list_head *elem1, struct list_head *elem2)
{
	struct list_head *elem1_prev = elem1->prev;
	struct list_head *elem1_next = elem1->next;

	struct list_head *elem2_prev = elem2->prev;
	struct list_head *elem2_next = elem2->next;

	if ((elem1_next == elem2 && elem2_prev ==elem1) || (elem2_next == elem1 && elem1_prev == elem2)) {
		/* Adjacent */

		elem1->prev = elem1_next;
		elem1->next = elem2_next;

		elem2->prev = elem1_prev;
		elem2->next = elem2_prev;
	} else {
		elem1->prev = elem2_prev;
		elem1->next = elem2_next;

		elem2->prev = elem1_prev;
		elem2->next = elem1_next;
	}

	/* Update adjacent heads */
	elem1->prev->next = elem1;
	elem1->next->prev = elem1;

	elem2->prev->next = elem2;
	elem2->next->prev = elem2;
}
#endif
