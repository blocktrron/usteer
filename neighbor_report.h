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
#include <stdint.h>

#ifndef __APMGR_NR_H
#define __APMGR_NR_H

int usteer_nr_valid(uint8_t *nr_buf, unsigned int nr_buf_len);
int usteer_nr_len(uint8_t *nr_buf, unsigned int nr_buf_len);

int usteer_nr_remove_subelem(uint8_t *nr_buf, unsigned int nr_buf_len, uint8_t subelem);
int usteer_nr_set_subelement(uint8_t *nr_buf, unsigned int nr_buf_len, uint8_t subelem, uint8_t *data, unsigned int data_len);
uint8_t *usteer_nr_get_bssid(uint8_t *nr_buf, unsigned int nr_buf_len);

#endif