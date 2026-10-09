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

/* Extracted from db.c and util.c; no GUI, globals, or station identity policy. */
#include "aprs_position.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void substr(char *out, const char *in, size_t count)
{
  size_t length = strlen(in);
  if (length > count) length = count;
  memcpy(out, in, length);
  out[length] = 0;
}
static int is_num_chr(char c) { return c >= '0' && c <= '9'; }
static int is_num_or_sp(char c) { return is_num_chr(c) || c == ' '; }
long xastir_convert_lat_s2l(char *lat)        /* N=0°=°, Ctr=90°, S=180° */
{
  long centi_sec;
  char copy[15];
  char n[15];
  char *p;
  char offset;


  // Find the decimal point if present
  p = strstr(lat, ".");

  if (p == NULL)  // No decimal point found
  {
    return(0l);
  }

  memset(copy, '\0', sizeof(copy));
  offset = p - lat;   // Arithmetic on pointers
  switch (offset)
  {
    case 0:     // .MM[MM]N
      return(0l); // Bad, no degrees or minutes
      break;
    case 1:     // M.MM[MM]N
      return(0l); // Bad, no degrees
      break;
    case 2:     // MM.MM[MM]N
      return(0l); // Bad, no degrees
      break;
    case 3:     // DMM.MM[MM]N
      snprintf(copy,
                      sizeof(copy),
                      "0%s",  // Add a leading '0'
                      lat);
      break;
    case 4:     // DDMM.MM[MM]N
      snprintf(copy,
                      sizeof(copy),
                      "%s",   // Copy verbatim
                      lat);
      break;
    default:
      break;
  }

  copy[14] = '\0';
  centi_sec=0l;
  if (copy[4]=='.'
      && (   (char)toupper((int)copy[ 5])=='N'
             || (char)toupper((int)copy[ 6])=='N'
             || (char)toupper((int)copy[ 7])=='N'
             || (char)toupper((int)copy[ 8])=='N'
             || (char)toupper((int)copy[ 9])=='N'
             || (char)toupper((int)copy[10])=='N'
             || (char)toupper((int)copy[11])=='N'
             || (char)toupper((int)copy[ 5])=='S'
             || (char)toupper((int)copy[ 6])=='S'
             || (char)toupper((int)copy[ 7])=='S'
             || (char)toupper((int)copy[ 8])=='S'
             || (char)toupper((int)copy[ 9])=='S'
             || (char)toupper((int)copy[10])=='S'
             || (char)toupper((int)copy[11])=='S'))
  {

    substr(n, copy, 2);       // degrees
    centi_sec=atoi(n)*60*60*100;

    substr(n, copy+2, 2);     // minutes
    centi_sec += atoi(n)*60*100;

    substr(n, copy+5, 4);     // fractional minutes
    // Keep the fourth digit if present, as it resolves to 0.6
    // of a 1/100 sec resolution.  Two counts make one count in
    // the Xastir coordinate system.

    // Extend the digits to full precision by adding zeroes on
    // the end.
    strncat(n, "0000", sizeof(n) - 1 - strlen(n));

    // Get rid of the N/S character
    if (!isdigit((int)n[2]))
    {
      n[2] = '0';
    }
    if (!isdigit((int)n[3]))
    {
      n[3] = '0';
    }

    // Terminate substring at the correct digit
    n[4] = '\0';
//fprintf(stderr,"Lat: %s\n", n);

    // Add 0.5 (Poor man's rounding)
    centi_sec += (long)((atoi(n) * 0.6) + 0.5);

    if (       (char)toupper((int)copy[ 5])=='N'
               || (char)toupper((int)copy[ 6])=='N'
               || (char)toupper((int)copy[ 7])=='N'
               || (char)toupper((int)copy[ 8])=='N'
               || (char)toupper((int)copy[ 9])=='N'
               || (char)toupper((int)copy[10])=='N'
               || (char)toupper((int)copy[11])=='N')
    {
      centi_sec = -centi_sec;
    }

    centi_sec += 90*60*60*100;
  }
  return(centi_sec);
}

long xastir_convert_lon_s2l(char *lon)       /* W=0°, Ctr=180°, E=360° */
{
  long centi_sec;
  char copy[16];
  char n[16];
  char *p;
  char offset;


  // Find the decimal point if present
  p = strstr(lon, ".");

  if (p == NULL)  // No decimal point found
  {
    return(0l);
  }

  memset(copy, '\0', sizeof(copy));
  offset = p - lon;   // Arithmetic on pointers
  switch (offset)
  {
    case 0:     // .MM[MM]N
      return(0l); // Bad, no degrees or minutes
      break;
    case 1:     // M.MM[MM]N
      return(0l); // Bad, no degrees
      break;
    case 2:     // MM.MM[MM]N
      return(0l); // Bad, no degrees
      break;
    case 3:     // DMM.MM[MM]N
      snprintf(copy,
                      sizeof(copy),
                      "00%s",  // Add two leading zeroes
                      lon);
      break;
    case 4:     // DDMM.MM[MM]N
      snprintf(copy,
                      sizeof(copy),
                      "0%s",   // Add leading '0'
                      lon);
      break;
    case 5:     // DDDMM.MM[MM]N
      snprintf(copy,
                      sizeof(copy),
                      "%s",   // Copy verbatim
                      lon);
      break;
    default:
      break;
  }

  copy[15] = '\0';
  centi_sec=0l;
  if (copy[5]=='.'
      && (   (char)toupper((int)copy[ 6])=='W'
             || (char)toupper((int)copy[ 7])=='W'
             || (char)toupper((int)copy[ 8])=='W'
             || (char)toupper((int)copy[ 9])=='W'
             || (char)toupper((int)copy[10])=='W'
             || (char)toupper((int)copy[11])=='W'
             || (char)toupper((int)copy[12])=='W'
             || (char)toupper((int)copy[ 6])=='E'
             || (char)toupper((int)copy[ 7])=='E'
             || (char)toupper((int)copy[ 8])=='E'
             || (char)toupper((int)copy[ 9])=='E'
             || (char)toupper((int)copy[10])=='E'
             || (char)toupper((int)copy[11])=='E'
             || (char)toupper((int)copy[12])=='E'))
  {

    substr(n,copy,3);    // degrees 013
    centi_sec=atoi(n)*60*60*100;

    substr(n,copy+3,2);  // minutes 26
    centi_sec += atoi(n)*60*100;
    // 01326.66E  01326.660E

    substr(n,copy+6,4);  // fractional minutes 66E 660E or 6601
    // Keep the fourth digit if present, as it resolves to 0.6
    // of a 1/100 sec resolution.  Two counts make one count in
    // the Xastir coordinate system.

    // Extend the digits to full precision by adding zeroes on
    // the end.
    strncat(n, "0000", sizeof(n) - 1 - strlen(n));

    // Get rid of the E/W character
    if (!isdigit((int)n[2]))
    {
      n[2] = '0';
    }
    if (!isdigit((int)n[3]))
    {
      n[3] = '0';
    }

    n[4] = '\0';    // Make sure substring is terminated
//fprintf(stderr,"Lon: %s\n", n);

    // Add 0.5 (Poor man's rounding)
    centi_sec += (long)((atoi(n) * 0.6) + 0.5);

    if (       (char)toupper((int)copy[ 6])=='W'
               || (char)toupper((int)copy[ 7])=='W'
               || (char)toupper((int)copy[ 8])=='W'
               || (char)toupper((int)copy[ 9])=='W'
               || (char)toupper((int)copy[10])=='W'
               || (char)toupper((int)copy[11])=='W'
               || (char)toupper((int)copy[12])=='W')
    {
      centi_sec = -centi_sec;
    }

    centi_sec +=180*60*60*100;;
  }
  return(centi_sec);
}

void xastir_overlay_symbol(char symbol, char data, XastirPosition *fill)
{

  if ( data != '/' && data !='\\')    // Symbol overlay
  {

    if (data >= 'a' && data <= 'j')
    {
      // Found a compressed posit numerical overlay
      data = data - 'a'+'0';  // Convert to a digit
    }
    if ( (data >= '0' && data <= '9')
         || (data >= 'A' && data <= 'Z') )
    {
      // Found normal overlay character
      fill->aprs_symbol.aprs_type = '\\';
      fill->aprs_symbol.special_overlay = data;
    }
    else
    {
      // Bad overlay character.  Don't use it.  Insert the
      // normal alternate table character instead.
      fill->aprs_symbol.aprs_type = '\\';
      fill->aprs_symbol.special_overlay='\0';
    }
  }
  else      // No overlay character
  {
    fill->aprs_symbol.aprs_type = data;
    fill->aprs_symbol.special_overlay='\0';
  }
  fill->aprs_symbol.aprs_symbol = symbol;
}

/* DAO implementation by Tapio Sokura OH2KKU, 2007-11-15 (from db.c). */
int xastir_decode_dao (int *lat, int *lon, char *datumch, char *daocomment)
{
  char *searchval, *rval;
  size_t slen;

  // Loop around searching for !DAO!, return the first valid match.
  // The first '!' is found using strchr, the rest of the
  // string is validated more manually.
  searchval = daocomment;
  rval = strchr(searchval, '!');
  while (rval != NULL)
  {

    // Check the remaining string length so we don't
    // run past string end
    slen = strlen(rval);
    if (slen < 5)
    {
      break;
    }

    if (rval[4] == '!' && rval[1] >= '!' && rval[1] <= '{')
    {
      // found the !DAO! terminator and datum char is
      // within the allowable range

      if (rval[1] >= 'A' && rval[1] <= 'Z')
      {
        // looks like human readable format

        if (rval[2] == ' ' && rval[3] == ' ')
        {
          // only datum information present
          *datumch = rval[1];
          memmove(rval, rval + 5, slen - 4);
          return 1;

        }
        else if (rval[2] >= '0' && rval[2] <= '9' &&
                 rval[3] >= '0' && rval[3] <= '9')
        {
          // human readable format 0-9 lat/lon ok

          // ASCII - 48 = the integer digit we want.
          // Multiply by 10, because we only get
          // thousandths of a minute with human
          // readable format.
          *lat = ((int)rval[2] - 48) * 10;
          *lon = ((int)rval[3] - 48) * 10;
          *datumch = rval[1];
          memmove(rval, rval + 5, slen - 4);
          return 2;
        }
        // not ok for human readable format, continue searching

      }
      else if (rval[1] >= 'a' && rval[1] <= 'z')
      {
        // looks like base-91 format

        if (rval[2] == ' ' && rval[3] == ' ')
        {
          // only datum information present
          *datumch = rval[1];
          memmove(rval, rval + 5, slen - 4);
          return 1;

        }
        else if (rval[2] >= '!' && rval[2] <= '{' &&
                 rval[3] >= '!' && rval[3] <= '{')
        {
          // base-91 lat/lon ok
          unsigned int lats, lons;
          float latval, lonval;
          lats = rval[2] - 33; // get base91 values
          lons = rval[3] - 33;
          latval = lats / 91.0 * 100; // do proper scaling
          lonval = lons / 91.0 * 100;
          *lat = (int)(latval + 0.5); // round and store
          *lon = (int)(lonval + 0.5);
          *datumch = rval[1];
          memmove(rval, rval + 5, slen - 4);
          return 3;
        }
        // not ok for base91 format, continue searching
      }
      // Datum chars outside A-Z and a-z are not
      // handled (here at least).
    }

    // If we end up here, we didn't find a match.
    // Search for the next '!' char.
    searchval = rval + 1;
    rval = strchr(searchval, '!');
  }

  // No more string left to search and no match.
  return 0;
}

int xastir_extract_position(XastirPosition *p_station, char **info, int grid)
{
  int ok, dao_lat, dao_lon, dao_rval;
  char temp_lat[10+1];
  char temp_lon[11+1];
  char temp_grid[8+1] = {0};
  char *my_data;
  char dao_datumch;
  float gridlat;
  float gridlon;
  my_data = (*info);

  if (!grid)  // Not a grid
  {
    ok = (int)(strlen(my_data) >= 19);
    ok = (int)(ok && my_data[4]=='.' && my_data[14]=='.'
               && (toupper(my_data[7]) =='N' || toupper(my_data[7]) =='S')
               && (toupper(my_data[17])=='E' || toupper(my_data[17])=='W'));
    // errors found:  [4]: X   [7]: n s   [17]: w e
    if (ok)
    {
      ok =             is_num_chr(my_data[0]);           // 5230.31N/01316.88E>
      ok = (int)(ok && is_num_chr(my_data[1]));          // 0123456789012345678
      ok = (int)(ok && is_num_or_sp(my_data[2]));
      ok = (int)(ok && is_num_or_sp(my_data[3]));
      ok = (int)(ok && is_num_or_sp(my_data[5]));
      ok = (int)(ok && is_num_or_sp(my_data[6]));
      ok = (int)(ok && is_num_chr(my_data[9]));
      ok = (int)(ok && is_num_chr(my_data[10]));
      ok = (int)(ok && is_num_chr(my_data[11]));
      ok = (int)(ok && is_num_or_sp(my_data[12]));
      ok = (int)(ok && is_num_or_sp(my_data[13]));
      ok = (int)(ok && is_num_or_sp(my_data[15]));
      ok = (int)(ok && is_num_or_sp(my_data[16]));
    }

    if (ok)
    {
      xastir_overlay_symbol(my_data[18], my_data[8], p_station);
      p_station->pos_amb = 0;
      // spaces in latitude set position ambiguity, spaces in longitude do not matter
      // we will adjust the lat/long to the center of the rectangle of ambiguity
      if (my_data[2] == ' ')        // nearest degree
      {
        p_station->pos_amb = 4;
        my_data[2]  = my_data[12] = '3';
        my_data[3]  = my_data[5]  = my_data[6]  = '0';
        my_data[13] = my_data[15] = my_data[16] = '0';
      }
      else if (my_data[3] == ' ')   // nearest 10 minutes
      {
        p_station->pos_amb = 3;
        my_data[3]  = my_data[13] = '5';
        my_data[5]  = my_data[6]  = '0';
        my_data[15] = my_data[16] = '0';
      }
      else if (my_data[5] == ' ')   // nearest minute
      {
        p_station->pos_amb = 2;
        my_data[5]  = my_data[15] = '5';
        my_data[6]  = '0';
        my_data[16] = '0';
      }
      else if (my_data[6] == ' ')   // nearest 1/10th minute
      {
        p_station->pos_amb = 1;
        my_data[6]  = my_data[16] = '5';
      }

      snprintf(temp_lat,
                      sizeof(temp_lat),
                      "%s",
                      my_data);
      temp_lat[9] = toupper(my_data[7]);
      temp_lat[10] = '\0';

      snprintf(temp_lon,
                      sizeof(temp_lon),
                      "%s",
                      my_data+9);
      temp_lon[10] = toupper(my_data[17]);
      temp_lon[11] = '\0';

      // Check for !DAO!, beginning from the comment field.
      // Datum is not used for the time being.
      // Note: error/precision information (the white box on the map) is
      // not updated here, because changes to p_station->lat/lon_precision
      // are overridden in the calling function.
      dao_rval = xastir_decode_dao(&dao_lat, &dao_lon, &dao_datumch, my_data + 19);
      if (dao_rval == 2 || dao_rval == 3)
      {
        // 48 is the magic number to add to a single digit integer to
        // get the same digit in ASCII.
        temp_lat[7] = (char)(dao_lat / 10 + 48);
        temp_lat[8] = (char)(dao_lat % 10 + 48);
        temp_lon[8] = (char)(dao_lon / 10 + 48);
        temp_lon[9] = (char)(dao_lon % 10 + 48);
        // Signal that this is an accuracy-enhanced !DAO! position,
        // so the calling function can set the error boxes accordingly
        // (once somebody implements it).
        ok = dao_rval;
      }
      else
      {
        // no valid !DAO! _location_ found, pad with zeroes instead
        temp_lat[7] = '0';
        temp_lat[8] = '0';
        temp_lon[8] = '0';
        temp_lon[9] = '0';
      }

      // Callsign check here also checks SSID for an exact
      // match
      //            if (!is_my_call(p_station->call_sign,1)) {      // don't change my position, I know it better...
      /* Local-station policy belongs to the caller. */
      {

        p_station->coord_lat = xastir_convert_lat_s2l(temp_lat);   // ...in case of position ambiguity
        p_station->coord_lon = xastir_convert_lon_s2l(temp_lon);
      }

      (*info) += 19;                  // delete position from comment
    }
  }
  else   // It is a grid
  {
    // first sanity checks, need more
    ok = strlen(my_data) >= 4 && is_num_chr(my_data[2]);
    ok = (int)(ok && is_num_chr(my_data[3]));
    ok = (int)(ok && ((my_data[0]>='A')&&(my_data[0]<='R')));
    ok = (int)(ok && ((my_data[1]>='A')&&(my_data[1]<='R')));
    if (ok)
    {
      snprintf(temp_grid,
                      sizeof(temp_grid),
                      "%s",
                      my_data);
      // this test treats >6 digit grids as 4 digit grids; >6 are uncommon.
      // the spec mentioned 4 or 6, I'm not sure >6 is even allowed.
      if ( (temp_grid[6] != ']') || (temp_grid[4] == 0) || (temp_grid[5] == 0))
      {
        p_station->pos_amb = 6; // 1deg lat x 2deg lon
        temp_grid[4] = 'L';
        temp_grid[5] = 'L';
      }
      else
      {
        p_station->pos_amb = 5; // 2.5min lat x 5min lon
        temp_grid[4] = toupper(temp_grid[4]);
        temp_grid[5] = toupper(temp_grid[5]);
      }
      // These equations came from what I read in the qgrid source code and
      // various mailing list archives.
      gridlon= (20.*((float)temp_grid[0]-65.) + 2.*((float)temp_grid[2]-48.) + 5.*((float)temp_grid[4]-65.)/60.) - 180.;
      gridlat= (10.*((float)temp_grid[1]-65.) + ((float)temp_grid[3]-48.) + 5.*(temp_grid[5]-65.)/120.) - 90.;
      // could check for my callsign here, and avoid changing it...
      p_station->coord_lat = (unsigned long)(32400000l + (360000.0 * (-gridlat)));
      p_station->coord_lon = (unsigned long)(64800000l + (360000.0 * gridlon));
      p_station->aprs_symbol.aprs_type = '/';
      p_station->aprs_symbol.aprs_symbol = 'G';
    }        // is it valid grid or not - "ok"
    // could cut off the grid square from the comment here, but why bother?
  } // is it grid or not
  return(ok);
}

int xastir_extract_comp_position(XastirPosition *p_station, char **info)
{
  int ok;
  int x1, x2, x3, x4, y1, y2, y3, y4;
  int c = 0;
  int s = 0;
  int T = 0;
  int len;
  char *my_data;
  float lon = 0;
  float lat = 0;
  // We were extracting  the range from the posit, but never using it.
  // GCC 6.x whines
  //    float range;
  int skip = 0;
  char L;




  //fprintf(stderr,"xastir_extract_comp_position start: %s\n",*info);

  // compressed data format  /YYYYXXXX$csT  is a fixed 13-character field
  // used for ! / @ = data IDs
  //   /     Symbol Table ID or overlay: '/' '\' A-Z a-j
  //   YYYY  compressed latitude
  //   XXXX  compressed longitude
  //   $     Symbol Code
  //   cs    compressed
  //            course/speed
  //            radio range
  //            altitude
  //   T     compression type ID

  my_data = (*info);

  // Check leading char.  Must be one of these:
  // '/'
  // '\'
  // A-Z
  // a-j
  //
  L = my_data[0];
  if (   L == '/'
         || L == '\\'
         || ( L >= 'A' && L <= 'Z' )
         || ( L >= 'a' && L <= 'j' ) )
  {
    // We're good so far
  }
  else
  {
    // Note one of the symbol table or overlay characters, so
    // there's something funky about this packet.  It's not a
    // properly formatted compressed position.
    return(0);
  }

  //fprintf(stderr,"my_data: %s\n",my_data);

  // If c = space, csT bytes are ignored.  Minimum length:  8
  // bytes for lat/lon, 2 for symbol, 3 for csT for a total of 13.
  len = strlen(my_data);
  ok = (int)(len >= 13);

  if (ok)
  {
    y1 = (int)my_data[1] - '!';
    y2 = (int)my_data[2] - '!';
    y3 = (int)my_data[3] - '!';
    y4 = (int)my_data[4] - '!';
    x1 = (int)my_data[5] - '!';
    x2 = (int)my_data[6] - '!';
    x3 = (int)my_data[7] - '!';
    x4 = (int)my_data[8] - '!';

    // csT bytes
    if (my_data[10] == ' ') // Space
    {
      c = -1;  // This causes us to ignore csT
    }
    else
    {
      c = (int)my_data[10] - '!';
      s = (int)my_data[11] - '!';
      T = (int)my_data[12] - '!';
    }
    skip = 13;

    // Convert ' ' to '0'.  Not specified in APRS Reference!  Do
    // we need it?
    if (x1 == -1)
    {
      x1 = '\0';
    }
    if (x2 == -1)
    {
      x2 = '\0';
    }
    if (x3 == -1)
    {
      x3 = '\0';
    }
    if (x4 == -1)
    {
      x4 = '\0';
    }
    if (y1 == -1)
    {
      y1 = '\0';
    }
    if (y2 == -1)
    {
      y2 = '\0';
    }
    if (y3 == -1)
    {
      y3 = '\0';
    }
    if (y4 == -1)
    {
      y4 = '\0';
    }

    ok = (int)(ok && (x1 >= '\0' && x1 < 91));  //  /YYYYXXXX$csT
    ok = (int)(ok && (x2 >= '\0' && x2 < 91));  //  0123456789012
    ok = (int)(ok && (x3 >= '\0' && x3 < 91));
    ok = (int)(ok && (x4 >= '\0' && x4 < 91));
    ok = (int)(ok && (y1 >= '\0' && y1 < 91));
    ok = (int)(ok && (y2 >= '\0' && y2 < 91));
    ok = (int)(ok && (y3 >= '\0' && y3 < 91));
    ok = (int)(ok && (y4 >= '\0' && y4 < 91));

    T &= 0x3F;      // DK7IN: force Compression Byte to valid format
    // mask off upper two unused bits, they should be zero!?

    ok = (int)(ok && (c == -1 || ((c >=0 && c < 91) && (s >= 0 && s < 91) && (T >= 0 && T < 64))));

    if (ok)
    {
      lat = (((y1 * 91 + y2) * 91 + y3) * 91 + y4 ) / 380926.0; // in deg, 0:  90°N
      lon = (((x1 * 91 + x2) * 91 + x3) * 91 + x4 ) / 190463.0; // in deg, 0: 180°W
      lat *= 60 * 60 * 100;                       // in 1/100 sec
      lon *= 60 * 60 * 100;                       // in 1/100 sec

      // The below check should _not_ be done.  Compressed
      // format can resolve down to about 1 foot worldwide
      // (0.3 meters).
      //if ((((long)(lat+4) % 60) > 8) || (((long)(lon+4) % 60) > 8))
      //    ok = 0;   // check max resolution 0.01 min to
      // catch even more errors
    }
  }

  if (ok)
  {
    xastir_overlay_symbol(my_data[9], my_data[0], p_station);      // Symbol / Table

    // Callsign check here includes checking SSID for an exact
    // match
    //        if (!is_my_call(p_station->call_sign,1)) {  // don't change my position, I know it better...
    /* Local-station policy belongs to the caller. */
    {

      // Record the uncompressed lat/long that we just
      // computed.
      p_station->coord_lat = (long)((lat));               // in 1/100 sec
      p_station->coord_lon = (long)((lon));               // in 1/100 sec
    }

    if (c >= 0)                                     // ignore csT if c = ' '
    {
      if (c < 90)     // Found course/speed or altitude bytes
      {
        if ((T & 0x18) == 0x10)     // check for GGA (with altitude)
        {
          snprintf(p_station->altitude, sizeof(p_station->altitude), "%06.0f",pow(1.002,(double)(c*91+s))*0.3048);
        }
        else   // Found compressed course/speed bytes
        {

          // Convert 0 degrees to 360 degrees so that
          // Xastir will see it as a valid course and do
          // dead-reckoning properly on this station
          if (c == 0)
          {
            c = 90;
          }

          // Compute course in degrees
          snprintf(p_station->course,
                          sizeof(p_station->course),
                          "%03d",
                          c*4);

          // Compute speed in knots
          snprintf(p_station->speed,
                          sizeof(p_station->speed),
                          "%03.0f",
                          pow( 1.08,(double)s ) - 1.0);

          //fprintf(stderr,"Decoded speed:%s, course:%s\n",p_station->speed,p_station->course);

        }
      }
      else      // Found pre-calculated radio range bytes
      {
        if (c == 90)
        {
          // pre-calculated radio range
          // Commented out to silence GCC 6.x warning about
          // "set but unused" variables.
          // range = 2 * pow(1.08,(double)s);    // miles

          // DK7IN: dirty hack...  but better than nothing
          if (s <= 5)                         // 2.9387 mi
          {
            snprintf(p_station->power_gain, sizeof(p_station->power_gain), "PHG%s0", "000");
          }
          else if (s <= 17)                   // 7.40 mi
          {
            snprintf(p_station->power_gain, sizeof(p_station->power_gain), "PHG%s0", "111");
          }
          else if (s <= 36)                   // 31.936 mi
          {
            snprintf(p_station->power_gain, sizeof(p_station->power_gain), "PHG%s0", "222");
          }
          else if (s <= 75)                   // 642.41 mi
          {
            snprintf(p_station->power_gain, sizeof(p_station->power_gain), "PHG%s0", "333");
          }
          else                       // max 90:  2037.8 mi
          {
            snprintf(p_station->power_gain, sizeof(p_station->power_gain), "PHG%s0", "444");
          }
        }
      }
    }
    (*info) += skip;    // delete position from comment
  }



  //fprintf(stderr,"  xastir_extract_comp_position end: %s\n",*info);

  return(ok);
}
