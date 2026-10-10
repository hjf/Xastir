// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprs_position.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
  char expanded[513], data[32] = "(_fn\"Oj/";
  int emergency;
  CHECK(xastir_expand_mice("490SUP", data, expanded, sizeof(expanded), &emergency));
  CHECK(strncmp(expanded, "4903.50N/11207.74Wj251/", 22) == 0);
  CHECK(strstr(expanded, "Mic-E") != NULL);
  CHECK(emergency);
  /* Destination flags and latitude digits; invalid path must never be decoded. */
  CHECK(!xastir_expand_mice("490!UP", data, expanded, sizeof(expanded), &emergency));
  CHECK(!xastir_expand_mice("49", data, expanded, sizeof(expanded), &emergency));
  for (int i = 0; i < 8; ++i) {
    char truncated[9] = "(_fn\"Oj/"; truncated[i] = 0;
    CHECK(!xastir_expand_mice("490SUP", truncated, expanded, sizeof(expanded), &emergency));
  }
  CHECK(!xastir_expand_mice("490SUP", data, expanded, 10, &emergency));
  data[2] = (char)0xff;
  CHECK(!xastir_expand_mice("490SUP", data, expanded, sizeof(expanded), &emergency));
  strcpy(data, "(_fn\"Oj/>");
  CHECK(xastir_expand_mice("490SUP", data, expanded, sizeof(expanded), &emergency));
  CHECK(strstr(expanded, "TH-D7") != NULL);
  /* No message bits in the first three destination chars means emergency. */
  CHECK(xastir_expand_mice("490SUP", "(_fn\"Oj/", expanded, sizeof(expanded), &emergency));
  /* Test all destinations against a fixed body, including all ambiguity levels. */
  for (const char **p = (const char *[]){"490SUP", "490SUZ", "490SZZ", "490ZZZ", "49ZZZZ", NULL}; *p; ++p) {
    CHECK(xastir_expand_mice(*p, "(_fn\"Oj/", expanded, sizeof(expanded), &emergency));
    XastirPosition position = {0}; char *comment = expanded;
    CHECK(xastir_extract_position(&position, &comment, 0));
    CHECK(position.coord_lat >= 0 && position.coord_lat <= 64800000);
  }
  return 0;
}
