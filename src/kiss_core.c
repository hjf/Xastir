// SPDX-License-Identifier: GPL-2.0-or-later
/* Extracted from legacy interface.c KISS framing, The Xastir Group.
 * Keeps binary bytes and command/port metadata separate from text processing. */
#include "kiss_core.h"
#include <string.h>
#define FEND 0xc0
#define FESC 0xdb
#define TFEND 0xdc
#define TFESC 0xdd
void xastir_kiss_init(XastirKissDecoder *d) { memset(d, 0, sizeof(*d)); }
int xastir_kiss_decode_byte(XastirKissDecoder *d, unsigned char byte, XastirKissFrame *frame)
{
  if (byte == FEND) {
    int ready = d->started && !d->discard && d->length && !d->escaped;
    if (ready) {
      frame->port = d->buffer[0] >> 4; frame->command = d->buffer[0] & 15;
      frame->data = d->buffer + 1; frame->length = d->length - 1;
    }
    d->started = 1; d->discard = 0; d->length = 0; d->escaped = 0;
    return ready;
  }
  if (!d->started || d->discard) return 0;
  if (d->escaped) {
    d->escaped = 0;
    if (byte == TFEND) byte = FEND;
    else if (byte == TFESC) byte = FESC;
    else { d->discard = 1; d->length = 0; return -1; }
  } else if (byte == FESC) { d->escaped = 1; return 0; }
  if (d->length == sizeof(d->buffer)) { d->discard = 1; d->length = 0; return -1; }
  d->buffer[d->length++] = byte; return 0;
}
size_t xastir_kiss_encode(unsigned port, unsigned command, const unsigned char *data, size_t length,
                         unsigned char *out, size_t capacity)
{
  if (port > 15 || command > 15 || length > XASTIR_KISS_MAX_FRAME || (!data && length) || !out) return 0;
  unsigned char type = (unsigned char)((port << 4) | command);
  size_t size = 3 + (type == FEND || type == FESC);
  for (size_t i = 0; i < length; ++i) size += 1 + (data[i] == FEND || data[i] == FESC);
  if (size > capacity) return 0;
  size_t offset = 0; out[offset++] = FEND;
  for (size_t i = 0; i <= length; ++i) {
    unsigned char byte = i ? data[i - 1] : type;
    if (byte == FEND || byte == FESC) { out[offset++] = FESC; out[offset++] = byte == FEND ? TFEND : TFESC; }
    else out[offset++] = byte;
  }
  out[offset++] = FEND; return offset;
}
