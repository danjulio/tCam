/*
 * Log Ring Buffer
 *
 * See log_ring.h for a description of this module's role.
 *
 * This file is part of tCam.
 *
 * tCam is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * tCam is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with tCam.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "log_ring.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


//
// Log Ring variables
//
static char* ring = NULL;
static size_t head = 0;          // next write position
static bool wrapped = false;     // true once the ring has filled at least once
static SemaphoreHandle_t ring_mutex = NULL;
static vprintf_like_t prev_vprintf = NULL;


//
// Log Ring forward declarations
//
static int ring_vprintf(const char* fmt, va_list args);


//
// Log Ring API
//
void log_ring_init()
{
	// PSRAM by preference - 16KB of internal RAM is too dear for diagnostics
	ring = heap_caps_malloc(LOG_RING_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (ring == NULL) {
		ring = heap_caps_malloc(LOG_RING_SIZE, MALLOC_CAP_8BIT);
	}
	if (ring == NULL) {
		// Diagnostics must never take the system down with them
		return;
	}

	ring_mutex = xSemaphoreCreateMutex();
	if (ring_mutex == NULL) {
		heap_caps_free(ring);
		ring = NULL;
		return;
	}

	prev_vprintf = esp_log_set_vprintf(ring_vprintf);
}


size_t log_ring_copy(char* dst, size_t max)
{
	size_t n = 0;

	if ((ring == NULL) || (dst == NULL) || (max == 0)) {
		if ((dst != NULL) && (max != 0)) dst[0] = 0;
		return 0;
	}

	xSemaphoreTake(ring_mutex, portMAX_DELAY);

	if (wrapped) {
		// Oldest data runs from head to the end, then start to head
		size_t tail_len = LOG_RING_SIZE - head;

		if (tail_len < max - 1) {
			memcpy(dst, ring + head, tail_len);
			n = tail_len;
			size_t rest = head;
			if (rest > (max - 1 - n)) rest = max - 1 - n;
			memcpy(dst + n, ring, rest);
			n += rest;
		} else {
			// Keep the NEWEST part when the caller's buffer is smaller
			memcpy(dst, ring + head + (tail_len - (max - 1)), max - 1);
			n = max - 1;
		}
	} else {
		n = (head < max - 1) ? head : max - 1;
		// Keep the newest part here too
		memcpy(dst, ring + (head - n), n);
	}

	xSemaphoreGive(ring_mutex);

	dst[n] = 0;
	return n;
}


//
// Log Ring internal functions
//
static int ring_vprintf(const char* fmt, va_list args)
{
	char buf[256];
	int len;
	va_list args_copy;

	// The serial console gets the untouched original call
	va_copy(args_copy, args);

	len = vsnprintf(buf, sizeof(buf), fmt, args);
	if (len > 0) {
		size_t n = ((size_t) len < sizeof(buf)) ? (size_t) len : sizeof(buf) - 1;
		size_t i;

		// Never log from in here and never block long - drop the entry rather
		// than stall the logging task if the mutex is held
		if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
			for (i = 0; i < n; i++) {
				ring[head] = buf[i];
				if (++head >= LOG_RING_SIZE) {
					head = 0;
					wrapped = true;
				}
			}
			xSemaphoreGive(ring_mutex);
		}
	}

	len = (prev_vprintf != NULL) ? prev_vprintf(fmt, args_copy) : len;
	va_end(args_copy);
	return len;
}
