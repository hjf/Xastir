// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef XASTIR_MESSAGE_STORE_H
#define XASTIR_MESSAGE_STORE_H
#include "database.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { Message *data; long *index; long count, capacity; } XastirMessageStore;
void xastir_message_store_reindex(XastirMessageStore *store);
void xastir_message_store_free(XastirMessageStore *store);
int xastir_msg_comp_active(const void *a, const void *b);
int xastir_msg_comp_data(XastirMessageStore *store, const void *a, const void *b);
int xastir_msg_input_database(XastirMessageStore *store, Message *m_fill);
long xastir_msg_find_data(XastirMessageStore *store, Message *m_fill);
void xastir_msg_replace_data(XastirMessageStore *store, Message *m_fill, long record_num);
void xastir_msg_get_data(XastirMessageStore *store, Message *m_fill, long record_num);
#ifdef __cplusplus
}
#endif
#endif
