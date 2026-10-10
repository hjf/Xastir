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

/* Mic-E expansion extracted from db.c. UI notification belongs to callers. */
#include "aprs_position.h"
#include <stdio.h>
#include <string.h>

int xastir_expand_mice(const char *path, const char *info, char *expanded, size_t capacity, int *emergency)
{
  int  ii;
  int  offset;
  unsigned char s_b1;
  unsigned char s_b2;
  unsigned char s_b3;
  unsigned char s_b4;
  unsigned char s_b5;
  unsigned char s_b6;
  // unsigned char s_b7;
  int  north,west,long_offset;
  int  d,m,h;
  char temp[513];     // Note: Must be big in case we get long concatenated packets
  char new_info[513]; // Note: Must be big in case we get long concatenated packets
  int  course;
  int  speed;
  int  msg1,msg2,msg3,msg;
  int  info_size;
  long alt;
  int  msgtyp;
  char rig_type[10];


  if (!path || !info || !expanded || !emergency || capacity < 1) return 0;
  *emergency = 0;
  expanded[0] = 0;
  if (strlen(info) < 8 || strlen(info) > 512 || strcspn(path, "-,") != 6) return 0;
  for (ii = 0; ii < 6; ++ii) {
    char c = path[ii];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'L')
          || (c >= 'P' && c <= 'Z'))) return 0;
  }
  for (ii = 0; ii < 6; ++ii) if ((unsigned char)info[ii] < 28 || (unsigned char)info[ii] > 127) return 0;

  // MIC-E Data Format   [APRS Reference, chapter 10]

  // todo:  error check
  //        drop wrong positions from receive errors...
  //        drop 0N/0E position (p.25)

  /* First 7 bytes of info[] contains the APRS data type ID,    */
  /* longitude, speed, course.                    */
  /* The 6-byte destination field of path[] contains latitude,    */
  /* N/S bit, E/W bit, longitude offset, message code.        */
  /*

    MIC-E Destination Field Format:
    -------------------------------
    Ar1DDDD0 Br1DDDD0 Cr1MMMM0 Nr1MMMM0 Lr1HHHH0 Wr1HHHH0 CrrSSID0
    D = Latitude Degrees.
    M = Latitude Minutes.
    H = Latitude Hundredths of Minutes.
    ABC = Message bits, complemented.
    N = N/S latitude bit (N=1).
    W = E/W longitude bit (W=1).
    L = 100's of longitude degrees (L=1 means add 100 degrees to longitude
    in the Info field).
    C = Command/Response flag (see AX.25 specification).
    r = reserved for future use (currently 0).

  */
  /****************************************************************************
   * I still don't handle:                                                     *
   *    Custom message bits                                                    *
   *    SSID special routing                                                   *
   *    Beta versions of the MIC-E (which use a slightly different format).    *
   *                                                                           *
   * DK7IN : lat/long with custom msg works, altitude/course/speed works       *
   *****************************************************************************/



  // Note that the first MIC-E character was not passed to us, so we're
  // starting just past it.
  // Check for valid symbol table character.  Should be '/' or '\'
  // or 0-9, A-Z.
  //
  if (        info[7] == '/'                          // Primary table
              ||  info[7] == '\\'                         // Alternate table
              || (info[7] >= '0' && info[7] <= '9')       // Overlay char
              || (info[7] >= 'A' && info[7] <= 'Z') )     // Overlay char
  {

    // We're good, keep going

  }
  else   // Symbol table or overlay char incorrect
  {

    if (info[6] == '/' || info[6] == '\\')      // Found it back one char in string
    {
      // Don't print out the full info string here because it
      // can contain unprintable characters.  In fact, we
      // should check the chars we do print out to make sure
      // they're printable, else print a space char.
    
    }

    return(0);  // No good, not MIC-E format or corrupted packet.  Return 1
    // so that it won't get added to the database at all.
  }

  // Check for valid symbol.  Should be between '!' and '~' only.
  if (info[6] < '!' || info[6] > '~')
  {
  

    return(0);  // No good, not MIC-E format or corrupted packet.  Return 1
    // so that it won't get added to the database at all.
  }

  // Check for minimum MIC-E size.
  if (strlen(info) < 8)
  {
  

    return(0);  // No good, not MIC-E format or corrupted packet.  Return 1
    // so that it won't get added to the database at all.
  }

  // Check for 8-bit characters in the first eight slots.  Not
  // allowed per Mic-E chapter of the spec.
  for (ii = 0; ii < 8; ii++)
  {
    if ((unsigned char)info[ii] > 0x7f)
    {
      // 8-bit data was found in the lat/long/course/speed
      // portion.  Bad packet.  Drop it.
      //fprintf(stderr, "%s: 8-bits found in Mic-E packet initial portion. Dropping it.\n", call_sign);
      return(0);
    }
  }

  // Check whether we have more data.  If flag character is 0x1d
  // (8-bit telemetry flag) then don't do the 8-bit check below.
  if (strlen(info) > 8)
  {

    // Check for the 8-bit telemetry flag
    if ((unsigned char)info[8] == 0x1d)
    {
      // 8-bit telemetry found, skip the check loop below
    }
    else    // 8-bit telemetry flag was not found.  Check that
    {
      // we only have 7-bit characters through the rest of
      // the packet.

      for (ii = 8; ii < (int)strlen(info); ii++)
      {

        if ((unsigned char)info[ii] > 0x7f)
        {
          // 8-bit data was found.  Bad packet.  Drop it.
          //fprintf(stderr, "%s: 8-bits found in Mic-E packet final portion (not 8-bit telemetry). Dropping it.\n", call_sign);
          return(0);
        }
      }
    }
  }

  //fprintf(stderr,"Path1:%s\n",path);

  msg1 = (int)( ((unsigned char)path[0] & 0x40) >>4 );
  msg2 = (int)( ((unsigned char)path[1] & 0x40) >>5 );
  msg3 = (int)( ((unsigned char)path[2] & 0x40) >>6 );
  msg = msg1 | msg2 | msg3;   // We now have the complemented message number in one variable
  msg = msg ^ 0x07;           // And this is now the normal message number
  msgtyp = 0;                 // DK7IN: Std message, I have to add custom msg decoding

  //fprintf(stderr,"Msg: %d\n",msg);

  /* Snag the latitude from the destination field, Assume TAPR-2 */
  /* DK7IN: latitude now works with custom message */
  s_b1 = (unsigned char)( (path[0] & 0x0f) + (char)0x2f );
  //fprintf(stderr,"path0:%c\ts_b1:%c\n",path[0],s_b1);
  if (path[0] & 0x10)     // A-J
  {
    s_b1 += (unsigned char)1;
  }

  if (s_b1 > (unsigned char)0x39)        // K,L,Z
  {
    s_b1 = (unsigned char)0x20;
  }
  //fprintf(stderr,"s_b1:%c\n",s_b1);

  s_b2 = (unsigned char)( (path[1] & 0x0f) + (char)0x2f );
  //fprintf(stderr,"path1:%c\ts_b2:%c\n",path[1],s_b2);
  if (path[1] & 0x10)     // A-J
  {
    s_b2 += (unsigned char)1;
  }

  if (s_b2 > (unsigned char)0x39)        // K,L,Z
  {
    s_b2 = (unsigned char)0x20;
  }
  //fprintf(stderr,"s_b2:%c\n",s_b2);

  s_b3 = (unsigned char)( (path[2] & (char)0x0f) + (char)0x2f );
  //fprintf(stderr,"path2:%c\ts_b3:%c\n",path[2],s_b3);
  if (path[2] & 0x10)     // A-J
  {
    s_b3 += (unsigned char)1;
  }

  if (s_b3 > (unsigned char)0x39)        // K,L,Z
  {
    s_b3 = (unsigned char)0x20;
  }
  //fprintf(stderr,"s_b3:%c\n",s_b3);

  s_b4 = (unsigned char)( (path[3] & 0x0f) + (char)0x30 );
  //fprintf(stderr,"path3:%c\ts_b4:%c\n",path[3],s_b4);
  if (s_b4 > (unsigned char)0x39)        // L,Z
  {
    s_b4 = (unsigned char)0x20;
  }
  //fprintf(stderr,"s_b4:%c\n",s_b4);

  s_b5 = (unsigned char)( (path[4] & 0x0f) + (char)0x30 );
  //fprintf(stderr,"path4:%c\ts_b5:%c\n",path[4],s_b5);
  if (s_b5 > (unsigned char)0x39)        // L,Z
  {
    s_b5 = (unsigned char)0x20;
  }
  //fprintf(stderr,"s_b5:%c\n",s_b5);

  s_b6 = (unsigned char)( (path[5] & 0x0f) + (char)0x30 );
  //fprintf(stderr,"path5:%c\ts_b6:%c\n",path[5],s_b6);
  if (s_b6 > (unsigned char)0x39)        // L,Z
  {
    s_b6 = (unsigned char)0x20;
  }
  //fprintf(stderr,"s_b6:%c\n",s_b6);

  // s_b7 =  (unsigned char)path[6];        // SSID, not used here
  //fprintf(stderr,"path6:%c\ts_b7:%c\n",path[6],s_b7);

  //fprintf(stderr,"\n");

  // Special tests for 'L' due to position ambiguity deviances in
  // the APRS spec table.  'L' has the 0x40 bit set, but they
  // chose in the spec to have that represent position ambiguity
  // _without_ the North/West/Long Offset bit being set.  Yuk!
  // Please also note that the tapr.org Mic-E document (not the
  // APRS spec) has the state of the bit wrong in columns 2 and 3
  // of their table.  Reverse them.
  if (path[3] == 'L')
  {
    north = 0;
  }
  else
  {
    north = (int)((path[3] & 0x40) == (char)0x40);  // N/S Lat Indicator
  }

  if (path[4] == 'L')
  {
    long_offset = 0;
  }
  else
  {
    long_offset = (int)((path[4] & 0x40) == (char)0x40);  // Longitude Offset
  }

  if (path[5] == 'L')
  {
    west = 0;
  }
  else
  {
    west = (int)((path[5] & 0x40) == (char)0x40);  // W/E Long Indicator
  }

  //fprintf(stderr,"north:%c->%d\tlat:%c->%d\twest:%c->%d\n",path[3],north,path[4],long_offset,path[5],west);

  /* Put the latitude string into the temp variable */
  snprintf(temp, sizeof(temp), "%c%c%c%c.%c%c%c%c",s_b1,s_b2,s_b3,s_b4,s_b5,s_b6,
                  (north ? 'N': 'S'), info[7]);   // info[7] = symbol table

  /* Compute degrees longitude */
  snprintf(new_info,
                  sizeof(new_info),
                  "%s",
                  temp);
  d = (int) info[0]-28;

  if (long_offset)
  {
    d += 100;
  }

  if ((180<=d)&&(d<=189))  // ??
  {
    d -= 80;
  }

  if ((190<=d)&&(d<=199))  // ??
  {
    d -= 190;
  }

  /* Compute minutes longitude */
  m = (int) info[1]-28;
  if (m>=60)
  {
    m -= 60;
  }

  /* Compute hundredths of minutes longitude */
  h = (int) info[2]-28;
  /* Add the longitude string into the temp variable */
  snprintf(temp, sizeof(temp), "%03d%02d.%02d%c%c",d,m,h,(west ? 'W': 'E'), info[6]);
  strncat(new_info,
          temp,
          sizeof(new_info) - 1 - strlen(new_info));

  /* Compute speed in knots */
  speed = (int)( ( info[3] - (char)28 ) * (char)10 );
  speed += ( (int)( (info[4] - (char)28) / (char)10) );
  if (speed >= 800)
  {
    speed -= 800;  // in knots
  }

  /* Compute course */
  course = (int)( ( ( (info[4] - (char)28) % 10) * (char)100) + (info[5] - (char)28) );
  if (course >= 400)
  {
    course -= 400;
  }

  /*  ???
      fprintf(stderr,"info[4]-28 mod 10 - 4 = %d\n",( ( (int)info[4]) - 28) % 10 - 4);
      fprintf(stderr,"info[5]-28 = %d\n", ( (int)info[5]) - 28 );
  */
  snprintf(temp, sizeof(temp), "%03d/%03d",course,speed);
  strncat(new_info,
          temp,
          sizeof(new_info) - 1 - strlen(new_info));
  offset = 8;   // start of rest of info

  /* search for rig type in Mic-E data */
  rig_type[0] = '\0';
  if (info[offset] != '\0' && (info[offset] == '>' || info[offset] == ']'))
  {
    /* detected type code:     > TH-D7    ] TM-D700 */
    if (info[offset] == '>')
      snprintf(rig_type,
                      sizeof(rig_type),
                      " TH-D7");
    else
      snprintf(rig_type,
                      sizeof(rig_type),
                      " TM-D700");

    offset++;
  }

  info_size = (int)strlen(info);
  /* search for compressed altitude in Mic-E data */  // {
  if (info_size >= offset+4 && info[offset+3] == '}')    // {
  {
    /* detected altitude  ___} */
    alt = ((((long)info[offset] - (long)33) * (long)91 +(long)info[offset+1] - (long)33) * (long)91
           + (long)info[offset+2] - (long)33) - 10000;  // altitude in meters
    alt /= 0.3048;                                // altitude in feet, as in normal APRS

    //32808 is -10000 meters, or 10 km (deepest ocean), which is as low as a MIC-E
    //packet may go.  Upper limit is mostly a guess.
    if ( (alt > 500000) || (alt < -32809) )    // Altitude is whacko.  Skip it.
    {
    
      offset += 4;
    }
    else    // Altitude is ok
    {
      snprintf(temp, sizeof(temp), " /A=%06ld",alt);
      offset += 4;
      strncat(new_info,
              temp,
              sizeof(new_info) - 1 - strlen(new_info));
    }
  }

  /* start of comment */
  if (strlen(rig_type) > 0)
  {
    snprintf(temp, sizeof(temp), "%s",rig_type);
    strncat(new_info,
            temp,
            sizeof(new_info) - 1 - strlen(new_info));
  }

  strncat(new_info,
          " Mic-E ",
          sizeof(new_info) - 1 - strlen(new_info));
  if (msgtyp == 0)
  {
    switch (msg)
    {
      case 1:
        strncat(new_info,
                "Enroute",
                sizeof(new_info) - 1 - strlen(new_info));
        break;

      case 2:
        strncat(new_info,
                "In Service",
                sizeof(new_info) - 1 - strlen(new_info));
        break;

      case 3:
        strncat(new_info,
                "Returning",
                sizeof(new_info) - 1 - strlen(new_info));
        break;

      case 4:
        strncat(new_info,
                "Committed",
                sizeof(new_info) - 1 - strlen(new_info));
        break;

      case 5:
        strncat(new_info,
                "Special",
                sizeof(new_info) - 1 - strlen(new_info));
        break;

      case 6:
        strncat(new_info,
                "Priority",
                sizeof(new_info) - 1 - strlen(new_info));
        break;

      case 7:
        strncat(new_info,
                "Emergency",
                sizeof(new_info) - 1 - strlen(new_info));

        // Do a popup to alert the operator to this
        // condition.  Make sure we haven't popped up an
        // emergency message for this station within the
        // last 30 minutes.  If we pop these up constantly
        // it gets quite annoying.
        // EMERGENCY

        *emergency = 1;

        break;

      default:
        strncat(new_info,
                "Off Duty",
                sizeof(new_info) - 1 - strlen(new_info));
    }
  }
  else
  {
    snprintf(temp, sizeof(temp), "Custom%d",msg);
    strncat(new_info,
            temp,
            sizeof(new_info) - 1 - strlen(new_info));
  }

  if (info[offset] != '\0')
  {
    /* Append the rest of the message to the expanded MIC-E message */
    for (ii=offset; ii<info_size; ii++)
    {
      temp[ii-offset] = info[ii];
    }

    temp[info_size-offset] = '\0';
    strncat(new_info,
            " ",
            sizeof(new_info) - 1 - strlen(new_info));
    strncat(new_info,
            temp,
            sizeof(new_info) - 1 - strlen(new_info));
  }



  // We don't transmit Mic-E protocol from Xastir, so we know it's
  // not our station's packets or our object/item packets,
  // therefore the last two parameters here are both zero.
  //
  if (strlen(new_info) >= capacity) return 0;
  memcpy(expanded, new_info, strlen(new_info) + 1);
  return 1;
}
