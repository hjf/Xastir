// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_service.h"
#include "message_core.h"
#include <QDateTime>
#include <algorithm>
#include <new>
#include <cstring>

namespace {
bool fail(QString *error, const QString &text) { if (error) *error = text; return false; }
}
MessageService::MessageService(QString callsign, QObject *parent, Clock clock)
  : QObject(parent), callsign_(callsign.trimmed().toUpper()), clock_(std::move(clock)),
    core_(xastir_messages_create(callsign_.toUtf8().constData()), xastir_messages_destroy)
{
  if (!core_) throw std::bad_alloc();
  if (!clock_) clock_ = [] { return QDateTime::currentMSecsSinceEpoch(); };
  xastir_messages_set_callbacks(core_.get(), this, transmit, incoming);
  revision_ = xastir_messages_revision(core_.get());
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, &MessageService::processRetries);
  timer_.start();
}
int MessageService::transmit(void *user, const char *info, int device, const char *path, char *error, size_t capacity)
{
  auto *self = static_cast<MessageService *>(user);
  QString reason;
  const bool sent = self->transport_ && self->transport_(QByteArray(info), device, QString::fromUtf8(path), &reason);
  const QByteArray bytes = reason.toUtf8();
  if (capacity) {
    const size_t count = std::min(capacity-1, size_t(bytes.size()));
    std::memcpy(error, bytes.constData(), count); error[count] = 0;
  }
  return sent;
}
void MessageService::incoming(void *user, const char *peer, int group)
{
  auto *self = static_cast<MessageService *>(user);
  self->sync();
  emit self->incomingMessage(QString::fromUtf8(peer), group);
}
void MessageService::sync()
{
  const bool enabled = xastir_messages_auto(core_.get());
  if (autoReply_ != enabled) { autoReply_ = enabled; emit autoReplyChanged(enabled); }
  if (revision_ == xastir_messages_revision(core_.get())) return;
  revision_ = xastir_messages_revision(core_.get());
  messages_.clear();
  const auto *store = xastir_messages_history(core_.get());
  for (long i = 0; i < store->count; ++i) {
    const Message &m = store->data[i];
    if (m.active != RECORD_ACTIVE) continue;
    AprsMessage row;
    row.key = m.key; row.sender = QString::fromLatin1(m.from_call_sign);
    row.recipient = QString::fromLatin1(m.call_sign); row.text = QString::fromLatin1(m.message_line);
    row.id = QString::fromLatin1(m.seq); row.path = QString::fromUtf8(m.path);
    row.outgoing = m.outgoing; row.group = m.group; row.pending = m.pending; row.retryable = m.retryable;
    row.device = m.device; row.attempts = m.tries; row.timestamp = m.timestamp_ms; row.nextRetry = m.next_retry_ms;
    switch (m.status) {
      case XMSG_QUEUED: row.status = tr("Queued"); break;
      case XMSG_EARLIER: row.status = tr("Waiting for earlier message"); break;
      case XMSG_TIMEOUT: row.status = tr("Timed out — no ACK"); break;
      case XMSG_WAITING: row.status = tr("Waiting: %1").arg(QString::fromUtf8(m.error)); break;
      case XMSG_GROUP_SENT: row.status = tr("Sent (group; no ACK)"); break;
      case XMSG_AWAITING: row.status = tr("Awaiting ACK"); break;
      case XMSG_CANCELLED: row.status = tr("Cancelled"); break;
      case XMSG_ACKED: row.status = tr("Acknowledged"); break;
      case XMSG_REJECTED: row.status = tr("Rejected"); break;
      case XMSG_QUERY_SENT: row.status = tr("Query sent"); break;
      case XMSG_RECEIVED: row.status = tr("Received"); break;
      case XMSG_HEARD: row.status = tr("Heard"); break;
      case XMSG_AUTO_SENT: row.status = tr("Auto reply sent (no ACK)"); break;
      case XMSG_AUTO_FAILED: row.status = tr("Auto reply not sent: %1").arg(QString::fromUtf8(m.error)); break;
    }
    messages_.append(row);
  }
  std::sort(messages_.begin(), messages_.end(), [](const AprsMessage &a, const AprsMessage &b) { return a.key < b.key; });
  emit changed();
}
bool MessageService::validPath(const QString &path, QString *error)
{
  if (path.contains(QChar(0)) || !xastir_message_valid_path(path.toUtf8().constData())) return fail(error, tr("Invalid digipeater in path (at most eight AX.25 addresses)."));
  return true;
}
bool MessageService::validText(const QString &text, QString *error)
{
  if (text.contains(QChar(0)) || !xastir_message_valid_text(text.toUtf8().constData()))
    return fail(error, tr("Message text must contain 1–67 printable ASCII characters without |, ~, or {."));
  return true;
}
bool MessageService::sendMessage(QString recipient, QString text, bool group, int device, QString path, QString *error)
{
  recipient = recipient.trimmed().toUpper();
  if (recipient.contains(QChar(0))) return fail(error, tr("Invalid recipient."));
  if (!validText(text, error) || !validPath(path, error)) return false;
  if (!transport_ || device < 0) return fail(error, tr("Select a configured AGWPE interface."));
  char reason[256] = {};
  const bool sent = xastir_messages_send(core_.get(), recipient.toUtf8().constData(), text.toUtf8().constData(),
      group, device, path.toUtf8().constData(), clock_(), reason, sizeof(reason));
  if (sent && xastir_messages_is_group(core_.get(), recipient.toUtf8().constData())) groups_.insert(recipient);
  sync();
  return sent || fail(error, QString::fromUtf8(reason));
}
bool MessageService::sendQuery(const QByteArray &query, int device, QString *error)
{
  if (query.contains('\0')) return fail(error, tr("Unsupported station query."));
  if (!transport_ || device < 0) return fail(error, tr("Select a configured AGWPE interface."));
  char reason[256] = {};
  const bool sent = xastir_messages_query(core_.get(), query.constData(), device, clock_(), reason, sizeof(reason));
  sync(); return sent || fail(error, QString::fromUtf8(reason));
}
void MessageService::receivePacket(const QByteArray &packet, int device)
{
  if (packet.contains('\0')) return;
  xastir_messages_receive(core_.get(), packet.constData(), device, clock_()); sync();
}
void MessageService::processRetries() { xastir_messages_process(core_.get(), clock_()); sync(); }
void MessageService::cancel(quint64 key) { xastir_messages_cancel(core_.get(), key); sync(); }
void MessageService::retry(quint64 key)
{
  char reason[256] = {};
  if (!xastir_messages_retry(core_.get(), key, clock_(), reason, sizeof(reason))) emit operationError(QString::fromUtf8(reason));
  sync();
}
void MessageService::clearOutgoing() { xastir_messages_clear_outgoing(core_.get()); sync(); }
bool MessageService::setAutoReplyText(QString text, QString *error)
{
  if (!validText(text, error) || !xastir_messages_set_auto_text(core_.get(), text.toUtf8().constData()))
    return fail(error, tr("Auto reply text must contain 1–64 printable ASCII characters."));
  autoText_ = QString::fromUtf8(xastir_messages_auto_text(core_.get())); return true;
}
void MessageService::setAutoReply(bool enabled) { xastir_messages_set_auto(core_.get(), enabled); sync(); }
void MessageService::setSatelliteMode(bool enabled) { xastir_messages_set_satellite(core_.get(), enabled); }
bool MessageService::satelliteMode() const { return xastir_messages_satellite(core_.get()); }
bool MessageService::addGroup(QString group)
{
  group = group.trimmed().toUpper();
  if (group.contains(QChar(0))) return false;
  if (!xastir_messages_add_group(core_.get(), group.toUtf8().constData())) return false;
  groups_.insert(group); return true;
}
