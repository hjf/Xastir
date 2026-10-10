// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprs_position.h"
#include <stdio.h>
#include <string.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
int main(void)
{
  char beacon[128];
  CHECK(xastir_build_position_beacon(49+3.5/60,-(72+1.75/60),'/', '>',"hi",beacon,sizeof(beacon)));
  CHECK(strcmp(beacon,"=4903.50N/07201.75W>hi")==0);
  CHECK(xastir_build_position_beacon(-34.999999,179.999999,'/', '-',"",beacon,sizeof(beacon)));
  CHECK(strcmp(beacon,"=3500.00S/18000.00E-")==0);
  CHECK(!xastir_build_position_beacon(91,0,'/','-',"",beacon,sizeof(beacon)));
  CHECK(!xastir_build_position_beacon(0,0,'/','-',"bad\nline",beacon,sizeof(beacon)));
  CHECK(!xastir_build_position_beacon(0,0,'/','-',"",beacon,10));
  CHECK(xastir_format_fixed_position(beacon,sizeof(beacon),'!',"4903.50N/07201.75W>","","","12345678901234567890123456789012345678901234567890",0));
  CHECK(strlen(beacon)==63);
  XastirPosition result = {0};
  char text[128] = "4903.50N/07201.75W>hello!W12!";
  char *comment = text;
  CHECK(xastir_extract_position(&result, &comment, 0) == 2);
  CHECK(result.coord_lat == 14738994); /* 49 degrees 3.501 minutes north */
  CHECK(result.coord_lon == 38869488); /* 72 degrees 1.752 minutes west */
  CHECK(strcmp(comment, "hello") == 0);
  CHECK(result.aprs_symbol.aprs_symbol == '>');
  strcpy(text, "4903.  N107201.  W-"); comment = text;
  CHECK(xastir_extract_position(&result, &comment, 0));
  CHECK(result.pos_amb == 2);
  CHECK(result.aprs_symbol.special_overlay == '1');
  strcpy(text, "FN20ab]"); comment = text;
  CHECK(xastir_extract_position(&result, &comment, 1));
  CHECK(result.pos_amb == 5);
  CHECK(result.aprs_symbol.aprs_symbol == 'G');
  /* Every truncated position must fail without reading past its terminator. */
  for (int length = 0; length < 19; ++length) {
    strcpy(text, "4903.50N/07201.75W>"); text[length] = 0; comment = text;
    CHECK(!xastir_extract_position(&result, &comment, 0));
    CHECK(comment == text);
  }
  for (int length = 0; length < 13; ++length) {
    strcpy(text, "/5L!!<*e7>7P["); text[length] = 0; comment = text;
    CHECK(!xastir_extract_comp_position(&result, &comment));
  }
  memset(&result, 0, sizeof(result));
  strcpy(text, "/5L!!<*e7>7P["); comment = text;
  CHECK(xastir_extract_comp_position(&result, &comment));
  CHECK(comment == text + 13);
  CHECK(strcmp(result.course, "088") == 0);
  CHECK(result.speed[0] != 0);
  xastir_overlay_symbol('>', 'c', &result);
  CHECK(result.aprs_symbol.aprs_type == '\\' && result.aprs_symbol.special_overlay == '2');
  strcpy(text, "F"); comment = text;
  CHECK(!xastir_extract_position(&result, &comment, 1));
  strcpy(text, "4960.00N/07201.75W>"); comment = text;
  CHECK(!xastir_extract_position(&result, &comment, 0));
  strcpy(text, "4903. 0N/07201.  W>"); comment = text;
  CHECK(!xastir_extract_position(&result, &comment, 0));
  strcpy(text, "9000.01N/07201.75W>"); comment = text;
  CHECK(!xastir_extract_position(&result, &comment, 0));
  return 0;
}
