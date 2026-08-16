/*
 * Captive Portal DNS Responder
 *
 * See dns_hijack.c for a description of this module's role.
 *
 * Copyright 2020-2022 Dan Julio
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
#ifndef DNS_HIJACK_H
#define DNS_HIJACK_H

#include <stdint.h>


//
// DNS Hijack API
//

/**
 * Start answering all A queries with ap_addr (a network-order IPv4 address).
 * Safe to call more than once; only the first call creates the task.
 */
void dns_hijack_start(uint32_t ap_addr);

/**
 * Stop the responder.
 */
void dns_hijack_stop();

#endif /* DNS_HIJACK_H */
