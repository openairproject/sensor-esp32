/*
 * oap_debug.h
 *
 *  Created on: Mar 13, 2017
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

typedef struct heap_log {
	size_t heap;
	char* tag;
	struct heap_log* next;
} heap_log;

void heap_log_print(heap_log* log, heap_log* prev);
heap_log* heap_log_take(heap_log* log, const char* msg);
void heap_log_free(heap_log* log);

size_t avg_free_heap_size();

void log_task_stack(const char* task);
void log_heap_size(const char* msg);
void reduce_heap_size_to(size_t size);
