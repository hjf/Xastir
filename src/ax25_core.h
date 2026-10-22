// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_AX25_CORE_H
#define XASTIR_AX25_CORE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define XASTIR_AX25_MAX_FRAME 4096
/* Extracted from interface.c:fix_up_callsign/send_ax25_frame and
 * db.c:decode_ax25_address/decode_ax25_header. No transport or UI globals.
 * All binary buffers use explicit lengths. Zero indicates invalid input or
 * insufficient capacity. AX.25 frames exclude HDLC flags and FCS bytes. */
int xastir_ax25_encode_address(const char *call, int last, int allow_repeated, unsigned char out[7]);
int xastir_ax25_decode_address(const unsigned char address[7], int show_repeated, char *out, size_t capacity);
size_t xastir_ax25_build(const char *source, const char *destination, const char *path,
    const unsigned char *information, size_t length, unsigned char *out, size_t capacity);
/* Convert APRS UI frames (03/13, F0) to TNC2. The information field is copied
 * exactly, including binary bytes. Return length excludes the extra NUL byte. */
size_t xastir_ax25_to_tnc2(const unsigned char *frame, size_t length, unsigned char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
