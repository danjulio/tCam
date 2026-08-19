/*
 * Captive Portal DNS Responder
 *
 * Answers every A-record query with the camera's own SoftAP address.  Phones and
 * laptops probe a known URL immediately after associating; when that probe resolves
 * to the camera and returns a redirect, the operating system pops the web UI up on
 * its own.  The practical effect is that joining the camera's WiFi network is the
 * whole setup procedure - there is no app to install and no address to type.
 *
 * Only runs while the camera is acting as an access point.  In station mode the
 * network already has a real DNS server and hijacking queries would be hostile.
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
#include "dns_hijack.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <string.h>


//
// DNS Hijack constants
//
#define DNS_PORT            53
#define DNS_MAX_LEN         256

#define DNS_QR_RESPONSE     0x80
#define DNS_OPCODE_MASK     0x78
#define DNS_FLAG_AA         0x04
#define DNS_RCODE_NOTIMPL   0x04

#define DNS_TYPE_A          1
#define DNS_CLASS_IN        1
#define DNS_ANSWER_TTL      60


//
// DNS Hijack types
//
typedef struct __attribute__((packed)) {
	uint16_t id;
	uint8_t  flags1;
	uint8_t  flags2;
	uint16_t qd_count;
	uint16_t an_count;
	uint16_t ns_count;
	uint16_t ar_count;
} dns_header_t;


//
// DNS Hijack variables
//
static const char* TAG = "dns_hijack";

static TaskHandle_t dns_task_handle = NULL;
static uint32_t portal_addr = 0;


//
// DNS Hijack forward declarations
//
static void dns_hijack_task(void* arg);


//
// DNS Hijack API
//
void dns_hijack_start(uint32_t ap_addr)
{
	portal_addr = ap_addr;

	if (dns_task_handle == NULL) {
		xTaskCreatePinnedToCore(&dns_hijack_task, "dns_task", 3072, NULL, 2,
		                        &dns_task_handle, 0);
	}
}


void dns_hijack_stop()
{
	if (dns_task_handle != NULL) {
		vTaskDelete(dns_task_handle);
		dns_task_handle = NULL;
	}
}


//
// DNS Hijack internal functions
//
static void dns_hijack_task(void* arg)
{
	int sock = -1;
	socklen_t addr_len;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	uint8_t rx[DNS_MAX_LEN];
	uint8_t tx[DNS_MAX_LEN + 16];

	(void) arg;

	ESP_LOGI(TAG, "Start task");

	while (1) {
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (sock < 0) {
			ESP_LOGE(TAG, "socket failed: errno %d", errno);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family      = AF_INET;
		server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		server_addr.sin_port        = htons(DNS_PORT);

		if (bind(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) != 0) {
			ESP_LOGE(TAG, "bind failed: errno %d", errno);
			close(sock);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		while (1) {
			int len;
			int qname_len;
			int reply_len;
			dns_header_t* hdr;
			uint8_t* p;

			addr_len = sizeof(client_addr);
			len = recvfrom(sock, rx, sizeof(rx), 0,
			               (struct sockaddr*) &client_addr, &addr_len);
			if (len < 0) {
				ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
				break;
			}

			// Need a header plus at least a root label and the type/class pair
			if (len < (int) sizeof(dns_header_t) + 5) continue;

			hdr = (dns_header_t*) rx;

			// Only answer standard queries carrying exactly one question
			if ((hdr->flags1 & DNS_QR_RESPONSE) != 0) continue;
			if ((hdr->flags1 & DNS_OPCODE_MASK) != 0) continue;
			if (ntohs(hdr->qd_count) != 1) continue;

			// Walk the QNAME label sequence to find the type/class that follow it
			qname_len = 0;
			p = rx + sizeof(dns_header_t);
			while ((p + qname_len) < (rx + len)) {
				uint8_t label = *(p + qname_len);

				if (label == 0) {
					qname_len++;
					break;
				}
				// Reject compression pointers and over-long labels outright
				if (label > 63) {
					qname_len = -1;
					break;
				}
				qname_len += label + 1;
			}
			if (qname_len <= 0) continue;

			// The question must be fully present
			if ((p + qname_len + 4) > (rx + len)) continue;

			// Only A/IN questions get an answer; anything else is declined so the
			// client can fall back rather than believe a malformed reply
			{
				uint16_t qtype  = (uint16_t) ((*(p + qname_len) << 8) | *(p + qname_len + 1));
				uint16_t qclass = (uint16_t) ((*(p + qname_len + 2) << 8) | *(p + qname_len + 3));

				reply_len = sizeof(dns_header_t) + qname_len + 4;
				memcpy(tx, rx, reply_len);
				hdr = (dns_header_t*) tx;
				hdr->flags1 = DNS_QR_RESPONSE | DNS_FLAG_AA;
				hdr->flags2 = 0;
				hdr->an_count = 0;
				hdr->ns_count = 0;
				hdr->ar_count = 0;

				if ((qtype != DNS_TYPE_A) || (qclass != DNS_CLASS_IN)) {
					hdr->flags2 = DNS_RCODE_NOTIMPL;
				} else {
					// Answer: pointer to the question's name, A/IN, TTL, our address
					uint8_t* a = tx + reply_len;

					*a++ = 0xC0;                        // compression pointer ...
					*a++ = sizeof(dns_header_t);        // ... to the QNAME above
					*a++ = 0; *a++ = DNS_TYPE_A;
					*a++ = 0; *a++ = DNS_CLASS_IN;
					*a++ = 0; *a++ = 0;
					*a++ = 0; *a++ = DNS_ANSWER_TTL;
					*a++ = 0; *a++ = 4;                 // rdlength
					memcpy(a, &portal_addr, 4);         // already network order
					a += 4;

					hdr->an_count = htons(1);
					reply_len = a - tx;
				}

				sendto(sock, tx, reply_len, 0,
				       (struct sockaddr*) &client_addr, addr_len);
			}
		}

		close(sock);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
