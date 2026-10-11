// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_VECTOR_MAP_H
#define XASTIR_VECTOR_MAP_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { double longitude, latitude; } XastirMapPoint;
/* Borrowed geometry, valid for the callback only. Rings belong to one feature:
 * keep them together when filling polygons so interior rings remain holes. */
typedef struct {
  const XastirMapPoint *points;
  size_t point_count;
  const int *parts;
  int part_count, polygon, color, fill_color, line_width;
  const char *label;
  double label_longitude, label_latitude;
} XastirMapFeature;
typedef int (*XastirMapVisitor)(void *context, const XastirMapFeature *feature);
/* UI-independent extraction from map_dos.c's binary header/coordinate decoding
 * and map_shp.c's Shapelib feature traversal. Limits apply before allocation. */
int xastir_read_binary_map(const unsigned char *data, size_t size,
    XastirMapVisitor visitor, void *context, char *error, size_t capacity);
int xastir_read_shapefile(const char *path, XastirMapVisitor visitor,
    void *context, char *error, size_t capacity);
int xastir_has_shapefile_support(void);
#ifdef __cplusplus
}
#endif
#endif
