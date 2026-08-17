/*
 * Persistent Storage Module
 *
 * Manage the persistent storage kept in the ESP32 NVS and provide access
 * routines to it.
 *
 * NOTE:
 *  1. It is assumed that only one task will access persistent storage at a time.
 *  2. Some internal naming is reflective of the fact that this module existed first
 *     for a Wifi-only system with ethernet support added later.
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
#ifndef PS_UTILITIES_H
#define PS_UTILITIES_H

#include "net_utilities.h"
#include "sys_utilities.h"
#include <stdbool.h>
#include <stdint.h>



//
// PS Utilities Constants
//

// Base part of the default SSID/Camera name - the last 4 nibbles of the ESP32's
// mac address are appended as ASCII characters
#define PS_DEFAULT_AP_SSID "tCam-Mini-"

// Field lengths
#define PS_SSID_MAX_LEN     32
#define PS_PW_MAX_LEN       63
#define PS_OLD_PW_MAX_LEN   32

// Saved station networks the camera can roam between.  Five covers "home,
// cottage, workshop" with room to spare while keeping the NVS blob small.
#define PS_NUM_SAVED_NETS   5

// Per-network flag: use the stored static address instead of DHCP
#define PS_SAVED_NET_FLAG_STATIC_IP 0x01

typedef struct {
	char ssid[PS_SSID_MAX_LEN+1];    // empty string = free slot
	char pw[PS_PW_MAX_LEN+1];
	uint8_t flags;
	uint8_t ip_addr[4];              // index 3 holds the first octet
	uint8_t netmask[4];
} ps_saved_net_t;


//
// PS Utilities API
//
bool ps_init(int brd, int iface);
void ps_get_lep_state(json_config_t* state);
void ps_set_lep_state(const json_config_t* state);
void ps_get_net_info(net_info_t* info);
void ps_set_net_info(const net_info_t* info);
bool ps_reinit_net();
bool ps_has_new_cam_name(const net_info_t* info);
char ps_nibble_to_ascii(uint8_t n);

/**
 * Saved station networks.  get copies all PS_NUM_SAVED_NETS entries; set stores
 * and commits the whole table.  upsert adds a network or updates the entry with
 * the same ssid (evicting the last slot if the table is full); forget removes by
 * ssid.  All return through the same committed table.
 */
void ps_get_saved_nets(ps_saved_net_t* list);
void ps_set_saved_nets(const ps_saved_net_t* list);
bool ps_upsert_saved_net(const ps_saved_net_t* net);
bool ps_forget_saved_net(const char* ssid);

#endif /* PS_UTILITIES_H */