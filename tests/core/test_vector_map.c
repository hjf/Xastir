// SPDX-License-Identifier: GPL-2.0-or-later
#include "vector_map.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int features, points, parts, polygon;
static int visit(void *context, const XastirMapFeature *feature)
{
  (void)context; ++features; points += feature->point_count;
  parts = feature->part_count; polygon = feature->polygon;
  for (size_t i=0; i<feature->point_count; ++i) {
    assert(isfinite(feature->points[i].latitude)); assert(fabs(feature->points[i].latitude)<=90);
    assert(isfinite(feature->points[i].longitude)); assert(fabs(feature->points[i].longitude)<=180);
  }
  return 1;
}
static int cancel(void *context, const XastirMapFeature *feature) { (void)context; (void)feature; return 0; }
static void be32(unsigned char *p, uint32_t value) { p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value; }
int main(void)
{
  char error[256];
  FILE *file=fopen(VECTOR_DATA_DIR "/worldhi.map","rb"); assert(file);
  assert(!fseek(file,0,SEEK_END)); long size=ftell(file); rewind(file);
  unsigned char *data=malloc(size); assert(data); assert(fread(data,1,size,file)==(size_t)size); fclose(file);
  assert(xastir_read_binary_map(data,size,visit,NULL,error,sizeof(error)));
  assert(features>1000); assert(points>25000);
  assert(!xastir_read_binary_map(data,size-1,visit,NULL,error,sizeof(error)));
  assert(!xastir_read_binary_map(data,size,cancel,NULL,error,sizeof(error)));
  be32(data+108,UINT32_MAX); assert(!xastir_read_binary_map(data,size,visit,NULL,error,sizeof(error))); free(data);
  unsigned char fixture[296]={0}; memcpy(fixture,"WU2Z2.00",8); be32(fixture+108,4);
  for(int i=0;i<4;++i) { fixture[256+i*10]=i==0?255:9; be32(fixture+258+i*10,64800000+i*360000); be32(fixture+262+i*10,32400000); }
  features=points=0; assert(xastir_read_binary_map(fixture,sizeof(fixture),visit,NULL,error,sizeof(error))); assert(features==1);assert(points==4);
  be32(fixture+262,UINT32_MAX); assert(!xastir_read_binary_map(fixture,sizeof(fixture),visit,NULL,error,sizeof(error)));
  if (xastir_has_shapefile_support()) {
    features=points=0; assert(xastir_read_shapefile(VECTOR_DATA_DIR "/tests/core/fixtures/multipart_line.shp",visit,NULL,error,sizeof(error)));
    assert(features==1 && points==4 && parts==2 && !polygon);
    features=points=0; assert(xastir_read_shapefile(VECTOR_DATA_DIR "/tests/core/fixtures/polygon_hole.shp",visit,NULL,error,sizeof(error)));
    assert(features==1 && points==10 && parts==2 && polygon);
    assert(!xastir_read_shapefile(VECTOR_DATA_DIR "/tests/core/fixtures/missing.shp",visit,NULL,error,sizeof(error)));
    features=points=0; assert(xastir_read_shapefile(VECTOR_DATA_DIR "/NaturalEarthVector/ne_50m_admin_0_countries_lakes.shp",visit,NULL,error,sizeof(error)));
    assert(features==242 && points==102053);
  }
  puts("vector map fixtures passed");return 0;
}
