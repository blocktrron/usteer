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

void usteer_node_set_blob(struct blob_attr **dest, struct blob_attr *val)
{
	int new_len;
	int len;

	if (!val) {
		free(*dest);
		*dest = NULL;
		return;
	}

	len = *dest ? blob_pad_len(*dest) : 0;
	new_len = blob_pad_len(val);
	if (new_len != len)
		*dest = realloc(*dest, new_len);
	memcpy(*dest, val, new_len);
}

static int usteer_ubus_hexstr_get_byte(const char *str, int byte)
{
	unsigned long ul;
	char tmp[3];

	if (byte > (strlen(str) / 2))
		return -1;

	tmp[0] = str[byte * 2];
	tmp[1] = str[(byte * 2) + 1];
	tmp[2] = 0;

	ul = strtoul(tmp, NULL, 16);

	return (int) ul;
}

const char *usteer_node_get_nr_data(struct usteer_node *node)
{
	struct blobmsg_policy policy[3] = {
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
		{ .type = BLOBMSG_TYPE_STRING },
	};
	struct blob_attr *tb[3];

	if (!node->rrm_nr)
		return NULL;

	blobmsg_parse_array(policy, ARRAY_SIZE(tb), tb,
			    blobmsg_data(node->rrm_nr),
			    blobmsg_data_len(node->rrm_nr));
	if (!tb[2])
		return NULL;

	return blobmsg_get_string(tb[2]);
}

uint8_t usteer_node_get_op_class(struct usteer_node *node)
{
	const char *rrm_nr_str = usteer_node_get_nr_data(node);
	int op_class;

	if (!rrm_nr_str)
		return 0;

	op_class = usteer_ubus_hexstr_get_byte(rrm_nr_str, 11);
	if (op_class < 0)
		return 0;

	return (uint8_t) op_class;
}

uint8_t usteer_node_get_channel(struct usteer_node *node)
{
	const char *rrm_nr_str = usteer_node_get_nr_data(node);
	int channel;

	if (!rrm_nr_str)
		return 0;

	channel = usteer_ubus_hexstr_get_byte(rrm_nr_str, 12);
	if (channel < 0)
		return 0;

	return (uint8_t) channel;
}
