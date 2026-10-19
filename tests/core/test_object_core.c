// SPDX-License-Identifier: GPL-2.0-or-later
#include "object_core.h"
#include "aprs_position.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
  char packet[256];
  CHECK(xastir_build_object_item(packet, sizeof(packet), "Aid Point", 1, 1, 49+3.5/60, -(72+1.75/60), '/', 'h', "Open", 9, 23, 45));
  CHECK(!strcmp(packet, ";Aid Point*092345z4903.50N/07201.75WhOpen"));
  CHECK(xastir_build_object_item(packet, sizeof(packet), "Aid Point", 1, 0, 49+3.5/60, -(72+1.75/60), '/', 'h', "Closed", 10, 0, 1));
  CHECK(!strcmp(packet, ";Aid Point_100001z4903.50N/07201.75WhClosed"));
  CHECK(xastir_build_object_item(packet, sizeof(packet), "Tent", 0, 1, -27.5, -59.25, '\\', '-', "", 0, 0, 0));
  CHECK(!strcmp(packet, ")Tent!2730.00S\\05915.00W-"));
  CHECK(xastir_build_object_item(packet, sizeof(packet), "Tent", 0, 0, -27.5, -59.25, '\\', '-', "", 0, 0, 0));
  CHECK(!strcmp(packet, ")Tent_2730.00S\\05915.00W-"));
  char comment[45]; memset(comment, 'a', 43); comment[43] = 0;
  CHECK(xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, 0, 0, 'A', '-', comment, 31, 23, 59)); CHECK(strlen(packet) == 80);
  CHECK(xastir_build_object_item(packet, sizeof(packet), "Item", 0, 1, 0, 0, '/', '-', comment, 0, 0, 0)); CHECK(strlen(packet) == 68);
  comment[43] = 'a'; comment[44] = 0;
  CHECK(!xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, 0, 0, '/', '-', comment, 1, 0, 0));
  CHECK(!xastir_build_object_item(packet, 10, "Obj", 1, 1, 0, 0, '/', '-', "", 1, 0, 0) && !packet[0]);
  CHECK(!xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, NAN, 0, '/', '-', "", 1, 0, 0));
  CHECK(!xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, 0, 181, '/', '-', "", 1, 0, 0));
  CHECK(!xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, 0, 0, '?', '-', "", 1, 0, 0));
  CHECK(!xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, 0, 0, '/', '-', "bad\ntext", 1, 0, 0));
  CHECK(!xastir_build_object_item(packet, sizeof(packet), "Obj", 1, 1, 0, 0, '/', '-', "", 0, 0, 0));
  CHECK(!xastir_object_valid_name("AB", 0) && !xastir_object_valid_name("A!B", 0) && !xastir_object_valid_name("A_B", 0));
  CHECK(xastir_object_valid_name("A!B", 1) && xastir_object_valid_name("Aid Point", 1));
  CHECK(!xastir_object_valid_name("0123456789", 1) && !xastir_object_valid_name("\xff", 1));
  XastirObjectSchedule schedule = {0}; CHECK(!xastir_object_schedule_due(&schedule, 100000));
  xastir_object_schedule_start(&schedule, 1000, 1800, 0);
  CHECK(schedule.next_ms == 21000 && !xastir_object_schedule_due(&schedule, 20999));
  CHECK(xastir_object_schedule_due(&schedule, 21000));
  xastir_object_schedule_accepted(&schedule, 21000, 1800, 0); CHECK(schedule.interval == 40 && schedule.next_ms == 61000);
  xastir_object_schedule_accepted(&schedule, 61000, 1800, 1000); CHECK(schedule.interval == 64 && schedule.remaining == -1);
  for (int i = 0; i < 20; ++i) xastir_object_schedule_accepted(&schedule, schedule.next_ms, 1800, 0);
  CHECK(schedule.interval == 1800);
  xastir_object_schedule_start(&schedule, 1000, 60, 1);
  for (int i = 0; i < 19; ++i) { CHECK(schedule.remaining > 0); xastir_object_schedule_accepted(&schedule, schedule.next_ms, 60, 0); }
  CHECK(schedule.remaining == 1); xastir_object_schedule_accepted(&schedule, schedule.next_ms, 60, 0);
  CHECK(!schedule.remaining && !xastir_object_schedule_due(&schedule, 99999999));
  return 0;
}
