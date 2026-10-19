/*
 *
 * XASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2026 The Xastir Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 */

#ifndef XASTIR_STATION_VIEW_H
#define XASTIR_STATION_VIEW_H
#include "station_db.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _selections
{
  int none;
  int mine;
  int tnc;
  int direct;
  int via_digi;
  int net;
  int tactical;
  int old_data;

  int stations;
  int fixed_stations;
  int moving_stations;
  int weather_stations;
  int CWOP_wx_stations;
  int objects;
  int weather_objects;
  int gauge_objects;
  int other_objects;
  int aircraft_objects;
  int vessel_objects;
} Selections;
typedef struct _what_to_display
{
  int callsign;
  int label_all_trackpoints;
  int symbol;
  int symbol_rotate;
  int trail;

  int course;
  int speed;
  int speed_short;
  int altitude;

  int weather;
  int weather_text;
  int temperature_only;
  int wind_barb;

  int aloha_circle;
  int ambiguity;
  int phg;
  int default_phg;
  int phg_of_moving;

  int df_data;
  int df_beamwidth_data;
  int df_bearing_data;
  int dr_data;
  int dr_arc;
  int dr_course;
  int dr_symbol;

  int dist_bearing;
  int last_heard;
} What_to_display;

/* Original View menu categories from list_gui.c. Mobile means a retained trail,
 * local means heard through a TNC. Object lists include inactive records. */
typedef enum {
  XASTIR_LIST_ALL, XASTIR_LIST_MOBILE, XASTIR_LIST_WEATHER, XASTIR_LIST_LOCAL,
  XASTIR_LIST_LAST, XASTIR_LIST_OBJECTS, XASTIR_LIST_OWN_OBJECTS, XASTIR_LIST_COUNT
} XastirStationList;
/* Display conversions extracted from main.c:update_units and the legacy
 * weather/station lists. Storage and protocol values never use display units. */
typedef enum {
  XASTIR_DISPLAY_DISTANCE, XASTIR_DISPLAY_SHORT_DISTANCE, XASTIR_DISPLAY_ALTITUDE,
  XASTIR_DISPLAY_SPEED, XASTIR_DISPLAY_WIND, XASTIR_DISPLAY_TEMPERATURE,
  XASTIR_DISPLAY_PRESSURE, XASTIR_DISPLAY_RAIN
} XastirDisplayQuantity;
double xastir_display_value(double value, XastirDisplayQuantity quantity, int english);
const char *xastir_display_unit(XastirDisplayQuantity quantity, int english);
int xastir_station_list_contains(const DataRow *row, int type);
/* Name order, or newest first for LAST. skip advances before matching; forward
 * selects the normal direction. Returned rows remain owned by db. */
const DataRow *xastir_station_list_member(const XastirStationDb *db, const DataRow *row,
                                         int type, int skip, int forward);

int xastir_station_visible(const DataRow *row, const Selections *selection, time_t now,
                          int clear_seconds, int direct_seconds, int aircraft_seconds);
void xastir_compute_DR_position(long lon, long lat, double nautical_miles, double course, long *out_lon, long *out_lat);
double xastir_phg_range(char power, char height, char gain);
double xastir_shg_range(char signal, char height, char gain);
double xastir_distance_radians(double lat1, double lon1, double lat2, double lon2);
double xastir_distance(double lat1, double lon1, double lat2, double lon2, double *bearing);
void xastir_destination(double lat, double lon, double bearing, double meters, double *out_lat, double *out_lon);
double xastir_aloha_radius(const XastirStationDb *db, double lat, double lon, aloha_stats *stats);
int xastir_is_weather_data(char *data, int len);
int xastir_extract_weather_item(char *data, char type, int length, char *out);
int xastir_test_extract_weather_item(char *data, char type, int length);
int xastir_extract_weather(DataRow *row, char *info, int compressed, time_t now);
int xastir_extract_bearing_NRQ(char *info, char *bearing, char *nrq);
int xastir_extract_powergain_range(char *info, char *phg);
int xastir_extract_omnidf(char *info, char *shg);
#ifdef __cplusplus
}
#endif
#endif
