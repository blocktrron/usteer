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
 *   Copyright (C) 2021 David Bauer <mail@david-bauer.net> 
 */

#include "usteer.h"

static struct usteer_timeout_queue tq;

static struct usteer_candidate *
usteer_candidate_list_get_idx(struct sta *sta, int idx)
{
	struct usteer_candidate *c;
	int i = 0;

	list_for_each_entry(c, &sta->candidates, sta_list) {
		if (i == idx)
			return c;
		i++;
	}

	return NULL;
}

static unsigned int
usteer_candidate_list_len(struct sta *sta)
{
	struct usteer_candidate *c;
	unsigned int i = 0;

	/* libubox does not offer this. Why? */

	list_for_each_entry(c, &sta->candidates, sta_list) {
		i++;
	}

	return i;
}

void
usteer_candidate_list_sort(struct sta *sta)
{
	struct usteer_candidate *ref, *candidate;
	int cl_len = usteer_candidate_list_len(sta);
	int i, j;

	for (i = 0; i < cl_len - 1; i++) { 
		for (j = 0; j < cl_len - i - 1; j++) {
			ref = usteer_candidate_list_get_idx(sta, j);
			candidate = usteer_candidate_list_get_idx(sta, j + 1);

			if (ref->score < candidate->score) {
				usteer_list_swap(&ref->sta_list, &candidate->sta_list);
			}
		}
	}
}

void
usteer_candidate_node_cleanup(struct usteer_node *node)
{
	struct usteer_candidate *c, *tmp;

	list_for_each_entry_safe(c, tmp, &node->candidates, node_list)
		usteer_candidate_del(c);
}

void
usteer_candidate_sta_cleanup(struct sta *sta)
{
	struct usteer_candidate *c, *tmp;

	list_for_each_entry_safe(c, tmp, &sta->candidates, sta_list)
		usteer_candidate_del(c);
}

struct usteer_candidate *
usteer_candidate_get(struct sta *sta, struct usteer_node *node, bool create)
{
	struct usteer_candidate *c;

	list_for_each_entry(c, &sta->candidates, sta_list) {
		if (c->node == node)
			return c;
	}

	if (!create)
		return NULL;

	c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;

	/* Set node & add to nodes list */
	c->node = node;
	list_add(&c->node_list, &node->candidates);

	/* Set sta & add to STAs list */
	c->sta = sta;
	list_add(&c->sta_list, &sta->candidates);

	usteer_timeout_set(&tq, &c->timeout, 10 * 1000);

	return c;
}

void
usteer_candidate_del(struct usteer_candidate *c)
{
	usteer_timeout_cancel(&tq, &c->timeout);
	list_del(&c->node_list);
	list_del(&c->sta_list);
	free(c);
}

static void
usteer_candidate_timeout(struct usteer_timeout_queue *q, struct usteer_timeout *t)
{
	struct usteer_candidate *c = container_of(t, struct usteer_candidate, timeout);

	usteer_candidate_del(c);
}

static void __usteer_init usteer_candidate_init(void)
{
	usteer_timeout_init(&tq);
	tq.cb = usteer_candidate_timeout;
}
