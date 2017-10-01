/*
 * c_utils.h
 *
 *  Created on: Oct 1, 2017
 *      Author: kris
 *
 *  This file is part of OpenAirProject-ESP32.
 *
 *  OpenAirProject-ESP32 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenAirProject-ESP32 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with OpenAirProject-ESP32.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMPONENTS_OAP_COMMON_INCLUDE_C_UTILS_H_
#define COMPONENTS_OAP_COMMON_INCLUDE_C_UTILS_H_


/*
 * c_utils.h
 *
 *  Created on: Nov 15, 2016
 *      Author: kolban
 */

typedef struct _list_t {
	void *value;
	struct _list_t *next;
	struct _list_t *prev;
} list_t;

//typedef int(*node_value_comparator)(void* a, void* b);
typedef int(*node_value_predicate)(void* data);

list_t *list_createList();
void list_deleteList(list_t *pList, int withFree);
void list_deleteListAndValues(list_t *pList, node_value_predicate disposer);
void list_insert(list_t *pList, void *value);
void list_remove(list_t *pList, list_t *pEntry, int withFree);
list_t *list_next(list_t *pList);
void list_removeByValue(list_t *pList, void *value, int withFree);
list_t *list_first(list_t *pList);
void list_insert_after(list_t *pEntry, void *value);
void list_insert_before(list_t *pEntry, void *value);
void *list_get_value(list_t *pList);
list_t *list_find(list_t *pList, node_value_predicate predicate);


#endif /* COMPONENTS_OAP_COMMON_INCLUDE_C_UTILS_H_ */
