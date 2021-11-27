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

#ifndef __APMGR_NR_H
#define __APMGR_NR_H

#include <string.h>
#include <stdlib.h>

int usteer_nr_valid(char *nr_buf, unsigned int nr_buf_len);
int usteer_nr_len(char *nr_buf, unsigned int nr_buf_len);

int usteer_nr_remove_subelem(char *nr_buf, unsigned int nr_buf_len, char subelem);
int usteer_nr_set_subelement(char *nr_buf, unsigned int nr_buf_len, char subelem, char *data, unsigned int data_len);

#endif