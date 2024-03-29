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

#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>

#include <libubox/blobmsg_json.h>

#include "usteer.h"
#include "event.h"
#include "node.h"

struct ubus_context *ubus_ctx;
struct usteer_config config = {};
struct blob_attr *host_info_blob;
uint64_t current_time;
static int dump_time;

LIST_HEAD(node_handlers);

const char * const event_types[__EVENT_TYPE_MAX] = {
	[EVENT_TYPE_PROBE] = "probe",
	[EVENT_TYPE_AUTH] = "auth",
	[EVENT_TYPE_ASSOC] = "assoc",
};

int 
usteer_load_hex(char *hexstr, char *output, int output_size)
{
	char *pos = hexstr;
	size_t hexstr_len;
	int i;
	
	hexstr_len = strlen(hexstr);

	if (hexstr_len % 2 != 0 || hexstr_len > output_size * 2)
		return -1;

	for (i = 0; i < hexstr_len / 2; i++) {
		sscanf(pos, "%2hhx", &output[i]);
		pos += 2;
	}

	return i;
}

void
usteer_dump_hex(char *buf, size_t buf_size, char *output)
{
	size_t i;

	for (i = 0; i < buf_size; i++)
		sprintf(&output[i*2], "%02X", (unsigned char)buf[i]);
}

int usteer_rcpi_to_rssi(int rcpi)
{
	return (rcpi / 2) - 110;
}

void log_msg(char *msg)
{
	if (config.syslog)
		syslog(LOG_INFO, "%s\n", msg);
	else
		fprintf(stderr, "%s\n", msg);
}

void debug_msg(int level, const char *func, int line, const char *format, ...)
{
	va_list ap;

	if (config.debug_level < level)
		return;

	if (!config.syslog)
		fprintf(stderr, "[%s:%d] ", func, line);

	va_start(ap, format);
	if (config.syslog)
		vsyslog(level >= MSG_DEBUG ? LOG_DEBUG : LOG_INFO, format, ap);
	else
		vfprintf(stderr, format, ap);
	va_end(ap);

}

void debug_msg_cont(int level, const char *format, ...)
{
	va_list ap;

	if (config.debug_level < level)
		return;

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

void usteer_init_defaults(void)
{
	memset(&config, 0, sizeof(config));

	config.sta_block_timeout = 30 * 1000;
	config.local_sta_timeout = 120 * 1000;
	config.measurement_report_timeout = 120 * 1000;
	config.measurement_policy_timeout = 120 * 1000;
	config.local_sta_update = 1 * 1000;
	config.max_retry_band = 5;
	config.max_neighbor_reports = 8;
	config.seen_policy_timeout = 30 * 1000;
	config.band_steering_threshold = 5;
	config.load_balancing_threshold = 5;
	config.remote_update_interval = 1000;
	config.initial_connect_delay = 0;
	config.remote_node_timeout = 10;

	config.roam_kick_delay = 100;
	config.roam_process_timeout = 5 * 1000;
	config.roam_scan_tries = 3;
	config.roam_scan_timeout = 0;
	config.roam_scan_interval = 10 * 1000;
	config.roam_trigger_interval = 60 * 1000;

	config.min_snr_kick_delay = 5 * 1000;

	config.load_kick_enabled = false;
	config.load_kick_threshold = 75;
	config.load_kick_delay = 10 * 1000;
	config.load_kick_min_clients = 10;
	config.load_kick_reason_code = 5; /* WLAN_REASON_DISASSOC_AP_BUSY */

	config.debug_level = MSG_FATAL;
}

void usteer_update_time(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	current_time = (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options]\n"
		"Options:\n"
		" -v:           Increase debug level (repeat for more messages):\n"
		"               1: info messages\n"
		"               2: debug messages\n"
		"               3: verbose debug messages\n"
		"               4: include network messages\n"
		"               5: include extra testing messages\n"
		" -i <name>:    Connect to other instances on interface <name>\n"
		" -s:		Output log messages via syslog instead of stderr\n"
		" -D <n>:	Do not daemonize, wait for <n> seconds and print\n"
		"		remote hosts and nodes\n"
		"\n", prog);
	return 1;
}

static void
usteer_dump_timeout(struct uloop_timeout *t)
{
	struct usteer_remote_host *host;
	struct usteer_remote_node *rn;
	struct blob_buf b = {};
	char *str;
	void *c;

	blob_buf_init(&b, 0);

	c = blobmsg_open_table(&b, "hosts");
	avl_for_each_element(&remote_hosts, host, avl)
		usteer_dump_host(&b, host);
	blobmsg_close_table(&b, c);

	c = blobmsg_open_table(&b, "nodes");
	for_each_remote_node(rn)
		usteer_dump_node(&b, &rn->node);
	blobmsg_close_table(&b, c);

	str = blobmsg_format_json(b.head, true);
	blob_buf_free(&b);

	puts(str);
	free(str);

	uloop_end();
}

int main(int argc, char **argv)
{
	struct uloop_timeout dump_timer;
	int ch;

	usteer_init_defaults();

	while ((ch = getopt(argc, argv, "D:i:sv")) != -1) {
		switch(ch) {
		case 'v':
			config.debug_level++;
			break;
		case 's':
			config.syslog = true;
			break;
		case 'i':
			usteer_interface_add(optarg);
			break;
		case 'D':
			dump_time = atoi(optarg);
			break;
		default:
			return usage(argv[0]);
		}
	}

	openlog("usteer", 0, LOG_USER);

	config_set_event_log_types(NULL);
	usteer_update_time();
	uloop_init();

	ubus_ctx = ubus_connect(NULL);
	if (!ubus_ctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -1;
	}

	ubus_add_uloop(ubus_ctx);
	if (dump_time) {
		dump_timer.cb = usteer_dump_timeout;
		uloop_timeout_set(&dump_timer, dump_time * 1000);
	} else {
		usteer_ubus_init(ubus_ctx);
		usteer_local_nodes_init(ubus_ctx);
	}
	uloop_run();

	uloop_done();
	return 0;
}
