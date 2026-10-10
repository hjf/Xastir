/*
 *
 * XASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2026 The Xastir Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 */

/* Extracted station selection, metadata, and radio range logic from db.c/util.c. */
#include "station_view.h"
#include "aprs_position.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define EARTH_RADIUS_METERS 6378138.0
#define M_PI 3.14159265358979323846
static int view_digit(char c) { return c >= '0' && c <= '9'; }
static int view_aprs_char(char c) { return view_digit(c) || c == '-' || c == ' ' || c == '.'; }
static void view_substr(char *out, const char *in, size_t count) {
  size_t n = strlen(in); if (n > count) n = count; memcpy(out,in,n); out[n] = 0;
}
int xastir_station_visible(const DataRow *p_station, const Selections *selection, time_t secs_now, int sec_clear, int st_direct_timeout, int aircraft_sec_clear)
{

  if (!p_station || !selection || !(p_station->flag & ST_ACTIVE)) return 0;
  // Check overall flag
  if (selection->none)
  {
    return 0;
  }

  // Check tactical flag
  if (selection->tactical
      && (p_station->tactical_call_sign == NULL
          || p_station->tactical_call_sign[0] == '\0'))
  {
    return 0;
  }

  // Check for my station and my objects/items
//    if (strcmp(p_station->call_sign, my_callsign) == 0
//        || (is_my_call(p_station->origin, 1)        // If station is owned by me (including SSID)
//            && (   p_station->flag & ST_OBJECT      // And it's an object
//                || p_station->flag & ST_ITEM) ) ) { // or an item
//    if ( (p_station->flag & (ST_MYSTATION | ST_MYOBJITEM)) || is_my_object_item(p_station) ) {
  if ( (p_station->flag & (ST_MYSTATION | ST_MYOBJITEM)) )
  {
    if (!selection->mine)
    {
      return 0;
    }
  }
  // Not mine, so check these next things
  else
  {
    // Check whether we wish to display TNC heard stations
    if (p_station->flag & ST_VIATNC)
    {
      if (!selection->tnc)
      {
        return 0;
      }

      // Check whether we wish to display directly heard stations
      if (p_station->flag & ST_DIRECT
          && secs_now < (p_station->direct_heard + st_direct_timeout))
      {
        if (!selection->direct)
        {
          return 0;
        }
      }
      // Check whether we wish to display stations heard via a digi
      else
      {
        if (!selection->via_digi)
        {
          return 0;
        }
      }
    }
    // Check whether we wish to display net stations
    else
    {
      if (!selection->net)
      {
        return 0;
      }
    }

    //N7IPB - check for aircraft and if so check aircraft timeout
    // return 0 if timedout - else continue
    if ((aircraft_sec_clear != 0)
        && ((p_station->aprs_symbol.aprs_symbol == '^')
            || (p_station->aprs_symbol.aprs_symbol == '\'')
            || (p_station->aprs_symbol.aprs_symbol == 'X')))
    {
      if ((p_station->sec_heard + (aircraft_sec_clear * 5)) < secs_now)
      {
        return 0;
      }
    }
    // Check if we want to display data past the clear time
    if (!selection->old_data)
    {
      if ((p_station->sec_heard + sec_clear) < secs_now)
      {
        return 0;
      }
    }
  }


  // Check whether object or item
  if (p_station->flag & (ST_OBJECT | ST_ITEM))
  {
    // Check whether we wish to display objects/items
    if (!selection->objects ||
        (!selection->weather_objects && !selection->gauge_objects && !selection->aircraft_objects && !selection->vessel_objects && !selection->other_objects))
    {
      return 0;
    }

    // Check if WX info and we wish to see it
    if (p_station->weather_data)
    {
      return selection->weather_objects;
    }
    // Check if water gauge and we wish to see it
    else if (p_station->aprs_symbol.aprs_type == '/'
             && p_station->aprs_symbol.aprs_symbol == 'w')
    {
      return selection->gauge_objects;
    }
    // Check if aircraft and we wish to see it
    else if (p_station->aprs_symbol.aprs_type == '/'
             && ((p_station->aprs_symbol.aprs_symbol == '^')
                 || (p_station->aprs_symbol.aprs_symbol == '\'')
                 || (p_station->aprs_symbol.aprs_symbol == 'X')) )
    {
      return selection->aircraft_objects;
    }
    // Check if vessel and we wish to see it
    else if (p_station->aprs_symbol.aprs_type == '/'
             && ((p_station->aprs_symbol.aprs_symbol == 's')
                 || (p_station->aprs_symbol.aprs_symbol == 'Y')) )
    {
      return selection->vessel_objects;
    }
    // Check if we wish to see other objects/items
    else
    {
      return selection->other_objects;
    }
  }
  else      // Not an object or item
  {
    if (!selection->stations ||
        (!selection->fixed_stations && !selection->moving_stations && !selection->weather_stations))
    {
      return 0;
    }

    // Check if we wish to see weather stations
    if (p_station->weather_data)
    {
      // We have weather data

      // Check whether it is a citizen's weather station.
      // Note that there are quite a few countries with two-letter
      // prefixes whose second letter is W, so let's be careful how we
      // filter here.
      // All Citizen's weather stations seen to date have had
      // CW through GW and then four digits.  There will undoubtedly be
      // more added in future years, so let's not be *too* restrictive,
      // but let's not be too aggressive, either.
      if (((p_station->call_sign[0] >= 'C' && p_station->call_sign[0] <= 'G')
           || (p_station->call_sign[0] >= 'c' && p_station->call_sign[0] <= 'g'))
          && (p_station->call_sign[1] == 'W' || p_station->call_sign[1] == 'w'))
      {
        if ( view_digit(p_station->call_sign[2])
             && view_digit(p_station->call_sign[3])
             && view_digit(p_station->call_sign[4])
             && view_digit(p_station->call_sign[5]) )
        {
          return(selection->weather_stations && selection->CWOP_wx_stations);
        }
        else
        {
          return selection->weather_stations;
        }
      }
      else
      {
        return selection->weather_stations;
      }
    }
    // Check if we wish to see other stations
    else
    {
      if (p_station->flag & ST_MOVING)
      {
        return selection->moving_stations;
      }
      else
      {
        return selection->fixed_stations;
      }
    }
  }
}

double xastir_phg_range(char p, char h, char g)
{
  double power, height, gain, range;

  if ( (p < '0') || (p > '9') )   // Power is outside limits
  {
    power = 0.0;
  }
  else
  {
    power = (double)( (p-'0')*(p-'0') );  // lclint said: "Assignment of char to double" here
  }

  if (h < '0')   // Height is outside limits (there is no upper limit according to the spec)
  {
    height = 10.0;
  }
  else
  {
    height = 10.0 * pow(2.0, (double)(h-'0'));
  }

  if ( (g < '0') || (g > '9') )   // Gain is outside limits
  {
    gain = 1.0;
  }
  else
  {
    gain = pow(10.0, (double)(g-'0') / 10.0);
  }

  range = sqrt(2.0 * height * sqrt((power / 10.0) * (gain / 2.0)));

//    if (range > 70.0)
//        fprintf(stderr,"PHG%c%c%c results in range of %f\n", p, h, g, range);

  // Note:  Bob Bruninga, WB4APR, decided to cut PHG circles by
  // 1/2 in order to show more realistic mobile ranges.
  range = range / 2.0;

  return(range);
}

double xastir_shg_range(char s, char h, char g)
{
  double power, height, gain, range;

  if ( (s < '0') || (s > '9') )   // Power is outside limits
  {
    s = '0';
  }

  if (s == '0')               // No signal strength
  {
    power = 10.0 / 0.8;  // Preventing divide by zero (same as DOSaprs does it)
  }
  else
  {
    power = 10 / (s - '0');  // Makes circle smaller with higher signal strengths
  }

  if (h < '0')   // Height is outside limits (there is no upper limit according to the spec)
  {
    height = 10.0;
  }
  else
  {
    height = 10.0 * pow(2.0, (double)(h-'0'));
  }

  if ( (g < '0') || (g > '9') )   // Gain is outside limits
  {
    gain = 1.0;
  }
  else
  {
    gain = pow(10.0, (double)(g-'0') / 10.0);
  }

  range = sqrt(2.0 * height * sqrt((power / 10.0) * (gain / 2.0)));

  range = (range * 40) / 51;   // Present fudge factors used in DOSaprs

  //fprintf(stderr,"SHG%c%c%c results in range of %f\n", s, h, g, range);

  return(range);
}

double xastir_distance_radians(double lat1, double lon1, double lat2, double lon2)
{
  double dlon, dlat;
  double a, c, d;
  double R = EARTH_RADIUS_METERS;
#define square(x) (x)*(x)


  dlon = lon2 - lon1;
  dlat = lat2 - lat1;
  a = square((sin(dlat/2.0))) + cos(lat1) * cos(lat2) * square((sin(dlon/2.0)));
  a = fmin(1.0, fmax(0.0, a));
  c = 2.0 * atan2(sqrt(a), sqrt(1.0-a));
  d = R * c;

  return(d);
}

int xastir_is_weather_data(char *data, int len)
{
  int ok = 1;
  int i;
  int count = 0;

  for (i=0; ok && i<len; i++)
    if (!view_aprs_char(data[i]))
    {
      ok = 0;
    }

  // Count filler characters.  Must equal zero or field width to
  // be a weather field.  There doesn't appear to be a case where
  // a single period is allowed in any weather-related fields.
  //
  for (i=0; ok && i<len; i++)
  {
    if (data[i] == ' ' || data[i] == '.')
    {
      count++;
    }
  }
  if (count != 0 && count != len)
  {
    ok = 0;
  }

  return(ok);
}

int xastir_extract_weather_item(char *data, char type, int datalen, char *temp)
{
  int i,ofs,found,len;


  //fprintf(stderr,"%s\n",data);

  found=0;
  len = (int)strlen(data);
  for(ofs=0; !found && ofs<len-datalen; ofs++)      // search for start sequence
    if (data[ofs]==type)
    {
      found=1;
      if (!xastir_is_weather_data(data+ofs+1, datalen))
      {
        found=0;
      }
    }
  if (found)     // ofs now points after type character
  {
    view_substr(temp,data+ofs,datalen);
    for (i=ofs-1; i<len-datalen; i++)      // delete item from info field
    {
      data[i] = data[i+datalen+1];
    }
    if((temp[0] == ' ') || (temp[0] == '.'))
    {
      // found it, but it doesn't contain a value!
      // Clean up and report "not found" - PE1DNN
      temp[0] = '\0';
      found = 0;
    }
    else
    {

    }
  }
  else
  {
    temp[0] = '\0';
  }
  return(found);
}

int xastir_test_extract_weather_item(char *data, char type, int datalen)
{
  int ofs,found,len;

  found=0;
  len = (int)strlen(data);
  for(ofs=0; !found && ofs<len-datalen; ofs++)      // search for start sequence
    if (data[ofs]==type)
    {
      found=1;
      if (!xastir_is_weather_data(data+ofs+1, datalen))
      {
        found=0;
      }
    }

  // We really should test for numbers here (with an optional
  // leading '-'), and test across the length of the substring.
  //
  if(found && ((data[ofs] == ' ') || (data[ofs] == '.')))
  {
    // found it, but it doesn't contain a value!
    // report "not found" - PE1DNN
    found = 0;
  }

  //fprintf(stderr,"test_extract: %c %d\n",type,found);
  return(found);
}

int xastir_extract_weather(DataRow *p_station, char *data, int compr, time_t now)
{

  char temp[5];
  int  ok = 1;
  WeatherRow *weather;
  char course[4];
  char speed[4];
  int in_knots = 0;

  //WE7U
  // Try copying the string to a temporary string, then do some
  // extractions to see if a few weather items are present?  This
  // would allow us to have the weather items in any order, and if
  // enough of them were present, we consider it to be a weather
  // packet?  We'd need to qualify all of the data to make sure we had
  // the proper number of digits for each.  The trick is to make sure
  // we don't decide it's a weather packet if it's not.  We don't know
  // what people might send in packets in the future.

  if (compr)          // compressed position report
  {
    // Look for weather data in fixed locations first
    if (strlen(data) >= 8
        && data[0] =='g' && xastir_is_weather_data(&data[1],3)
        && data[4] =='t' && xastir_is_weather_data(&data[5],3))
    {

      // Snag WX course/speed from compressed position data.
      // This speed is in knots.  This assumes that we've
      // already extracted speed/course from the compressed
      // packet.  extract_comp_position() extracts
      // course/speed as well.
      memcpy(speed, p_station->speed, sizeof(speed));
      speed[sizeof(speed)-1] = '\0';  // Terminate string
      memcpy(course, p_station->course, sizeof(course));
      course[sizeof(course)-1] = '\0';  // Terminate string
      in_knots = 1;

      //fprintf(stderr,"Found compressed wx\n");
    }
    // Look for weather data in non-fixed locations (RAWS WX
    // Stations?)
    else if ( strlen(data) >= 8
              && xastir_test_extract_weather_item(data,'g',3)
              && xastir_test_extract_weather_item(data,'t',3) )
    {

      // Snag WX course/speed from compressed position data.
      // This speed is in knots.  This assumes that we've
      // already extracted speed/course from the compressed
      // packet.  extract_comp_position() extracts
      // course/speed as well.
      memcpy(speed, p_station->speed, sizeof(speed));
      speed[sizeof(speed)-1] = '\0';  // Terminate string
      memcpy(course, p_station->course, sizeof(course));
      course[sizeof(course)-1] = '\0';  // Terminate string
      in_knots = 1;

      //fprintf(stderr,"Found compressed WX in non-fixed locations! %s:%s\n",
      //    p_station->call_sign,data);

    }
    else    // No weather data found
    {
      ok = 0;

      //fprintf(stderr,"No compressed wx\n");
    }
  }
  else      // Look for non-compressed weather data
  {
    // Look for weather data in defined locations first
    if (strlen(data)>=15 && data[3]=='/'
        && xastir_is_weather_data(data,3) && xastir_is_weather_data(&data[4],3)
        && data[7] =='g' && xastir_is_weather_data(&data[8], 3)
        && data[11]=='t' && xastir_is_weather_data(&data[12],3))      // Complete Weather Report
    {

      // Get speed/course.  Speed is in knots.
      (void)xastir_extract_speed_course(data,speed,course);
      in_knots = 1;

      // Either one not found?  Try again.
      if ( (speed[0] == '\0') || (course[0] == '\0') )
      {

        // Try to get speed/course from 's' and 'c' fields
        // (another wx format).  Speed is in mph.
        (void)xastir_extract_weather_item(data,'c',3,course); // wind direction (in degrees)
        (void)xastir_extract_weather_item(data,'s',3,speed);  // sustained one-minute wind speed (in mph)
        in_knots = 0;
      }

      //fprintf(stderr,"Found Complete Weather Report\n");
    }
    // Look for date/time and weather in fixed locations first
    else if (strlen(data)>=16
             && data[0] =='c' && xastir_is_weather_data(&data[1], 3)
             && data[4] =='s' && xastir_is_weather_data(&data[5], 3)
             && data[8] =='g' && xastir_is_weather_data(&data[9], 3)
             && data[12]=='t' && xastir_is_weather_data(&data[13],3))   // Positionless Weather Data
    {
      //fprintf(stderr,"Found positionless wx data\n");
      // Try to snag speed/course out of first 7 bytes.  Speed
      // is in knots.
      (void)xastir_extract_speed_course(data,speed,course);
      in_knots = 1;

      // Either one not found?  Try again.
      if ( (speed[0] == '\0') || (course[0] == '\0') )
      {
        //fprintf(stderr,"Trying again for course/speed\n");
        // Also try to get speed/course from 's' and 'c' fields
        // (another wx format)
        (void)xastir_extract_weather_item(data,'c',3,course); // wind direction (in degrees)
        (void)xastir_extract_weather_item(data,'s',3,speed);  // sustained one-minute wind speed (in mph)
        in_knots = 0;
      }

      //fprintf(stderr,"Found weather\n");
    }
    // Look for weather data in non-fixed locations (RAWS WX
    // Stations?)
    else if (strlen (data) >= 16
             && xastir_test_extract_weather_item(data,'h',2)
             && xastir_test_extract_weather_item(data,'g',3)
             && xastir_test_extract_weather_item(data,'t',3) )
    {

      // Try to snag speed/course out of first 7 bytes.  Speed
      // is in knots.
      (void)xastir_extract_speed_course(data,speed,course);
      in_knots = 1;

      // Either one not found?  Try again.
      if ( (speed[0] == '\0') || (course[0] == '\0') )
      {

        // Also try to get speed/course from 's' and 'c' fields
        // (another wx format)
        (void)xastir_extract_weather_item(data,'c',3,course); // wind direction (in degrees)
        (void)xastir_extract_weather_item(data,'s',3,speed);  // sustained one-minute wind speed (in mph)
        in_knots = 0;
      }

      //fprintf(stderr,"Found WX in non-fixed locations!  %s:%s\n",
      //    p_station->call_sign,data);
    }
    else    // No weather data found
    {
      ok = 0;

      //fprintf(stderr,"No wx found\n");
    }
  }

  if (ok)
  {
    ok = xastir_get_weather_record(p_station);     // get existing or create new weather record
  }

  if (ok)
  {
    weather = p_station->weather_data;

    // Copy into weather speed variable.  Convert knots to mph
    // if necessary.
    if (in_knots)
    {
      snprintf(weather->wx_speed,
                      sizeof(weather->wx_speed),
                      "%03.0f",
                      atoi(speed) * 1.1508);  // Convert knots to mph
    }
    else
    {
      // Already in mph.  Copy w/no conversion.
      snprintf(weather->wx_speed,
                      sizeof(weather->wx_speed),
                      "%s",
                      speed);
    }

    snprintf(weather->wx_course,
                    sizeof(weather->wx_course),
                    "%s",
                    course);

    if (compr)          // course/speed was taken from normal data, delete that
    {
      // fix me: we delete a potential real speed/course now
      // we should differentiate between normal and weather data in compressed position decoding...
      //            p_station->speed_time[0]     = '\0';
      p_station->speed[0]          = '\0';
      p_station->course[0]         = '\0';
    }

    (void)xastir_extract_weather_item(data,'g',3,weather->wx_gust);      // gust (peak wind speed in mph in the last 5 minutes)

    (void)xastir_extract_weather_item(data,'t',3,weather->wx_temp);      // temperature (in deg Fahrenheit), could be negative

    (void)xastir_extract_weather_item(data,'r',3,weather->wx_rain);      // rainfall (1/100 inch) in the last hour

    (void)xastir_extract_weather_item(data,'p',3,weather->wx_prec_24);   // rainfall (1/100 inch) in the last 24 hours

    (void)xastir_extract_weather_item(data,'P',3,weather->wx_prec_00);   // rainfall (1/100 inch) since midnight

    if (xastir_extract_weather_item(data,'h',2,weather->wx_hum))         // humidity (in %, 00 = 100%)
    {
      snprintf(weather->wx_hum, sizeof(weather->wx_hum), "%03d",(atoi(weather->wx_hum)+99)%100+1);
    }

    if (xastir_extract_weather_item(data,'b',5,weather->wx_baro))  // barometric pressure (1/10 mbar / 1/10 hPascal)
      snprintf(weather->wx_baro,
                      sizeof(weather->wx_baro),
                      "%0.1f",
                      (float)(atoi(weather->wx_baro)/10.0));

    // If we parsed a speed/course, a second 's' parameter means
    // snowfall.  Try to parse it, but only in the case where
    // we've parsed speed out of this packet already.
    if ( (speed[0] != '\0') && (course[0] != '\0') )
    {
      (void)xastir_extract_weather_item(data,'s',3,weather->wx_snow);      // snowfall, inches in the last 24 hours
    }

    (void)xastir_extract_weather_item(data,'L',3,temp);                  // luminosity (in watts per square meter) 999 and below

    (void)xastir_extract_weather_item(data,'l',3,temp);                  // luminosity (in watts per square meter) 1000 and above

    (void)xastir_extract_weather_item(data,'#',3,temp);                  // raw rain counter

    (void)xastir_extract_weather_item(data,'F',3,weather->wx_fuel_temp); // Fuel Temperature in °F (RAWS)

    if (xastir_extract_weather_item(data,'f',2,weather->wx_fuel_moisture))// Fuel Moisture (RAWS) (in %, 00 = 100%)
      snprintf(weather->wx_fuel_moisture,
                      sizeof(weather->wx_fuel_moisture),
                      "%03d",
                      (atoi(weather->wx_fuel_moisture)+99)%100+1);

    //    xastir_extract_weather_item(data,'w',3,temp);                          // ?? text wUII

    // now there should be the name of the weather station...

    // Create a timestamp from the current time
    weather->wx_time[0] = 0;

    // Set the timestamp in the weather record so that we can
    // decide whether or not to "ghost" the weather data later.
    weather->wx_sec_time=now;
//        weather->wx_data=1;  // we don't need this

//        case ('.'):/* skip */
//            wx_strpos+=4;
//            break;

//        default:
//            wx_done=1;
//            weather->wx_type=data[wx_strpos];
//            if(strlen(data)>wx_strpos+1)
//                snprintf(weather->wx_station,
//                    sizeof(weather->wx_station),
//                    "%s",
//                    data+wx_strpos+1);
//            break;
  }
  return(ok);
}

int xastir_extract_bearing_NRQ(char *info, char *bearing, char *nrq)
{
  int i,found,len;

  len = (int)strlen(info);
  found = 0;
  if (len >= 8)
  {
    found = 1;
    for(i=1; found && i<8; i++)         // check data format
      if(!(isdigit((unsigned char)info[i]) || (i==4 && info[i]=='/')))
      {
        found=0;
      }
  }
  if (found)
  {
    view_substr(bearing,info+1,3);
    view_substr(nrq,info+5,3);

    //fprintf(stderr,"Bearing: %s\tNRQ: %s\n", bearing, nrq);

    for (i=0; i<=len-8; i++)      // delete bearing/nrq from info field
    {
      info[i] = info[i+8];
    }
  }

  //    if (!found || nrq[2] == '0') {   // Q of 0 means useless bearing
  if (!found)
  {
    bearing[0] ='\0';
    nrq[0]='\0';
  }
  return(found);
}

int xastir_extract_powergain_range(char *info, char *phgd)
{
  int i,found,len;
  char *info2;


  //fprintf(stderr,"Info:%s\n",info);

  // Check whether two strings of interest are present and snag a
  // pointer to them.
  info2 = strstr(info,"RNG");
  if (!info2)
  {
    info2 = strstr(info,"PHG");
  }
  if (!info2)
  {
    phgd[0] = '\0';
    return(0);
  }

  found=0;
  len = (int)strlen(info2);

  if (len >= 9 && strncmp(info2,"PHG",3)==0
      && info2[7]=='/'
      && info2[8]!='A'  // trailing '/' not defined in Reference...
      && isdigit((unsigned char)info2[3])
      && isdigit((unsigned char)info2[4])
      && isdigit((unsigned char)info2[5])
      && isdigit((unsigned char)info2[6]))
  {
    view_substr(phgd,info2,7);
    found = 1;
    for (i=0; i<=len-8; i++)      // delete powergain from data extension field
    {
      info2[i] = info2[i+8];
    }
  }
  else
  {
    if (len >= 7 && strncmp(info2,"PHG",3)==0
        && isdigit((unsigned char)info2[3])
        && isdigit((unsigned char)info2[4])
        && isdigit((unsigned char)info2[5])
        && isdigit((unsigned char)info2[6]))
    {
      view_substr(phgd,info2,7);
      found = 1;
      for (i=0; i<=len-7; i++)      // delete powergain from data extension field
      {
        info2[i] = info2[i+7];
      }
    }
    else if (len >= 7 && strncmp(info2,"RNG",3)==0
             && isdigit((unsigned char)info2[3])
             && isdigit((unsigned char)info2[4])
             && isdigit((unsigned char)info2[5])
             && isdigit((unsigned char)info2[6]))
    {
      view_substr(phgd,info2,7);
      found = 1;
      for (i=0; i<=len-7; i++)      // delete powergain from data extension field
      {
        info2[i] = info2[i+7];
      }
    }
    else
    {
      phgd[0] = '\0';
    }
  }
  return(found);
}

int xastir_extract_omnidf(char *info, char *phgd)
{
  int i,len;

  len = (int)strlen(info);
  if (len >= 8 && strncmp(info,"DFS",3)==0 && info[7]=='/'    // trailing '/' not defined in Reference...
      && isdigit((unsigned char)info[3]) && isdigit((unsigned char)info[5]) && isdigit((unsigned char)info[6]))
  {
    view_substr(phgd,info,7);
    for (i=0; i<=len-8; i++)      // delete omnidf from data extension field
    {
      info[i] = info[i+8];
    }
    return(1);
  }
  else
  {
    phgd[0] = '\0';
    return(0);
  }
}

/* Great-circle navigation for the display layer, in degrees and meters. */
double xastir_distance(double lat1, double lon1, double lat2, double lon2, double *bearing)
{
  double a=lat1*M_PI/180, b=lat2*M_PI/180, d=(lon2-lon1)*M_PI/180;
  if (bearing) *bearing=fmod(atan2(sin(d)*cos(b),cos(a)*sin(b)-sin(a)*cos(b)*cos(d))*180/M_PI+360,360);
  return xastir_distance_radians(a,lon1*M_PI/180,b,lon2*M_PI/180);
}
void xastir_destination(double lat, double lon, double bearing, double meters, double *out_lat, double *out_lon)
{
  double a=lat*M_PI/180, b=lon*M_PI/180, c=bearing*M_PI/180, d=meters/EARTH_RADIUS_METERS;
  double end=asin(fmax(-1,fmin(1,sin(a)*cos(d)+cos(a)*sin(d)*cos(c))));
  *out_lat=end*180/M_PI;
  *out_lon=fmod((b+atan2(sin(c)*sin(d)*cos(a),cos(d)-sin(a)*sin(end)))*180/M_PI+540,360)-180;
}
/* Original calc_aloha_distance classifications and channel-load weights,
 * with the station context and observer position supplied by the caller. */
static int aloha_compare(const void *av,const void *bv)
{
  const aloha_entry *a=av,*b=bv;
  return (a->distance>b->distance)-(a->distance<b->distance);
}
double xastir_aloha_radius(const XastirStationDb *db, double lat, double lon, aloha_stats *stats)
{
  aloha_entry *entries=calloc((size_t)(db->count?db->count:1),sizeof(*entries));
  int count=0, copies=1, sum=0, i;
  memset(stats,0,sizeof(*stats));
  if (!entries) return -1;
  for (const DataRow *row=db->first;row;row=row->n_next) {
    if (!(row->flag&ST_VIATNC) || !(row->flag&ST_ACTIVE)) continue;
    aloha_entry *entry=&entries[count++];
    double row_lat=row->position_valid?row->latitude:90.0-row->coord_lat/360000.0;
    double row_lon=row->position_valid?row->longitude:row->coord_lon/360000.0-180.0;
    if (!row->position_valid && (!row->coord_lat || !row->coord_lon)) {--count;continue;}
    entry->distance=xastir_distance(lat,lon,row_lat,row_lon,NULL);
    if (row->newest_trackpoint && row->speed[0]) entry->is_mobile=1;
    else if ((row->aprs_symbol.aprs_type=='/' && strchr("'<=>()*0COPRSUXY[^abefgjkpsuv",row->aprs_symbol.aprs_symbol))
        || (row->aprs_symbol.aprs_type=='\\' && strchr("/0>AKOS^knsuv",row->aprs_symbol.aprs_symbol))) entry->is_other_mobile=1;
    else if (row->weather_data || row->aprs_symbol.aprs_symbol=='_') entry->is_wx=1;
    else if (row->aprs_symbol.aprs_symbol=='#') entry->is_digi=1;
    else entry->is_home=1;
  }
  qsort(entries,(size_t)count,sizeof(*entries),aloha_compare);
  for(i=0;i<count && sum<1800;++i) {
    ++stats->total;
    if(entries[i].is_digi) {sum+=copies*3;++copies;++stats->digis;}
    else if(entries[i].is_home) {sum+=copies*2;++stats->homes;}
    else if(entries[i].is_wx) {sum+=copies*6;++stats->wxs;}
    else if(entries[i].is_mobile) {sum+=copies*15;++stats->mobiles_in_motion;}
    else if(entries[i].is_other_mobile) {sum+=copies*7;++stats->other_mobiles;}
  }
  double radius=(i>0 && i<count && sum>=1800)?entries[i-1].distance:-1;
  free(entries);return radius;
}


/* Coordinate boundaries for the original dr_utils.c rhumb-line calculation. */
static int view_convert_from(float *lon,float *lat,long x,long y)
{
  if (x<0 || x>129600000 || y<0 || y>64800000) return 0;
  *lon=(float)((x-64800000)/360000.0); *lat=(float)((32400000-y)/360000.0);return 1;
}
static int view_convert_to(unsigned long *x,unsigned long *y,float lon,float lat)
{
  if (!isfinite(lon) || !isfinite(lat) || lon < -180 || lon>180 || lat < -90 || lat>90) return 0;
  *x=(unsigned long)(64800000+360000.0*lon);*y=(unsigned long)(32400000-360000.0*lat);return 1;
}
void xastir_compute_DR_position(long x_long,   // input
                         long y_lat,     // input
                         double range,   // input in nautical miles
                         double course,  // input in ° true
                         long *x_long2,  // output
                         long *y_lat2)   // output
{
  double bearing_radians, lat_M_radians;
  float lat_A, lat_B, lon_A, lon_B, lat_M;
  int ret;
  unsigned long x_u_long, y_u_lat;


//fprintf(stderr,"Distance:%fnm, Course:%f,  Time:%d\n",
//    range,
//    course,
//    (int)(sec_now() - p_station->sec_heard));

  if (!isfinite(range) || !isfinite(course)) { *x_long2=x_long; *y_lat2=y_lat; return; }
  // Bearing in radians
  bearing_radians = (double)((course/360.0) * 2.0 * M_PI);

  // Convert lat/long to floats
  ret = view_convert_from( &lon_A,
                                         &lat_A,
                                         x_long,
                                         y_lat);

  // Check if conversion ok
  if (!ret)
  {
    // Problem during conversion.  Exit without changes.
    *x_long2 = x_long;
    *y_lat2 = y_lat;
    return;
  }

  // Compute new latitude
  lat_B = (float)((double)(lat_A) + (range/60.0) * cos(bearing_radians));

  // Compute mid-range latitude
  lat_M = (lat_A + lat_B) / 2.0;

  // Convert lat_M to radians
  lat_M_radians = (double)((lat_M/360.0) * 2.0 * M_PI);

  // Compute new longitude
  lon_B = (float)((double)(lon_A)
                  + (range/60.0) * ( sin(bearing_radians) / cos(lat_M_radians)) );

  lon_B = (float)(fmod((double)lon_B+540.0,360.0)-180.0);

//fprintf(stderr,"Lat:%f,  Lon:%f\n", lat_B, lon_B);

  ret = view_convert_to(&x_u_long,
                                      &y_u_lat,
                                      lon_B,
                                      lat_B);

  // Check if conversion ok
  if (!ret)
  {
    // Problem during conversion.  Exit without changes.
    *x_long2 = x_long;
    *y_lat2 = y_lat;
    return;
  }

  // Convert from unsigned long to long
  *x_long2 = (long)x_u_long;
  *y_lat2  = (long)y_u_lat;
}
