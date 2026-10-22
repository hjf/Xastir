// SPDX-License-Identifier: GPL-2.0-or-later
#include "ax25_core.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
  unsigned char frame[4096], text[4224], address[7]; char call[11];
  const unsigned char wire[] = {0x82,0xa0,0xa4,0xa6,0x40,0x40,0x60, 0x9c,0x60,0x86,0x82,0x98,0x98,0x67, 3,0xf0,'>','O','K'};
  size_t size = xastir_ax25_build("N0CALL-3", "APRS", "", (const unsigned char *)">OK", 3, frame, sizeof(frame));
  CHECK(size == sizeof(wire) && !memcmp(frame, wire, size));
  CHECK(xastir_ax25_to_tnc2(wire, sizeof(wire), text, sizeof(text)) == strlen("N0CALL-3>APRS:>OK"));
  CHECK(!strcmp((char *)text, "N0CALL-3>APRS:>OK"));
  const unsigned char binary[] = {0,13,10,0xc0,0xdb,0xff};
  size = xastir_ax25_build("n0call-15", "APRS", "WIDE1-1*, WIDE2-2", binary, sizeof(binary), frame, sizeof(frame));
  CHECK(size == 30 + sizeof(binary));
  CHECK(frame[20] == 0xe2 && frame[27] == 0x65);
  const char *header = "N0CALL-15>APRS,WIDE1-1*,WIDE2-2:";
  size_t n = xastir_ax25_to_tnc2(frame, size, text, sizeof(text));
  CHECK(n == strlen(header) + sizeof(binary) && !memcmp(text, header, strlen(header)));
  CHECK(!memcmp(text + strlen(header), binary, sizeof(binary)));
  frame[28] = 0x13; CHECK(xastir_ax25_to_tnc2(frame, size, text, sizeof(text)));
  frame[28] = 0; CHECK(!xastir_ax25_to_tnc2(frame, size, text, sizeof(text)));
  frame[28] = 3; frame[29] = 0xcc; CHECK(!xastir_ax25_to_tnc2(frame, size, text, sizeof(text)));
  for (size_t i = 0; i < 16; ++i) CHECK(!xastir_ax25_to_tnc2(wire, i, text, sizeof(text)));
  CHECK(!xastir_ax25_to_tnc2(wire, sizeof(wire), text, 5));
  CHECK(xastir_ax25_encode_address("ABC123-15*", 1, 1, address));
  CHECK(xastir_ax25_decode_address(address, 1, call, sizeof(call)) && !strcmp(call, "ABC123-15*"));
  CHECK(!xastir_ax25_encode_address("ABC123-15*", 1, 0, address));
  for (const char **p = (const char *[]){"", "TOOLONG", "A B", "CALL-16", "CALL-", "CALL--1", "CALL-x", NULL}; *p; ++p)
    CHECK(!xastir_ax25_encode_address(*p, 1, 0, address));
  CHECK(!xastir_ax25_build("LOCAL", "APRS", "WIDE1-1,", binary, sizeof(binary), frame, sizeof(frame)));
  CHECK(!xastir_ax25_build("LOCAL", "APRS", "A,A,A,A,A,A,A,A,A", binary, sizeof(binary), frame, sizeof(frame)));
  CHECK(!xastir_ax25_build("LOCAL", "APRS", "", binary, sizeof(binary), frame, 16));
  memcpy(frame, wire, sizeof(wire)); frame[6] |= 1; CHECK(!xastir_ax25_to_tnc2(frame, sizeof(wire), text, sizeof(text)));
  memcpy(frame, wire, sizeof(wire)); frame[0] |= 1; CHECK(!xastir_ax25_to_tnc2(frame, sizeof(wire), text, sizeof(text)));
  memcpy(frame, wire, sizeof(wire)); frame[13] &= ~1; CHECK(!xastir_ax25_to_tnc2(frame, sizeof(wire), text, sizeof(text)));
  return 0;
}
