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

static bool
usteer_rrm_nr_list_insert(struct usteer_nr *nr_buf, int nr_buf_len, struct usteer_node *node)
{
	int i;

	/* Check if insert would be duplicate */
	for (i = 0; i < nr_buf_len && nr_buf[i].node != NULL; i++) {
		if (nr_buf[i].node == node)
			return false;
	}

	if (i == nr_buf_len)
		return false;

	nr_buf[i].node = node;
	return true;
}

static int
classify_load(struct usteer_node *node)
{
	return (node->load / 10) * 10;
}

static bool
nr_has_lower_load(struct usteer_nr *ref, struct usteer_nr *candidate)
{
	struct usteer_node *ref_node = ref->node;
	struct usteer_node *candidate_node = candidate->node;

	if (candidate == NULL || candidate_node == NULL) {
		return false;
	} else if (ref == NULL || ref_node == NULL) {
		return true;
	}

	int ref_load = classify_load(ref_node);
	int candidate_load = classify_load(candidate_node);

	return ref_load > candidate_load || (ref_load == candidate_load && candidate_node->freq > 4000 && ref_node->freq < 4000);
}

static bool
nr_has_higher_priority(struct usteer_nr *ref, struct usteer_nr *candidate)
{
	return ref->priority < candidate->priority;
}

static void
usteer_rrm_nr_list_sort(struct usteer_nr *nr_buf, int nr_buf_len, bool (*sort_fun)(struct usteer_nr *, struct usteer_nr *))
{
	struct usteer_nr *ref, *candidate;
	struct usteer_nr tmp;
	int i, j;

	for (i = 0; i < nr_buf_len - 1; i++) { 
		for (j = 0; j < nr_buf_len - i - 1; j++) {
			ref = &nr_buf[j];
			candidate = &nr_buf[j + 1];

			if (sort_fun(ref, candidate)) {
				memcpy(&tmp, ref, sizeof(struct usteer_nr));
				memcpy(ref, candidate, sizeof(struct usteer_nr));
				memcpy(candidate, &tmp, sizeof(struct usteer_nr));
			}
		}
	}
}

static int
usteer_rrm_nr_list_add_local_nodes(struct usteer_nr *nr_buf, int nr_buf_len,
				   struct usteer_node *node_ref,
				   enum usteer_reference_node_rating node_ref_pref)
{
	struct usteer_node *node;
	int inserted = 0;

	for_each_local_node(node) {
		if (inserted == nr_buf_len)
			return inserted;
		
		if (node_ref == node && node_ref_pref == RN_RATING_EXCLUDE) {
			continue;
		}

		if (strcmp(node->ssid, node_ref->ssid)) {
			continue;
		}

		if (usteer_rrm_nr_list_insert(nr_buf, nr_buf_len, node)) {
			inserted++;
		}
	}

	return inserted;
}

static int
usteer_rrm_nr_list_add_remote_nodes(struct usteer_nr *nr_buf, int nr_buf_len,
				    struct usteer_node *node_ref)
{
	struct usteer_node *node, *last_remote_neighbor = NULL;;
	int inserted = 0;

	while (inserted < nr_buf_len) {
		node = usteer_node_get_next_neighbor(node_ref, last_remote_neighbor);
		if (!node) {
			/* No more nodes available */
			break;
		}

		if (usteer_rrm_nr_list_insert(nr_buf, nr_buf_len, node)) {
			inserted++;
		}
		last_remote_neighbor = node;
	}
	return inserted;
}

#define NR_MAX_PREFERENCE	255
#define NR_MIN_PREFERENCE	0

static void
usteer_rrm_nr_list_add_ordered_preference(struct usteer_nr *nr_buf, int nr_buf_len,
					  struct usteer_node *node_ref,
					  enum usteer_reference_node_rating node_ref_pref)
{
	uint8_t pref = NR_MAX_PREFERENCE;
	int i;

	if (node_ref_pref == RN_RATING_PREFER)
		pref--;

	for (i = 0; i < nr_buf_len && nr_buf[i].node; i++) {
		nr_buf[i].priority = pref;
		if (nr_buf[i].node == node_ref) {
			if (node_ref_pref == RN_RATING_PREFER) {
				nr_buf[i].priority = NR_MAX_PREFERENCE;
				continue;
			} else if (node_ref_pref == RN_RATING_FORBID) {
				nr_buf[i].priority = NR_MIN_PREFERENCE;
				continue;
			}
		}
		pref--;
	}
}

static void
usteer_rrm_nr_list_add_load_preference(struct usteer_nr *nr_buf, int nr_buf_len,
				       struct usteer_node *node_ref,
				       enum usteer_reference_node_rating node_ref_pref)
{
	int i;

	for (i = 0; i < nr_buf_len && nr_buf[i].node; i++) {
		nr_buf[i].priority = NR_MAX_PREFERENCE - 5 - classify_load(nr_buf[i].node);
		if (nr_buf[i].node == node_ref) {
			if (node_ref_pref == RN_RATING_PREFER) {
				nr_buf[i].priority = NR_MAX_PREFERENCE;
				continue;
			} else if (node_ref_pref == RN_RATING_FORBID) {
				nr_buf[i].priority = NR_MIN_PREFERENCE;
				continue;
			}
		}
	}
}

int
usteer_rrm_nr_list_get_for_node(struct usteer_nr *nr_buf, int nr_buf_len,
				struct usteer_node *node_ref,
				enum usteer_reference_node_rating node_ref_pref)
{
	int inserted = 0;
	

	if (nr_buf_len == 0)
		return 0;

	/* Add local nodes */
	inserted += usteer_rrm_nr_list_add_local_nodes(nr_buf, nr_buf_len,
						       node_ref, node_ref_pref);

	/* Add remote nodes */
	inserted += usteer_rrm_nr_list_add_remote_nodes(nr_buf, nr_buf_len, node_ref);

	/* Sort list order based on load */
	usteer_rrm_nr_list_sort(nr_buf, nr_buf_len, &nr_has_lower_load);

	/* Add preferences */
	usteer_rrm_nr_list_add_load_preference(nr_buf, nr_buf_len, node_ref, node_ref_pref);

	/* Sort by preference */
	usteer_rrm_nr_list_sort(nr_buf, nr_buf_len, &nr_has_higher_priority);

	return inserted;
}

static int
usteer_rrm_nr_list_add_seen_remote_nodes(struct usteer_nr *nr_buf, int nr_buf_len,
				  struct sta_info *si_ref,
				  enum usteer_reference_node_rating node_ref_pref)
{
	struct usteer_node *node, *node_ref;
	struct usteer_remote_node *rn, *n;
	struct sta_info *si;
	int inserted = 0;

	node_ref = si_ref->node;

	/* ToDo sort nodes by signal level */
	list_for_each_entry(si, &si_ref->sta->nodes, list) {
		node = si->node;

		if (inserted == nr_buf_len)
			return inserted;

		if (!usteer_policy_node_selectable_by_sta(si_ref, si, 0))
			continue;

		if (node == node_ref)
			continue;
		
		if (node->type != NODE_TYPE_REMOTE)
			continue;

		if (usteer_rrm_nr_list_insert(nr_buf, nr_buf_len, node)) {
			inserted++;
		}

		rn = container_of(node, struct usteer_remote_node, node);
		list_for_each_entry(n, &rn->host->nodes, host_list) {
			if (inserted == nr_buf_len)
				return inserted;

			if (strcmp(n->node.ssid, node_ref->ssid) != 0)
				continue;
			
			/* Redundant */
			if (&n->node == node)
				continue;
			
			if (usteer_rrm_nr_list_insert(nr_buf, nr_buf_len, &n->node)) {
				inserted++;
			}
		}
	}

	return inserted;
}

static int
usteer_rrm_nr_list_add_candidate_list(struct usteer_nr *nr_buf, int nr_buf_len,
				      enum usteer_reference_node_rating node_ref_pref,
				      char *candidate_list,
				      int candidate_list_len)
{
	struct usteer_node *n;
	uint8_t *c, *bssid;
	int nr_element_len;
	int inserted = 0;
	int i;

	for (i = 0; (c = usteer_element_list_get_idx((uint8_t *) candidate_list, candidate_list_len, i)); i++) {
		nr_element_len = usteer_element_length((uint8_t *) candidate_list, candidate_list_len, c);
		if (nr_element_len <= 0)
			continue;

		bssid = usteer_nr_get_bssid(&c[2], nr_element_len - 2);
		if (!bssid)
			continue;

		n = usteer_node_by_bssid(bssid);
		if (!n)
			continue;
		
		/* ToDo: consider own node preference (exclusion) */

		if (usteer_rrm_nr_list_insert(nr_buf, nr_buf_len, n))
			inserted++;
	}

	return inserted;
}

int
usteer_rrm_nr_list_get_for_sta(struct usteer_nr *nr_buf, int nr_buf_len,
			       struct sta_info *si,
			       enum usteer_reference_node_rating node_ref_pref,
			       char *candidate_list,
			       int candidate_list_len)
{
	struct usteer_node *node_ref = si->node;
	int num_preferred;
	int inserted = 0;

	/* Prefer currently connected node if signal is below scan_threshold */
	if (node_ref_pref == RN_RATING_REGULAR && si->signal > usteer_snr_to_signal(si->node, config.roam_scan_snr))
		node_ref_pref == RN_RATING_PREFER;

	if (nr_buf_len == 0)
		return 0;

	/* ToDo: Prefer own node if signal & load are fine (Only if current rating is regular) */

	/* Try to evaluate candidate list */
	inserted += usteer_rrm_nr_list_add_candidate_list(nr_buf, nr_buf_len, node_ref_pref, candidate_list, candidate_list_len);
	if (inserted) {
		/* ToDo: Consider applying the preference from the querier.
		 * Order by it's preference first, reorder based on load and
		 * apply new absolute order. This would retain the preference
		 * from the node and just reorder by our known load
		 */ 

		/* Sort list order based on load */
		usteer_rrm_nr_list_sort(nr_buf, nr_buf_len, &nr_has_lower_load);

		/* Add preferences */
		usteer_rrm_nr_list_add_ordered_preference(nr_buf, nr_buf_len, node_ref, node_ref_pref);

		/* Sort by preference */
		usteer_rrm_nr_list_sort(nr_buf, nr_buf_len, &nr_has_higher_priority);

		return inserted;
	}

	/* Add nodes based on nodes the STA have seen */
	inserted += usteer_rrm_nr_list_add_seen_remote_nodes(nr_buf, nr_buf_len,
						      si, node_ref_pref);

	/* Add local nodes */
	inserted += usteer_rrm_nr_list_add_local_nodes(nr_buf, nr_buf_len,
						       node_ref, node_ref_pref);

	num_preferred = inserted;

	/* Sort list order based on load (preferred nodes only) */
	usteer_rrm_nr_list_sort(nr_buf, num_preferred, &nr_has_lower_load);

	/* Add remote nodes (based on general roam activity) */
	inserted += usteer_rrm_nr_list_add_remote_nodes(nr_buf, nr_buf_len, node_ref);

	/* Sort list order based on load (general roam activity nodes only) */
	usteer_rrm_nr_list_sort(&nr_buf[num_preferred], nr_buf_len - num_preferred, &nr_has_lower_load);

	/* Add preferences */
	usteer_rrm_nr_list_add_ordered_preference(nr_buf, nr_buf_len, node_ref, node_ref_pref);

	/* Sort by preference */
	usteer_rrm_nr_list_sort(nr_buf, nr_buf_len, &nr_has_higher_priority);

	return inserted;
}

char *
usteer_rrm_get_nr_data_for_usteer_nr(struct usteer_nr *nr)
{
	struct blobmsg_policy policy[3] = {
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[3];
	char nr_buf[MAX_NR_SIZE] = {};
	static char nr_str[MAX_NR_STRLEN + 1];
	struct usteer_node *node = nr->node;
	uint8_t priority = nr->priority;

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
