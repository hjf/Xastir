// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_KISS_CORE_H
#define XASTIR_KISS_CORE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define XASTIR_KISS_MAX_FRAME 4096
#define XASTIR_KISS_MAX_ENCODED (2 * (XASTIR_KISS_MAX_FRAME + 1) + 2)
typedef struct {
  unsigned char buffer[XASTIR_KISS_MAX_FRAME + 1];
  size_t length;
  int started, escaped, discard;
} XastirKissDecoder;
typedef struct { unsigned port, command; const unsigned char *data; size_t length; } XastirKissFrame;
void xastir_kiss_init(XastirKissDecoder *);
/* Incremental framing extracted from interface.c:port_read. Returns 1 for a
 * completed frame, -1 when dropping an invalid/oversized frame, 0 otherwise.
 * Output borrows decoder storage until the next call. Unknown commands remain
 * distinguishable from data. Malformed escapes discard through the next FEND. */
int xastir_kiss_decode_byte(XastirKissDecoder *, unsigned char, XastirKissFrame *);
/* Both the command/port byte and payload are escaped. Standard KISS only. */
size_t xastir_kiss_encode(unsigned port, unsigned command, const unsigned char *data, size_t length,
                         unsigned char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
