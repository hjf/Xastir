// SPDX-License-Identifier: GPL-2.0-or-later
/* Protocol logic extracted from legacy interface.c and db.c, The Xastir Group.
 * Adds bounds/address validation and binary-safe assembly. */
#include "ax25_core.h"
#include <stdio.h>
#include <string.h>

int xastir_ax25_encode_address(const char *call, int last, int allow_repeated, unsigned char out[7])
{
  if (!call || !out) return 0;
  size_t length = strlen(call), base = 0;
  int repeated = length && call[length-1] == '*', ssid = 0;
  if (repeated) { if (!allow_repeated) return 0; --length; }
  while (base < length && call[base] != '-') ++base;
  if (!base || base > 6) return 0;
  unsigned char address[7];
  for (size_t i = 0; i < 6; ++i) {
    unsigned char c = i < base ? (unsigned char)call[i] : ' ';
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (i < base && !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    address[i] = (unsigned char)(c << 1);
  }
  if (base < length) {
    size_t digits = length - base - 1;
    if (digits < 1 || digits > 2) return 0;
    for (size_t i = base + 1; i < length; ++i) {
      if (call[i] < '0' || call[i] > '9') return 0;
      ssid = ssid * 10 + call[i] - '0';
    }
    if (ssid > 15) return 0;
  }
  address[6] = (unsigned char)(0x60 | (ssid << 1) | (repeated ? 0x80 : 0) | !!last);
  memcpy(out, address, 7); return 1;
}
int xastir_ax25_decode_address(const unsigned char address[7], int show_repeated, char *out, size_t capacity)
{
  if (!address || !out || !capacity) return 0;
  out[0] = 0;
  char call[11]; size_t length = 0; int padding = 0;
  for (int i = 0; i < 6; ++i) {
    if (address[i] & 1) return 0;
    unsigned char c = address[i] >> 1;
    if (c == ' ') padding = 1;
    else {
      if (padding || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
      call[length++] = (char)c;
    }
  }
  if (!length) return 0;
  const unsigned ssid = (address[6] >> 1) & 15;
  if (ssid) length += (size_t)snprintf(call + length, sizeof(call) - length, "-%u", ssid);
  if (show_repeated && (address[6] & 0x80)) call[length++] = '*';
  call[length] = 0;
  if (capacity <= length) return 0;
  memcpy(out, call, length + 1); return 1;
}
size_t xastir_ax25_build(const char *source, const char *destination, const char *path,
    const unsigned char *information, size_t length, unsigned char *out, size_t capacity)
{
  unsigned char addresses[70]; size_t count = 2;
  if (!out || (length && !information) || length > XASTIR_AX25_MAX_FRAME - 16
      || !xastir_ax25_encode_address(destination, 0, 0, addresses)
      || !xastir_ax25_encode_address(source, 0, 0, addresses + 7)) return 0;
  if (path && *path) {
    const char *start = path;
    while (*start) {
      if (count == 10) return 0;
      const char *end = strchr(start, ','); if (!end) end = start + strlen(start);
      const char *next = *end ? end + 1 : end;
      while (start < end && *start == ' ') ++start;
      while (end > start && end[-1] == ' ') --end;
      char call[11]; size_t n = (size_t)(end - start);
      if (!n || n >= sizeof(call)) return 0;
      memcpy(call, start, n); call[n] = 0;
      if (!xastir_ax25_encode_address(call, 0, 1, addresses + count * 7)) return 0;
      ++count;
      if (*next == 0 && next > end && next[-1] == ',') return 0;
      start = next;
    }
  }
  size_t size = count * 7 + 2 + length;
  if (size > capacity || size > XASTIR_AX25_MAX_FRAME) return 0;
  addresses[count * 7 - 1] |= 1;
  memcpy(out, addresses, count * 7); out[count * 7] = 3; out[count * 7 + 1] = 0xf0;
  if (length) memcpy(out + count * 7 + 2, information, length);
  return size;
}
size_t xastir_ax25_to_tnc2(const unsigned char *frame, size_t length, unsigned char *out, size_t capacity)
{
  if (!out || !capacity) return 0;
  out[0] = 0;
  if (!frame || length < 16 || length > XASTIR_AX25_MAX_FRAME) return 0;
  char addresses[10][11]; size_t count = 0, offset = 0; int last = 0;
  while (!last && count < 10) {
    if (length - offset < 7 || !xastir_ax25_decode_address(frame + offset, count >= 2, addresses[count], sizeof(addresses[count]))) return 0;
    last = frame[offset + 6] & 1; ++count; offset += 7;
  }
  if (!last || count < 2 || length - offset < 2 || (frame[offset] & 0xef) != 3 || frame[offset + 1] != 0xf0) return 0;
  offset += 2;
  char header[128]; int n = snprintf(header, sizeof(header), "%s>%s", addresses[1], addresses[0]);
  for (size_t i = 2; i < count; ++i) n += snprintf(header + n, sizeof(header) - (size_t)n, ",%s", addresses[i]);
  header[n++] = ':';
  size_t size = (size_t)n + length - offset;
  if (capacity <= size) return 0;
  memcpy(out, header, (size_t)n); memcpy(out + n, frame + offset, length - offset); out[size] = 0;
  return size;
}
