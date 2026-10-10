// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_APRS_POSITION_H
#define XASTIR_APRS_POSITION_H
#include <stddef.h>
#include "coordinate_formats.h"
#ifdef __cplusplus
extern "C" {
#endif
/* UI-independent result. Coordinates retain Xastir's centisecond offsets:
 * latitude 0 = north pole, longitude 0 = 180 west. Strings are optional
 * decoded extensions, empty when absent. Input must be writable/NUL terminated;
 * successful extraction advances it and removes DAO from the comment. */
typedef struct {
  long coord_lat, coord_lon;
  unsigned char pos_amb;
  struct { char aprs_type, aprs_symbol, special_overlay; } aprs_symbol;
  char altitude[32], speed[32], course[32], power_gain[32];
} XastirPosition;
/* info starts after the Mic-E data type byte. Returns 1 on success. */
int xastir_expand_mice(const char *destination, const char *info, char *expanded, size_t capacity, int *emergency);
void xastir_overlay_symbol(char symbol, char table, XastirPosition *position);
int xastir_decode_dao(int *lat, int *lon, char *datum, char *comment);
int xastir_extract_position(XastirPosition *position, char **info, int grid);
int xastir_extract_comp_position(XastirPosition *position, char **info);
int xastir_extract_speed_course(char *info, char *speed, char *course);
int xastir_extract_altitude(char *info, char *altitude);
void xastir_convert_lat_l2s(long lat, char *str, int capacity, int format);
void xastir_convert_lon_l2s(long lon, char *str, int capacity, int format);
/* Legacy fixed-position assembly; explicit parameters replace UI globals.
 * Returns 1 on success; information excludes TNC framing CR/LF. */
int xastir_format_fixed_position(char *out, size_t capacity, char message_type,
    const char *position, const char *phg, const char *altitude, const char *comment, int compressed);
int xastir_build_position_beacon(double latitude, double longitude, char table,
    char symbol, const char *comment, char *out, size_t capacity);
long xastir_convert_lat_s2l(char *lat);
long xastir_convert_lon_s2l(char *lon);
#ifdef __cplusplus
}
#endif
#endif
