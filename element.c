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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "element.h"

#define ELEMENT_TYPE(p)	(	p[0])
#define ELEMENT_LEN(p)		(p[1])
#define ELEMENT_TOTAL_LEN(p)	(ELEMENT_LEN(p) + 2)
#define ELEMENT_NEXT(p)		(p + ELEMENT_TOTAL_LEN(p))	

static int
usteer_element_header_accessible(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element)
{
	uint8_t *endptr = buf + buf_len;

	return !!(tagged_element != NULL && tagged_element + 1 < endptr);
}

static int
usteer_element_accessible(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element)
{
	uint8_t *endptr = buf + buf_len;

	if (!usteer_element_header_accessible(buf, buf_len, tagged_element))
		return 0;

	if (tagged_element + ELEMENT_TOTAL_LEN(tagged_element) > endptr)
		return 0;
	return 1;
}

int
usteer_element_length(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element)
{
	if (!usteer_element_accessible(buf, buf_len, tagged_element) ||
	    ELEMENT_LEN(tagged_element) == 0)
		return -1;

	return ELEMENT_LEN(tagged_element);
}

int
usteer_element_valid(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element)
{
	return !!(usteer_element_length(buf, buf_len, tagged_element) > 0);
}

int
usteer_element_type(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element)
{
	if (!usteer_element_valid(buf, buf_len, tagged_element))
		return -1;

	return ELEMENT_TYPE(tagged_element);
}

static int
usteer_element_list_has_next(uint8_t *buf, unsigned int buf_len, uint8_t *current_element)
{
	/* Check if next element is valid */
	if (!usteer_element_valid(buf, buf_len, ELEMENT_NEXT(current_element)))
		return 0;

	return 1;
}

static uint8_t *
usteer_element_list_next(uint8_t *element)
{
	return ELEMENT_NEXT(element);
}

static int
usteer_element_list_empty(uint8_t *buf, unsigned int buf_len, uint8_t *p)
{
	uint8_t *endptr = buf + buf_len;

	while (p < endptr) {
		if (*p != 0)
			return 0;
		p++;
	}

	return 1;
}

int
usteer_element_list_len(uint8_t *buf, unsigned int buf_len)
{
	uint8_t *p = buf;
	unsigned int determined_len;

	if (usteer_element_list_empty(buf, buf_len, p))
		return 0;
	
	if (!usteer_element_valid(buf, buf_len, p))
		return -1;

	while (usteer_element_list_has_next(buf, buf_len, p)) {
		p = usteer_element_list_next(p);
	}

	/* Determine length of valid list part */
	determined_len = usteer_element_list_next(p) - buf;

	/* Can't be longer than buffer */
	if (determined_len > buf_len)
		return -1;

	/* Check if determined Length matches buf-length.  */
	if (determined_len == buf_len)
		return determined_len;

	/* Check if buffer is all zeros after last element */
	if (!usteer_element_list_empty(buf, buf_len, usteer_element_list_next(p)))
		return -1;	

	return determined_len;
}

int
usteer_element_list_valid(uint8_t *buf, unsigned int buf_len)
{
	return !(usteer_element_list_len(buf, buf_len) < 0);
}

static uint8_t *
usteer_element_list_find_element(uint8_t *buf, unsigned int buf_len, uint8_t element)
{
	uint8_t *p = buf;

	if (!usteer_element_list_valid(buf, buf_len))
		return NULL;

	while (p != NULL) {
		if (ELEMENT_TYPE(p) == element)
			return p;

		if (!usteer_element_list_has_next(buf, buf_len, p))
			return NULL;

		p = usteer_element_list_next(p);
	}

	return p;
}

static int
usteer_element_list_remove_element(uint8_t *buf, unsigned int buf_len, uint8_t elem)
{
	uint8_t *p = usteer_element_list_find_element(buf, buf_len, elem);
	int element_len_total;

	if (!p)
		return 0;

	element_len_total = ELEMENT_TOTAL_LEN(p);

	/* Zero out subelement space */
	memset(&p[0], 0, element_len_total);

	/* Shift proceeding buffer */
	memcpy(p, &p[element_len_total], buf_len - (&p[element_len_total] - buf));

	/* Zero out shifted space */
	memset(buf + buf_len - element_len_total, 0, element_len_total);

	return 0;
}

int
usteer_element_list_set_element(uint8_t *buf, unsigned int buf_len, uint8_t element, uint8_t *data, unsigned int data_len)
{
	uint8_t *p;
	uint8_t *endptr = buf + buf_len;
	int element_len;
	int list_len = usteer_element_list_len(buf, buf_len);

	if (list_len < 0)
		return -1;

	/* Search for subelement to remove */
	p = usteer_element_list_find_element(buf, buf_len, element);
	if (p) {
		element_len = usteer_element_length(buf, buf_len, p);
		/* Verify we have space for new data after removal */
		if (element_len < data_len &&
		    buf_len - list_len + element_len < data_len) {
			return -1;
		}

		usteer_element_list_remove_element(buf, buf_len, element);
	}

	list_len = usteer_element_list_len(buf, buf_len);
	if (list_len < 0)
		return -1;

	p = buf + list_len;
	if (!p || p + 2 + data_len > endptr)
		return -1;

	p[0] = element;
	p[1] = data_len;
	memcpy(&p[2], data, data_len);

	return 0;
}
