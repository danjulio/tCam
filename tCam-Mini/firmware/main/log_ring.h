/*
 * Log Ring Buffer
 *
 * Captures everything the firmware logs into a ring buffer in PSRAM so the web
 * UI can show the system log without a serial cable.  The serial console keeps
 * working exactly as before - this taps the stream, it does not divert it.
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
#ifndef LOG_RING_H
#define LOG_RING_H

#include <stddef.h>

// Held log history.  16KB is roughly the last few hundred lines - enough to
// cover a boot plus recent activity.
#define LOG_RING_SIZE 16384

//
// Log Ring API
//
void log_ring_init();

// Copy the buffered log, oldest first, into dst (null-terminated).  max
// includes the terminator.  Returns the number of characters written.
size_t log_ring_copy(char* dst, size_t max);

#endif /* LOG_RING_H */
