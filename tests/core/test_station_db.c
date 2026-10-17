// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int consistent(const XastirStationDb *db)
{
  int names = 0, times = 0; const DataRow *previous = NULL;
  for (const DataRow *row = db->first; row; row = row->n_next) {
    if (row->n_prev != previous || (previous && strcmp(previous->call_sign, row->call_sign) >= 0)) return 0;
    previous = row; ++names;
  }
  if (previous != db->last) return 0;
  previous = NULL;
  for (const DataRow *row = db->oldest; row; row = row->t_newer) {
    if (row->t_older != previous || (previous && previous->sec_heard > row->sec_heard)) return 0;
    previous = row; ++times;
  }
  return previous == db->newest && names == times && names == db->count;
}
int main(void)
{
  XastirStationDb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
  CHECK(a && b); xastir_station_db_init(a, 100); xastir_station_db_init(b, 100);
  for (int i = 0; i < 1000; ++i) {
    char name[10]; snprintf(name, sizeof(name), "T%04d", (i*337)%1000);
    CHECK(xastir_station_upsert(a, name, 100+i));
    CHECK(consistent(a));
  }
  CHECK(b->count == 0);
  CHECK(xastir_station_status(b, "STATUS", "171234z  Ready  ", 90));
  DataRow *status = NULL;
  CHECK(xastir_search_station_name(b, &status, "STATUS", 1));
  CHECK(!status->position_valid && (status->flag & ST_STATUS));
  CHECK(strcmp(status->status_data->text_ptr, "Ready") == 0);
  CHECK(status->status_data->sec_heard == 90);
  CHECK(xastir_station_status(b, "STATUS", "ready", 91));
  CHECK(status->status_data->next == NULL && status->status_data->sec_heard == 91);
  for (int i = 0; i < 30; ++i) {
    char text[32]; snprintf(text, sizeof(text), "Status %d", i);
    CHECK(xastir_station_status(b, "STATUS", text, 100+i));
  }
  int statuses = 0;
  for (CommentRow *s = status->status_data; s; s = s->next) ++statuses;
  CHECK(statuses == MAX_STATUS_LINES);
  CHECK(xastir_station_status(b, "STATUS", "Status 20", 140));
  CHECK(strcmp(status->status_data->text_ptr, "Status 20") == 0);
  CHECK(status->status_data->sec_heard == 140);
  statuses = 0;
  for (CommentRow *s = status->status_data; s; s = s->next) ++statuses;
  CHECK(statuses == MAX_STATUS_LINES);
  char oversized[514]; memset(oversized, 'x', sizeof(oversized)); oversized[513] = 0;
  CHECK(!xastir_station_status(b, "STATUS", oversized, 150));
  CHECK(!xastir_station_status(b, "STATUS", "bad\nline", 150));
  CHECK(!xastir_station_status(b, "BAD NAME", "Ready", 150));
  CHECK(status->sec_heard == 140 && consistent(b));
  DataRow *row = NULL;
  CHECK(xastir_search_station_name(a, &row, "T0001", 1));
  CHECK(xastir_get_weather_record(row));
  row->multipoint_data = calloc(1, sizeof(MultipointRow)); CHECK(row->multipoint_data);
  for (int i = 0; i < 30; ++i) {
    char text[32]; snprintf(text, sizeof(text), "comment %d", i);
    xastir_add_comment(a, row, text); xastir_add_status(a, row, text);
  }
  int count = 0; for (CommentRow *p = row->comment_data; p; p = p->next) ++count;
  CHECK(count == MAX_COMMENT_LINES);
  char duplicate[] = "COMMENT 20"; xastir_add_comment(a, row, duplicate);
  CHECK(strcmp(row->comment_data->text_ptr, duplicate) == 0);
  CHECK(xastir_store_trail_point(a, row, 100, 200, 100, "", "", "", 0));
  CHECK(xastir_store_trail_point(a, row, 110, 210, 200, "12", "10", "090", 0));
  CHECK(row->newest_trackpoint->course == 90);
  xastir_station_del_ptr(a, row); CHECK(consistent(a)); CHECK(a->count == 999);
  XastirStationReport report = {0};
  report.name = "Park"; report.source = "OWNER"; report.latitude = -27.44; report.longitude = -58.99;
  report.symbol_table = 'A'; report.symbol_code = '#'; report.kind = 1;
  report.course_valid = 1; report.course = 45.25; report.comment = "hi";
  CHECK(xastir_station_update(b, &report, 100));
  CHECK(xastir_search_station_name(b, &row, "Park", 1));
  CHECK(row->flag & ST_OBJECT); CHECK(row->heading == 45.25);
  CHECK(strcmp(row->origin, "OWNER") == 0);
  CHECK(!xastir_station_status(b, "Park", "Wrong kind", 110));
  CHECK(row->sec_heard == 100 && !row->status_data);
  report.longitude = -58.90; CHECK(xastir_station_update(b, &report, 200));
  CHECK(row->oldest_trackpoint && row->oldest_trackpoint->sec == 100);
  CHECK(row->newest_trackpoint->sec == 200);
  CHECK(row->num_packets == 2);
  report.latitude = 91; CHECK(!xastir_station_update(b, &report, 300)); CHECK(row->num_packets == 2);
  report.latitude = -27.44; report.kind = 2; CHECK(xastir_station_update(b, &report, 300));
  CHECK((row->flag & ST_ITEM) && !(row->flag & ST_OBJECT));
  CHECK(!xastir_station_remove(b, "Park", 0));
  CHECK(!xastir_station_remove(b, "Park", 1));
  CHECK(xastir_station_remove(b, "Park", 2));
  CHECK(!xastir_station_remove(b, "Park", 2));
  CHECK(consistent(b));
  CHECK(xastir_station_upsert(b, "\xc3\x81rea", 100));
  CHECK(xastir_station_upsert(b, "ALPHA", 100));
  CHECK(xastir_station_upsert(b, "BETA", 100));
  CHECK(consistent(b)); CHECK(xastir_search_station_name(b, &row, "ALPHA", 1));
  row->flag |= ST_MYSTATION;
  CHECK(!xastir_station_remove(b, "ALPHA", 1));
  CHECK(!xastir_station_remove(b, "ALPHA", 2));
  double local_latitude = row->latitude;
  report.name = "ALPHA"; report.kind = 0; report.latitude = 40; report.longitude = 20;
  CHECK(xastir_station_update(b, &report, 200));
  CHECK(row->latitude == local_latitude);
  xastir_station_expire(b, 400); CHECK(b->count == 1); CHECK(consistent(b));
  xastir_delete_all_stations(a); xastir_delete_all_stations(b);
  CHECK(a->count == 0 && a->first == NULL && a->newest == NULL && a->tracked == 0);
  CHECK(b->count == 0 && b->tracked == 0);
  free(a); free(b); return 0;
}
