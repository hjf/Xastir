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

/* Station storage and ancillary record ownership extracted from db.c. */
#include "station_db.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
/* Case-insensitive comparison and trimming are pure string operations. */
static int compare_ascii_case(const char *a, const char *b)
{
  while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
  return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
static void remove_trailing_spaces(char *text)
{
  size_t n = strlen(text);
  while (n && text[n-1] == ' ') text[--n] = 0;
}
static void remove_leading_spaces(char *text)
{
  size_t n = strspn(text, " ");
  memmove(text, text+n, strlen(text+n)+1);
}
/* Existing comment allocation paths must report failure without terminating the app. */
void xastir_init_station(XastirStationDb *db, DataRow *p_station)
{
  // the list pointers should already be set
  p_station->oldest_trackpoint  = NULL;         // no trail
  p_station->newest_trackpoint  = NULL;         // no trail
  p_station->trail_color        = 0;
  p_station->weather_data       = NULL;         // no weather
  p_station->coord_lat          = 0l;           //  90°N  \ undefined
  p_station->coord_lon          = 0l;           // 180°W  / position
  p_station->pos_amb            = 0;            // No ambiguity
  p_station->error_ellipse_radius = 600;        // In cm, default 6 meters
  p_station->lat_precision      = 60;           // In 100ths of seconds latitude (60 = 0.01 minutes)
  p_station->lon_precision      = 60;           // In 100ths of seconds longitude (60 = 0.01 minutes)
  p_station->call_sign[0]       = '\0';         // ?????
  p_station->tactical_call_sign = NULL;
  p_station->sec_heard          = 0;
  p_station->time_sn            = 0;
  p_station->flag               = 0;            // set all flags to inactive
  p_station->object_retransmit  = -1;           // transmit forever
  p_station->last_transmit_time = db->now;    // Used for object/item decaying algorithm
  p_station->transmit_time_increment = 0;       // Used in data_add()
  //    p_station->last_modified_time = 0;            // Used for object/item dead-reckoning
  p_station->record_type        = '\0';
  p_station->data_via           = '\0';         // L local, T TNC, I internet, F file
  p_station->heard_via_tnc_port = 0;
  p_station->heard_via_tnc_last_time = 0;
  p_station->last_port_heard    = 0;
  p_station->num_packets        = 0;
  p_station->aprs_symbol.aprs_type = '\0';
  p_station->aprs_symbol.aprs_symbol = '\0';
  p_station->aprs_symbol.special_overlay = '\0';
  p_station->aprs_symbol.area_object.type           = AREA_NONE;
  p_station->aprs_symbol.area_object.color          = AREA_GRAY_LO;
  p_station->aprs_symbol.area_object.sqrt_lat_off   = 0;
  p_station->aprs_symbol.area_object.sqrt_lon_off   = 0;
  p_station->aprs_symbol.area_object.corridor_width = 0;
  //    p_station->station_time_type  = '\0';
  p_station->origin[0]          = '\0';        // no object
  p_station->packet_time[0]     = '\0';
  p_station->node_path_ptr      = NULL;
  p_station->pos_time[0]        = '\0';
  //    p_station->altitude_time[0]   = '\0';
  p_station->altitude[0]        = '\0';
  //    p_station->speed_time[0]      = '\0';
  p_station->speed[0]           = '\0';
  p_station->course[0]          = '\0';
  p_station->bearing[0]         = '\0';
  p_station->NRQ[0]             = '\0';
  p_station->power_gain[0]      = '\0';
  p_station->signal_gain[0]     = '\0';
  p_station->signpost[0]        = '\0';
  p_station->probability_min[0] = '\0';
  p_station->probability_max[0] = '\0';
  //    p_station->station_time[0]    = '\0';
  p_station->sats_visible[0]    = '\0';
  p_station->status_data        = NULL;
  p_station->comment_data       = NULL;
  p_station->df_color           = -1;

  // Show that there are no other points associated with this
  // station. We could also zero all the entries of the
  // multipoints[][] array, but nobody should be looking there
  // unless this is non-zero.
  // KG4NBB

  p_station->num_multipoints = 0;
  p_station->multipoint_data = NULL;
}

void xastir_remove_name(XastirStationDb *db, DataRow *p_rem)        // todo: return pointer to next element
{
  int update_shortcuts = 0;
  int hash_key;   // We use a 14-bit hash key


  // Do a quick check to see if we're removing a station record
  // that is pointed to by our pointer shortcuts array.
  // If so, update our pointer shortcuts after we're done.
  //
  // We create the hash key out of the lower 7 bits of the first
  // two characters, creating a 14-bit key (1 of 16384)
  //
  hash_key = (int)((p_rem->call_sign[0] & 0x7f) << 7);
  hash_key = hash_key | (int)(p_rem->call_sign[1] & 0x7f);

  if (db->shortcuts[hash_key] == p_rem)
  {
    // Yes, we're trying to remove a record that a hash key
    // directly points to.  We'll need to redo that hash key
    // after we remove the record.
    update_shortcuts++;
  }


  // Proceed to the station record removal
  //
  if (p_rem->n_prev == NULL)   // Appears to be first element in list
  {

    if (db->first == p_rem)    // Yes, head of list
    {

      // Make list head point to 2nd element in list (or NULL)
      // so that we can delete the current record.
      db->first = p_rem->n_next;
    }
    else    // No, not first element in list.  Problem!  The
    {
      // list pointers are inconsistent for some reason.
      // The chain has been broken and we have dangling
      // pointers.

      fprintf(stderr,
              "xastir_remove_name(db, ): ERROR: p->n_prev == NULL but p != db->first\n");

      abort();    // Cause a core dump at this point
      // Perhaps we could do some repair to the list pointers here?  Start
      // at the other end of the chain and navigate back to this end, then
      // fix up db->first to point to it?  This is at the risk of a memory
      // leak, but at least Xastir might continue to run.

    }
  }
  else    // Not the first element in the list.  Fix up pointers
  {
    // to skip the current record.
    p_rem->n_prev->n_next = p_rem->n_next;
  }


  if (p_rem->n_next == NULL)   // Appears to be last element in list
  {

    if (db->last == p_rem)     // Yes, tail of list
    {

      // Make list tail point to previous element in list (or
      // NULL) so that we can delete the current record.
      db->last = p_rem->n_prev;
    }
    else    // No, not last element in list.  Problem!  The list
    {
      // pointers are inconsistent for some reason.  The
      // chain has been broken and we have dangling
      // pointers.

      fprintf(stderr,
              "xastir_remove_name(db, ): ERROR: p->n_next == NULL but p != db->last\n");

      abort();    // Cause a core dump at this point
      // Perhaps we could do some repair to the list pointers here?  Start
      // at the other end of the chain and navigate back to this end, then
      // fix up db->last to point to it?  This is at the risk of a memory
      // leak, but at least Xastir might continue to run.

    }
  }
  else    // Not the last element in the list.  Fix up pointers to
  {
    // skip the current record.
    p_rem->n_next->n_prev = p_rem->n_prev;
  }


  // Update our pointer shortcuts.  Pass the removed hash_key to
  // the function so that we can try to redo just that hash_key
  // pointer.
  if (update_shortcuts)
  {
    //fprintf(stderr,"\t\t\t\t\t\tRemoval of hash key: %i\n", hash_key);

    // The -1 tells the function to redo all of the hash table
    // pointers because we deleted one of them.  Later we could
    // optimize this so that only the specific pointer is fixed
    // up.
    xastir_station_shortcuts_update_function(db, -1, NULL);
  }
}

void xastir_remove_time(XastirStationDb *db, DataRow *p_rem)        // todo: return pointer to next element
{

  if (p_rem->t_older == NULL)   // Appears to be first element in list
  {

    if (db->oldest == p_rem)    // Yes, head of list (oldest)
    {

      // Make oldest list head point to 2nd element in list (or NULL)
      // so that we can delete the current record.
      db->oldest = p_rem->t_newer;
    }
    else    // No, not first (oldest) element in list.  Problem!
    {
      // The list pointers are inconsistent for some
      // reason.  The chain has been broken and we have
      // dangling pointers.

      fprintf(stderr,
              "xastir_remove_time(db, ): ERROR: p->t_older == NULL but p != db->oldest\n");

      abort();    // Cause a core dump at this point
      // Perhaps we could do some repair to the list pointers here?  Start
      // at the other end of the chain and navigate back to this end, then
      // fix up db->oldest to point to it?  This is at the risk of a memory
      // leak, but at least Xastir might continue to run.

    }
  }
  else    // Not the first (oldest) element in the list.  Fix up
  {
    // pointers to skip the current record.
    p_rem->t_older->t_newer = p_rem->t_newer;
  }


  if (p_rem->t_newer == NULL)   // Appears to be last (newest) element in list
  {

    if (db->newest == p_rem)     // Yes, head of list (newest)
    {

      // Make newest list head point to previous element in
      // list (or NULL) so that we can delete the current
      // record.
      db->newest = p_rem->t_older;
    }
    else    // No, not newest element in list.  Problem!  The
    {
      // list pointers are inconsistent for some reason.
      // The chain has been broken and we have dangling
      // pointers.

      fprintf(stderr,
              "xastir_remove_time(db, ): ERROR: p->t_newer == NULL but p != db->newest\n");

      abort();    // Cause a core dump at this point
      // Perhaps we could do some repair to the list pointers here?  Start
      // at the other end of the chain and navigate back to this end, then
      // fix up db->newest to point to it?  This is at the risk of a memory
      // leak, but at least Xastir might continue to run.

    }
  }
  else    // Not the newest element in the list.  Fix up pointers
  {
    // to skip the current record.
    p_rem->t_newer->t_older = p_rem->t_older;
  }
}

void xastir_insert_name(XastirStationDb *db, DataRow *p_new, DataRow *p_name)
{

  // Set up pointer to next record (or NULL), sorted by name
  p_new->n_next = p_name;

  if (p_name == NULL)         // Add to end of list
  {

    p_new->n_prev = db->last;

    if (db->last == NULL)     // If we have an empty list
    {
      db->first = p_new;  // Add it to the head of the list
    }

    else    // List wasn't empty, add to the end of the list.
    {
      db->last->n_next = p_new;
    }

    db->last = p_new;
  }

  else    // Insert new record ahead of p_name record
  {

    p_new->n_prev = p_name->n_prev;

    if (p_name->n_prev == NULL)     // add to begin of list
    {
      db->first = p_new;
    }
    else
    {
      p_name->n_prev->n_next = p_new;
    }

    p_name->n_prev = p_new;
  }
}

void xastir_insert_time(XastirStationDb *db, DataRow *p_new, DataRow *p_time)
{

  // Set up pointer to next record (or NULL), sorted by time
  p_new->t_newer = p_time;

  if (p_time == NULL)                 // add to end of list (becomes newest station)
  {

    p_new->t_older = db->newest;         // connect to previous end of list

    if (db->newest == NULL)             // if list empty, create list
    {
      db->oldest = p_new;  // it's now our only station on the list
    }
    else
    {
      db->newest->t_newer = p_new;  // list not empty, link original last record to our new one
    }

    db->newest = p_new;                 // end of list (newest record pointer) points to our new record
  }

  else                              // Else we're inserting into the middle of the list somewhere
  {

    p_new->t_older = p_time->t_older;

    if (p_time->t_older == NULL)     // add to end of list (new record becomes oldest station)
    {
      db->oldest = p_new;
    }
    else
    {
      p_time->t_older->t_newer = p_new;  // else
    }

    p_time->t_older = p_new;
  }
}

void xastir_delete_station_memory(XastirStationDb *db, DataRow *p_del)
{
  if (p_del == NULL)
  {
    return;
  }
  xastir_remove_name(db, p_del);
  xastir_remove_time(db, p_del);
  free(p_del);
  db->count--;
}

DataRow *xastir_insert_new_station(XastirStationDb *db, DataRow *p_name, DataRow *p_time)
{
  DataRow *p_new;

  p_new = (DataRow *)calloc(1, sizeof(DataRow));

  if (p_new != NULL)                  // we really got the memory
  {
    xastir_insert_name(db, p_new,p_name);      // insert element into name ordered list
    xastir_insert_time(db, p_new,p_time);      // insert element into time ordered list
  }
  else    // p_new == NULL
  {
    fprintf(stderr,"ERROR: we got no memory for station storage\n");
  }

  return(p_new);                      // return pointer to new element
}

DataRow *xastir_add_new_station(XastirStationDb *db, DataRow *p_name, DataRow *p_time, char *call)
{
  DataRow *p_new;
  int hash_key;   // We use a 14-bit hash key



  if (call[0] == '\0' || strlen(call) >= sizeof(p_new->call_sign))
  {
    // Do nothing.  No update needed.  Callsign is empty.
    return(NULL);
  }

  //fprintf(stderr,"Adding new station: %s\n",call);

  p_new = xastir_insert_new_station(db, p_name,p_time);  // allocate memory

  if (p_new == NULL)
  {
    // Couldn't allocate space for the station
    return(NULL);
  }

  xastir_init_station(db, p_new);                    // initialize new station record
  snprintf(p_new->call_sign,
                  sizeof(p_new->call_sign),
                  "%s",
                  call);
  db->count++;

  // Do some quick checks to see if we just inserted a new hash
  // key or inserted at the beginning of a hash key (making the
  // old pointer incorrect).  If so, update our pointers to match.

  // We create the hash key out of the lower 7 bits of the first
  // two characters, creating a 14-bit key (1 of 16384)
  //
  hash_key = (int)((call[0] & 0x7f) << 7);
  hash_key = hash_key | (int)(call[1] & 0x7f);

  if (db->shortcuts[hash_key] == NULL)
  {
    // New hash key entry point found.  Fill in the pointer.
    //fprintf(stderr,"New hash key: %i, call: %s\n",
    //    hash_key,
    //    call);

    xastir_station_shortcuts_update_function(db, hash_key, p_new);
  }
  else if (p_new->n_prev == NULL)
  {
    // We just inserted at the beginning of the list.  Assume
    // that we inserted at the beginning of our hash_key
    // segment.
    //fprintf(stderr,"Start of list hash_key: %i, call: %s\n",
    //    hash_key,
    //    call);

    xastir_station_shortcuts_update_function(db, hash_key, p_new);
  }
  else
  {
    // Check whether either of the first two chars of the new
    // callsign and the previous callsign are different.  If so,
    // we need to update the hash table entry for our new record
    // 'cuz we're at the start of a new hash table entry.
    if (p_new->n_prev->call_sign[0] != call[0]
        || p_new->n_prev->call_sign[1] != call[1])
    {
      //fprintf(stderr,"Hash segment start: %i, call: %s\n",
      //    hash_key,
      //    call);

      xastir_station_shortcuts_update_function(db, hash_key, p_new);
    }
  }

  //if (p_new->n_prev != NULL) {
  //    fprintf(stderr,"\tprev: %s",
  //        p_new->n_prev->call_sign);
  //}

  //if (p_new->n_next != NULL) {
  //    fprintf(stderr,"\t\tnext: %s",
  //        p_new->n_next->call_sign);
  //}
  //
  //fprintf(stderr,"\n");

  return(p_new);                      // return pointer to new element
}

void xastir_move_station_time(XastirStationDb *db, DataRow *p_curr, DataRow *p_time)
{

  if (p_curr != NULL)                 // need a valid record
  {
    xastir_remove_time(db, p_curr);
    xastir_insert_time(db, p_curr,p_time);
  }
}

void xastir_move_station_name(XastirStationDb *db, DataRow *p_curr, DataRow *p_name)
{

  if (p_curr != NULL)                 // need a valid record
  {
    xastir_remove_name(db, p_curr);
    xastir_insert_name(db, p_curr,p_name);
  }
}

void xastir_station_shortcuts_update_function(XastirStationDb *db, int hash_key_in, DataRow *p_rem)
{
  int ii;
  DataRow *ptr;
  int prev_hash_key = 0x0000;
  int hash_key;


  // I just changed the function so that we can pass in the hash_key
  // that we wish to update:  We should be able to speed things up by
  // updating one hash key instead of all 16384 pointers.

  if ( (hash_key_in != -1)
       && (hash_key_in >= 0)
       && (hash_key_in < 16384) )
  {

    // We're adding/changing a hash key entry
    db->shortcuts[hash_key_in] = p_rem;
    //fprintf(stderr,"%i ",hash_key_in);
  }
  else    // We're removing a hash key entry.
  {

    // Clear and rebuild the entire hash table.

    //??????????????????????????????????????????????????
    // Clear all of the pointers before we begin????
    //??????????????????????????????????????????????????
    for (ii = 0; ii < 16384; ii++)
    {
      db->shortcuts[ii] = NULL;
    }

    ptr = db->first;  // Start of list


    // Loop through entire list, writing the pointer into the
    // db->shortcuts array whenever a new character is
    // encountered.  Do this until the end of the array or the end
    // of the list.
    //
    while ( (ptr != NULL) && (prev_hash_key < 16384) )
    {

      // We create the hash key out of the lower 7 bits of the
      // first two characters, creating a 14-bit key (1 of 16384)
      //
      hash_key = (int)((ptr->call_sign[0] & 0x7f) << 7);
      hash_key = hash_key | (int)(ptr->call_sign[1] & 0x7f);

      if (hash_key > prev_hash_key)
      {

        // We found the next hash_key.  Store the pointer at the
        // correct location.
        if (hash_key < 16384)
        {
          db->shortcuts[hash_key] = ptr;
          //fprintf(stderr,"%i ", hash_key);
        }
        prev_hash_key = hash_key;
      }
      ptr = ptr->n_next;
    }
    //fprintf(stderr,"\n");

  }

}

int xastir_search_station_name(XastirStationDb *db, DataRow **p_name, char *call, int exact)
{
  int kk;
  int hash_key;
  int result;
  int ok = 1;


  (*p_name) = db->first;                                // start of alphabet

  if (call[0] == '\0')
  {
    // If call we're searching for is empty, return db->first as
    // the pointer.
    return(0);
  }

  // We create the hash key out of the lower 7 bits of the first
  // two characters, creating a 14-bit key (1 of 16384)
  //
  hash_key = (int)((call[0] & 0x7f) << 7);
  hash_key = hash_key | (int)(call[1] & 0x7f);

  // Look for a match using hash table lookup
  //
  if (!db->non_ascii_names) {
  (*p_name) = db->shortcuts[hash_key];

  if ((*p_name) == NULL)      // No hash-table entry found.
  {
    int mm;

    //fprintf(stderr,"No hash-table entry found: call:%s\n",call);


    // No index found for that letter.  Walk the array until
    // we find an entry that is filled.  That'll be our
    // potential insertion point (insertion into the list will
    // occur just ahead of the hash entry).
    for (mm = hash_key; mm < 16384; mm++)
    {
      if (db->shortcuts[mm] != NULL)
      {
        (*p_name) = db->shortcuts[mm];
        break;
      }
    }
  }
//    else {
//fprintf(stderr,"Hash key %d=%s, searching for call: %s\n",
//    hash_key,
//    (*p_name)->call_sign,
//    call);
//    }

  // If we got to this point, we either have a NULL pointer or a
  // real hash-table pointer entry.  A non-NULL pointer means that
  // we have a match for the lower seven bits of the first two
  // characters of the callsign.  Check the rest of the callsign,
  // and jump out of the loop if we get outside the linear search
  // area (if first two chars are different).

  }
  kk = (int)strlen(call);

  // Search linearly through list.  Stop at end of list or break.
  while ( (*p_name) != NULL)
  {

    if (exact)
    {
      // Check entire string for exact match
      result = strcmp( call, (*p_name)->call_sign );
    }
    else
    {
      // Check first part of string for match
      result = strncmp( call, (*p_name)->call_sign, kk );
    }

    if (result < 0)     // We went past the right location.
    {
      // We're done.
      ok = 0;
      //fprintf(stderr,"Went past possible entry point, searching for call: %s\n",call);
      break;
    }
    else if (result == 0)   // Found a possible match
    {
      //fprintf(stderr,"Found possible match: list:%s call:%s\n",
      //    (*p_name)->call_sign,
      //    call);
      break;
    }
    else    // Result > 0.  We haven't found it yet.
    {
      (*p_name) = (*p_name)->n_next;  // Next element in list
    }
  }

  // Did we find anything?
  if ( (*p_name) == NULL)
  {
    ok = 0;
    //fprintf(stderr,"End of list reached, call: %s\n",call);
    return(ok); // Nope.  No match found.
  }

  // If "exact" is set, check that the string lengths match as
  // well.  If not, we didn't find it.
  if (exact && ok && strlen((*p_name)->call_sign) != strlen(call))
  {
    ok = 0;
  }

  return(ok);         // if not ok: p_name points to correct insert position in name list
}

int xastir_search_station_time(XastirStationDb *db, DataRow **p_time, time_t heard, int serial)
{
  int ok = 1;

  (*p_time) = db->newest;                                 // newest station
  if (heard == 0)                                     // we want the newest station
  {
    if (db->newest == NULL)
    {
      ok = 0;  // empty list
    }
  }
  else
  {
    while((*p_time) != NULL)                        // check time
    {
      if ((*p_time)->sec_heard <= heard)          // compare
      {
        break;  // found time or earlier
      }
      (*p_time) = (*p_time)->t_older;              // next element
    }
    // we now probably have found the entry
    if ((*p_time) != NULL && (*p_time)->sec_heard == heard)
    {
      // we got a match, but there may be more of them
      if (serial >= 0)                            // check serial number, ignored if -1
      {
        while((*p_time) != NULL)                // for unique time index
        {
          if ((*p_time)->sec_heard == heard && (*p_time)->time_sn <= serial)  // compare
          {
            break;  // found it (same time, maybe earlier SN)
          }
          if ((*p_time)->sec_heard < heard)   // compare
          {
            break;  // found it (earlier time)
          }
          (*p_time) = (*p_time)->t_older;      // consider next element
        }
        if ((*p_time) == NULL || (*p_time)->sec_heard != heard || (*p_time)->time_sn != serial)
        {
          ok = 0;  // no perfect match
        }
      }
    }
    else
    {
      ok = 0;  // no perfect match
    }
  }
  return(ok);
}

int xastir_delete_trail(XastirStationDb *db, DataRow *fill)
{

  if (fill->newest_trackpoint != NULL)
  {
    TrackRow *current;
    TrackRow *next;

    // Free the TrackRow records
    current = fill->oldest_trackpoint;
    while (current != NULL)
    {
      next = current->next;
      free(current);
      current = next;
    }

    fill->oldest_trackpoint = NULL;
    fill->newest_trackpoint = NULL;
    db->tracked--;
    return(1);
  }
  return(0);
}

int xastir_store_trail_point(XastirStationDb *db, DataRow *p_station,
                      long lon,
                      long lat,
                      time_t sec,
                      char *alt,
                      char *speed,
                      char *course,
                      short stn_flag)
{

  char flag;
  TrackRow *ptr;

  //fprintf(stderr,"store_trail_point: %s\n",p_station->call_sign);



  // Allocate storage for the new track point
  ptr = malloc(sizeof(TrackRow));
  if (ptr == NULL)
  {
  
    return(0); // Failed due to malloc
  }

  // Check whether we have any track data saved
  if (p_station->newest_trackpoint == NULL)
  {
    // new trail, do initialization

  
    db->tracked++;

    // Assign a new trail color 'cuz it's a new trail
    p_station->trail_color = 0;
  }

  // Start linking the record to the new end of the chain
  ptr->prev = p_station->newest_trackpoint;   // Link to record or NULL
  ptr->next = NULL;   // Newest end of chain

  // Have an older record already?
  if (p_station->newest_trackpoint != NULL)   // Yes
  {
    p_station->newest_trackpoint->next = ptr;
  }
  else    // No, this is our first record
  {
    p_station->oldest_trackpoint = ptr;
  }

  // Link it in as our newest record
  p_station->newest_trackpoint = ptr;



  ptr->trail_long_pos = lon;
  ptr->trail_lat_pos  = lat;
  ptr->sec            = sec;

  if (alt[0] != '\0')
  {
    ptr->altitude = atoi(alt)*10;
  }
  else
  {
    ptr->altitude = -99999l;
  }

  if (speed[0] != '\0')
  {
    ptr->speed  = (long)(atof(speed)*18.52);
  }
  else
  {
    ptr->speed  = -1;
  }

  if (course[0] != '\0')
  {
    ptr->course = (int)(atof(course) + 0.5);  // Poor man's rounding
  }
  else
  {
    ptr->course = -1;
  }

  flag = '\0';                    // init flags

  if ((stn_flag & ST_DIRECT) != 0)
  {
    flag |= TR_LOCAL;  // set "local" flag
  }

  if (ptr->prev != NULL)      // we have at least two points...
  {
    // Check whether distance between points is too far.  We
    // must convert from degrees to the Xastir coordinate system
    // units, which are 100th of a second.
    if (    labs(lon - ptr->prev->trail_long_pos) > (db->segment_distance * 60*60*100) ||
            labs(lat - ptr->prev->trail_lat_pos)  > (db->segment_distance * 60*60*100) )
    {

      // Set "new track" flag if there's
      // "db->segment_distance" degrees or more between
      // points.  Originally was hard-coded to one degree, now
      // set by a slider in the timing dialog.
      flag |= TR_NEWTRK;
    }
    else
    {
      // Check whether trail went above our maximum time
      // between points.  If so, don't draw segment.
      if (labs(sec - ptr->prev->sec) > (db->segment_minutes *60))
      {

        // Set "new track" flag if long delay between
        // reception of two points.  Time is set by a slider
        // in the timing dialog.
        flag |= TR_NEWTRK;
      }
    }
  }
  else
  {
    // Set "new track" flag for first point received.
    flag |= TR_NEWTRK;
  }
  ptr->flag = flag;

  return(1);  // We succeeded
}

void xastir_expire_trail_points(XastirStationDb *db, DataRow *p_station, time_t sec)
{
  int ii = 0;
  int done = 0;
  TrackRow *ptr;


  //fprintf(stderr,"expire_trail_points: %s\n",p_station->call_sign);



  // Check whether we have any track data saved
  if (p_station->oldest_trackpoint == NULL)
  {
    return;     // Nothing to expire
  }

  // Iterate from oldest->newest trackpoints
  while (!done && p_station->oldest_trackpoint != NULL)
  {
    ptr = p_station->oldest_trackpoint;
    if ( (ptr->sec + sec) >= db->now )
    {
      // New trackpoint, within expire time.  Quit checking
      // the rest of the trackpoints for this station.
      done++;
    }
    else
    {
      //fprintf(stderr,"Found old trackpoint\n");

      // Track too old.  Unlink this trackpoint and free it.
      p_station->oldest_trackpoint = ptr->next;

      // End of chain in this direction
      if (p_station->oldest_trackpoint != NULL)
      {
        p_station->oldest_trackpoint->prev = NULL;
      }
      else
      {
        p_station->newest_trackpoint = NULL;
      }

      // Free up the space used by the expired trackpoint
      free(ptr);

      //fprintf(stderr,"Free'ing a trackpoint\n");

      ii++;

      // Reduce our count of mobile stations if the size of
      // the track just went to zero.
      if (p_station->oldest_trackpoint == NULL)
      {
        db->tracked--;
      }
    }
  }


}

int xastir_is_trailpoint_echo(XastirStationDb *db, DataRow *p_station)
{
  int packets = 1;
  time_t checktime;

  TrackRow *ptr;


  // Check whether we're to skip checking for dupes (reading in
  // objects/items from file is one such case).
  //
  if (db->skip_duplicates)
  {
    return(0);  // Say that it isn't an echo
  }

  // Start at newest end of linked list and compare.  Return if we're
  // beyond the checktime.
  ptr = p_station->newest_trackpoint;

  if (ptr == NULL)
  {
    return(0);  // first point couldn't be an echo
  }

  checktime = p_station->sec_heard - 30*60;

  while (ptr != NULL)
  {

    if (ptr->sec < checktime)
    {
      return(0);  // outside time frame, no echo found
    }

    if ((p_station->coord_lon == ptr->trail_long_pos)
        && (p_station->coord_lat == ptr->trail_lat_pos)
        && (p_station->speed[0] == '\0' || ptr->speed < 0
            || (long)(atof(p_station->speed)*18.52) == ptr->speed)
        // current: char knots, trail: long 0.1m (-1 is undef)
        && (p_station->course[0] == '\0' || ptr->course <= 0
            || atoi(p_station->course) == ptr->course)
        // current: char, trail: int (-1 is undef)
        && (p_station->altitude[0] == '\0' || ptr->altitude <= -99999l
            || atoi(p_station->altitude)*10 == ptr->altitude))
    {
      // current: char, trail: int (-99999l is undef)
    
      return(1);              // we found a delayed echo
    }
    ptr = ptr->prev;
    packets++;
  }
  return(0);                      // no echo found
}

void xastir_add_status(XastirStationDb *db, DataRow *p_station, char *status_string)
{
  CommentRow *ptr;
  int add_it = 0;
  int len;


  len = strlen(status_string);

  // Eliminate line-end chars
  if (len > 1)
  {
    if ( (status_string[len-1] == '\n')
         || (status_string[len-1] == '\r') )
    {
      status_string[len-1] = '\0';
    }
  }

  // Shorten it
  //fprintf(stderr,"1Status: (%s)\n",status_string);
  (void)remove_trailing_spaces(status_string);
  //fprintf(stderr,"2Status: (%s)\n",status_string);
  (void)remove_leading_spaces(status_string);
  //fprintf(stderr,"3Status: (%s)\n",status_string);

  len = strlen(status_string);

  // Check for valid pointer
  if (p_station != NULL)
  {

    // We should probably create a new station record for this station
    // if there isn't one.  This allows us to collect as much info about
    // a station as we can until a posit comes in for it.  Right now we
    // don't do this.  If we decide to do this in the future, we also
    // need a method to find out the info about that station without
    // having to click on an icon, 'cuz the symbol won't be on our map
    // until we have a posit.

    //fprintf(stderr,"Station:%s\tStatus:%s\n",p_station->call_sign,status_string);

    // Check whether we have any data stored for this station
    if (p_station->status_data == NULL)
    {
      if (len > 0)
      {
        // No status stored yet and new status is non-NULL,
        // so add it to the list.
        add_it++;
      }
    }
    else    // We have status data stored already
    {
      // Check for an identical string
      CommentRow *ptr2;
      int ii = 0;

      ptr = p_station->status_data;
      ptr2 = ptr;
      while (ptr != NULL)
      {

        // Note that both text_ptr and comment_string can be
        // empty strings.

        if (compare_ascii_case(ptr->text_ptr, status_string) == 0)
        {
          // Found a matching string
          //fprintf(stderr,"Found match:
          //%s:%s\n",p_station->call_sign,status_string);

          // Instead of updating the timestamp, we'll delete the record from
          // the list and add it to the top in the code below.  Make sure to
          // tweak the "ii" pointer so that we don't end up shortening the
          // list unnecessarily.
          if (ptr == p_station->status_data)
          {

            // Only update the timestamp: We're at the
            // beginning of the list already.
            ptr->sec_heard = db->now;

            return; // No need to add a new record
          }
          else    // Delete the record
          {
            CommentRow *ptr3;

            // Keep a pointer to the record
            ptr3 = ptr;

            // Close the chain, skipping this record
            ptr2->next = ptr3->next;

            // Skip "ptr" over the record we wish to
            // delete
            ptr = ptr3->next;

            // Free the record
            free(ptr3->text_ptr);
            free(ptr3);

            // Muck with the counter 'cuz we just
            // deleted one record
            ii--;
          }
        }
        ptr2 = ptr; // Back one record
        if (ptr != NULL)
        {
          ptr = ptr->next;
        }
        ii++;
      }


      // No matching string found, or new timestamp found for
      // old record.  Add it to the top of the list.
      add_it++;
      //fprintf(stderr,"No match:
      //%s:%s\n",p_station->call_sign,status_string);

      // We counted the records.  If we have more than
      // MAX_STATUS_LINES records we'll delete/free the last
      // one to make room for the next.  This keeps us from
      // storing unique status records ad infinitum for active
      // stations, limiting the total space used.
      //
      if (ii >= MAX_STATUS_LINES)
      {
        // We know we didn't get a match, and that our list
        // is full (as full as we want it to be).  Traverse
        // the list again, looking for ptr2->next->next ==
        // NULL.  If found, free last record and set the
        // ptr2->next pointer to NULL.
        ptr2 = p_station->status_data;
        while (ptr2->next->next != NULL)
        {
          ptr2 = ptr2->next;
        }
        // At this point, we have a pointer to the last
        // record in ptr2->next.  Free it and the text
        // string in it.
        free(ptr2->next->text_ptr);
        free(ptr2->next);
        ptr2->next = NULL;
      }
    }

    if (add_it)     // We add to the beginning so we don't have
    {
      // to traverse the linked list.  This also
      // puts new records at the beginning of the
      // list to keep them in sorted order.

      CommentRow *entry = malloc(sizeof(*entry));
      if (!entry) return;
      entry->text_ptr = malloc((size_t)len + 1);
      if (!entry->text_ptr) { free(entry); return; }
      entry->next = p_station->status_data;
      p_station->status_data = entry;

      // Fill in the string
      snprintf(p_station->status_data->text_ptr,
                      len+1,
                      "%s",
                      status_string);

      // Fill in the timestamp
      p_station->status_data->sec_heard = db->now;

      //fprintf(stderr,"Station:%s\tStatus:%s\n\n",p_station->call_sign,p_station->status_data->text_ptr);
    }
  }
}

void xastir_add_comment(XastirStationDb *db, DataRow *p_station, char *comment_string)
{
  CommentRow *ptr;
  int add_it = 0;
  int len;


  len = strlen(comment_string);

  // Eliminate line-end chars
  if (len > 1)
  {
    if ( (comment_string[len-1] == '\n')
         || (comment_string[len-1] == '\r') )
    {
      comment_string[len-1] = '\0';
    }
  }

  // Shorten it
  //fprintf(stderr,"1Comment: (%s)\n",comment_string);
  (void)remove_trailing_spaces(comment_string);
  //fprintf(stderr,"2Comment: (%s)\n",comment_string);

  ///////TVR DEBUGGING RESULTS 28 March 2007:
  //NO! DON'T DO THIS --- it breaks multipoint objects!
  //    (void)remove_leading_spaces(comment_string);
  ///////////////////////////////////////////

  //fprintf(stderr,"3Comment: (%s)\n",comment_string);

  len = strlen(comment_string);

  // Check for valid pointer
  if (p_station != NULL)
  {

    // Check whether we have any data stored for this station
    if (p_station->comment_data == NULL)
    {
      if (len > 0)
      {
        // No comments stored yet and new comment is
        // non-NULL, so add it to the list.
        add_it++;
      }
    }
    else    // We have comment data stored already
    {
      // Check for an identical string
      CommentRow *ptr2;
      int ii = 0;

      ptr = p_station->comment_data;
      ptr2 = ptr;
      while (ptr != NULL)
      {

        // Note that both text_ptr and comment_string can be
        // empty strings.

        if (compare_ascii_case(ptr->text_ptr, comment_string) == 0)
        {
          // Found a matching string
          //fprintf(stderr,"Found match: %s:%s\n",p_station->call_sign,comment_string);

          // Instead of updating the timestamp, we'll delete the record from
          // the list and add it to the top in the code below.  Make sure to
          // tweak the "ii" pointer so that we don't end up shortening the
          // list unnecessarily.
          if (ptr == p_station->comment_data)
          {
            // Only update the timestamp:  We're at the
            // beginning of the list already.
            ptr->sec_heard = db->now;

            return; // No need to add a new record
          }
          else    // Delete the record
          {
            CommentRow *ptr3;

            // Keep a pointer to the record
            ptr3 = ptr;

            // Close the chain, skipping this record
            ptr2->next = ptr3->next;

            // Skip "ptr" over the record we with to
            // delete
            ptr = ptr3->next;

            // Free the record
            free(ptr3->text_ptr);
            free(ptr3);

            // Muck with the counter 'cuz we just
            // deleted one record
            ii--;
          }
        }
        ptr2 = ptr; // Keep this pointer one record back as
        // we progress.

        if (ptr != NULL)
        {
          ptr = ptr->next;
        }

        ii++;
      }
      // No matching string found, or new timestamp found for
      // old record.  Add it to the top of the list.
      add_it++;
      //fprintf(stderr,"No match: %s:%s\n",p_station->call_sign,comment_string);

      // We counted the records.  If we have more than
      // MAX_COMMENT_LINES records we'll delete/free the last
      // one to make room for the next.  This keeps us from
      // storing unique comment records ad infinitum for
      // active stations, limiting the total space used.
      //
      if (ii >= MAX_COMMENT_LINES)
      {

        // We know we didn't get a match, and that our list
        // is full (as we want it to be).  Traverse the list
        // again, looking for ptr2->next->next == NULL.  If
        // found, free that last record and set the
        // ptr2->next pointer to NULL.
        ptr2 = p_station->comment_data;
        while (ptr2->next->next != NULL)
        {
          ptr2 = ptr2->next;
        }
        // At this point, we have a pointer to the last
        // record in ptr2->next.  Free it and the text
        // string in it.
        free(ptr2->next->text_ptr);
        free(ptr2->next);
        ptr2->next = NULL;
      }
    }

    if (add_it)     // We add to the beginning so we don't have
    {
      // to traverse the linked list.  This also
      // puts new records at the beginning of the
      // list to keep them in sorted order.

      CommentRow *entry = malloc(sizeof(*entry));
      if (!entry) return;
      entry->text_ptr = malloc((size_t)len + 1);
      if (!entry->text_ptr) { free(entry); return; }
      entry->next = p_station->comment_data;
      p_station->comment_data = entry;

      // Fill in the string
      snprintf(p_station->comment_data->text_ptr,
                      len+1,
                      "%s",
                      comment_string);

      // Fill in the timestamp
      p_station->comment_data->sec_heard = db->now;
    }
  }
}

void xastir_station_del(XastirStationDb *db, char *call)
{
  DataRow *p_name;                      // DK7IN: do it with move... ?

  if (xastir_search_station_name(db, &p_name, call, 1))
  {
    (void)xastir_delete_trail(db, p_name);       // Free track storage if it exists.
    (void)xastir_delete_weather(p_name);     // Free weather memory, if allocated
    (void)xastir_delete_multipoints(p_name); // Free multipoint memory, if allocated
    (void)xastir_delete_comments_and_status(p_name);  // Free comment storage if it exists
    if (p_name->node_path_ptr != NULL)// Free malloc'ed path
    {
      free(p_name->node_path_ptr);
    }
    if (p_name->tactical_call_sign != NULL)
    {
      free(p_name->tactical_call_sign);
    }
    xastir_delete_station_memory(db, p_name);    // Free memory
  }
}

void xastir_station_del_ptr(XastirStationDb *db, DataRow *p_name)
{

  //fprintf(stderr,"db.c:xastir_station_del_ptr(db, ): %s\n",p_name->call_sign);

  if (p_name != NULL)
  {

    // A bit of debug code:  Attempting to find out if we're
    // deleting our own objects from time to time.  Leave this
    // in until we're sure the problem has been fixed.
    ////        if (is_my_call(p_name->origin,1)) { // Check SSID as well
    //        if ( is_my_object_item(p_name) ) { // Check SSID as well
    //            fprintf(stderr,"station_del_ptr: Removing my own object: %s\n",
    //                p_name->call_sign);
    //        }



    (void)xastir_delete_trail(db, p_name);     // Free track storage if it exists.
    (void)xastir_delete_weather(p_name);   // free weather memory, if allocated
    (void)xastir_delete_multipoints(p_name); // Free multipoint memory, if allocated
    (void)xastir_delete_comments_and_status(p_name);  // Free comment storage if it exists
    if (p_name->node_path_ptr != NULL)  // Free malloc'ed path
    {
      free(p_name->node_path_ptr);
    }
    if (p_name->tactical_call_sign != NULL)
    {
      free(p_name->tactical_call_sign);
    }
    xastir_delete_station_memory(db, p_name);  // free memory, update
    // linked lists, update
    // db->count

    //fprintf(stderr,"db.c:xastir_station_del_ptr(db, ): Deleted station\n");

  }
}

void xastir_delete_all_stations(XastirStationDb *db)
{
  DataRow *p_name;
  DataRow *p_curr;
  int ii;


  // Clear all of the pointers before we begin
  for (ii = 0; ii < 16384; ii++)
  {
    db->shortcuts[ii] = NULL;
  }

  p_name = db->first;
  while (p_name != NULL)
  {
    p_curr = p_name;
    p_name = p_name->n_next;
    xastir_station_del_ptr(db, p_curr);
    //(void)xastir_delete_trail(db, p_curr);     // free trail memory, if allocated
    //(void)xastir_delete_weather(p_curr);   // free weather memory, if allocated
    //(void)xastir_delete_multipoints(p_curr);// Free multipoint memory, if allocated
    //xastir_delete_station_memory(db, p_curr);  // free station memory
  }
  if (db->count != 0)
  {
    fprintf(stderr,
            "ERROR: db->count should be 0 after stations delete, is %d\n",
            db->count);
    db->count = 0;
  }
}

int xastir_next_station_name(XastirStationDb *db, DataRow **p_curr)
{

  if ((*p_curr) == NULL)
  {
    (*p_curr) = db->first;
  }
  else
  {
    (*p_curr) = (*p_curr)->n_next;
  }
  if ((*p_curr) != NULL)
  {
    return(1);
  }
  else
  {
    return(0);
  }
}

int xastir_prev_station_name(XastirStationDb *db, DataRow **p_curr)
{

  if ((*p_curr) == NULL)
  {
    (*p_curr) = db->last;
  }
  else
  {
    (*p_curr) = (*p_curr)->n_prev;
  }
  if ((*p_curr) != NULL)
  {
    return(1);
  }
  else
  {
    return(0);
  }
}

int xastir_next_station_time(XastirStationDb *db, DataRow **p_curr)
{

  if ((*p_curr) == NULL)
  {
    (*p_curr) = db->oldest;  // Grab oldest station if NULL passed to us???
  }
  else
  {
    (*p_curr) = (*p_curr)->t_newer;  // Else grab newer station
  }
  if ((*p_curr) != NULL)
  {
    return(1);
  }
  else
  {
    return(0);
  }
}

int xastir_prev_station_time(XastirStationDb *db, DataRow **p_curr)
{

  if ((*p_curr) == NULL)
  {
    (*p_curr) = db->newest;  // Grab newest station if NULL passed to us???
  }
  else
  {
    (*p_curr) = (*p_curr)->t_older;
  }
  if ((*p_curr) != NULL)
  {
    return(1);
  }
  else
  {
    return(0);
  }
}

void xastir_init_weather(WeatherRow *weather)      // clear weather data
{

  weather->wx_sec_time             = (time_t)0;
  weather->wx_storm                = 0;
  weather->wx_time[0]              = '\0';
  weather->wx_course[0]            = '\0';
  weather->wx_speed[0]             = '\0';
  weather->wx_speed_sec_time       = 0; // ??
  weather->wx_gust[0]              = '\0';
  weather->wx_hurricane_radius[0]  = '\0';
  weather->wx_trop_storm_radius[0] = '\0';
  weather->wx_whole_gale_radius[0] = '\0';
  weather->wx_temp[0]              = '\0';
  weather->wx_rain[0]              = '\0';
  weather->wx_rain_total[0]        = '\0';
  weather->wx_snow[0]              = '\0';
  weather->wx_prec_24[0]           = '\0';
  weather->wx_prec_00[0]           = '\0';
  weather->wx_hum[0]               = '\0';
  weather->wx_baro[0]              = '\0';
  weather->wx_fuel_temp[0]         = '\0';
  weather->wx_fuel_moisture[0]     = '\0';
  weather->wx_type                 = '\0';
  weather->wx_station[0]           = '\0';
}

int xastir_get_weather_record(DataRow *fill)      // get or create weather storage
{
  int ok=1;

  if (fill->weather_data == NULL)        // new weather data, allocate storage and init
  {
    fill->weather_data = malloc(sizeof(WeatherRow));
    if (fill->weather_data == NULL)
    {
      fprintf(stderr,"Couldn't allocate memory in xastir_get_weather_record()\n");
      ok = 0;
    }
    else
    {
      xastir_init_weather(fill->weather_data);
    }
  }
  return(ok);
}

int xastir_delete_weather(DataRow *fill)      // delete weather storage, if allocated
{

  if (fill->weather_data != NULL)
  {
    free(fill->weather_data);
    fill->weather_data = NULL;
    return(1);
  }
  return(0);
}

int xastir_delete_multipoints(DataRow *fill)   // delete multipoint storage, if allocated
{

  if (fill->multipoint_data != NULL)
  {
    //fprintf(stderr,"Removing multipoint data, %s\n", fill->call_sign);
    free(fill->multipoint_data);
    fill->multipoint_data = NULL;
    fill->num_multipoints = 0;
    return(1);
  }
  return(0);
}

int xastir_delete_comments_and_status(DataRow *fill)
{

  // If the pointers are empty, we're done
  if (       (fill->comment_data == NULL)
             && (fill->status_data  == NULL) )
  {
    return(0);
  }

  if (fill->comment_data != NULL)     // We have comment records
  {
    CommentRow *ptr;
    CommentRow *ptr_next;

    ptr = fill->comment_data;
    ptr_next = ptr->next;
    while (ptr != NULL)
    {
      // Free the actual text string that we malloc'ed
      if (ptr->text_ptr != NULL)
      {
        free(ptr->text_ptr);
      }
      free(ptr);
      ptr = ptr_next; // Advance to next record
      if (ptr != NULL)
      {
        ptr_next = ptr->next;
      }
      else
      {
        ptr_next = NULL;
      }
    }
  }
  if (fill->status_data != NULL)     // We have status records
  {
    CommentRow *ptr;
    CommentRow *ptr_next;

    ptr = fill->status_data;
    ptr_next = ptr->next;
    while (ptr != NULL)
    {
      // Free the actual text string that we malloc'ed
      if (ptr->text_ptr != NULL)
      {
        free(ptr->text_ptr);
      }
      free(ptr);
      ptr = ptr_next; // Advance to next record
      if (ptr != NULL)
      {
        ptr_next = ptr->next;
      }
      else
      {
        ptr_next = NULL;
      }
    }
  }
  fill->comment_data = NULL;
  fill->status_data = NULL;
  return(1);
}
void xastir_station_db_init(XastirStationDb *db, time_t now)
{
  memset(db, 0, sizeof(*db));
  db->now = now;
  db->segment_distance = 1;
  db->segment_minutes = 30;
}
void xastir_station_touch(XastirStationDb *db, DataRow *row, time_t now)
{
  db->now = now;
  xastir_remove_time(db, row);
  row->sec_heard = now;
  row->time_sn = db->next_serial++;
  DataRow *where = NULL;
  xastir_search_station_time(db, &where, now, row->time_sn);
  /* Legacy search returns an equal/older row; insertion is BEFORE its argument. */
  if (where && (where->sec_heard < now || where->time_sn <= row->time_sn)) where = where->t_newer;
  if (!where && db->oldest && db->oldest->sec_heard > now) where = db->oldest;
  xastir_insert_time(db, row, where);
}
DataRow *xastir_station_upsert(XastirStationDb *db, const char *name, time_t now)
{
  if (!name || !*name || strlen(name) >= XASTIR_STATION_NAME_SIZE) return NULL;
  for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
    if (*p >= 128) db->non_ascii_names = 1;
  db->now = now;
  DataRow *row = NULL;
  if (!xastir_search_station_name(db, &row, (char *)name, 1))
    row = xastir_add_new_station(db, row, NULL, (char *)name);
  if (row) xastir_station_touch(db, row, now);
  return row;
}
void xastir_station_expire(XastirStationDb *db, time_t before)
{
  DataRow *row = db->oldest;
  while (row && row->sec_heard < before) {
    DataRow *next = row->t_newer;
    if (!(row->flag & (ST_MYSTATION | ST_MYOBJITEM))) xastir_station_del_ptr(db, row);
    row = next;
  }
}

int xastir_station_update(XastirStationDb *db, const XastirStationReport *r, time_t now)
{
  if (!db || !r || !r->name || !*r->name || strlen(r->name) >= XASTIR_STATION_NAME_SIZE
      || (r->source && strlen(r->source) >= XASTIR_STATION_NAME_SIZE)
      || !isfinite(r->latitude) || fabs(r->latitude) > 90
      || !isfinite(r->longitude) || fabs(r->longitude) > 180
      || (r->comment && strlen(r->comment) > 512)
      || r->kind < 0 || r->kind > 2
      || (r->course_valid && (!isfinite(r->course) || r->course < 0 || r->course >= 360))) return 0;
  for (const unsigned char *p = (const unsigned char *)r->name; *p; ++p)
    if (*p < 32 || *p == 127) return 0;
  char table = r->symbol_table;
  if (table != '/' && table != '\\' && !(table >= '0' && table <= '9')
      && !(table >= 'A' && table <= 'Z')) return 0;
  if (r->symbol_code < '!' || r->symbol_code > '~') return 0;
  for (const unsigned char *p = (const unsigned char *)r->name; *p; ++p)
    if (*p >= 128) db->non_ascii_names = 1;
  DataRow *previous = NULL;
  time_t previous_heard = xastir_search_station_name(db, &previous, (char *)r->name, 1)
      ? previous->sec_heard : now;
  DataRow *row = xastir_station_upsert(db, r->name, now);
  if (!row) return 0;
  if ((row->flag & ST_MYSTATION) && !r->local) { ++row->num_packets; return 1; }
  if (r->local) row->flag |= ST_MYSTATION;
  if (row->position_valid && (row->coord_lat != (long)llround((90-r->latitude)*360000)
      || row->coord_lon != (long)llround((180+r->longitude)*360000))) {
    row->flag |= ST_MOVING;
    if (!row->newest_trackpoint)
      xastir_store_trail_point(db, row, row->coord_lon, row->coord_lat, previous_heard,
                              row->altitude, row->speed, row->course, row->flag);
  }
  row->latitude = r->latitude;
  row->longitude = r->longitude;
  row->coord_lat = (long)llround((90 - r->latitude) * 360000);
  row->coord_lon = (long)llround((180 + r->longitude) * 360000);
  row->heading = r->course;
  row->heading_valid = r->course_valid;
  row->position_valid = 1;
  row->emergency = r->emergency;
  row->flag = (row->flag & ~(ST_OBJECT | ST_ITEM)) | ST_ACTIVE
      | (r->kind == 1 ? ST_OBJECT : r->kind == 2 ? ST_ITEM : 0);
  row->aprs_symbol.aprs_type = (table == '/' || table == '\\') ? table : '\\';
  row->aprs_symbol.aprs_symbol = r->symbol_code;
  row->aprs_symbol.special_overlay = (table == '/' || table == '\\') ? 0 : table;
  snprintf(row->origin, sizeof(row->origin), "%s", r->source ? r->source : "");
  if (r->course_valid) snprintf(row->course, sizeof(row->course), "%03d", (int)r->course ? (int)r->course : 360);
  else row->course[0] = 0;
  row->pos_amb = r->ambiguity;
  if (r->speed) snprintf(row->speed, sizeof(row->speed), "%s", r->speed);
  if (row->speed[0] && atof(row->speed) > 0) row->flag |= ST_MOVING;
  if (r->altitude) snprintf(row->altitude, sizeof(row->altitude), "%s", r->altitude);
  if (r->comment) {
    size_t length = strlen(r->comment);
    char *text = malloc(length+1);
    if (text) { memcpy(text, r->comment, length+1); xastir_add_comment(db, row, text); free(text); }
  }
  if ((row->flag & ST_MOVING) && !xastir_is_trailpoint_echo(db, row))
    xastir_store_trail_point(db, row, row->coord_lon, row->coord_lat, now,
                            row->altitude, row->speed, row->course, row->flag);
  /* Keep trail ownership bounded even for continuously moving stations. */
  xastir_expire_trail_points(db, row, 3600);
  ++row->num_packets;
  return 1;
}

int xastir_station_remove(XastirStationDb *db, const char *name, int kind)
{
  DataRow *row = NULL;
  if (!db || !name || kind < 0 || kind > 2
      || !xastir_search_station_name(db, &row, (char *)name, 1)) return 0;
  const int stored_kind = row->flag & ST_OBJECT ? 1 : row->flag & ST_ITEM ? 2 : 0;
  if (kind != stored_kind) return 0;
  xastir_station_del_ptr(db, row);
  return 1;
}
