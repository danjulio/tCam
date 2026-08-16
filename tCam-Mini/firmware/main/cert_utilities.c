/*
 * TLS Certificate Utilities
 *
 * See cert_utilities.h for a description of this module's role.
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
#include "cert_utilities.h"
#include "ps_utilities.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>


//
// Cert Utilities constants
//
#define CERT_NVS_NAMESPACE "tcamtls"
#define CERT_NVS_CERT_KEY  "cert_pem"
#define CERT_NVS_PKEY_KEY  "pkey_pem"

// Identity a stored certificate was issued for.  Bumping the leading version
// invalidates every previously stored certificate, which is how a change to the
// generation code (new extensions, different key type) gets rolled out to
// cameras that already have one.
#define CERT_NVS_IDENT_KEY "ident"
#define CERT_FORMAT_VERSION 2
#define CERT_IDENT_MAX_LEN 64

#define CERT_PEM_MAX_LEN   2048
#define KEY_PEM_MAX_LEN    1024

// Fixed validity window: 2020 through 2050 (30 years).  Fixed strings rather than
// clock-derived because the camera's RTC starts at 1970 on power-up - a notBefore
// taken from an unset clock would make the certificate invalid.
#define CERT_NOT_BEFORE "20200101000000"
#define CERT_NOT_AFTER  "20500101000000"

// Extended key usage: id-kp-serverAuth (1.3.6.1.5.5.7.3.1), DER-encoded by hand
// as SEQUENCE { OID }.  Built literally rather than through
// mbedtls_x509write_crt_set_ext_key_usage() so this does not depend on an API
// that differs between mbedTLS 2.x and 3.x.
static const unsigned char EKU_SERVER_AUTH_DER[] = {
	0x30, 0x0A,                                            // SEQUENCE, 10 bytes
	0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01
};

// Stack for the temporary generation task.  mbedTLS EC point multiplication uses
// several KB of stack temporaries; running it on a caller's (smaller) stack
// overflowed and corrupted memory.  This task exists only for the second or so
// that generation takes and its stack is returned to the heap when it exits.
#define CERT_GEN_TASK_STACK 12288


//
// Cert Utilities variables
//
static const char* TAG = "cert_utilities";

static unsigned char* cert_buf = NULL;
static unsigned char* key_buf = NULL;
static size_t cert_buf_len = 0;    // includes null terminator
static size_t key_buf_len = 0;     // includes null terminator

// Identity the certificate being generated / just loaded is for
static char cert_host[PS_SSID_MAX_LEN+1];
static unsigned char cert_ip[4];
static char cert_ident[CERT_IDENT_MAX_LEN];


//
// Cert Utilities forward declarations
//
static bool cert_load_from_nvs();
static bool cert_generate();
static bool cert_store_to_nvs();
static bool cert_verify();
static int build_san(unsigned char* out, size_t out_len);


//
// Cert Utilities API
//
// Handshake between cert_get and the temporary generation task
static SemaphoreHandle_t gen_done_sem = NULL;
static volatile bool gen_result = false;

static void cert_gen_task(void* arg)
{
	gen_result = cert_generate();
	xSemaphoreGive(gen_done_sem);
	vTaskDelete(NULL);
}


bool cert_get(const char* host, const unsigned char* ip4,
              const unsigned char** cert_pem, size_t* cert_len,
              const unsigned char** key_pem, size_t* key_len)
{
	// Record the identity this certificate must be valid for
	strncpy(cert_host, (host != NULL) ? host : "tCam-Mini", PS_SSID_MAX_LEN);
	cert_host[PS_SSID_MAX_LEN] = 0;
	memcpy(cert_ip, ip4, 4);
	snprintf(cert_ident, sizeof(cert_ident), "%d|%s|%d.%d.%d.%d", CERT_FORMAT_VERSION,
	         cert_host, cert_ip[0], cert_ip[1], cert_ip[2], cert_ip[3]);

	if (cert_buf == NULL) {
		if (!cert_load_from_nvs()) {
			ESP_LOGI(TAG, "Issuing certificate for %s (this takes a moment)", cert_ident);

			// Run generation on a dedicated task with a stack sized for EC math,
			// then reclaim it - callers keep their small permanent stacks
			gen_done_sem = xSemaphoreCreateBinary();
			if (gen_done_sem == NULL) return false;

			if (xTaskCreatePinnedToCore(&cert_gen_task, "cert_gen", CERT_GEN_TASK_STACK,
			                            NULL, 1, NULL, 0) != pdPASS) {
				ESP_LOGE(TAG, "Could not start generation task");
				vSemaphoreDelete(gen_done_sem);
				gen_done_sem = NULL;
				return false;
			}

			(void) xSemaphoreTake(gen_done_sem, portMAX_DELAY);
			vSemaphoreDelete(gen_done_sem);
			gen_done_sem = NULL;

			if (!gen_result) {
				return false;
			}
			if (!cert_store_to_nvs()) {
				// Still usable this boot; it will be regenerated next time
				ESP_LOGE(TAG, "Could not persist certificate");
			}
		}

		// Parse back what we are about to hand the TLS stack.  esp-tls reports an
		// unusable certificate only as a generic handshake failure with no
		// indication of why, so check it here where the reason can be reported.
		//
		// When reading those failures, note that the codes are easy to confuse:
		//   -0x6980 NO_USABLE_CIPHERSUITE - our end has no suitable certificate
		//   -0x7380 NO_CIPHER_CHOSEN      - no ciphersuite in common
		//   -0x7780 FATAL_ALERT_MESSAGE   - the PEER rejected what we sent, which
		//                                   is what a browser does when the user
		//                                   has not accepted a self-signed cert
		if (!cert_verify()) {
			free(cert_buf); cert_buf = NULL;
			free(key_buf);  key_buf = NULL;
			return false;
		}
	}

	*cert_pem = cert_buf;
	*cert_len = cert_buf_len;
	*key_pem  = key_buf;
	*key_len  = key_buf_len;

	return true;
}


//
// Cert Utilities internal functions
//
static bool cert_load_from_nvs()
{
	char stored_ident[CERT_IDENT_MAX_LEN];
	esp_err_t err;
	nvs_handle_t h;
	size_t clen = 0, klen = 0, ilen = sizeof(stored_ident);

	err = nvs_open(CERT_NVS_NAMESPACE, NVS_READONLY, &h);
	if (err != ESP_OK) return false;

	// A certificate issued for a different name, address or format version is of
	// no use - browsers validate against subjectAltName, so a stale one would
	// produce a name mismatch on every visit
	if ((nvs_get_str(h, CERT_NVS_IDENT_KEY, stored_ident, &ilen) != ESP_OK) ||
	    (strcmp(stored_ident, cert_ident) != 0)) {
		ESP_LOGI(TAG, "Stored certificate does not match %s - reissuing", cert_ident);
		nvs_close(h);
		return false;
	}

	if ((nvs_get_blob(h, CERT_NVS_CERT_KEY, NULL, &clen) != ESP_OK) ||
	    (nvs_get_blob(h, CERT_NVS_PKEY_KEY, NULL, &klen) != ESP_OK) ||
	    (clen == 0) || (klen == 0) ||
	    (clen > CERT_PEM_MAX_LEN) || (klen > KEY_PEM_MAX_LEN)) {
		nvs_close(h);
		return false;
	}

	cert_buf = malloc(clen);
	key_buf = malloc(klen);
	if ((cert_buf == NULL) || (key_buf == NULL)) {
		nvs_close(h);
		free(cert_buf); cert_buf = NULL;
		free(key_buf);  key_buf = NULL;
		return false;
	}

	if ((nvs_get_blob(h, CERT_NVS_CERT_KEY, cert_buf, &clen) != ESP_OK) ||
	    (nvs_get_blob(h, CERT_NVS_PKEY_KEY, key_buf, &klen) != ESP_OK)) {
		nvs_close(h);
		free(cert_buf); cert_buf = NULL;
		free(key_buf);  key_buf = NULL;
		return false;
	}
	nvs_close(h);

	// Stored blobs include their null terminators; verify before trusting
	if ((cert_buf[clen-1] != 0) || (key_buf[klen-1] != 0)) {
		ESP_LOGE(TAG, "Stored certificate malformed - regenerating");
		free(cert_buf); cert_buf = NULL;
		free(key_buf);  key_buf = NULL;
		return false;
	}

	cert_buf_len = clen;
	key_buf_len = klen;
	ESP_LOGI(TAG, "Loaded certificate from NVS");
	return true;
}


static bool cert_generate()
{
	bool success = false;
	int ret;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_entropy_context entropy;
	mbedtls_mpi serial;
	mbedtls_pk_context key;
	mbedtls_x509write_cert crt;
	unsigned char serial_bytes[16];
	unsigned char san[160];
	int san_len;
	char subject[96];
	const char* pers = "tcam_cert_gen";

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_pk_init(&key);
	mbedtls_x509write_crt_init(&crt);
	mbedtls_mpi_init(&serial);

	cert_buf = malloc(CERT_PEM_MAX_LEN);
	key_buf = malloc(KEY_PEM_MAX_LEN);
	if ((cert_buf == NULL) || (key_buf == NULL)) {
		ESP_LOGE(TAG, "Buffer allocation failed");
		goto done;
	}

	ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
	                            (const unsigned char*) pers, strlen(pers));
	if (ret != 0) { ESP_LOGE(TAG, "drbg seed failed (-0x%04x)", -ret); goto done; }

	// EC P-256: generation takes well under a second on the ESP32, unlike RSA
	ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
	if (ret != 0) { ESP_LOGE(TAG, "pk setup failed (-0x%04x)", -ret); goto done; }

	ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
	                          mbedtls_ctr_drbg_random, &ctr_drbg);
	if (ret != 0) { ESP_LOGE(TAG, "key generation failed (-0x%04x)", -ret); goto done; }

	// Random positive serial number
	esp_fill_random(serial_bytes, sizeof(serial_bytes));
	serial_bytes[0] &= 0x7F;
	ret = mbedtls_mpi_read_binary(&serial, serial_bytes, sizeof(serial_bytes));
	if (ret != 0) { ESP_LOGE(TAG, "serial failed (-0x%04x)", -ret); goto done; }

	mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
	mbedtls_x509write_crt_set_subject_key(&crt, &key);
	mbedtls_x509write_crt_set_issuer_key(&crt, &key);

	ret = mbedtls_x509write_crt_set_serial(&crt, &serial);
	if (ret != 0) { ESP_LOGE(TAG, "set serial failed (-0x%04x)", -ret); goto done; }

	snprintf(subject, sizeof(subject), "CN=%s,O=tCam", cert_host);
	ret = mbedtls_x509write_crt_set_subject_name(&crt, subject);
	if (ret != 0) { ESP_LOGE(TAG, "subject failed (-0x%04x)", -ret); goto done; }
	ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject);
	if (ret != 0) { ESP_LOGE(TAG, "issuer failed (-0x%04x)", -ret); goto done; }
	ret = mbedtls_x509write_crt_set_validity(&crt, CERT_NOT_BEFORE, CERT_NOT_AFTER);
	if (ret != 0) { ESP_LOGE(TAG, "validity failed (-0x%04x)", -ret); goto done; }
	ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
	if (ret != 0) { ESP_LOGE(TAG, "constraints failed (-0x%04x)", -ret); goto done; }

	// subjectAltName.  Chrome and Safari ignore the common name entirely and
	// validate the hostname against SAN, so without this the certificate is
	// rejected outright rather than merely warned about.
	san_len = build_san(san, sizeof(san));
	if (san_len <= 0) { ESP_LOGE(TAG, "SAN build failed"); goto done; }
	ret = mbedtls_x509write_crt_set_extension(&crt,
			MBEDTLS_OID_SUBJECT_ALT_NAME, MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),
			0, san, san_len);
	if (ret != 0) { ESP_LOGE(TAG, "SAN failed (-0x%04x)", -ret); goto done; }

	// keyUsage must include digitalSignature: with ECDHE-ECDSA the server signs
	// the key exchange with this key, and mbedTLS rejects a certificate whose
	// keyUsage is present but lacks the bit the ciphersuite needs
	ret = mbedtls_x509write_crt_set_key_usage(&crt,
			MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_AGREEMENT);
	if (ret != 0) { ESP_LOGE(TAG, "key usage failed (-0x%04x)", -ret); goto done; }

	ret = mbedtls_x509write_crt_set_extension(&crt,
			MBEDTLS_OID_EXTENDED_KEY_USAGE, MBEDTLS_OID_SIZE(MBEDTLS_OID_EXTENDED_KEY_USAGE),
			0, EKU_SERVER_AUTH_DER, sizeof(EKU_SERVER_AUTH_DER));
	if (ret != 0) { ESP_LOGE(TAG, "ext key usage failed (-0x%04x)", -ret); goto done; }

	ret = mbedtls_x509write_crt_set_subject_key_identifier(&crt);
	if (ret != 0) { ESP_LOGE(TAG, "subject key id failed (-0x%04x)", -ret); goto done; }
	ret = mbedtls_x509write_crt_set_authority_key_identifier(&crt);
	if (ret != 0) { ESP_LOGE(TAG, "authority key id failed (-0x%04x)", -ret); goto done; }

	ret = mbedtls_x509write_crt_pem(&crt, cert_buf, CERT_PEM_MAX_LEN,
	                                mbedtls_ctr_drbg_random, &ctr_drbg);
	if (ret != 0) { ESP_LOGE(TAG, "cert pem write failed (-0x%04x)", -ret); goto done; }

	ret = mbedtls_pk_write_key_pem(&key, key_buf, KEY_PEM_MAX_LEN);
	if (ret != 0) { ESP_LOGE(TAG, "key pem write failed (-0x%04x)", -ret); goto done; }

	cert_buf_len = strlen((char*) cert_buf) + 1;
	key_buf_len = strlen((char*) key_buf) + 1;
	ESP_LOGI(TAG, "Generated certificate (%d + %d bytes, valid to 2050)",
	         (int) cert_buf_len, (int) key_buf_len);
	success = true;

done:
	if (!success) {
		free(cert_buf); cert_buf = NULL;
		free(key_buf);  key_buf = NULL;
	}
	mbedtls_mpi_free(&serial);
	mbedtls_x509write_crt_free(&crt);
	mbedtls_pk_free(&key);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	return success;
}


static bool cert_store_to_nvs()
{
	esp_err_t err;
	nvs_handle_t h;

	err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &h);
	if (err != ESP_OK) return false;

	if ((nvs_set_blob(h, CERT_NVS_CERT_KEY, cert_buf, cert_buf_len) != ESP_OK) ||
	    (nvs_set_blob(h, CERT_NVS_PKEY_KEY, key_buf, key_buf_len) != ESP_OK) ||
	    (nvs_set_str(h, CERT_NVS_IDENT_KEY, cert_ident) != ESP_OK) ||
	    (nvs_commit(h) != ESP_OK)) {
		nvs_close(h);
		return false;
	}

	nvs_close(h);
	ESP_LOGI(TAG, "Certificate stored");
	return true;
}


/**
 * Build the DER value of a subjectAltName extension covering the ways a browser
 * can reach this camera: its mDNS name, its bare hostname, and its current IP.
 *
 *   GeneralNames ::= SEQUENCE OF GeneralName
 *   GeneralName  ::= [2] IMPLICIT IA5String   (dNSName)
 *                  | [7] IMPLICIT OCTET STRING (iPAddress)
 *
 * Encoded by hand because mbedtls_x509write_crt_set_subject_alternative_name()
 * does not exist in every mbedTLS version ESP-IDF has shipped, whereas
 * set_extension() with a literal DER value works on all of them.  Lengths stay
 * under 128 bytes so single-byte DER length encoding is always valid here.
 *
 * Returns the number of bytes written, or -1 if the buffer is too small.
 */
static int build_san(unsigned char* out, size_t out_len)
{
	char local_name[PS_SSID_MAX_LEN + 8];
	int host_len, local_len;
	size_t body_len, total;
	unsigned char* p;

	snprintf(local_name, sizeof(local_name), "%s.local", cert_host);
	host_len = strlen(cert_host);
	local_len = strlen(local_name);

	// SEQUENCE header (2) + dNSName x2 (2 + len each) + iPAddress (2 + 4)
	body_len = (2 + local_len) + (2 + host_len) + (2 + 4);
	total = 2 + body_len;
	if ((body_len > 127) || (total > out_len)) return -1;

	p = out;
	*p++ = 0x30;                     // SEQUENCE
	*p++ = (unsigned char) body_len;

	*p++ = 0x82;                     // [2] dNSName
	*p++ = (unsigned char) local_len;
	memcpy(p, local_name, local_len);
	p += local_len;

	*p++ = 0x82;                     // [2] dNSName
	*p++ = (unsigned char) host_len;
	memcpy(p, cert_host, host_len);
	p += host_len;

	*p++ = 0x87;                     // [7] iPAddress
	*p++ = 4;
	memcpy(p, cert_ip, 4);
	p += 4;

	return (int) (p - out);
}


/**
 * Parse the certificate and key back and confirm they are usable as a TLS server
 * identity.  This is cheap insurance against a silent failure mode: if the stack
 * cannot use the certificate, the only symptom at handshake time is a generic
 * handshake failure that says nothing about the cause.
 */
static bool cert_verify()
{
	bool ok = false;
	int ret;
	mbedtls_pk_context pk;
	mbedtls_x509_crt crt;

	mbedtls_x509_crt_init(&crt);
	mbedtls_pk_init(&pk);

	ret = mbedtls_x509_crt_parse(&crt, cert_buf, cert_buf_len);
	if (ret != 0) {
		ESP_LOGE(TAG, "certificate does not parse (-0x%04x)", -ret);
		goto done;
	}

	ret = mbedtls_pk_parse_key(&pk, key_buf, key_buf_len, NULL, 0);
	if (ret != 0) {
		ESP_LOGE(TAG, "private key does not parse (-0x%04x)", -ret);
		goto done;
	}

	if (!mbedtls_pk_can_do(&crt.pk, MBEDTLS_PK_ECDSA)) {
		ESP_LOGE(TAG, "certificate key cannot do ECDSA - no ECDHE-ECDSA ciphersuite "
		              "will be usable");
		goto done;
	}

	if (mbedtls_pk_ec(crt.pk)->grp.id != MBEDTLS_ECP_DP_SECP256R1) {
		ESP_LOGE(TAG, "certificate is on curve %d, not secp256r1",
		         (int) mbedtls_pk_ec(crt.pk)->grp.id);
		goto done;
	}

	ESP_LOGI(TAG, "Certificate OK: %s, ECDSA/secp256r1, %d byte PEM",
	         cert_ident, (int) cert_buf_len);
	ok = true;

done:
	mbedtls_pk_free(&pk);
	mbedtls_x509_crt_free(&crt);
	return ok;
}
