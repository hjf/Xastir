// SPDX-License-Identifier: GPL-2.0-or-later
/* Extracted from objects.c and object_utils.c (The Xastir Group, GPL-2.0+).
 * Fixed-position adapters supply coordinates/time; no GUI or identity globals. */
#include "object_core.h"
#include "aprs_position.h"
#include <stdio.h>
#include <string.h>

int xastir_object_valid_name(const char *name, int is_object)
{
  if (!name) return 0;
  const size_t length = strlen(name);
  if (length > 9 || length < (is_object ? 1u : 3u)) return 0;
  for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
    if (*p < 32 || *p > 126 || (!is_object && (*p == '!' || *p == '_'))) return 0;
  return 1;
}
int xastir_format_object_item(char *out, size_t capacity, const char *name, int is_object,
    const char *timestamp, const char *latitude, char table, const char *longitude,
    char symbol, const char *speed_course, const char *altitude)
{
  if (!out || !capacity) return 0;
  int n = is_object
    ? snprintf(out, capacity, ";%-9s*%s%s%c%s%c%s%s", name, timestamp, latitude, table, longitude, symbol, speed_course, altitude)
    : snprintf(out, capacity, ")%s!%s%c%s%c%s%s", name, latitude, table, longitude, symbol, speed_course, altitude);
  if (n < 0 || (size_t)n >= capacity) { out[0] = 0; return 0; }
  return 1;
}
int xastir_object_mark_killed(char *packet, size_t capacity, int is_object, int active)
{
  if (active || !packet || !capacity || strlen(packet) >= capacity) return 0;
  if (is_object) { if (strlen(packet) <= 10) return 0; packet[10] = '_'; return 1; }
  for (size_t i = 0; packet[i] && i < 11; ++i) if (packet[i] == '!') { packet[i] = '_'; return 1; }
  return 0;
}
void xastir_object_append_comment(char *packet, size_t capacity, const char *comment, const char *name, int is_object)
{
  if (!packet || !capacity || !comment || !name) return;
  size_t length = strlen(packet), limit = is_object ? 80 : 64 + strlen(name);
  while (*comment && length < limit && length + 1 < capacity) packet[length++] = *comment++;
  if (length < capacity) packet[length] = 0;
}
int xastir_build_object_item(char *out, size_t capacity, const char *name, int is_object, int active,
    double latitude, double longitude, char table, char symbol, const char *comment, int day, int hour, int minute)
{
  if (!out || !capacity) return 0;
  out[0] = 0;
  if (!xastir_object_valid_name(name, is_object) || name[strlen(name)-1] == ' ' || !comment || strlen(comment) > 43
      || (is_object && (day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59))) return 0;
  if (table != '/' && table != '\\' && !(table >= 'A' && table <= 'Z') && !(table >= '0' && table <= '9')) return 0;
  for (const unsigned char *p = (const unsigned char *)comment; *p; ++p) if (*p < 32 || *p > 126) return 0;
  char position[128], lat[9], lon[10], timestamp[8] = "";
  if (!xastir_build_position_beacon(latitude, longitude, table, symbol, "", position, sizeof(position))) return 0;
  memcpy(lat, position+1, 8); lat[8] = 0; memcpy(lon, position+10, 9); lon[9] = 0;
  if (is_object) snprintf(timestamp, sizeof(timestamp), "%02d%02d%02dz", day, hour, minute);
  if (!xastir_format_object_item(out, capacity, name, is_object, timestamp, lat, table, lon, symbol, "", "")) return 0;
  xastir_object_mark_killed(out, capacity, is_object, active);
  if (strlen(out) + strlen(comment) >= capacity) { out[0] = 0; return 0; }
  xastir_object_append_comment(out, capacity, comment, name, is_object);
  return 1;
}
void xastir_object_schedule_start(XastirObjectSchedule *s, int64_t now, int maximum, int killed)
{
  if (maximum < 1) maximum = 1;
  s->interval = maximum < 20 ? maximum : 20; s->remaining = killed ? 20 : -1;
  s->next_ms = now + s->interval * 1000;
}
int xastir_object_schedule_due(const XastirObjectSchedule *s, int64_t now) { return s->remaining && now >= s->next_ms; }
void xastir_object_schedule_accepted(XastirObjectSchedule *s, int64_t now, int maximum, unsigned jitter)
{
  if (!s->remaining) return;
  if (s->remaining > 0 && !--s->remaining) { s->next_ms = 0; return; }
  if (maximum < 1) maximum = 1;
  if (maximum > 86400) maximum = 86400;
  int interval = s->interval * 2; if (interval > maximum) interval = maximum;
  if (jitter > 1000) jitter = 1000;
  interval -= (int)(jitter * (unsigned)(interval / 5) + 500) / 1000;
  if (interval < 1) interval = 1;
  s->interval = interval; s->next_ms = now + interval * 1000;
}
