// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;} } while(0)
static int list_tests(void)
{
  DataRow row = {0}; WeatherRow weather = {0}; TrackRow point = {0};
  CHECK(!xastir_station_list_contains(NULL, XASTIR_LIST_ALL));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_ALL));
  row.flag = ST_ACTIVE | ST_MOVING;
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_ALL));
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_LAST));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_MOBILE));
  row.newest_trackpoint = &point;
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_MOBILE));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_WEATHER));
  row.weather_data = &weather;
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_WEATHER));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_LOCAL));
  row.flag |= ST_VIATNC;
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_LOCAL));
  row.data_via = 'I'; /* An Internet update does not erase prior TNC reception. */
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_LOCAL));
  row.flag |= ST_MYOBJITEM;
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_OWN_OBJECTS));
  row.flag = ST_ITEM | ST_MYOBJITEM;
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_OBJECTS));
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_OWN_OBJECTS));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_ALL));
  CHECK(!xastir_station_list_contains(&row, -1));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_COUNT));
  row.flag = ST_OBJECT;
  CHECK(xastir_station_list_contains(&row, XASTIR_LIST_OBJECTS));
  CHECK(!xastir_station_list_contains(&row, XASTIR_LIST_OWN_OBJECTS));

  XastirStationDb *db = calloc(1, sizeof(*db)); CHECK(db); xastir_station_db_init(db, 1000);
  DataRow *a = xastir_station_upsert(db, "A", 200);
  DataRow *b = xastir_station_upsert(db, "B", 100);
  DataRow *c = xastir_station_upsert(db, "C", 200);
  DataRow *d = xastir_station_upsert(db, "D", 300);
  CHECK(a && b && c && d);
  a->flag = ST_ACTIVE; b->flag = ST_OBJECT;
  c->flag = ST_ACTIVE | ST_VIATNC; d->flag = ST_ITEM | ST_MYOBJITEM;
  CHECK(xastir_station_list_member(db, NULL, XASTIR_LIST_ALL, 0, 1) == a);
  CHECK(xastir_station_list_member(db, a, XASTIR_LIST_ALL, 1, 1) == c);
  CHECK(xastir_station_list_member(db, c, XASTIR_LIST_ALL, 1, 0) == a);
  CHECK(!xastir_station_list_member(db, c, XASTIR_LIST_ALL, 1, 1));
  CHECK(xastir_station_list_member(db, NULL, XASTIR_LIST_LAST, 0, 1) == c);
  CHECK(xastir_station_list_member(db, c, XASTIR_LIST_LAST, 1, 1) == a);
  CHECK(xastir_station_list_member(db, a, XASTIR_LIST_LAST, 1, 0) == c);
  CHECK(xastir_station_list_member(db, NULL, XASTIR_LIST_OBJECTS, 0, 1) == b);
  CHECK(xastir_station_list_member(db, b, XASTIR_LIST_OBJECTS, 1, 1) == d);
  /* Reverse object traversal must not accidentally include regular TNC rows. */
  CHECK(xastir_station_list_member(db, d, XASTIR_LIST_OBJECTS, 1, 0) == b);
  CHECK(xastir_station_list_member(db, NULL, XASTIR_LIST_OWN_OBJECTS, 0, 1) == d);
  CHECK(!xastir_station_list_member(db, NULL, XASTIR_LIST_COUNT, 0, 1));
  xastir_delete_all_stations(db); free(db);
  return 0;
}
int main(void)
{
  CHECK(fabs(xastir_display_value(10, XASTIR_DISPLAY_SPEED, 0) - 18.52) < 1e-9);
  CHECK(fabs(xastir_display_value(10, XASTIR_DISPLAY_SPEED, 1) - 11.508) < 1e-9);
  CHECK(fabs(xastir_display_value(1000, XASTIR_DISPLAY_DISTANCE, 0) - 1) < 1e-9);
  CHECK(fabs(xastir_display_value(1609.344, XASTIR_DISPLAY_DISTANCE, 1) - 1) < 1e-9);
  CHECK(fabs(xastir_display_value(0.9144, XASTIR_DISPLAY_SHORT_DISTANCE, 1) - 1) < 1e-9);
  CHECK(fabs(xastir_display_value(100, XASTIR_DISPLAY_ALTITUDE, 1) - 328.084) < 1e-9);
  CHECK(fabs(xastir_display_value(10, XASTIR_DISPLAY_WIND, 0) - 16.09344) < 1e-9);
  CHECK(fabs(xastir_display_value(-40, XASTIR_DISPLAY_TEMPERATURE, 0) + 40) < 1e-9);
  CHECK(fabs(xastir_display_value(50, XASTIR_DISPLAY_TEMPERATURE, 0) - 10) < 1e-9);
  CHECK(fabs(xastir_display_value(50, XASTIR_DISPLAY_TEMPERATURE, 1) - 50) < 1e-9);
  CHECK(fabs(xastir_display_value(1000, XASTIR_DISPLAY_PRESSURE, 1) - 29.53) < 1e-9);
  CHECK(fabs(xastir_display_value(12, XASTIR_DISPLAY_RAIN, 0) - 3.048) < 1e-9);
  CHECK(fabs(xastir_display_value(12, XASTIR_DISPLAY_RAIN, 1) - .12) < 1e-9);
  CHECK(!strcmp(xastir_display_unit(XASTIR_DISPLAY_SPEED, 1), "mph"));
  CHECK(!strcmp(xastir_display_unit(XASTIR_DISPLAY_PRESSURE, 0), "hPa"));
  CHECK(list_tests() == 0);
  DataRow row={0};
  Selections s={0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1};
  row.flag=ST_ACTIVE|ST_VIATNC|ST_DIRECT;row.sec_heard=row.direct_heard=1000;
  strcpy(row.call_sign,"TEST");row.aprs_symbol.aprs_type='/';row.aprs_symbol.aprs_symbol='-';
  CHECK(xastir_station_visible(&row,&s,1100,3600,3600,0));
  s.none=1;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.none=0;
  s.direct=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.direct=1;
  row.flag&=~ST_DIRECT;s.via_digi=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.via_digi=1;
  s.tnc=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.tnc=1;
  row.flag=ST_ACTIVE;s.net=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.net=1;
  CHECK(!xastir_station_visible(&row,&s,5000,3600,3600,0));s.old_data=1;CHECK(xastir_station_visible(&row,&s,5000,3600,3600,0));s.old_data=0;
  s.tactical=1;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));row.tactical_call_sign="RESCUE";CHECK(xastir_station_visible(&row,&s,1100,3600,3600,0));s.tactical=0;
  row.flag|=ST_MYSTATION;s.mine=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.mine=1;row.flag&=~ST_MYSTATION;
  s.fixed_stations=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.fixed_stations=1;
  row.flag|=ST_MOVING;s.moving_stations=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.moving_stations=1;
  CHECK(xastir_get_weather_record(&row));strcpy(row.call_sign,"CW1234");s.CWOP_wx_stations=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.CWOP_wx_stations=1;
  s.weather_stations=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.weather_stations=1;
  row.flag|=ST_OBJECT;s.weather_objects=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));s.weather_objects=1;
  xastir_delete_weather(&row);
  char codes[]="w^s-";int *selectors[]={&s.gauge_objects,&s.aircraft_objects,&s.vessel_objects,&s.other_objects};
  for(int i=0;i<4;++i){row.aprs_symbol.aprs_symbol=codes[i];CHECK(xastir_station_visible(&row,&s,1100,3600,3600,0));*selectors[i]=0;CHECK(!xastir_station_visible(&row,&s,1100,3600,3600,0));*selectors[i]=1;}
  char wx[]="090/010g020t032r001p002P003h00b10123";
  CHECK(xastir_extract_weather(&row,wx,0,1234));
  CHECK(!strcmp(row.weather_data->wx_speed,"012"));CHECK(!strcmp(row.weather_data->wx_temp,"032"));
  CHECK(!strcmp(row.weather_data->wx_hum,"100"));CHECK(!strcmp(row.weather_data->wx_baro,"1012.3"));CHECK(row.weather_data->wx_sec_time==1234);
  xastir_delete_weather(&row);
  char cold[]="c180s005g010t-05h50b10000";CHECK(xastir_extract_weather(&row,cold,0,1250));CHECK(!strcmp(row.weather_data->wx_temp,"-05"));
  xastir_delete_weather(&row);
  char blank[]="t...g...h..";CHECK(!xastir_extract_weather(&row,blank,0,0));
  char original[]="090/010g020t032";
  for(size_t n=0;n<strlen(original);++n){char data[40];memcpy(data,original,n);data[n]=0;CHECK(!xastir_extract_weather(&row,data,0,0));}
  char phg[]="PHG5130/A=000123";char out[10];CHECK(xastir_extract_powergain_range(phg,out));CHECK(!strcmp(out,"PHG5130"));CHECK(!strcmp(phg,"/A=000123"));
  char df[]="/180/123 comment";char bearing[4],nrq[4];CHECK(xastir_extract_bearing_NRQ(df,bearing,nrq));CHECK(!strcmp(bearing,"180") && !strcmp(nrq,"123"));
  CHECK(isfinite(xastir_phg_range('5','1','3')) && xastir_phg_range('5','1','3')>0);
  double bearing_deg;CHECK(xastir_distance(0,0,0,0,&bearing_deg)==0);CHECK(fabs(xastir_distance(0,0,0,1,&bearing_deg)-111319.51)<1);CHECK(fabs(bearing_deg-90)<.001);
  CHECK(isfinite(xastir_distance(0,0,0,180,NULL)));
  double lat,lon;xastir_destination(0,179.99,90,5000,&lat,&lon);CHECK(fabs(lat)<.000001 && lon<0);
  long dr_lon,dr_lat;
  xastir_compute_DR_position(64800000,32400000,60,90,&dr_lon,&dr_lat);
  CHECK(labs(dr_lon-65160000)<10 && labs(dr_lat-32400000)<10);
  xastir_compute_DR_position(129599000,32400000,60,90,&dr_lon,&dr_lat);
  CHECK(dr_lon<360000 && dr_lon>=0);
  xastir_compute_DR_position(64800000,32400000,NAN,90,&dr_lon,&dr_lat);
  CHECK(dr_lon==64800000 && dr_lat==32400000);
  XastirStationDb *db=calloc(1,sizeof(*db));CHECK(db);xastir_station_db_init(db,1000);aloha_stats stats;
  CHECK(xastir_aloha_radius(db,0,0,&stats)==-1 && !stats.total);
  for(int i=0;i<130;++i){char name[10];snprintf(name,sizeof(name),"T%04d",i);DataRow *r=xastir_station_upsert(db,name,1000);CHECK(r);r->flag=ST_ACTIVE|ST_VIATNC|ST_MOVING;r->position_valid=1;r->latitude=0;r->longitude=(i+1)*.001;strcpy(r->speed,"010");CHECK(xastir_store_trail_point(db,r,1,1,1000,"","010","090",0));}
  CHECK(xastir_aloha_radius(db,0,0,&stats)>0 && stats.mobiles_in_motion==120 && stats.total==120);
  xastir_delete_all_stations(db);free(db);return 0;
}
