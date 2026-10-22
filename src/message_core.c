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

/* Delivery state derived from messages.c's transmit queue and db.c's message
 * decoder/history. Transport and presentation are explicit callbacks. */
#include "message_core.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int xastir_bump_message_counter(char *message_counter)
{

  int bump_warning = 0;
  message_counter[2] = '\0';  // Terminate at 2 chars

  // Increment the least significant digit
  message_counter[1]++;

  // Span the gaps between the correct ranges
  if (message_counter[1] == ':')
  {
    message_counter[1] = 'A';
  }

  if (message_counter[1] == '[')
  {
    message_counter[1] = 'a';
  }

  if (message_counter[1] == '{')
  {
    message_counter[1] = '0';
    message_counter[0]++;   // Roll over to next char
  }

  // Span the gaps between the correct ranges
  if (message_counter[0] == ':')
  {
    message_counter[0] = 'A';
  }

  if (message_counter[0] == '[')
  {
    message_counter[0] = 'a';
  }

  if (message_counter[0] == '{')
  {
    message_counter[0] = '0';
    bump_warning = 1;
  }
  return bump_warning;
}

int xastir_message_valid_recipient(const char *name)
{
  size_t n = name ? strlen(name) : 0;
  if (!n || n > 9 || !isalnum((unsigned char)name[0])) return 0;
  for (size_t i = 0; i < n; ++i)
    if (!((name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '-')) return 0;
  return 1;
}
int xastir_message_valid_text(const char *text)
{
  size_t n = text ? strlen(text) : 0;
  int nonspace = 0;
  if (!n || n > 67) return 0;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = text[i];
    if (c < 32 || c > 126 || c == '|' || c == '~' || c == '{') return 0;
    if (c != ' ') nonspace = 1;
  }
  return nonspace;
}
static int valid_id(const char *id)
{
  size_t n = strlen(id);
  if (!n || n > 5) return 0;
  for (size_t i = 0; i < n; ++i)
    if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= 'a' && id[i] <= 'z')
        || (id[i] >= '0' && id[i] <= '9'))) return 0;
  return 1;
}
static void copy(char *out, size_t capacity, const char *in)
{
  if (capacity) snprintf(out, capacity, "%s", in ? in : "");
}
static void normalize(char *s)
{
  size_t n = strlen(s), lead = strspn(s, " ");
  while (n > lead && s[n-1] == ' ') --n;
  memmove(s, s+lead, n-lead); s[n-lead] = 0;
  for (char *p = s; *p; ++p) *p = (char)toupper((unsigned char)*p);
}
int xastir_message_valid_path(const char *path)
{
  if (!path || !*path) return 1;
  if (strlen(path) >= 200) return 0;
  char value[200]; copy(value, sizeof(value), path); normalize(value);
  if (!*value) return 1;
  char *start = value;
  for (int count = 0; count < 8; ++count) {
    char *end = strchr(start, ',');
    if (end) *end = 0;
    normalize(start);
    char *dash = strchr(start, '-');
    if (dash) {
      *dash++ = 0;
      size_t n = strlen(dash);
      if (!n || n > 2 || !isdigit((unsigned char)dash[0]) || (n == 2 && !isdigit((unsigned char)dash[1]))
          || atoi(dash) > 15 || (n == 2 && dash[0] != '1')) return 0;
    }
    size_t n = strlen(start);
    if (!n || n > 6) return 0;
    for (size_t i = 0; i < n; ++i) if (!isalnum((unsigned char)start[i])) return 0;
    if (!end) return 1;
    start = end+1;
  }
  return 0;
}
/* The header / last-{ / Reply-ACK separation follows decode_message in db.c,
 * with length validation before copies and no calls to dialogs or igating. */
int xastir_message_bulletin_kind(const char *recipient)
{
  if (!xastir_message_valid_recipient(recipient) || strlen(recipient) < 4 || strncmp(recipient, "BLN", 3)) return 0;
  return recipient[3] >= 'A' && recipient[3] <= 'Z' ? 2 : recipient[3] >= '0' && recipient[3] <= '9' ? 1 : 0;
}
static int bulletin_valid_text(const char *text)
{
  if (!text || strlen(text) > 67) return 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
    if (*p < 32 || *p > 126 || *p == '|' || *p == '~' || *p == '{') return 0;
  return 1;
}
int xastir_decode_message(const char *information, XastirDecodedMessage *out)
{
  if (!information || !out) return 0;
  size_t n = strlen(information);
  if (n < 11 || n > 127 || information[0] != ':' || information[10] != ':') return 0;
  memset(out, 0, sizeof(*out));
  memcpy(out->recipient, information+1, 9); normalize(out->recipient);
  if (!xastir_message_valid_recipient(out->recipient)) return 0;
  out->bulletin = xastir_message_bulletin_kind(out->recipient);
  char body[117]; copy(body, sizeof(body), information+11);
  n = strlen(body);
  while (n && (body[n-1] == '\r' || body[n-1] == '\n')) body[--n] = 0;
  if (!out->bulletin && (!strncmp(body, "ack", 3) || !strncmp(body, "rej", 3))) {
    char *id = body+3, *reply = strchr(id, '}');
    if (reply) {
      *reply++ = 0;
      if (*reply && !valid_id(reply)) return 0;
      if (strchr(reply, '}')) return 0;
    }
    if (valid_id(id)) {
      copy(out->id, sizeof(out->id), id);
      copy(out->reply_id, sizeof(out->reply_id), reply);
      out->kind = body[0] == 'a' ? 1 : 2;
      return 1;
    }
    if (reply) return 0;
  }
  char *marker = strrchr(body, '{');
  if (marker) {
    char *id = marker+1;
    if (strlen(id) >= sizeof(out->original_id)) return 0;
    copy(out->original_id, sizeof(out->original_id), id);
    char *reply = strchr(id, '}');
    if (reply) {
      *reply++ = 0;
      if (*reply && !valid_id(reply)) return 0;
      out->reply_capable = 1;
      copy(out->reply_id, sizeof(out->reply_id), reply);
    }
    if (!valid_id(id)) return 0;
    copy(out->id, sizeof(out->id), id);
    *marker = 0;
  } else if (!out->bulletin && !strncmp(body, "AA:", 3)) {
    char *reply = strrchr(body, '}');
    if (reply && valid_id(reply+1)) {
      copy(out->reply_id, sizeof(out->reply_id), reply+1); *reply = 0;
    }
  }
  if (!(out->bulletin ? bulletin_valid_text(body) : xastir_message_valid_text(body))) return 0;
  copy(out->text, sizeof(out->text), body);
  return 1;
}

#define HISTORY_LIMIT 2000
#define CACHE_LIMIT 2048
#define GROUP_LIMIT 128
struct Peer {
  char call[10], ack[6], path[200];
  int capable, device, route_valid;
  int64_t auto_time, last_seen;
  int auto_sent;
};
struct Seen {
  char sender[10], recipient[10], id[6], text[68];
  int64_t time, ack_time;
  int used, ack_sent;
};
struct XastirMessages {
  XastirMessageStore history;
  Message_transmit pool[MAX_OUTGOING_MESSAGES];
  struct Peer peers[CACHE_LIMIT];
  struct Seen seen[CACHE_LIMIT];
  size_t peer_count, seen_next;
  char callsign[10], counter[3], auto_text[65], groups[GROUP_LIMIT][10];
  size_t group_count;
  int auto_reply, satellite;
  uint64_t next_key, revision;
  void *user;
  XastirMessageTransmit tx;
  XastirMessageIncoming incoming;
};
XastirMessages *xastir_messages_create(const char *callsign)
{
  XastirMessages *core = calloc(1, sizeof(*core));
  if (!core) return NULL;
  copy(core->callsign, sizeof(core->callsign), callsign); normalize(core->callsign);
  /* Avoid silently truncating a bad configured callsign into a valid one. */
  if (callsign && strlen(callsign) > 9) core->callsign[0] = 0;
  copy(core->counter, sizeof(core->counter), "00"); core->next_key = 1;
  xastir_messages_add_group(core, "ALL");
  xastir_messages_add_group(core, "CQ");
  xastir_messages_add_group(core, "QST");
  return core;
}
void xastir_messages_destroy(XastirMessages *c)
{
  if (c) { xastir_message_store_free(&c->history); free(c); }
}
void xastir_messages_set_callbacks(XastirMessages *c, void *user, XastirMessageTransmit tx, XastirMessageIncoming incoming)
{ c->user = user; c->tx = tx; c->incoming = incoming; }
const XastirMessageStore *xastir_messages_history(const XastirMessages *c) { return &c->history; }
uint64_t xastir_messages_revision(const XastirMessages *c) { return c->revision; }
static Message *record(XastirMessages *c, uint64_t key)
{
  for (long i = 0; i < c->history.count; ++i)
    if (c->history.data[i].active == RECORD_ACTIVE && c->history.data[i].key == key) return &c->history.data[i];
  return NULL;
}
static void trim(XastirMessages *c)
{
  int count = 0;
  for (long i = 0; i < c->history.count; ++i) if (c->history.data[i].active == RECORD_ACTIVE) ++count;
  while (count > HISTORY_LIMIT) {
    Message *oldest = NULL;
    for (long i = 0; i < c->history.count; ++i) {
      Message *m = &c->history.data[i];
      if (m->active == RECORD_ACTIVE && !m->pending && (!oldest || m->key < oldest->key)) oldest = m;
    }
    if (!oldest) break;
    oldest->active = RECORD_NOTACTIVE; --count;
  }
}
static uint64_t append(XastirMessages *c, Message *m)
{
  m->active = RECORD_ACTIVE; m->key = c->next_key++;
  m->sec_heard = (time_t)(m->timestamp_ms / 1000);
  if (!xastir_msg_input_database(&c->history, m)) return 0;
  trim(c); ++c->revision;
  return m->key;
}
static Message *bulletin_record(XastirMessages *c, const char *sender, const char *recipient)
{
  for (long i = 0; i < c->history.count; ++i) {
    Message *m = &c->history.data[i];
    if (m->active == RECORD_ACTIVE && m->type == MESSAGE_BULLETIN && !m->outgoing
        && !strcmp(m->from_call_sign, sender) && !strcmp(m->call_sign, recipient)) return m;
  }
  return NULL;
}
int xastir_messages_store_bulletin(XastirMessages *c, const char *sender, const char *recipient,
                                  const char *text, int device, int64_t now)
{
  if (!c || !xastir_message_valid_recipient(sender) || !xastir_message_bulletin_kind(recipient)
      || !bulletin_valid_text(text) || now < 0) return 0;
  Message *existing = bulletin_record(c, sender, recipient);
  if (existing) {
    /* msg_data_add in db.c keeps duplicate timestamps unless the text changed
     * or the previous bulletin is more than eight hours old. */
    if (now < existing->timestamp_ms) return 1;
    const int different = strcmp(existing->message_line, text);
    const int fresh = different || now - existing->timestamp_ms > 8 * 3600000;
    if (!fresh && existing->device == device) return 1;
    if (different) copy(existing->message_line, sizeof(existing->message_line), text);
    if (fresh) { existing->timestamp_ms = now; existing->sec_heard = (time_t)(now / 1000); }
    existing->device = device; existing->data_via = device == 256 ? 'I' : 'T';
    ++c->revision; return 1;
  }
  Message m = {0}; m.type = MESSAGE_BULLETIN; m.group = 1;
  m.timestamp_ms = now; m.device = device; m.data_via = device == 256 ? 'I' : 'T'; m.status = XMSG_HEARD;
  copy(m.from_call_sign, sizeof(m.from_call_sign), sender); copy(m.call_sign, sizeof(m.call_sign), recipient);
  copy(m.message_line, sizeof(m.message_line), text);
  return append(c, &m) != 0;
}
int xastir_messages_restore(XastirMessages *c, const Message *saved)
{
  if (!c || !saved || !saved->key || saved->key >= INT64_MAX ||
      saved->status < XMSG_QUEUED || saved->status > XMSG_INTERRUPTED ||
      record(c, saved->key)) return 0;
  Message m = *saved;
  m.active = RECORD_ACTIVE;
  m.call_sign[sizeof(m.call_sign)-1] = 0;
  m.from_call_sign[sizeof(m.from_call_sign)-1] = 0;
  m.message_line[sizeof(m.message_line)-1] = 0;
  m.seq[sizeof(m.seq)-1] = 0;
  m.path[sizeof(m.path)-1] = 0;
  m.error[sizeof(m.error)-1] = 0;
  if (c->next_key <= m.key) c->next_key = m.key + 1;
  if (m.type == MESSAGE_BULLETIN && !m.outgoing) {
    if (!xastir_message_bulletin_kind(m.call_sign) || !xastir_message_valid_recipient(m.from_call_sign)
        || !bulletin_valid_text(m.message_line)) return 0;
    m.pending = m.retryable = m.tries = 0; m.next_retry_ms = 0;
    m.group = 1; m.status = XMSG_HEARD; m.seq[0] = m.path[0] = m.error[0] = 0;
    Message *existing = bulletin_record(c, m.from_call_sign, m.call_sign);
    if (existing) {
      if (m.timestamp_ms >= existing->timestamp_ms) { const uint64_t key = existing->key; *existing = m; existing->key = key; ++c->revision; }
      return 1;
    }
  }
  if (m.pending) {
    m.status = XMSG_INTERRUPTED;
    m.retryable = m.outgoing;
  }
  m.pending = 0;
  m.next_retry_ms = 0;
  if (!xastir_msg_input_database(&c->history, &m)) return 0;
  if (!m.outgoing && m.type != MESSAGE_BULLETIN && m.device >= 0 && m.device <= 256) {
    struct Seen *seen = &c->seen[c->seen_next++ % CACHE_LIMIT];
    memset(seen, 0, sizeof(*seen));
    seen->used = 1; seen->time = m.timestamp_ms;
    copy(seen->sender, sizeof(seen->sender), m.from_call_sign);
    copy(seen->recipient, sizeof(seen->recipient), m.call_sign);
    copy(seen->id, sizeof(seen->id), m.seq);
    copy(seen->text, sizeof(seen->text), m.message_line);
  }
  if (c->next_key <= m.key) c->next_key = m.key+1;
  trim(c); ++c->revision;
  return 1;
}
uint64_t xastir_messages_next_key(const XastirMessages *c) { return c->next_key; }
const char *xastir_messages_counter(const XastirMessages *c) { return c->counter; }
int xastir_messages_restore_counter(XastirMessages *c, const char *counter, uint64_t next_key)
{
  if (!c || !counter || strlen(counter) != 2 || !next_key || next_key >= INT64_MAX) return 0;
  for (int i=0;i<2;++i)
    if (!((counter[i]>='0' && counter[i]<='9') || (counter[i]>='A' && counter[i]<='Z') ||
          (counter[i]>='a' && counter[i]<='z'))) return 0;
  copy(c->counter, sizeof(c->counter), counter);
  if (c->next_key < next_key) c->next_key = next_key;
  return 1;
}
static int error(char *out, size_t capacity, const char *text) { copy(out, capacity, text); return 0; }
static struct Peer *peer(XastirMessages *c, const char *call)
{
  for (size_t i = 0; i < c->peer_count; ++i) if (!strcmp(c->peers[i].call, call)) return &c->peers[i];
  struct Peer *p = NULL;
  if (c->peer_count < CACHE_LIMIT) p = &c->peers[c->peer_count++];
  else {
    /* Retain peers with live messages; recycle the least recently used idle peer. */
    for (size_t i = 0; i < c->peer_count; ++i) {
      int pending = 0;
      for (int j = 0; j < MAX_OUTGOING_MESSAGES; ++j)
        if (c->pool[j].active == MESSAGE_ACTIVE && !strcmp(c->pool[j].to_call_sign, c->peers[i].call)) pending = 1;
      if (!pending && (!p || c->peers[i].last_seen < p->last_seen)) p = &c->peers[i];
    }
  }
  if (!p) return NULL;
  memset(p, 0, sizeof(*p)); copy(p->call, sizeof(p->call), call); return p;
}
static int information(char *out, size_t capacity, const char *recipient, const char *body)
{
  int n = snprintf(out, capacity, ":%-9s:%s", recipient, body);
  return n >= 0 && (size_t)n < capacity;
}
static int transmit(XastirMessages *c, const char *info, int device, const char *path, char *err, size_t capacity)
{
  if (err && capacity) err[0] = 0;
  if (!c->tx) return error(err, capacity, "No transport");
  return c->tx(c->user, info, device, path, err, capacity);
}
static int allocate_id(XastirMessages *c, char out[6])
{
  for (int n = 0; n < 62*62; ++n) {
    copy(out, 6, c->counter); xastir_bump_message_counter(c->counter);
    int used = 0;
    for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i)
      if (c->pool[i].active == MESSAGE_ACTIVE && !strcmp(c->pool[i].seq, out)) used = 1;
    if (!used) return 1;
  }
  return 0;
}
static Message_transmit *free_slot(XastirMessages *c)
{
  for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) if (c->pool[i].active != MESSAGE_ACTIVE) return &c->pool[i];
  return NULL;
}
/* Queue admission follows output_message(): per-recipient ordering and an
 * initial seven-second interval. FIFO keys replace its wrapping-ID ordering. */
static Message_transmit *enqueue(XastirMessages *c, Message *m)
{
  Message_transmit *q = free_slot(c);
  if (!q) return NULL;
  memset(q, 0, sizeof(*q)); q->active = MESSAGE_ACTIVE;
  copy(q->to_call_sign, sizeof(q->to_call_sign), m->call_sign);
  copy(q->from_call_sign, sizeof(q->from_call_sign), m->from_call_sign);
  copy(q->message_line, sizeof(q->message_line), m->message_line);
  copy(q->path, sizeof(q->path), m->path); copy(q->seq, sizeof(q->seq), m->seq);
  q->key = m->key; q->device = m->device; q->group = m->group;
  q->tries = 0; q->next_time = 7; q->active_time = 0;
  return q;
}
static void attempt(XastirMessages *c, Message_transmit *q, int64_t now)
{
  Message *m = record(c, q->key);
  if (!m) { q->active = MESSAGE_CLEAR; return; }
  ++c->revision;
  if (!q->group) {
    for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) {
      Message_transmit *earlier = &c->pool[i];
      if (earlier->active == MESSAGE_ACTIVE && earlier->key < q->key && !earlier->group
          && !strcmp(earlier->to_call_sign, q->to_call_sign)) {
        q->wait_on_first_ack = 1; m->status = XMSG_EARLIER;
        m->next_retry_ms = q->active_time = now + 1000; return;
      }
    }
  }
  q->wait_on_first_ack = 0;
  if (q->tries >= MAX_TRIES) {
    q->active = MESSAGE_CLEAR; m->pending = 0; m->status = XMSG_TIMEOUT; return;
  }
  char body[90], info[128]; copy(body, sizeof(body), q->message_line);
  if (!q->group) {
    struct Peer *p = peer(c, q->to_call_sign);
    snprintf(body, sizeof(body), "%s{%s}%s", q->message_line, q->seq,
        p && p->capable && strlen(p->ack) <= 2 ? p->ack : "");
  }
  if (!information(info, sizeof(info), q->to_call_sign, body)) return;
  if (!transmit(c, info, q->device, q->path, m->error, sizeof(m->error))) {
    if (!m->error[0]) copy(m->error, sizeof(m->error), "No transport");
    m->status = XMSG_WAITING; m->next_retry_ms = q->active_time = now + 7000; return;
  }
  ++q->tries; m->tries = q->tries;
  if (q->group) { q->active = MESSAGE_CLEAR; m->pending = 0; m->status = XMSG_GROUP_SENT; }
  else {
    m->status = XMSG_AWAITING;
    m->next_retry_ms = q->active_time = now + q->next_time * 1000;
    /* check_and_transmit_messages(): exponential backoff, cap ten minutes. */
    q->next_time *= 2;
    if (q->next_time > 600) q->next_time = 600;
    m->interval = q->next_time;
  }
}
int xastir_messages_send(XastirMessages *c, const char *to, const char *text, int group, int device,
                         const char *path, int64_t now, char *err, size_t capacity)
{
  if (!xastir_message_valid_recipient(c->callsign)) return error(err, capacity, "Configure your station callsign first.");
  if (!xastir_message_valid_recipient(to)) return error(err, capacity, "Recipient must be 1–9 letters, digits or hyphens.");
  if (!xastir_message_valid_text(text)) return error(err, capacity, "Message text must contain 1–67 printable ASCII characters without |, ~, or {.");
  if (!xastir_message_valid_path(path)) return error(err, capacity, "Invalid digipeater in path.");
  if (!c->tx || device < 0) return error(err, capacity, "Select a configured interface.");
  if (!free_slot(c)) return error(err, capacity, "The outgoing queue is full (100 pending messages).");
  Message m = {0};
  copy(m.call_sign, sizeof(m.call_sign), to); copy(m.from_call_sign, sizeof(m.from_call_sign), c->callsign);
  copy(m.message_line, sizeof(m.message_line), text); copy(m.path, sizeof(m.path), path);
  m.group = group || xastir_messages_is_group(c, to); m.outgoing = m.pending = m.retryable = 1;
  m.device = device; m.timestamp_ms = now; m.status = XMSG_QUEUED;
  if (!m.group && !allocate_id(c, m.seq)) return error(err, capacity, "No message identifiers are available.");
  if (m.group && !xastir_messages_add_group(c, to)) return error(err, capacity, "The message group list is full.");
  struct Peer *p = m.group ? NULL : peer(c, to);
  if (!m.group && !p) return error(err, capacity, "The message peer list is full.");
  uint64_t key = append(c, &m);
  if (!key) return error(err, capacity, "Cannot allocate message history.");
  if (p) { p->last_seen = now; p->device = device; copy(p->path, sizeof(p->path), path); p->route_valid = 1; }
  c->auto_reply = 0;
  Message_transmit *q = enqueue(c, &m); attempt(c, q, now);
  return 1;
}
void xastir_messages_process(XastirMessages *c, int64_t now)
{
  /* Single-threaded callbacks may inspect state but must not mutate it. */
  for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i)
    if (c->pool[i].active == MESSAGE_ACTIVE && c->pool[i].active_time <= now) attempt(c, &c->pool[i], now);
}
void xastir_messages_cancel(XastirMessages *c, uint64_t key)
{
  Message *m = record(c, key);
  if (!m || !m->pending) return;
  for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) if (c->pool[i].key == key) c->pool[i].active = MESSAGE_CLEAR;
  m->pending = 0; m->status = XMSG_CANCELLED; ++c->revision;
}
int xastir_messages_retry(XastirMessages *c, uint64_t key, int64_t now, char *err, size_t capacity)
{
  Message *m = record(c, key);
  if (!m || !m->outgoing || !m->retryable || m->status == XMSG_ACKED || m->status == XMSG_REJECTED) return 1;
  Message_transmit *q = NULL;
  for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) if (c->pool[i].active == MESSAGE_ACTIVE && c->pool[i].key == key) q = &c->pool[i];
  if (!q) {
    if (!free_slot(c)) return error(err, capacity, "The outgoing queue is full (100 pending messages).");
    /* An old ID can be reused after rollover; a manual retry must not steal it. */
    for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) {
      if (!m->group && c->pool[i].active == MESSAGE_ACTIVE && !strcmp(c->pool[i].seq, m->seq)) {
        if (!allocate_id(c, m->seq)) return error(err, capacity, "No message identifiers are available.");
        break;
      }
    }
    xastir_message_store_reindex(&c->history);
    q = enqueue(c, m);
  }
  q->tries = 0; q->next_time = 7; m->tries = 0; m->pending = 1;
  attempt(c, q, now); return 1;
}
void xastir_messages_clear_outgoing(XastirMessages *c)
{
  for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) c->pool[i].active = MESSAGE_CLEAR;
  for (long i = 0; i < c->history.count; ++i)
    if (c->history.data[i].outgoing) c->history.data[i].active = RECORD_NOTACTIVE;
  ++c->revision;
}
static void acknowledge(XastirMessages *c, const char *from, const char *seq, int rejected)
{
  /* clear_acked_message(): exact source, recipient (including SSID), and sequence. */
  for (int i = 0; i < MAX_OUTGOING_MESSAGES; ++i) {
    Message_transmit *q = &c->pool[i];
    if (q->active == MESSAGE_ACTIVE && !q->group && !strcmp(q->to_call_sign, from)
        && !strcmp(q->from_call_sign, c->callsign) && !strcmp(q->seq, seq)) {
      Message *m = record(c, q->key);
      q->active = MESSAGE_CLEAR;
      if (m) { m->pending = 0; m->status = rejected ? XMSG_REJECTED : XMSG_ACKED; m->acked = 1; }
      ++c->revision; return;
    }
  }
}
int xastir_messages_query(XastirMessages *c, const char *query, int device, int64_t now, char *err, size_t capacity)
{
  if (strcmp(query, "?APRS?") && strcmp(query, "?IGATE?") && strcmp(query, "?WX?")) return error(err, capacity, "Unsupported station query.");
  if (device < 0) return error(err, capacity, "Select a configured interface.");
  if (!transmit(c, query, device, "", err, capacity)) return 0;
  c->auto_reply = 0;
  Message m = {0}; m.outgoing = m.group = 1; m.status = XMSG_QUERY_SENT; m.device = device; m.timestamp_ms = now;
  copy(m.from_call_sign, sizeof(m.from_call_sign), c->callsign); copy(m.call_sign, sizeof(m.call_sign), "APRS");
  copy(m.message_line, sizeof(m.message_line), query); append(c, &m); return 1;
}
int xastir_messages_set_auto_text(XastirMessages *c, const char *text)
{
  char value[68];
  if (!text || strlen(text) > 64) return 0;
  snprintf(value, sizeof(value), "AA:%s", text);
  if (!xastir_message_valid_text(text) || !xastir_message_valid_text(value)) return 0;
  copy(c->auto_text, sizeof(c->auto_text), text); return 1;
}
const char *xastir_messages_auto_text(const XastirMessages *c) { return c->auto_text; }
void xastir_messages_set_auto(XastirMessages *c, int enabled) { c->auto_reply = !!enabled; }
int xastir_messages_auto(const XastirMessages *c) { return c->auto_reply; }
void xastir_messages_set_satellite(XastirMessages *c, int enabled) { c->satellite = !!enabled; }
int xastir_messages_satellite(const XastirMessages *c) { return c->satellite; }
int xastir_messages_is_group(const XastirMessages *c, const char *name)
{
  for (size_t i = 0; i < c->group_count; ++i) if (!strcmp(c->groups[i], name)) return 1;
  return 0;
}
int xastir_messages_add_group(XastirMessages *c, const char *name)
{
  if (!xastir_message_valid_recipient(name)) return 0;
  if (xastir_messages_is_group(c, name)) return 1;
  if (c->group_count == GROUP_LIMIT) return 0;
  copy(c->groups[c->group_count++], 10, name); return 1;
}
int xastir_decode_message_packet(const char *original, char *sender, size_t capacity, XastirDecodedMessage *decoded)
{
  const char *packet = original, *arrow = NULL, *colon = NULL;
  if (!packet || !sender || !decoded || capacity < 10 || strlen(packet) > 4096
      || strchr(packet, '\r') || strchr(packet, '\n')) return 0;
  for (int depth = 0; depth <= 4; ++depth) {
    arrow = strchr(packet, '>'); if (!arrow || arrow == packet) return 0;
    colon = strchr(arrow+1, ':'); if (!colon || colon == arrow+1) return 0;
    if (colon[1] != '}') break;
    if (depth == 4) return 0;
    packet = colon+2;
  }
  if (arrow - packet > 9) return 0;
  memset(sender, 0, 10); memcpy(sender, packet, (size_t)(arrow-packet)); normalize(sender);
  if (!xastir_message_valid_recipient(sender)) return 0;
  return xastir_decode_message(colon+1, decoded);
}
void xastir_messages_receive(XastirMessages *c, const char *original, int device, int64_t now)
{
  char sender[10]; XastirDecodedMessage decoded;
  if (!xastir_decode_message_packet(original, sender, sizeof(sender), &decoded)) return;
  if (decoded.bulletin) { xastir_messages_store_bulletin(c, sender, decoded.recipient, decoded.text, device, now); return; }
  const int to_me = c->callsign[0] && !strcmp(decoded.recipient, c->callsign);
  if (decoded.kind) {
    if (to_me) acknowledge(c, sender, decoded.id, decoded.kind == 2);
    return;
  }
  if (!strcmp(sender, c->callsign)) {
    for (long i = 0; i < c->history.count; ++i) {
      Message *m = &c->history.data[i];
      if (m->active == RECORD_ACTIVE && m->outgoing && !strcmp(m->call_sign, decoded.recipient)
          && !strcmp(m->message_line, decoded.text) && !strcmp(m->seq, decoded.id)) return;
    }
  }
  struct Peer *p = peer(c, sender); if (!p) return;
  p->last_seen = now;
  if (to_me && decoded.reply_capable) p->capable = 1;
  if (to_me && decoded.reply_id[0]) acknowledge(c, sender, decoded.reply_id, 0);
  struct Seen *seen = NULL;
  for (int i = 0; i < CACHE_LIMIT; ++i) {
    struct Seen *entry = &c->seen[i];
    if (entry->used && !strcmp(entry->sender, sender) && !strcmp(entry->recipient, decoded.recipient)
        && !strcmp(entry->id, decoded.id) && !strcmp(entry->text, decoded.text)) { seen = entry; break; }
  }
  const int duplicate = seen && now - seen->time < (decoded.id[0] ? 3600000 : 30000);
  if (!seen) {
    seen = &c->seen[c->seen_next++ % CACHE_LIMIT]; memset(seen, 0, sizeof(*seen)); seen->used = 1;
    copy(seen->sender, sizeof(seen->sender), sender); copy(seen->recipient, sizeof(seen->recipient), decoded.recipient);
    copy(seen->id, sizeof(seen->id), decoded.id); copy(seen->text, sizeof(seen->text), decoded.text);
  }
  seen->time = now;
  /* Internet reception must never select a previously used RF reply route. */
  const int compatible_route = p->route_valid && ((p->device == 256) == (device == 256));
  int route_device = compatible_route ? p->device : device;
  const char *route_path = compatible_route ? p->path : "";
  if (to_me && strcmp(sender, c->callsign) && decoded.id[0]) {
    copy(p->ack, sizeof(p->ack), decoded.id);
    int throttled = 0;
    for (int i = 0; i < CACHE_LIMIT; ++i) {
      const struct Seen *entry = &c->seen[i];
      if (entry->used && entry->ack_sent && !strcmp(entry->sender, sender) && !strcmp(entry->id, decoded.id)
          && now - entry->ack_time < 30000) throttled = 1;
    }
    if (!c->satellite && !throttled) {
      char body[32], info[128], err[256]; snprintf(body, sizeof(body), "ack%s", decoded.original_id);
      information(info, sizeof(info), sender, body);
      if (transmit(c, info, route_device, route_path, err, sizeof(err))) { seen->ack_sent = 1; seen->ack_time = now; }
    }
  }
  if (!duplicate) {
    Message m = {0}; copy(m.call_sign, sizeof(m.call_sign), decoded.recipient);
    copy(m.from_call_sign, sizeof(m.from_call_sign), sender); copy(m.message_line, sizeof(m.message_line), decoded.text);
    copy(m.seq, sizeof(m.seq), decoded.id); m.timestamp_ms = now; m.device = device;
    m.group = xastir_messages_is_group(c, decoded.recipient) && !to_me;
    m.status = to_me ? XMSG_RECEIVED : XMSG_HEARD;
    if (!append(c, &m)) return;
    if ((to_me || m.group) && c->incoming) c->incoming(c->user, to_me ? sender : decoded.recipient, !to_me);
  }
  if (to_me && !duplicate && c->auto_reply && strcmp(sender, c->callsign) && c->auto_text[0]
      && strncmp(decoded.text, "AA:", 3) && (!p->auto_sent || now - p->auto_time >= 60000)) {
    p->auto_sent = 1; p->auto_time = now;
    char body[80], info[128]; snprintf(body, sizeof(body), "AA:%s%s%s", c->auto_text,
      p->capable && p->ack[0] && strlen(p->ack) <= 2 ? "}" : "",
      p->capable && p->ack[0] && strlen(p->ack) <= 2 ? p->ack : "");
    information(info, sizeof(info), sender, body);
    Message m = {0}; m.outgoing = 1; m.device = route_device; m.timestamp_ms = now;
    copy(m.path, sizeof(m.path), route_path); copy(m.from_call_sign, sizeof(m.from_call_sign), c->callsign);
    copy(m.call_sign, sizeof(m.call_sign), sender); snprintf(m.message_line, sizeof(m.message_line), "AA:%s", c->auto_text);
    m.tries = transmit(c, info, route_device, route_path, m.error, sizeof(m.error)) ? 1 : 0;
    m.status = m.tries ? XMSG_AUTO_SENT : XMSG_AUTO_FAILED; append(c, &m);
  }
}

void xastir_messages_acknowledge(XastirMessages *c, const char *from, const char *to, const char *seq, int rejected)
{
  if (!from || !to || !seq || strcmp(to, c->callsign)) return;
  char id[13];
  if (strlen(seq) >= sizeof(id)) return;
  copy(id, sizeof(id), seq);
  char *reply = strchr(id, '}');
  if (reply) *reply = 0;
  if (valid_id(id)) acknowledge(c, from, id, rejected);
}
