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

#ifndef __APMGR_TAGS_H
#define __APMGR_TAGS_H


int
usteer_element_length(char *buf, unsigned int buf_len, char *tagged_element);

int
usteer_element_valid(char *buf, unsigned int buf_len, char *tagged_element);

int
usteer_element_type(char *buf, unsigned int buf_len, char *tagged_element);


int
usteer_element_list_len(char *buf, unsigned int buf_len);

int
usteer_element_list_valid(char *buf, unsigned int buf_len);

int
usteer_element_list_set_element(char *buf, unsigned int buf_len, char element, char *data, unsigned int data_len);


#endif