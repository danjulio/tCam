/*
 * TLS Certificate Utilities
 *
 * Provides the self-signed certificate used by the on-camera HTTPS server.  The
 * key pair and certificate are generated on the camera itself the first time they
 * are needed - EC P-256, self-signed, valid for 30 years - and stored in NVS, so
 * every camera has a unique private key that never leaves the device.  This is
 * deliberately not a build-time certificate: a key embedded in a published
 * firmware image is public knowledge.
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
#ifndef CERT_UTILITIES_H
#define CERT_UTILITIES_H

#include <stdbool.h>
#include <stddef.h>


//
// Cert Utilities API
//

/**
 * Return the camera's certificate and private key as null-terminated PEM strings,
 * generating and persisting them on first use.  The returned lengths INCLUDE the
 * null terminator, as the esp-tls stack expects for PEM input.  Buffers are
 * heap-allocated and owned by this module; they remain valid for the life of the
 * system.  Returns false if generation or storage fails.
 *
 * host is the camera name (used for the CN and for <host>.local) and ip4 is the
 * address the camera is currently reachable on.  Both go into the certificate's
 * subjectAltName; a stored certificate issued for a different name or address is
 * discarded and reissued, since browsers match against SAN and ignore CN.
 */
bool cert_get(const char* host, const unsigned char* ip4,
              const unsigned char** cert_pem, size_t* cert_len,
              const unsigned char** key_pem, size_t* key_len);

#endif /* CERT_UTILITIES_H */
