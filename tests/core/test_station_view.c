// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;} } while(0)
int main(void)
{
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
