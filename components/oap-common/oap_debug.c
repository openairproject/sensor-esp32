/*
 * oap_debug.c
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

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include <sys/time.h>
#include "oap_debug.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/task.h"

#define TAG "mem"
/**
 * this method is surprisingly stack heavy - it takes ~ 1000 bytes.
 * before using it, adjust task stack accordingly.
 */
void log_task_stack(const char* task) {
	//uxTaskGetStackHighWaterMark is marked as UNTESTED
	#if !CONFIG_FREERTOS_ASSERT_ON_UNTESTED_FUNCTION
		ESP_LOGD(TAG, "stack min %d (task %s)", uxTaskGetStackHighWaterMark( NULL ), task);
	#endif
}

/**
 * current free heap is not very useful because it changes dynamically with multiple tasks running in parallel.
 * to detect any leaks, we use a time window and choose the max value as real 'free heap'
 */
#define SAMPLES 10
static size_t heap_samples[SAMPLES] = {.0};
uint8_t sample_idx = 0;

size_t avg_free_heap_size() {
	size_t max = 0;
	for (int i = 0; i < SAMPLES; i++) {
		if (heap_samples[i] > max) max = heap_samples[i];
	}
	return max;
}

void log_heap_size(const char* msg) {
	size_t free_heap = xPortGetFreeHeapSize();
	if (heap_samples[sample_idx%SAMPLES] == 0) heap_samples[sample_idx%SAMPLES] = free_heap;

	ESP_LOGD(TAG, "heap min %d avg %d (%s)",
			xPortGetMinimumEverFreeHeapSize(),
			avg_free_heap_size(),
			msg);
	heap_samples[sample_idx%SAMPLES] = free_heap;
	sample_idx++;
}

static void* dummy;
void reduce_heap_size_to(size_t size) {
	size_t reduce_by = xPortGetFreeHeapSize() - size;
	if (reduce_by > 0) {
		ESP_LOGE(TAG, "********************** REDUCE HEAP BY %d TO %d bytes !!!!!!!!!!!", reduce_by, size);
		do {
			size_t block = reduce_by > 10000 ? 10000 : reduce_by;
			reduce_by-=block;
			dummy = malloc(block);
			if (!dummy) {
				ESP_LOGE(TAG, "FAILED TO ALLOCATE!");
			}
		} while (reduce_by > 0);
	}
}

static int heap_log_count = 0;

/*
	    | 100 |
		| -1  |
	99  |	  | 100 (c=1)
		| -10 |
	 	| -1  |
	88  |     | 90  (c=2)
*/

void heap_log_list(heap_log* log) {
	heap_log* prev = NULL;
	int c = 0;
	while (log) {
		c++;
		heap_log_print(log, prev);
		prev = log;
		log = log->next;
	}
}

void heap_log_print(heap_log* log, heap_log* prev) {
	size_t comp_heap = log->heap + heap_log_count * sizeof(heap_log);
	if (prev == NULL) {
		ESP_LOGD("debug", "----------> heap %25s: %d", log->tag, comp_heap);
	} else {
		size_t diff_heap = log->heap - prev->heap + sizeof(heap_log);
		ESP_LOGD("debug", "----------> heap %25s: %d \t (%d)", log->tag, comp_heap, diff_heap);
	}
}

heap_log* heap_log_take(heap_log* log, const char* msg) {
	heap_log* new_log = malloc(sizeof(heap_log));
	heap_log_count++;
	new_log->tag = strdup(msg);
	new_log->next = NULL;

	//find tail
	if (log != NULL) {
		while (log->next) {
			log = log->next;
		}
		log->next = new_log;
	}

	new_log->heap = xPortGetFreeHeapSize();
	//heap_log_print(new_log, log);
	return new_log;
}

void heap_log_free(heap_log* log) {
	if (log) {
		size_t initial_heap = log->heap;

		heap_log_list(log);
		heap_log* next;
		do {
			next = log->next;
			free(log->tag);
			free(log);
			heap_log_count--;
			log = next;
		} while (log);


		ESP_LOGD("debug", "----------> leaked : %d",  xPortGetFreeHeapSize() - initial_heap);
	}
}
