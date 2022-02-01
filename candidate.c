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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "element.h"
#include "node.h"
#include "remote.h"
#include "usteer.h"
#include "neighbor_report.h"


struct usteer_candidate_list *
usteer_candidate_list_get_empty(int max_length)
{
	struct usteer_candidate_list *cl;

	cl = calloc(1, sizeof(*cl));
	if (!cl)
		return NULL;

	INIT_LIST_HEAD(&cl->candidates);
	cl->max_length = max_length;

	return cl;
}

void
usteer_candidate_list_free(struct usteer_candidate_list *cl)
{
	struct usteer_candidate *c, *tmp;

	if (!cl)
		return;

	list_for_each_entry_safe(c, tmp, &cl->candidates, list) {
		free(c);
	}

	free(cl);
}

static bool
usteer_candidate_contains_node(struct usteer_candidate_list *cl, struct usteer_node *node)
{
	struct usteer_candidate *c;

	list_for_each_entry(c, &cl->candidates, list) {
		if (c->node == node)
			return true;
	}

	return false;
}

int
usteer_candidate_list_len(struct usteer_candidate_list *cl)
{
	struct usteer_candidate *c;
	int i = 0;

	/* libubox does not offer this. Why? */

	list_for_each_entry(c, &cl->candidates, list) {
		i++;
	}

	return i;
}

static struct usteer_candidate *
usteer_candidate_list_get_idx(struct usteer_candidate_list *cl, int idx)
{
	struct usteer_candidate *c;
	int i = 0;

	list_for_each_entry(c, &cl->candidates, list) {
		if (i == idx)
			return c;
		i++;
	}

	return NULL;
}

static bool
usteer_candidate_list_can_insert(struct usteer_candidate_list *cl)
{
	return !cl->max_length ||  usteer_candidate_list_len(cl) < cl->max_length;
}

static bool
usteer_candidate_list_can_insert_node(struct usteer_candidate_list *cl, struct usteer_node *node)
{
	if (!usteer_candidate_list_can_insert(cl))
		return false;
	
	return !usteer_candidate_contains_node(cl, node);
}

static int
usteer_candidate_classify_load(struct usteer_node *node)
{
	return (node->load / 10) * 10;
}

static bool
cl_sort_has_lower_load(struct usteer_candidate *ref, struct usteer_candidate *candidate)
{
	struct usteer_node *ref_node = ref->node;
	struct usteer_node *candidate_node = candidate->node;

	if (candidate == NULL || candidate_node == NULL) {
		return false;
	} else if (ref == NULL || ref_node == NULL) {
		return true;
	}

	int ref_load = usteer_candidate_classify_load(ref_node);
	int candidate_load = usteer_candidate_classify_load(candidate_node);

	return ref_load > candidate_load || (ref_load == candidate_load && candidate_node->freq > 4000 && ref_node->freq < 4000);
}

static bool
cl_sort_has_higher_priority(struct usteer_candidate *ref, struct usteer_candidate *candidate)
{
	return ref->priority < candidate->priority;
}

static void
usteer_candidate_list_swap(struct list_head *elem1, struct list_head *elem2)
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

static void
usteer_candidate_list_sort(struct usteer_candidate_list *cl, bool (*sort_fun)(struct usteer_candidate *, struct usteer_candidate *))
{
	struct usteer_candidate *ref, *candidate;
	int cl_len = usteer_candidate_list_len(cl);
	int i, j;

	for (i = 0; i < cl_len - 1; i++) { 
		for (j = 0; j < cl_len - i - 1; j++) {
			ref = usteer_candidate_list_get_idx(cl, j);
			candidate = usteer_candidate_list_get_idx(cl, j + 1);

			if (sort_fun(ref, candidate)) {
				usteer_candidate_list_swap(&ref->list, &candidate->list);
			}
		}
	}
}

#define NR_MAX_PREFERENCE	255
#define NR_MIN_PREFERENCE	0

static void
usteer_candidate_list_add_load_preference(struct usteer_candidate_list *cl,
					  struct usteer_node *node_ref,
					  enum usteer_reference_node_rating node_ref_pref)
{
	struct usteer_candidate *c;
	uint8_t pref = NR_MAX_PREFERENCE;
	int last_load = -1;

	if (node_ref_pref == RN_RATING_PREFER)
		pref--;

	for_each_candidate(cl, c) {
		if (last_load > -1 && last_load < c->node->load)
			pref--;

		c->priority = pref;
		last_load = c->node->load;

		if (c->node == node_ref) {
			if (node_ref_pref == RN_RATING_PREFER) {
				c->priority = NR_MAX_PREFERENCE;
				continue;
			} else if (node_ref_pref == RN_RATING_FORBID) {
				c->priority = NR_MIN_PREFERENCE;
				continue;
			}
		}
	}
}

static bool
usteer_candidate_list_add(struct usteer_candidate_list *cl, struct usteer_candidate *c)
{
	if (!usteer_candidate_list_can_insert_node(cl, c->node))
		return false;
	
	list_add_tail(&c->list, &cl->candidates);
	return true;
}

static bool
usteer_candidate_list_add_node(struct usteer_candidate_list *cl, struct usteer_node *n, int signal, uint32_t reasons)
{
	struct usteer_candidate *c;

	if (!usteer_candidate_list_can_insert_node(cl, n))
		return false;
	
	c = calloc(1, sizeof(*c));
	if (!c)
		return false;

	c->node = n;
	c->signal = signal;
	c->reasons = reasons;

	usteer_candidate_list_add(cl, c);
	
	return true;
}

static bool
usteer_candidate_list_add_better_node(struct usteer_candidate_list *cl, struct usteer_node *n, int signal, uint32_t reasons)
{
	struct usteer_candidate *c, *worst_candidate = NULL;

	/* Check if node is already in candidate list */
	if (usteer_candidate_contains_node(cl, n))
		return false;

	/* Check if max candidate-list size is not exceeded */
	if (usteer_candidate_list_add_node(cl, n, signal, reasons))
		return true;

	/* Find the candidate with the worst signal */
	for_each_candidate(cl, c) {
		if (!worst_candidate || c->signal < worst_candidate->signal)
			worst_candidate = c;
	}

	if (!worst_candidate || worst_candidate->signal >= signal)
		return false;

	/* Delete worst candidate from list */
	list_del(&worst_candidate->list);
	free(worst_candidate);

	/* Add candidate to list */
	return usteer_candidate_list_add_node(cl, n, signal, reasons);
}

static int
usteer_candidate_list_add_local_nodes(struct usteer_candidate_list *cl, struct usteer_node *node_ref,
				      enum usteer_reference_node_rating node_ref_pref)
{
	struct usteer_node *node;
	int inserted = 0;

	for_each_local_node(node) {
		if (node_ref == node && node_ref_pref == RN_RATING_EXCLUDE) {
			continue;
		}

		if (strcmp(node->ssid, node_ref->ssid)) {
			continue;
		}

		if (usteer_candidate_list_add_node(cl, node, 0, 0)) {
			inserted++;
		}
	}

	return inserted;
}

static int
usteer_candidate_list_add_remote_nodes(struct usteer_candidate_list *cl, struct usteer_node *node_ref)
{
	struct usteer_node *node, *last_remote_neighbor = NULL;;
	int inserted = 0;

	while (inserted < usteer_candidate_list_len(cl)) {
		node = usteer_node_get_next_neighbor(node_ref, last_remote_neighbor);
		if (!node) {
			/* No more nodes available */
			break;
		}

		if (usteer_candidate_list_add_node(cl, node, 0, 0)) {
			inserted++;
		}
		last_remote_neighbor = node;
	}
	return inserted;
}

int
usteer_candidate_list_add_for_node(struct usteer_candidate_list *cl, struct usteer_node *node_ref,
				   enum usteer_reference_node_rating node_ref_rating)
{
	int inserted = 0;

	/* Add local nodes */
	inserted += usteer_candidate_list_add_local_nodes(cl, node_ref, node_ref_rating);

	/* Add remote nodes */
	inserted += usteer_candidate_list_add_remote_nodes(cl, node_ref);

	/* Sort list order based on load */
	usteer_candidate_list_sort(cl, &cl_sort_has_lower_load);

	/* Add preferences */
	usteer_candidate_list_add_load_preference(cl, node_ref, node_ref_rating);

	/* Sort by preference */
	usteer_candidate_list_sort(cl, &cl_sort_has_higher_priority);

	return inserted;
}

static uint32_t
usteer_candidate_list_should_add_node(struct usteer_node *current_node, int current_signal,
				      struct usteer_node *new_node, int new_signal,
				      enum usteer_reference_node_rating node_ref_rating,
				      uint32_t required_criteria)
{
	uint32_t reasons;

	if (node_ref_rating == RN_RATING_EXCLUDE && current_node == new_node)
		return 0;

	reasons = usteer_policy_is_better_candidate(current_node, current_signal,
							new_node, new_signal);

	if (!reasons || (required_criteria && !(reasons & required_criteria)))
		return 0;

	return reasons;
}

static void
usteer_candidate_list_add_sta_seen(struct usteer_candidate_list *cl, struct sta_info *si,
				   enum usteer_reference_node_rating node_ref_rating,
				   uint32_t required_criteria, uint64_t signal_max_age)
{
	struct sta_info *foreign_si;
	uint32_t reasons;

	list_for_each_entry(foreign_si, &si->sta->nodes, list) {
		if (!usteer_policy_node_selectable_by_sta_info(si, foreign_si, signal_max_age))
			continue;

		reasons = usteer_candidate_list_should_add_node(si->node, si->signal, foreign_si->node,
								foreign_si->signal,
								node_ref_rating, required_criteria);
		if (!reasons)
			continue;

		usteer_candidate_list_add_better_node(cl, foreign_si->node, foreign_si->signal, reasons);
	}
}

int
usteer_candidate_list_add_for_sta(struct usteer_candidate_list *cl, struct sta_info *si,
				  enum usteer_reference_node_rating node_ref_rating,
				  uint32_t required_criteria, uint64_t signal_max_age)
{
	/* Add all nodes we have seen the STA on */
	usteer_candidate_list_add_sta_seen(cl, si, node_ref_rating, required_criteria, signal_max_age);

	/* Sort list order based on load */
	usteer_candidate_list_sort(cl, &cl_sort_has_lower_load);

	/* Add preferences */
	usteer_candidate_list_add_load_preference(cl, si->node, node_ref_rating);

	/* Sort by preference */
	usteer_candidate_list_sort(cl, &cl_sort_has_higher_priority);

	return 0;
}