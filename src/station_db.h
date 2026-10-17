// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_STATION_DB_H
#define XASTIR_STATION_DB_H
#include "database.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Each frontend owns an independent instance. All mutations are single-threaded.
 * Rows and all optional legacy weather/trail/comment structures belong to it. */
typedef struct XastirStationDb {
  DataRow *first, *last, *oldest, *newest;
  DataRow *shortcuts[16384];
  int count, tracked, next_serial;
  time_t now;
  int segment_distance, segment_minutes, skip_duplicates, non_ascii_names;
} XastirStationDb;
typedef struct {
  const char *name, *source;
  double latitude, longitude, course;
  int kind; /* 0 station, 1 object, 2 item */
  int course_valid, emergency;
  char symbol_table, symbol_code;
  const char *speed, *altitude, *comment;
  unsigned char ambiguity;
  int local;
} XastirStationReport;
/* Validated geographic import into the original DataRow storage. */
int xastir_station_update(XastirStationDb *db, const XastirStationReport *report, time_t now);
/* Remove only a matching entity kind; an object kill must not delete a station. */
int xastir_station_remove(XastirStationDb *db, const char *name, int kind);
void xastir_station_db_init(XastirStationDb *db, time_t now);
DataRow *xastir_station_upsert(XastirStationDb *db, const char *name, time_t now);
void xastir_station_touch(XastirStationDb *db, DataRow *row, time_t now);
void xastir_station_expire(XastirStationDb *db, time_t before);
void xastir_init_station(XastirStationDb *db, DataRow *p_station);
void xastir_remove_name(XastirStationDb *db, DataRow *p_rem);
void xastir_remove_time(XastirStationDb *db, DataRow *p_rem);
void xastir_insert_name(XastirStationDb *db, DataRow *p_new, DataRow *p_name);
void xastir_insert_time(XastirStationDb *db, DataRow *p_new, DataRow *p_time);
void xastir_delete_station_memory(XastirStationDb *db, DataRow *p_del);
DataRow *xastir_insert_new_station(XastirStationDb *db, DataRow *p_name, DataRow *p_time);
DataRow *xastir_add_new_station(XastirStationDb *db, DataRow *p_name, DataRow *p_time, char *call);
void xastir_move_station_time(XastirStationDb *db, DataRow *p_curr, DataRow *p_time);
void xastir_move_station_name(XastirStationDb *db, DataRow *p_curr, DataRow *p_name);
void xastir_station_shortcuts_update_function(XastirStationDb *db, int hash_key_in, DataRow *p_rem);
int xastir_search_station_name(XastirStationDb *db, DataRow **p_name, char *call, int exact);
int xastir_search_station_time(XastirStationDb *db, DataRow **p_time, time_t heard, int serial);
int xastir_delete_trail(XastirStationDb *db, DataRow *fill);
int xastir_store_trail_point(XastirStationDb *db, DataRow *p_station,
                      long lon,
                      long lat,
                      time_t sec,
                      char *alt,
                      char *speed,
                      char *course,
                      short stn_flag);
void xastir_expire_trail_points(XastirStationDb *db, DataRow *p_station, time_t sec);
int xastir_is_trailpoint_echo(XastirStationDb *db, DataRow *p_station);
void xastir_add_status(XastirStationDb *db, DataRow *p_station, char *status_string);
void xastir_add_comment(XastirStationDb *db, DataRow *p_station, char *comment_string);
void xastir_station_del(XastirStationDb *db, char *call);
void xastir_station_del_ptr(XastirStationDb *db, DataRow *p_name);
void xastir_delete_all_stations(XastirStationDb *db);
int xastir_next_station_name(XastirStationDb *db, DataRow **p_curr);
int xastir_prev_station_name(XastirStationDb *db, DataRow **p_curr);
int xastir_next_station_time(XastirStationDb *db, DataRow **p_curr);
int xastir_prev_station_time(XastirStationDb *db, DataRow **p_curr);
void xastir_init_weather(WeatherRow *weather);
int xastir_get_weather_record(DataRow *fill);
int xastir_delete_weather(DataRow *fill);
int xastir_delete_multipoints(DataRow *fill);
int xastir_delete_comments_and_status(DataRow *fill);
#ifdef __cplusplus
}
#endif
#endif
