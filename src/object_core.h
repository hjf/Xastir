// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_OBJECT_CORE_H
#define XASTIR_OBJECT_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int xastir_object_valid_name(const char *name, int is_object);
/* Uncompressed normal-object assembly extracted from object_utils.c. */
int xastir_format_object_item(char *out, size_t capacity, const char *name, int is_object,
    const char *timestamp, const char *latitude, char table, const char *longitude,
    char symbol, const char *speed_course, const char *altitude);
int xastir_object_mark_killed(char *packet, size_t capacity, int is_object, int active);
void xastir_object_append_comment(char *packet, size_t capacity, const char *comment, const char *name, int is_object);
/* Fixed uncompressed reports. UTC day/hour/minute are explicit dependencies.
 * Comments accept at most 43 printable ASCII bytes, without silent truncation. */
int xastir_build_object_item(char *out, size_t capacity, const char *name, int is_object, int active,
    double latitude, double longitude, char table, char symbol, const char *comment,
    int day, int hour, int minute);
/* Legacy 20-second initial interval, doubling with up to 20% jitter.
 * Only accepted transmissions consume the killed-report limit (20). */
typedef struct { int interval, remaining; int64_t next_ms; } XastirObjectSchedule;
void xastir_object_schedule_start(XastirObjectSchedule *, int64_t now, int maximum_seconds, int killed);
int xastir_object_schedule_due(const XastirObjectSchedule *, int64_t now);
void xastir_object_schedule_accepted(XastirObjectSchedule *, int64_t now, int maximum_seconds, unsigned jitter_permille);
#ifdef __cplusplus
}
#endif
#endif
