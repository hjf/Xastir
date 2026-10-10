// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_core.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
struct Fixture { int sent, online, incoming; char information[128], path[200]; int device; };
static int tx(void *user, const char *info, int device, const char *path, char *error, size_t capacity)
{
  struct Fixture *f = user;
  if (!f->online) { snprintf(error, capacity, "Disconnected"); return 0; }
  ++f->sent; snprintf(f->information, sizeof(f->information), "%s", info);
  snprintf(f->path, sizeof(f->path), "%s", path); f->device = device; return 1;
}
static void incoming(void *user, const char *peer, int group)
{ (void)peer; (void)group; ++((struct Fixture *)user)->incoming; }
static const Message *by_key(XastirMessages *core, uint64_t key)
{
  const XastirMessageStore *store = xastir_messages_history(core);
  for (long i = 0; i < store->count; ++i) if (store->data[i].active == RECORD_ACTIVE && store->data[i].key == key) return &store->data[i];
  return NULL;
}
int main(void)
{
  XastirDecodedMessage decoded;
  CHECK(xastir_decode_message(":LOCAL    :test{A1}B2", &decoded));
  CHECK(!strcmp(decoded.id, "A1") && !strcmp(decoded.reply_id, "B2") && decoded.reply_capable);
  CHECK(xastir_decode_message(":LOCAL    :ackA1}", &decoded) && decoded.kind == 1);
  CHECK(!xastir_decode_message(":LOCAL    :bad{123456", &decoded));
  CHECK(!xastir_decode_message(":LOCAL    :bad{A1}B2}", &decoded));
  CHECK(xastir_decode_message(":LOCAL    :AA:Away}A1", &decoded));
  CHECK(!strcmp(decoded.text, "AA:Away") && !strcmp(decoded.reply_id, "A1"));
  for (int length = 0; length < 12; ++length) {
    char text[] = ":LOCAL    :test{A1"; text[length] = 0;
    CHECK(!xastir_decode_message(text, &decoded));
  }
  CHECK(xastir_message_valid_path("WIDE1-1,WIDE2-2"));
  CHECK(!xastir_message_valid_path("WIDE1-16"));
  char counter[3] = "09"; CHECK(!xastir_bump_message_counter(counter) && !strcmp(counter, "0A"));
  strcpy(counter, "0Z"); CHECK(!xastir_bump_message_counter(counter) && !strcmp(counter, "0a"));
  strcpy(counter, "zz"); CHECK(xastir_bump_message_counter(counter) && !strcmp(counter, "00"));
  XastirMessages *a = xastir_messages_create("LOCAL"), *b = xastir_messages_create("OTHER");
  CHECK(a && b); struct Fixture fixture = {0}; char error[256];
  xastir_messages_set_callbacks(a, &fixture, tx, incoming);
  CHECK(xastir_messages_send(a, "REMOTE", "first", 0, 7, "WIDE1-1", 1000, error, sizeof(error)));
  CHECK(by_key(a, 1)->tries == 0 && by_key(a, 1)->pending);
  CHECK(xastir_messages_history(b)->count == 0);
  fixture.online = 1; xastir_messages_process(a, 8000);
  CHECK(fixture.sent == 1 && !strcmp(fixture.information, ":REMOTE   :first{00}"));
  CHECK(fixture.device == 7 && !strcmp(fixture.path, "WIDE1-1"));
  CHECK(by_key(a, 1)->next_retry_ms == 15000);
  CHECK(xastir_messages_send(a, "REMOTE", "second", 0, 7, "WIDE1-1", 8000, error, sizeof(error)));
  CHECK(fixture.sent == 1 && by_key(a, 2)->status == XMSG_EARLIER);
  xastir_messages_receive(a, "REMOTE>APRS::LOCAL-1  :ack00", 7, 8000); CHECK(by_key(a, 1)->pending);
  xastir_messages_receive(a, "WRONG>APRS::LOCAL    :ack00", 7, 8000); CHECK(by_key(a, 1)->pending);
  xastir_messages_receive(a, "REMOTE>APRS::LOCAL    :ack00", 7, 8000); CHECK(by_key(a, 1)->status == XMSG_ACKED);
  xastir_messages_process(a, 9000); CHECK(fixture.sent == 2 && by_key(a, 2)->tries == 1);
  xastir_messages_cancel(a, 2); CHECK(!by_key(a, 2)->pending);
  CHECK(xastir_messages_retry(a, 2, 9000, error, sizeof(error))); CHECK(by_key(a, 2)->pending);
  for (int i = 0; i < 18; ++i) xastir_messages_process(a, by_key(a, 2)->next_retry_ms);
  CHECK(by_key(a, 2)->status == XMSG_TIMEOUT && by_key(a, 2)->tries == 18);
  int count = fixture.sent;
  xastir_messages_set_satellite(a, 1);
  xastir_messages_receive(a, "REMOTE>APRS::LOCAL    :hello{A1}", 7, 1000000);
  CHECK(fixture.sent == count && fixture.incoming == 1);
  xastir_messages_set_satellite(a, 0);
  xastir_messages_receive(a, "REMOTE>APRS::LOCAL    :hello{A1}", 7, 1030000);
  CHECK(fixture.sent == count+1 && fixture.incoming == 1);
  CHECK(!strcmp(fixture.information, ":REMOTE   :ackA1}"));
  CHECK(xastir_messages_set_auto_text(a, "Away")); xastir_messages_set_auto(a, 1);
  xastir_messages_receive(a, "REMOTE>APRS::LOCAL    :new{A2}", 7, 1060000);
  CHECK(!strcmp(fixture.information, ":REMOTE   :AA:Away}A2"));
  CHECK(xastir_messages_send(a, "CQ", "group", 0, 7, "", 1070000, error, sizeof(error)));
  CHECK(!strcmp(fixture.information, ":CQ       :group") && !xastir_messages_auto(a));
  xastir_messages_clear_outgoing(a);
  /* Retention and recycling preserve live queue records and bounded memory. */
  for (int i = 0; i < 4500; ++i) {
    char packet[100]; snprintf(packet, sizeof(packet), "REMOTE>APRS::OTHER    :heard %d", i);
    xastir_messages_receive(a, packet, 7, 2000000+i);
  }
  const XastirMessageStore *history = xastir_messages_history(a);
  CHECK(history->capacity <= 2200);
  count = 0; for (long i = 0; i < history->count; ++i) if (history->data[i].active == RECORD_ACTIVE) ++count;
  CHECK(count == 2000);
  for (int i = 0; i < 2100; ++i) {
    char packet[100]; snprintf(packet, sizeof(packet), "P%04d>APRS::OTHER    :heard", i);
    xastir_messages_receive(a, packet, 7, 3000000+i);
  }
  CHECK(xastir_messages_send(a, "NEWPEER", "after cache fill", 0, 7, "", 4000000, error, sizeof(error)));
  xastir_messages_destroy(a); xastir_messages_destroy(b);
  /* Keep one ID pending while another conversation wraps the two-character counter. */
  XastirMessages *rollover = xastir_messages_create("LOCAL"); CHECK(rollover);
  xastir_messages_set_callbacks(rollover, &fixture, tx, incoming);
  CHECK(xastir_messages_send(rollover, "ONE", "keep pending", 0, 7, "", 1, error, sizeof(error)));
  for (int i = 0; i < 3845; ++i) {
    CHECK(xastir_messages_send(rollover, "TWO", "wrap", 0, 7, "", 2+i, error, sizeof(error)));
    char *marker = strchr(fixture.information, '{'); CHECK(marker);
    char id[3] = {marker[1], marker[2], 0}; CHECK(strcmp(id, "00") != 0);
    char ack[100]; snprintf(ack, sizeof(ack), "TWO>APRS::LOCAL    :ack%s", id);
    xastir_messages_receive(rollover, ack, 7, 2+i);
    CHECK(by_key(rollover, 1)->pending);
  }
  xastir_messages_destroy(rollover);
  /* Independent legacy history lookup keys must not collide by concatenation. */
  XastirMessageStore store = {0}; Message m = {0}; m.active = RECORD_ACTIVE;
  strcpy(m.call_sign, "AB"); strcpy(m.from_call_sign, "C"); strcpy(m.seq, "1"); m.sec_heard = 1;
  CHECK(xastir_msg_input_database(&store, &m));
  strcpy(m.call_sign, "A"); strcpy(m.from_call_sign, "BC"); m.sec_heard = 2;
  CHECK(xastir_msg_input_database(&store, &m));
  long index = xastir_msg_find_data(&store, &m); CHECK(index >= 0);
  Message result; xastir_msg_get_data(&store, &result, index); CHECK(!strcmp(result.call_sign, "A"));
  m.sec_heard = 100;
  CHECK(xastir_msg_input_database(&store, &m));
  index = xastir_msg_find_data(&store, &m);
  xastir_msg_get_data(&store, &result, index); CHECK(result.sec_heard == 100);
  xastir_message_store_free(&store);
  return 0;
}
