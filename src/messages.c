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

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "snprintf.h"
#include "globals.h"

#include <Xm/XmAll.h>
#include <X11/Xatom.h>
#include <X11/Shell.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#if HAVE_SYS_TIME_H
  #include <sys/time.h>
#endif // HAVE_SYS_TIME_H
#include <time.h>

#include "xastir.h"
#include "main.h"
#include "db_funcs.h"
#include "messages.h"
#include "util.h"
#include "interface.h"
#include "xa_config.h"
#include "encoding.h"

// Must be last include file
#include "leak_detection.h"



char group_data_file[400];
char *group_data_list = NULL;   // Need this NULL for Solaris!
int  group_data_count = 0;
int  group_data_max = 0;

char message_counter[5+1];

int auto_reply;
char auto_reply_message[100];

Message_Window mw[MAX_MESSAGE_WINDOWS+1];   // Send Message widgets

Message_transmit message_pool[MAX_OUTGOING_MESSAGES+1]; // Transmit message queue





void clear_message_windows(void)
{
  int i;

  begin_critical_section(&send_message_dialog_lock, "messages.c:clear_message_windows" );

  for (i = 0;  i < MAX_MESSAGE_WINDOWS; i++)
  {

    if (mw[i].send_message_dialog)
    {
      XtDestroyWidget(mw[i].send_message_dialog);
    }

    mw[i].send_message_dialog = (Widget)NULL;
    mw[i].to_call_sign[0] = '\0';
    mw[i].send_message_call_data = (Widget)NULL;
    mw[i].D700_mode = (Widget)NULL;
    mw[i].D7_mode = (Widget)NULL;
    mw[i].HamHUD_mode = (Widget)NULL;
    mw[i].message_data_line1 = (Widget)NULL;
    mw[i].message_data_line2 = (Widget)NULL;
    mw[i].message_data_line3 = (Widget)NULL;
    mw[i].message_data_line4 = (Widget)NULL;
    mw[i].send_message_text = (Widget)NULL;
  }

  end_critical_section(&send_message_dialog_lock, "messages.c:clear_message_windows" );

}





static int group_comp(const void *a, const void *b)
{
  if (!*(char *)a)
  {
    return ((int)(*(char *)b != '\0'));
  }
  return strcasecmp(a, b);
}





void group_build_list(char *filename)
{
  char *ptr;
  FILE *f;
  struct stat group_stat;
  int i;

  if (group_data_count == group_data_max)
  {
    ptr = realloc(group_data_list, (size_t)(group_data_max+10)*10);

    if (ptr)
    {
      group_data_list = ptr;
      group_data_max += 10;

//fprintf(stderr,                "group_data_max: %d\n", group_data_max);

    }
    else
    {
      fprintf(stderr,
              "Unable to allocate more memory for group_data_list (1)\n");
    }
  }


// Make sure we always listen for ourself, XASTIR, & our Version groups
  xastir_snprintf(&group_data_list[0],10,"%s",my_callsign);
  xastir_snprintf(&group_data_list[10],10,"XASTIR");
  xastir_snprintf(&group_data_list[20],10,"%s",XASTIR_TOCALL);
  group_data_count = 3;
// If we are in special group look for messages.
  if (altnet)
  {
    xastir_snprintf(&group_data_list[group_data_count*10],10,"%s",altnet_call);
    group_data_count++;
  }
//

  if (! stat(filename, &group_stat) )
  {
    f = fopen(filename, "r");  // File exists
  }
  else
  {
    f = fopen(filename, "w+");  // No file.  Create it and open it.
  }

  if (f == NULL)
  {
    fprintf(stderr,"Couldn't open file for reading -or- appending: %s\n", filename);
    return;
  }

  while (!feof(f))
  {
    if (group_data_count == group_data_max)
    {
      ptr = realloc(group_data_list, (size_t)(group_data_max+10)*10);
      if (ptr)
      {
        group_data_list = ptr;
        group_data_max += 10;

//fprintf(stderr,                "group_data_max(2): %d\n", group_data_max);

      }
      else
      {
        fprintf(stderr,
                "Unable to allocate more memory for group_data_list (2)\n");
      }
    }
    if (group_data_count < group_data_max)
    {
      group_data_list[group_data_count*10] = '\0';
      if (fgets(&group_data_list[group_data_count*10], 10, f) == NULL)
      {
        // Error reading file or end-of-file. Continue processing
        // the (partial?) string we just read, in the code below.
      }
      if ((ptr = strchr(&group_data_list[group_data_count*10], '\n')))
      {
        *ptr = '\0';
      }
      else
        while ((i = fgetc(f)) != EOF && i != '\n'); // clean-up after long group name

      // check for DOS EOL markup!
      if ((ptr = strchr(&group_data_list[group_data_count*10], '\r')))
      {
        *ptr = '\0';
      }
      if (group_data_list[group_data_count*10])
      {
        group_data_count++;
      }
    }
  }
  (void)fclose(f);
  qsort(group_data_list, (size_t)group_data_count, 10, group_comp);

  if (debug_level & 2)
  {
    for (i = 0; i < group_data_count; i++)
    {
      fprintf(stderr,"Group %2d: %s\n", i, &group_data_list[i*10]);
    }
  }
}





int group_active(char *from)
{
  static struct stat current_group_stat;
  struct stat group_stat;
  static char altgroup[10];
  char group_data_path[MAX_VALUE];

  get_user_base_dir(group_data_file, group_data_path,
                    sizeof(group_data_path));

  (void)remove_trailing_spaces(from);

  // If we cycle to/from special group or file changes, rebuild group list.
  if ((!stat( group_data_path, &group_stat )
       && (current_group_stat.st_size != group_stat.st_size
           || current_group_stat.st_mtime != group_stat.st_mtime
           || current_group_stat.st_ctime != group_stat.st_ctime)))
  {

// altgroup equates to "address of altgroup" which always evaluates
// as true.  Commenting it out of the conditional.  --we7u.
//                || (altgroup && strcasecmp(altgroup, VERSIONFRM))) {

    group_build_list( group_data_path );
    current_group_stat = group_stat;
    xastir_snprintf(altgroup,sizeof(altgroup),"%s",VERSIONFRM);
  }
  if (group_data_list != NULL)    // Causes segfault on Solaris 2.5 without this!
  {
    return (int)(bsearch(from, group_data_list, (size_t)group_data_count, (size_t)10, group_comp) != NULL);
  }
  else
  {
    return(0);
  }
}





int look_for_open_group_data(char *to)
{
  int i,found;
  char temp1[MAX_CALLSIGN+1];
  char *temp_ptr;


  begin_critical_section(&send_message_dialog_lock, "messages.c:look_for_open_group_data" );

  found = FALSE;
  for(i = 0; i < MAX_MESSAGE_WINDOWS; i++)
  {
    /* find station  */
    if(mw[i].send_message_dialog != NULL)
    {

      temp_ptr = XmTextFieldGetString(mw[i].send_message_call_data);
      xastir_snprintf(temp1,
                      sizeof(temp1),
                      "%s",
                      temp_ptr);
      XtFree(temp_ptr);

      (void)to_upper(temp1);
      /*fprintf(stderr,"Looking at call <%s> for <%s>\n",temp1,to);*/
      if(strcmp(temp1,to)==0)
      {
        found=(int)TRUE;
        break;
      }
    }
  }

  end_critical_section(&send_message_dialog_lock, "messages.c:look_for_open_group_data" );

  return(found);
}





// What we wish to do here:  Check for an active Send Message dialog
// that contains the callsign of interest.  If one doesn't exist,
// create one and pop it up.  We don't want to do this for duplicate
// message lines or duplicate acks for any particular QSO.  To do so
// would cause the Send Message dialog to pop up on every such
// received message, which is VERY annoying (that was the default in
// Xastir for years, and nobody liked it!).
//
int check_popup_window(char *from_call_sign, int group)
{
  int i,found,j,ret;
  char temp1[MAX_CALLSIGN+1];
  char *temp_ptr;


//fprintf(stderr,"\tcheck_popup_window()\n");

  ret = -1;
  found = -1;

  begin_critical_section(&send_message_dialog_lock, "messages.c:check_popup_window" );

  // Check for an already-created dialog for talking to this
  // particular call_sign.
  //
  for (i = 0; i < MAX_MESSAGE_WINDOWS; i++)
  {

    if (mw[i].send_message_dialog != NULL)   // If dialog created
    {

      temp_ptr = XmTextFieldGetString(mw[i].send_message_call_data);
      xastir_snprintf(temp1,
                      sizeof(temp1),
                      "%s",
                      temp_ptr);
      XtFree(temp_ptr);

      /*fprintf(stderr,"Looking at call <%s> for <%s>\n",temp1,from_call_sign);*/
      if (strcasecmp(temp1, from_call_sign) == 0)
      {
        // Found a call_sign match in a Send Message dialog!

//fprintf(stderr,"\tFound a Send_message dialog match\n");

        found = i;
        break;
      }
    }
  }

  end_critical_section(&send_message_dialog_lock, "messages.c:check_popup_window" );

  // If found == -1 at this point, we haven't found a Send Message
  // dialog that contains the call_sign of interest.
  //
  if (found == -1 && (group == 2 || group_active(from_call_sign)))
  {
    /* no window found Open one! */

//fprintf(stderr,"\tNo Send Message dialog found, creating one\n");

    begin_critical_section(&send_message_dialog_lock, "messages.c:check_popup_window2" );

    i= -1;
    for (j=0; j<MAX_MESSAGE_WINDOWS; j++)
    {
      if (!mw[j].send_message_dialog)
      {
        i=j;
        break;
      }
    }

    end_critical_section(&send_message_dialog_lock, "messages.c:check_popup_window2" );

    if (i!= -1)
    {

      if (group == 1)
      {
        temp1[0] = '*';
        temp1[1] = '\0';
      }
      else
      {
        temp1[0] = '\0';
      }

      strncat(temp1,
              from_call_sign,
              sizeof(temp1) - 1 - strlen(temp1));

      if (!disable_all_popups)
      {
        Send_message(appshell, temp1, NULL);
      }

      update_messages(1);

      ret=i;
    }
    else
    {
      fprintf(stderr,"No open windows!\n");
    }
  }
  else
  {
    /* window open! */
    // Pop it up
    ret=found;
  }

  if (found != -1)    // Already have a window
  {
    XtPopup(mw[i].send_message_dialog,XtGrabNone);
  }

  return(ret);
}





void clear_outgoing_message(int i)
{
  message_pool[i].active=MESSAGE_CLEAR;
  message_pool[i].to_call_sign[0] = '\0';
  message_pool[i].from_call_sign[0] = '\0';
  message_pool[i].message_line[0] = '\0';
  message_pool[i].seq[0] = '\0';
  message_pool[i].active_time=0;;
  message_pool[i].next_time=0L;
  message_pool[i].tries=0;
}





// Clear all pending transmit messages that are from us and to the
// callsign listed.  Perhaps it'd be better to time it out instead
// so that it still shows up in the message window?  Here we just
// erase it.
//
void clear_outgoing_messages_to(char *callsign)
{
  int ii;


//    fprintf(stderr,"Callsign: %s\n", callsign);

  // Run through the entire outgoing message queue
  for (ii = 0; ii < MAX_OUTGOING_MESSAGES; ii++)
  {

    // If it matches the callsign we're talking to
    if (strcasecmp(message_pool[ii].to_call_sign,callsign) == 0)
    {

      // Record a fake ack and add "*CANCELLED*" to the
      // message.  This will be displayed in the Send Message
      // dialog.
      msg_record_ack(message_pool[ii].to_call_sign,
                     message_pool[ii].from_call_sign,
                     message_pool[ii].seq,
                     0,  // Not a timeout
                     1); // Record a cancel

      // Clear it out.
      message_pool[ii].active=MESSAGE_CLEAR;
      message_pool[ii].to_call_sign[0] = '\0';
      message_pool[ii].from_call_sign[0] = '\0';
      message_pool[ii].message_line[0] = '\0';
      message_pool[ii].seq[0] = '\0';
      message_pool[ii].active_time=0;;
      message_pool[ii].next_time=0L;
      message_pool[ii].tries=0;
    }
  }
}





// Change path on all pending transmit messages that are from us and
// to the callsign listed.
//
void change_path_outgoing_messages_to(char *callsign, char *new_path)
{
  int ii;
  char my_callsign[20];


//fprintf(stderr,
//    "Changing all outgoing msgs to %s to new path: %s\n",
//    callsign,
//    new_path);

  xastir_snprintf(my_callsign,
                  sizeof(my_callsign),
                  "%s",
                  callsign);

  remove_trailing_spaces(my_callsign);

  // Run through the entire outgoing message queue
  for (ii = 0; ii < MAX_OUTGOING_MESSAGES; ii++)
  {

    if (message_pool[ii].active == MESSAGE_ACTIVE)
    {

//fprintf(stderr,"\t'%s'\n\t'%s'\n",
//    message_pool[ii].to_call_sign,
//    my_callsign);

      // If it matches the callsign we're talking to
      if (strcasecmp(message_pool[ii].to_call_sign,my_callsign) == 0)
      {

//fprintf(stderr,"\tFound an outgoing queued msg to change path on.\n");

        xastir_snprintf(message_pool[ii].path,
                        sizeof(message_pool[ii].path),
                        "%s",
                        new_path);
      }
    }
  }
}





time_t last_check_and_transmit = (time_t)0L;


// Kick the interval timer back to 7 and tries back to 1 for
// messages in this QSO.  Used to get a QSO going again when the
// interval timer has gotten large, but the message is important to
// get through quickly.
//
void kick_outgoing_timer(char *callsign)
{
  int ii;


//    fprintf(stderr,"Callsign: %s\n", callsign);

  // Run through the entire outgoing message queue
  for (ii = 0; ii < MAX_OUTGOING_MESSAGES; ii++)
  {

    // If it matches the callsign we're talking to
    if (strcasecmp(message_pool[ii].to_call_sign,callsign) == 0)
    {
      message_pool[ii].next_time = (time_t)7L;
      message_pool[ii].tries = 0;
      message_pool[ii].active_time = (time_t)0L;
    }
  }

  // Cause the transmit routine to get called again
  last_check_and_transmit = (time_t)0L;
}





void reset_outgoing_messages(void)
{
  int i;

  for(i=0; i<MAX_OUTGOING_MESSAGES; i++)
  {
    clear_outgoing_message(i);
  }
}





void clear_outgoing_messages(void)
{
  int i;

  for (i=0; i<MAX_OUTGOING_MESSAGES; i++)
  {
    clear_outgoing_message(i);
  }

  begin_critical_section(&send_message_dialog_lock, "messages.c:clear_outgoing_messages" );

  /* clear message send buttons */
  for (i=0; i<MAX_MESSAGE_WINDOWS; i++)
  {
    /* find station  */
//        if (mw[i].send_message_dialog!=NULL) /* clear submit */
//            XtSetSensitive(mw[i].button_ok,TRUE);
  }

  end_critical_section(&send_message_dialog_lock, "messages.c:clear_outgoing_messages" );

}





// Bumps message sequence ID up to the next value.
//
// Roll over message_counter if we hit the max.  Now with Reply/Ack
// protocol the max is only two characters worth.  We changed to
// sending the sequence number in Base-?? format in order to get
// more range from the 2-character variable.
//
int bump_message_counter(char *counter)
{
  return xastir_bump_message_counter(counter);
}





// Adds a message to the outgoing message queue.  Doesn't actually
// cause a transmit.  "check_and_transmit_messages()" is the
// function which actually gets things moving.
//
// We also stuff the message into the main message queue so that the
// queued messages will appear in the Send Message box.
//
/* Legacy presentation adapter. The shared engine owns queue/timers/history;
 * the old transport is called here, never from protocol decoding. */
static XastirMessages *legacy_delivery;
static int legacy_message_transmit(void *user, const char *info, int device,
                                   const char *path, char *error, size_t capacity)
{
  (void)user; (void)device; (void)error; (void)capacity;
  char recipient[10] = {0};
  if (info[0] == ':' && strlen(info) >= 11) {
    memcpy(recipient, info + 1, 9);
    remove_trailing_spaces(recipient);
  }
  transmit_message_data(recipient, (char *)info, (char *)path);
  return 1;
}
static XastirMessages *legacy_message_engine(void)
{
  if (!legacy_delivery) {
    legacy_delivery = xastir_messages_create(my_callsign);
    if (legacy_delivery)
      xastir_messages_set_callbacks(legacy_delivery, NULL, legacy_message_transmit, NULL);
  }
  return legacy_delivery;
}
void output_message(char *from, char *to, char *message, char *path)
{
  (void)from;
  XastirMessages *engine = legacy_message_engine();
  char error[256];
  if (engine && message)
    xastir_messages_send(engine, to, message, group_active(to), 0,
                         path ? path : "", (int64_t)sec_now() * 1000, error, sizeof(error));
}





// Here we're doing some routing of the transmitted packets.  We
// want to keep Xastir from transmitting on ports that aren't
// actively being used in the QSO, but also cover the case where
// ports can go up/down during the QSO.
//
// Note that igates might get into the act quite a bit for RF<->RF
// QSO's if we're sending to the internet too, but that's a bug in
// the igate software, and not something that Xastir should try to
// correct itself.
//
void transmit_message_data(char *to, char *message, char *path)
{
  DataRow *p_station;

  if (debug_level & 2)
  {
    fprintf(stderr,"Transmitting data to %s : %s\n",to,message);
  }

  p_station = NULL;


  if (strcmp(to, my_callsign) == 0)   // My station message
  {

    // Send out all active ports

    if (debug_level & 2)
    {
      fprintf(stderr,"My call VIA any way\n");
    }

    output_my_data(message,-1,0,0,0,path);

    // All done
    return;
  }


  if (!search_station_name(&p_station,to,1))
  {

    // No data record found for this station.  Send to all
    // active ports.

    if (debug_level & 2)
    {
      fprintf(stderr,"VIA any way\n");
    }

    output_my_data(message,-1,0,0,0,path);

    // All done
    return;
  }


  if (debug_level & 2)
  {
    fprintf(stderr,"found station %s\n",p_station->call_sign);
  }


  // It's not being sent to my callsign but to somebody else
  // "out there".  Because the truth is...


  if ( ((p_station->flag & ST_VIATNC) != 0)
       && (heard_via_tnc_in_past_hour(to)) )
  {

    int port_num;


    // Station was heard via a TNC port within the previous
    // hour.  Send to TNC port it was heard on.
    //
    output_my_data(message,p_station->heard_via_tnc_port,0,0,0,path);

    // Send to all internet ports.  Iterate through the port
    // definitions looking for internet ports, send the message
    // out once to each.
    //
    for (port_num = 0; port_num < MAX_IFACE_DEVICES; port_num++)
    {

      // If it's an internet port, send the message.
      if (port_data[port_num].device_type == DEVICE_NET_STREAM)
      {
        output_my_data(message,port_num,0,0,0,path);
      }
    }

    // All done
    return;
  }

  else if (p_station->data_via==DATA_VIA_NET)
  {
    int port_num;
    int active_internet_ports_found = 0;


    // Station was heard over an internet interface.  Check
    // whether we have any internet interfaces available with TX
    // enabled.  If so, send out those ports.  Else drop through
    // and hit the TRANSMIT-ALL clause at the end of this
    // function.

    // Iterate through the port definitions looking for internet
    // ports with transmit enabled.  Send the message out once
    // to each.
    //
    for (port_num = 0; port_num < MAX_IFACE_DEVICES; port_num++)
    {

      // If it's an internet port and transmit is enabled,
      // send the message and set the flag.
      if ( (port_data[port_num].device_type == DEVICE_NET_STREAM)
           && (port_data[port_num].active == DEVICE_IN_USE)
           && (port_data[port_num].status == DEVICE_UP)
           && (devices[port_num].transmit_data == 1) )
      {

        // Found a tx-enabled internet port that was up and
        // running.  Send the message out this port.
        output_my_data(message,port_num,0,0,0,path);

        active_internet_ports_found++;
      }
    }

    if (active_internet_ports_found)
    {
      // We found at least one tx-enabled internet interface
      // that was up and running.

      // All done.
      return;
    }
    else    // No active tx-enabled internet ports were found.
    {
      // Drop through to the TRANSMIT-ALL clause below.
    }
  }


  // We've NOT heard this station on a TNC port within the
  // last hour and have no active tx-enabled internet ports to
  // send to.  Send to ALL active ports.

  if (debug_level & 2)
  {
    fprintf(stderr,"VIA any way\n");
  }

  output_my_data(message,-1,0,0,0,path);
}





// The below variables and functions implement the capability to
// schedule ACK's some number of seconds out from the current time.
// We use it to schedule duplicate ACK's at 30/60/120 seconds out,
// but only if we see duplicate message lines from remote stations.
//
// Create a struct to hold the delayed ack's.
typedef struct _delayed_ack_record
{
  char to_call_sign[MAX_CALLSIGN+1];
  char message_line[MAX_MESSAGE_OUTPUT_LENGTH+1+5+1];
  char path[200];
  time_t active_time;
  struct _delayed_ack_record *next;
} delayed_ack_record, *delayed_ack_record_p;

// And a pointer to a list of them.
delayed_ack_record_p delayed_ack_list_head = NULL;


void transmit_message_data_delayed(char *to, char *message,
                                   char *path, time_t when)
{
  delayed_ack_record_p ptr = delayed_ack_list_head;


  // We need to run down the current list looking for any records
  // that are identical and within 30 seconds time-wise of this
  // one.  If so, don't allocate a new record.  This keeps the
  // dupes down on transmit so that at the most we transmit one
  // ack per 30 seconds per QSO, except of course for real-time
  // ack's which don't go through this function.

  // Run through the queue and check each record
  while (ptr != NULL)
  {

    if ( strcmp(ptr->to_call_sign,to) == 0
         && strcmp(ptr->message_line,message) == 0 )
    {

      //
      // We have matches on call_sign and message.  Check the
      // time next.
      //
      if (labs(when - ptr->active_time) < 30)
      {
        //
        // We're within 30 seconds of an identical ack.
        // Drop this new one (don't add it).
        //

//fprintf(stderr,"Dropping delayed ack: Too close timewise to another: %s, %s\n",
//    to, message);

        return; // Don't allocate new record on queue
      }
    }
    ptr = ptr->next;
  }

  // If we made it to here, there aren't any queued ACK's that are
  // close enough in time to drop this new one.  Add it to the
  // queue.

//fprintf(stderr, "Queuing ACK for delayed transmit: %s, %s\n",
//    to, message);

  // Allocate a record to hold it
  ptr = (delayed_ack_record_p)malloc(sizeof(delayed_ack_record));

  // Fill in the record
  xastir_snprintf(ptr->to_call_sign,
                  sizeof(ptr->to_call_sign),
                  "%s",
                  to);

  xastir_snprintf(ptr->message_line,
                  sizeof(ptr->message_line),
                  "%s",
                  message);

  if (path == NULL)
  {
    ptr->path[0] = '\0';
  }
  else
  {
    xastir_snprintf(ptr->path,
                    sizeof(ptr->path),
                    "%s",
                    path);
  }

  ptr->active_time = when;

  // Add the record to the head of the list
  ptr->next = delayed_ack_list_head;
  delayed_ack_list_head = ptr;
}





time_t delayed_transmit_last_check = (time_t)0;


void check_delayed_transmit_queue(int curr_sec)
{
  delayed_ack_record_p ptr = delayed_ack_list_head;
  int active_records = 0;


  // Skip this function if we did it during this second already.
  if (delayed_transmit_last_check == curr_sec)
  {
    return;
  }
  delayed_transmit_last_check = curr_sec;

//fprintf(stderr, "Checking delayed TX queue for something to transmit.\n");
//fprintf(stderr, ".");

  // Run down the linked list checking every record.
  while (ptr != NULL)
  {
    if (ptr->active_time != 0)     // Active record
    {
      char new_path[MAX_LINE_SIZE+1];


//fprintf(stderr, "Found active record\n");

      active_records++;


      // Check for a custom path having been set in the Send
      // Message dialog.  If so, use this for our outgoing
      // path instead and reset all of the queued message
      // paths to this station to this new path.
      //
      get_send_message_path(ptr->to_call_sign,
                            new_path,
                            sizeof(new_path));

      if (new_path[0] != '\0'
          && strcmp(new_path, ptr->path) != 0)
      {

        // We have a custom path set which is different than
        // the path saved with the outgoing message.  Change
        // the path to match the new path.
        //
//fprintf(stderr,
//    "Changing queued ack's to new path: %s\n",
//    new_path);

        memcpy(ptr->path, new_path, sizeof(ptr->path));
        ptr->path[sizeof(ptr->path)-1] = '\0';  // Terminate string
      }


      if (ptr->active_time <= sec_now())
      {
        // Transmit it
//fprintf(stderr,"Found something delayed to transmit!  %ld\n",sec_now());

        if (ptr->path[0] == '\0')
        {
          transmit_message_data(ptr->to_call_sign,
                                ptr->message_line,
                                NULL);
        }
        else
        {
          transmit_message_data(ptr->to_call_sign,
                                ptr->message_line,
                                ptr->path);
        }

        ptr->active_time = (time_t)0;
      }
    }
    ptr = ptr->next;
  }

  // Check if entire list contains inactive records.  If so,
  // delete the list.
  //
  if (!active_records && (delayed_ack_list_head != NULL))
  {
    // No active records, but the list isn't empty.  Reclaim the
    // records in the list.
    while (delayed_ack_list_head != NULL)
    {
      ptr = delayed_ack_list_head->next;
      free(delayed_ack_list_head);
//fprintf(stderr,"Free'ing delayed_ack record\n");
      delayed_ack_list_head = ptr;
    }
  }
}





void check_and_transmit_messages(time_t now)
{
  XastirMessages *engine = legacy_message_engine();
  if (engine) xastir_messages_process(engine, (int64_t)now * 1000);
}





// Function which marks a message as ack'ed in the transmit queue
// and releases the next message to allow it to be transmitted.
// Handles REPLY-ACK format or normal ACK format just fine.
//
void clear_acked_message(char *from, char *to, char *seq)
{
  XastirMessages *engine = legacy_message_engine();
  if (engine) xastir_messages_acknowledge(engine, from, to, seq, 0);
}





// This routine is not currently used.
//
void send_queued(char *to)
{
  int i;

  for (i=0; i<MAX_OUTGOING_MESSAGES ; i++)
  {
    /* Check for messages to call */
    if (message_pool[i].active==MESSAGE_ACTIVE)
      if (strcmp(to,message_pool[i].to_call_sign)==0)
      {
        message_pool[i].active_time=0;
      }

  }
}

