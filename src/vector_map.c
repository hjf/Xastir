// SPDX-License-Identifier: GPL-2.0-or-later
#include "vector_map.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef XASTIR_HAVE_SHAPELIB
#include <shapefil.h>
#endif
#define MAX_POINTS 1000000u
#define MAX_FEATURES 20000u
static int fail(char *error, size_t capacity, const char *message)
{
  if (error && capacity) snprintf(error, capacity, "%s", message);
  return 0;
}
static uint32_t be32(const unsigned char *p)
{
  return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3];
}
static int coordinate(uint32_t lon, uint32_t lat, unsigned scale, XastirMapPoint *point)
{
  const uint64_t x = (uint64_t)lon * scale, y = (uint64_t)lat * scale;
  if (x > 129600000 || y > 64800000) return 0;
  point->longitude = x / 360000.0 - 180;
  point->latitude = 90 - y / 360000.0;
  return 1;
}
int xastir_read_binary_map(const unsigned char *data, size_t size,
    XastirMapVisitor visitor, void *context, char *error, size_t capacity)
{
  /* draw_dos_map: uncompressed Windows-type maps have a 256-byte header,
   * big-endian counts and ten-byte vector records. Versions before 2.00 use
   * deciseconds, 2.00 uses Xastir centiseconds. WU2Z/Beta is worldhi.map. */
  if (!data || !visitor || size < 256 || memcmp(data, "WU2Z", 4))
    return fail(error, capacity, "Unsupported binary map (expected WU2Z uncompressed format)");
  if (memcmp(data + 4, "Beta", 4) && memcmp(data + 4, "1.00", 4) && memcmp(data + 4, "2.00", 4))
    return fail(error, capacity, "Unsupported binary map version");
  const unsigned scale = !memcmp(data + 4, "2.00", 4) ? 1 : 10;
  const uint32_t count = be32(data + 108), labels = be32(data + 112);
  if (count > MAX_POINTS || labels > MAX_FEATURES || count > (size - 256) / 10 ||
      labels > (size - 256 - (size_t)count * 10) / 44)
    return fail(error, capacity, "Truncated or oversized binary map");
  XastirMapPoint *points = malloc(((size_t)count + 1) * sizeof(*points));
  if (!points) return fail(error, capacity, "Cannot allocate binary map geometry");
  int part = 0, color = 255, behavior = 0;
  size_t used = 0;
  for (uint32_t i = 0; i <= count; ++i) {
    const unsigned char *record = i < count ? data + 256 + (size_t)i * 10 : NULL;
    const int next = record ? record[0] : 255;
    if (next != color || next == 255) {
      if (color != 255 && used >= 2) {
        XastirMapFeature feature = {points, used, &part, 1, !!(behavior & 128), color,
          behavior & 128 ? 96 + ((behavior & 127) <= 9 ? (behavior & 127) : 0) : -1,
          behavior & 1 ? 2 : 1, "", 0, 0};
        if (!visitor(context, &feature)) { free(points); return fail(error, capacity, "Map load cancelled"); }
      }
      if (color != 255 || next == 255) used = 0;
      color = next;
    }
    if (record) {
      if (!coordinate(be32(record+2), be32(record+6), scale, &points[used])) {
        free(points); return fail(error, capacity, "Invalid binary map coordinates");
      }
      ++used; behavior = record[1];
    }
  }
  free(points);
  for (uint32_t i = 0; i < labels; ++i) {
    const unsigned char *record = data + 256 + (size_t)count * 10 + (size_t)i * 44;
    if (record[1] != 0) return fail(error, capacity, "Embedded map symbols are not supported yet");
    XastirMapPoint point;
    if (!coordinate(be32(record+2), be32(record+6), scale, &point))
      return fail(error, capacity, "Invalid binary map label coordinates");
    char label[33]; memcpy(label, record+12, 32); label[32] = 0;
    XastirMapFeature feature = {&point, 1, &part, 1, 0, record[0]&127, -1, 1,
      label, point.longitude, point.latitude};
    if (!visitor(context, &feature)) return fail(error, capacity, "Map load cancelled");
  }
  return 1;
}
int xastir_has_shapefile_support(void)
{
#ifdef XASTIR_HAVE_SHAPELIB
  return 1;
#else
  return 0;
#endif
}
int xastir_read_shapefile(const char *path, XastirMapVisitor visitor,
    void *context, char *error, size_t capacity)
{
#ifdef XASTIR_HAVE_SHAPELIB
  SHPHandle map = SHPOpen(path, "rb");
  if (!map) return fail(error, capacity, "Cannot open shapefile");
  int count, type; SHPGetInfo(map, &count, &type, NULL, NULL);
  if (!visitor || count < 0 || count > (int)MAX_FEATURES || (type != SHPT_ARC && type != SHPT_POLYGON)) {
    SHPClose(map); return fail(error, capacity, "Expected a bounded WGS84 polyline or polygon shapefile");
  }
  DBFHandle dbf = DBFOpen(path, "rb");
  int name = dbf ? DBFGetFieldIndex(dbf, "NAME_LONG") : -1;
  if (dbf && name < 0) name = DBFGetFieldIndex(dbf, "name");
  const int label_x = dbf ? DBFGetFieldIndex(dbf, "LABEL_X") : -1;
  const int label_y = dbf ? DBFGetFieldIndex(dbf, "LABEL_Y") : -1;
  size_t total = 0;
  int ok = 1;
  for (int i = 0; i < count && ok; ++i) {
    SHPObject *object = SHPReadObject(map, i);
    if (!object) { ok = fail(error, capacity, "Cannot read shapefile feature"); break; }
    if (object->nSHPType == SHPT_NULL) { SHPDestroyObject(object); continue; }
    const int vertices = object->nVertices, parts = object->nParts;
    if (vertices < 0 || parts <= 0 || parts > vertices || (size_t)vertices > MAX_POINTS - total ||
        object->nSHPType != type || object->panPartStart[0] != 0) {
      SHPDestroyObject(object); ok = fail(error, capacity, "Invalid or oversized shapefile geometry"); break;
    }
    total += vertices;
    XastirMapPoint *points = malloc((size_t)vertices * sizeof(*points));
    if (!points) { SHPDestroyObject(object); ok = fail(error, capacity, "Cannot allocate shapefile geometry"); break; }
    for (int p = 0; p < parts; ++p)
      if (object->panPartStart[p] < 0 || object->panPartStart[p] >= vertices ||
          (p && object->panPartStart[p] <= object->panPartStart[p-1])) ok = 0;
    for (int p = 0; p < vertices; ++p) {
      const double lon = object->padfX[p], lat = object->padfY[p];
      if (!isfinite(lon) || !isfinite(lat) || fabs(lon) > 180 || fabs(lat) > 90) ok = 0;
      points[p].longitude = lon; points[p].latitude = lat;
    }
    if (ok) {
      const int row = dbf && i < DBFGetRecordCount(dbf);
      const char *label = row && name >= 0 ? DBFReadStringAttribute(dbf, i, name) : "";
      /* Copy DBF text: later numeric attribute reads reuse Shapelib's buffer. */
      char text[256]; snprintf(text, sizeof(text), "%s", label ? label : "");
      double lon = (object->dfXMin + object->dfXMax) / 2;
      double lat = (object->dfYMin + object->dfYMax) / 2;
      if (row && label_x >= 0 && label_y >= 0) {
        const double x = DBFReadDoubleAttribute(dbf, i, label_x), y = DBFReadDoubleAttribute(dbf, i, label_y);
        if (isfinite(x) && isfinite(y) && fabs(x) <= 180 && fabs(y) <= 90) { lon = x; lat = y; }
      }
      XastirMapFeature feature = {points, (size_t)vertices, object->panPartStart, parts,
        type == SHPT_POLYGON, 8, -1, 1, text, lon, lat};
      ok = visitor(context, &feature);
      if (!ok) fail(error, capacity, "Map load cancelled");
    } else fail(error, capacity, "Invalid shapefile parts or geographic coordinates");
    free(points); SHPDestroyObject(object);
  }
  if (dbf) DBFClose(dbf);
  SHPClose(map); return ok;
#else
  (void)path; (void)visitor; (void)context;
  return fail(error, capacity, "This build has no Shapelib support; use the bundled world map");
#endif
}
