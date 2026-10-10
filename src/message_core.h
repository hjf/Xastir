// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_MESSAGE_CORE_H
#define XASTIR_MESSAGE_CORE_H
#include "message_store.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MESSAGE_ACTIVE 'A'
#define MESSAGE_CLEAR 'C'
#define MAX_OUTGOING_MESSAGES 100
#define MAX_MESSAGE_OUTPUT_LENGTH 67
#define MAX_TRIES 18
/* Transmit record moved out of the Motif messages.h. */
typedef struct {
  char active;
  char to_call_sign[MAX_CALLSIGN+1], from_call_sign[MAX_CALLSIGN+1];
  char message_line[MAX_MESSAGE_OUTPUT_LENGTH+1], path[200], seq[MAX_MESSAGE_ORDER+1];
  int64_t active_time; /* milliseconds at the service boundary */
  time_t next_time; /* retry interval in seconds */
  int tries, wait_on_first_ack;
  uint64_t key;
  int device, group;
} Message_transmit;
typedef enum {
  XMSG_QUEUED, XMSG_EARLIER, XMSG_TIMEOUT, XMSG_WAITING, XMSG_GROUP_SENT,
  XMSG_AWAITING, XMSG_CANCELLED, XMSG_ACKED, XMSG_REJECTED, XMSG_QUERY_SENT,
  XMSG_RECEIVED, XMSG_HEARD, XMSG_AUTO_SENT, XMSG_AUTO_FAILED, XMSG_INTERRUPTED
} XastirMessageStatus;
typedef struct {
  char recipient[10], text[68], id[6], reply_id[6], original_id[13];
  int kind; /* 0 text, 1 ACK, 2 REJ */
  int reply_capable;
} XastirDecodedMessage;
int xastir_decode_message(const char *information, XastirDecodedMessage *decoded);
int xastir_bump_message_counter(char *counter);
int xastir_message_valid_text(const char *text);
int xastir_message_valid_recipient(const char *name);
int xastir_message_valid_path(const char *path);
typedef struct XastirMessages XastirMessages;
/* TX returns acceptance, not RF delivery. A failure consumes no retries.
 * Callback arguments are borrowed for this call. Callbacks must not mutate
 * this same engine; UI notifications should only read it. */
typedef int (*XastirMessageTransmit)(void *, const char *, int, const char *, char *, size_t);
typedef void (*XastirMessageIncoming)(void *, const char *, int);
XastirMessages *xastir_messages_create(const char *callsign);
void xastir_messages_destroy(XastirMessages *core);
void xastir_messages_set_callbacks(XastirMessages *core, void *user, XastirMessageTransmit tx, XastirMessageIncoming incoming);
const XastirMessageStore *xastir_messages_history(const XastirMessages *core);
/* Import archive records without callbacks, ACKs or queue admission. */
int xastir_messages_restore(XastirMessages *, const Message *);
uint64_t xastir_messages_next_key(const XastirMessages *);
const char *xastir_messages_counter(const XastirMessages *);
int xastir_messages_restore_counter(XastirMessages *, const char *, uint64_t next_key);
uint64_t xastir_messages_revision(const XastirMessages *core);
int xastir_messages_send(XastirMessages *, const char *to, const char *text, int group, int device, const char *path, int64_t now, char *error, size_t capacity);
int xastir_messages_query(XastirMessages *, const char *query, int device, int64_t now, char *error, size_t capacity);
void xastir_messages_receive(XastirMessages *, const char *packet, int device, int64_t now);
void xastir_messages_process(XastirMessages *, int64_t now);
void xastir_messages_acknowledge(XastirMessages *, const char *from, const char *to, const char *seq, int rejected);
void xastir_messages_cancel(XastirMessages *, uint64_t key);
int xastir_messages_retry(XastirMessages *, uint64_t key, int64_t now, char *error, size_t capacity);
void xastir_messages_clear_outgoing(XastirMessages *);
int xastir_messages_set_auto_text(XastirMessages *, const char *text);
const char *xastir_messages_auto_text(const XastirMessages *);
void xastir_messages_set_auto(XastirMessages *, int enabled);
int xastir_messages_auto(const XastirMessages *);
void xastir_messages_set_satellite(XastirMessages *, int enabled);
int xastir_messages_satellite(const XastirMessages *);
int xastir_messages_add_group(XastirMessages *, const char *name);
int xastir_messages_is_group(const XastirMessages *, const char *name);
#ifdef __cplusplus
}
#endif
#endif
