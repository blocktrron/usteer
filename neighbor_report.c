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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "element.h"
#include "neighbor_report.h"


#define NEIGHBOR_REPORT_MIN_LEN			(13)
#define NEIGHBOR_REPORT_SUBELEM_PTR(nr)		(&nr[NEIGHBOR_REPORT_MIN_LEN])


int
usteer_nr_len(char *nr_buf, unsigned int nr_buf_len)
{
	char *subelem_ptr = NEIGHBOR_REPORT_SUBELEM_PTR(nr_buf);
	int length = usteer_element_list_len(subelem_ptr, nr_buf_len - (subelem_ptr - nr_buf));

	if (nr_buf_len < (subelem_ptr - nr_buf))
		return -1;

	if (length < 0)
		return -1;
	
	return NEIGHBOR_REPORT_MIN_LEN + length;
}

int
usteer_nr_valid(char *nr_buf, unsigned int nr_buf_len)
{
	return !!(usteer_nr_len(nr_buf, nr_buf_len) >= NEIGHBOR_REPORT_MIN_LEN);
}

int
usteer_nr_set_subelement(char *nr_buf, unsigned int nr_buf_len, char subelem, char *data, unsigned int data_len)
{
	char *subelem_list = NEIGHBOR_REPORT_SUBELEM_PTR(nr_buf);
	int subelem_list_buf_len = nr_buf_len - (subelem_list - nr_buf);

	if (subelem_list_buf_len < 0)
		return -1;

	return usteer_element_list_set_element(subelem_list, subelem_list_buf_len, subelem, data, data_len);
}
