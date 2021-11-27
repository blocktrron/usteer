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

#include <stdint.h>

#ifndef __APMGR_TAGS_H
#define __APMGR_TAGS_H

int
usteer_element_length(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element);

int
usteer_element_valid(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element);

int
usteer_element_type(uint8_t *buf, unsigned int buf_len, uint8_t *tagged_element);


int
usteer_element_list_len(uint8_t *buf, unsigned int buf_len);

int
usteer_element_list_valid(uint8_t *buf, unsigned int buf_len);

int
usteer_element_list_set_element(uint8_t *buf, unsigned int buf_len, uint8_t element, uint8_t *data, unsigned int data_len);


#endif