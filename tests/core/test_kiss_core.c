// SPDX-License-Identifier: GPL-2.0-or-later
#include "kiss_core.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
  unsigned char encoded[XASTIR_KISS_MAX_ENCODED], data[] = {0,13,10,0xc0,0xdb};
  const unsigned char expected[] = {0xc0,0x30,0,13,10,0xdb,0xdc,0xdb,0xdd,0xc0};
  size_t size = xastir_kiss_encode(3, 0, data, sizeof(data), encoded, sizeof(encoded));
  CHECK(size == sizeof(expected) && !memcmp(encoded, expected, size));
  XastirKissDecoder decoder; XastirKissFrame frame;
  xastir_kiss_init(&decoder); CHECK(!xastir_kiss_decode_byte(&decoder, 42, &frame));
  for (size_t i = 0; i < size; ++i) {
    int result = xastir_kiss_decode_byte(&decoder, encoded[i], &frame);
    CHECK(result == (i == size - 1));
  }
  CHECK(frame.port == 3 && frame.command == 0 && frame.length == sizeof(data) && !memcmp(frame.data, data, sizeof(data)));
  unsigned char value = 192;
  const unsigned char parameter[] = {0xc0,2,0xdb,0xdc,0xc0};
  size = xastir_kiss_encode(0, 2, &value, 1, encoded, sizeof(encoded));
  CHECK(size == sizeof(parameter) && !memcmp(encoded, parameter, size));
  for (unsigned port = 0; port < 16; ++port) for (unsigned byte = 0; byte < 256; ++byte) {
    value = (unsigned char)byte; size = xastir_kiss_encode(port, 0, &value, 1, encoded, sizeof(encoded));
    for (size_t i = 0; i < size; ++i) xastir_kiss_decode_byte(&decoder, encoded[i], &frame);
    CHECK(frame.port == port && frame.command == 0 && frame.length == 1 && frame.data[0] == byte);
  }
  const unsigned char broken[] = {0xc0,0,42,0xdb,1,0xc0}; int dropped = 0, ready = 0;
  for (size_t i = 0; i < sizeof(broken); ++i) { int r = xastir_kiss_decode_byte(&decoder, broken[i], &frame); dropped += r < 0; ready += r > 0; }
  CHECK(dropped == 1 && !ready);
  xastir_kiss_decode_byte(&decoder, 0, &frame);
  for (size_t i = 0; i < XASTIR_KISS_MAX_FRAME; ++i) CHECK(!xastir_kiss_decode_byte(&decoder, 42, &frame));
  CHECK(xastir_kiss_decode_byte(&decoder, 42, &frame) == -1);
  CHECK(!xastir_kiss_decode_byte(&decoder, 0xc0, &frame));
  size = xastir_kiss_encode(15, 6, data, sizeof(data), encoded, sizeof(encoded));
  for (size_t i = 0; i < size; ++i) ready += xastir_kiss_decode_byte(&decoder, encoded[i], &frame) == 1;
  CHECK(ready == 1 && frame.port == 15 && frame.command == 6 && frame.length == sizeof(data));
  CHECK(!xastir_kiss_encode(16, 0, data, 1, encoded, sizeof(encoded)));
  CHECK(!xastir_kiss_encode(0, 0, data, sizeof(data), encoded, 5));
  return 0;
}
