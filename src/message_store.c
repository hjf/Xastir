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

/* Legacy message history and binary lookup extracted from db.c. */
#include "message_store.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
void xastir_message_store_reindex(XastirMessageStore *store)
{
  /* qsort lacks a portable context argument. Insert the appended index into
   * the sorted array without process-global comparator state. */
  for (long i = 1; i < store->count; ++i) {
    long entry = store->index[i], j = i;
    while (j > 0 && xastir_msg_comp_data(store, &entry, &store->index[j-1]) < 0) {
      store->index[j] = store->index[j-1]; --j;
    }
    store->index[j] = entry;
  }
}
int xastir_msg_comp_active(const void *a, const void *b)
{
  char temp_a[MAX_CALLSIGN+MAX_CALLSIGN+MAX_MESSAGE_ORDER+4];
  char temp_b[MAX_CALLSIGN+MAX_CALLSIGN+MAX_MESSAGE_ORDER+4];

  snprintf(temp_a, sizeof(temp_a), "%c%s%s%s",
                  ((Message*)a)->active, ((Message*)a)->call_sign,
                  ((Message*)a)->from_call_sign,
                  ((Message*)a)->seq);
  snprintf(temp_b, sizeof(temp_b), "%c%s%s%s",
                  ((Message*)b)->active, ((Message*)b)->call_sign,
                  ((Message*)b)->from_call_sign,
                  ((Message*)b)->seq);

  return(strcmp(temp_a, temp_b));
}

int xastir_msg_comp_data(XastirMessageStore *store, const void *a, const void *b)
{
  char temp_a[MAX_CALLSIGN+MAX_CALLSIGN+MAX_MESSAGE_ORDER+3];
  char temp_b[MAX_CALLSIGN+MAX_CALLSIGN+MAX_MESSAGE_ORDER+3];

  snprintf(temp_a, sizeof(temp_a), "%s|%s|%s",
                  store->data[*(long*)a].call_sign, store->data[*(long *)a].from_call_sign,
                  store->data[*(long *)a].seq);
  snprintf(temp_b, sizeof(temp_b), "%s|%s|%s", store->data[*(long*)b].call_sign,
                  store->data[*(long *)b].from_call_sign, store->data[*(long *)b].seq);

  return(strcmp(temp_a, temp_b));
}

int xastir_msg_input_database(XastirMessageStore *store, Message *m_fill)
{
  void *m_ptr;
  long i;

  if (store->count == store->capacity)
  {
    for (i = 0; i < store->count; i++)
    {

      // Check for a record that is marked RECORD_NOTACTIVE.
      // If found, use that record instead of malloc'ing a new
      // one.
      if (store->data[store->index[i]].active == RECORD_NOTACTIVE)
      {

        // Found an unused record.  Fill it in.
        memcpy(&store->data[store->index[i]], m_fill, sizeof(Message));

        // Sort store->data
        qsort(store->data, (size_t)store->count, sizeof(Message), xastir_msg_comp_active);

        for (i = 0; i < store->count; i++)
        {
          store->index[i] = i;
          if (store->data[i].active == RECORD_NOTACTIVE)
          {
            store->count = i;
            break;
          }
        }

        // Sort store->index
        xastir_message_store_reindex(store);

        // All done with this message.
        return 1;
      }
    }

    // Didn't find free message record.  Fetch some more space.
    // Get more store->data space.
    m_ptr = realloc(store->data, (store->capacity+MSG_INCREMENT)*sizeof(Message));
    if (m_ptr)
    {
      store->data = m_ptr;

      // Get more store->index space
      m_ptr = realloc(store->index, (store->capacity+MSG_INCREMENT)*sizeof(long));
      if (m_ptr)
      {
        store->index = m_ptr;
        store->capacity += MSG_INCREMENT;

        //fprintf(stderr, "Max Message Array: %ld\n", store->capacity);

      }
      else
      {
        return 0;
      }
    }
    else
    {
      return 0;
    }
  }
  if (store->count < store->capacity)
  {
    store->index[store->count] = store->count;

    // Copy message data into new message record.
    memcpy(&store->data[store->count++], m_fill, sizeof(Message));

    // Sort store->index
    xastir_message_store_reindex(store);
  }
  return store->count <= store->capacity;
}

long xastir_msg_find_data(XastirMessageStore *store, Message *m_fill)
{
  long record_start, record_mid, record_end, return_record, done;
  char tempfile[MAX_CALLSIGN+MAX_CALLSIGN+MAX_MESSAGE_ORDER+3];
  char tempfill[MAX_CALLSIGN+MAX_CALLSIGN+MAX_MESSAGE_ORDER+3];


  snprintf(tempfill, sizeof(tempfill), "%s|%s|%s",
                  m_fill->call_sign,
                  m_fill->from_call_sign,
                  m_fill->seq);

  return_record = -1L;
  if (store->index && store->count >= 1)
  {
    /* more than one record */
    record_start=0L;
    record_end = (store->count - 1);
    record_mid=(record_end-record_start)/2;

    done=0;
    while (!done)
    {

      /* get data for record start */
      snprintf(tempfile, sizeof(tempfile), "%s|%s|%s",
                      store->data[store->index[record_start]].call_sign,
                      store->data[store->index[record_start]].from_call_sign,
                      store->data[store->index[record_start]].seq);

      if (strcmp(tempfill, tempfile) < 0)
      {
        /* filename comes before */
        /*fprintf(stderr,"Before No data found!!\n");*/
        done=1;
        break;
      }
      else   /* get data for record end */
      {

        snprintf(tempfile, sizeof(tempfile), "%s|%s|%s",
                        store->data[store->index[record_end]].call_sign,
                        store->data[store->index[record_end]].from_call_sign,
                        store->data[store->index[record_end]].seq);

        if (strcmp(tempfill,tempfile)>=0)   /* at end or beyond */
        {
          if (strcmp(tempfill, tempfile) == 0)
          {
            return_record = record_end;
            //fprintf(stderr,"record %ld",return_record);
          }

          done=1;
          break;
        }
        else if ((record_mid == record_start) || (record_mid == record_end))
        {
          /* no mid for compare check to see if in the middle */
          done=1;
          snprintf(tempfile, sizeof(tempfile), "%s|%s|%s",
                          store->data[store->index[record_mid]].call_sign,
                          store->data[store->index[record_mid]].from_call_sign,
                          store->data[store->index[record_mid]].seq);
          if (strcmp(tempfill,tempfile)==0)
          {
            return_record = record_mid;
            //fprintf(stderr,"record: %ld",return_record);
          }
        }
      }
      if (!done)   /* get data for record mid */
      {
        snprintf(tempfile, sizeof(tempfile), "%s|%s|%s",
                        store->data[store->index[record_mid]].call_sign,
                        store->data[store->index[record_mid]].from_call_sign,
                        store->data[store->index[record_mid]].seq);

        if (strcmp(tempfill, tempfile) == 0)
        {
          return_record = record_mid;
          //fprintf(stderr,"record %ld",return_record);
          done = 1;
          break;
        }

        if(strcmp(tempfill, tempfile)<0)
        {
          record_end = record_mid;
        }
        else
        {
          record_start = record_mid;
        }

        record_mid = record_start+(record_end-record_start)/2;
      }
    }
  }
  if (return_record >= 0) {
    /* Sequence IDs are reused after restarts. Prefer the latest matching record. */
    long first = return_record;
    long physical = store->index[return_record];
    while (first > 0 && !xastir_msg_comp_data(store, &physical, &store->index[first-1])) --first;
    for (long i = first; i < store->count && !xastir_msg_comp_data(store, &physical, &store->index[i]); ++i) {
      const Message *candidate = &store->data[store->index[i]];
      const Message *best = &store->data[store->index[return_record]];
      if (candidate->sec_heard > best->sec_heard
          || (candidate->sec_heard == best->sec_heard && candidate->timestamp_ms > best->timestamp_ms)) return_record = i;
    }
  }
  return(return_record);
}

void xastir_msg_replace_data(XastirMessageStore *store, Message *m_fill, long record_num)
{
  memcpy(&store->data[store->index[record_num]], m_fill, sizeof(Message));
}

void xastir_msg_get_data(XastirMessageStore *store, Message *m_fill, long record_num)
{
  memcpy(m_fill, &store->data[store->index[record_num]], sizeof(Message));
}
void xastir_message_store_free(XastirMessageStore *store)
{
  free(store->data); free(store->index); memset(store, 0, sizeof(*store));
}
