// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_service.h"
#include "message_core.h"
#include <QDateTime>
#include <QJsonObject>
#include <algorithm>
#include <new>
#include <cstring>
#include <cstdio>

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
    row.status = statusText(m.status, QString::fromUtf8(m.error));
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

QJsonArray MessageService::archiveRecords() const
{
  QJsonArray result;
  const auto *store = xastir_messages_history(core_.get());
  for (long i=0;i<store->count;++i) {
    const auto &m=store->data[i];
    if (m.active != RECORD_ACTIVE) continue;
    result.append(QJsonObject{{"key",QString::number(m.key)},
      {"sender",QString::fromLatin1(m.from_call_sign)}, {"recipient",QString::fromLatin1(m.call_sign)},
      {"text",QString::fromLatin1(m.message_line)}, {"id",QString::fromLatin1(m.seq)},
      {"path",QString::fromUtf8(m.path)}, {"error",QString::fromUtf8(m.error)},
      {"outgoing",bool(m.outgoing)}, {"group",bool(m.group)}, {"pending",bool(m.pending)},
      {"retryable",bool(m.retryable)}, {"device",m.device}, {"status",m.status}, {"attempts",m.tries},
      {"timestamp",QString::number(m.timestamp_ms)}});
  }
  return result;
}
quint64 MessageService::nextMessageKey() const { return xastir_messages_next_key(core_.get()); }
QString MessageService::messageCounter() const { return QString::fromLatin1(xastir_messages_counter(core_.get())); }
void MessageService::restoreArchive(const QJsonArray &records, const QString &counter, quint64 nextKey)
{
  for (const auto &value:records) {
    const auto o=value.toObject();
    Message m{};
    m.key=o["key"].toString().toULongLong();
    auto field=[&o](char *out,size_t size,const char *key,bool latin=true) {
      const auto text=o[key].toString();
      const auto bytes=latin ? text.toLatin1() : text.toUtf8();
      std::snprintf(out,size,"%s",bytes.constData());
    };
    field(m.from_call_sign,sizeof(m.from_call_sign),"sender");
    field(m.call_sign,sizeof(m.call_sign),"recipient");
    field(m.message_line,sizeof(m.message_line),"text");
    field(m.seq,sizeof(m.seq),"id");field(m.path,sizeof(m.path),"path",false);field(m.error,sizeof(m.error),"error",false);
    m.outgoing=o["outgoing"].toBool();m.group=o["group"].toBool();m.pending=o["pending"].toBool();
    m.retryable=o["retryable"].toBool();m.device=o["device"].toInt(-1);m.status=o["status"].toInt(-1);
    m.tries=o["attempts"].toInt();m.timestamp_ms=o["timestamp"].toString().toLongLong();
    m.sec_heard=m.timestamp_ms/1000;
    xastir_messages_restore(core_.get(),&m);
  }
  xastir_messages_restore_counter(core_.get(),counter.toLatin1().constData(),nextKey);
  sync();
}
void MessageService::receiveInternetPacket(const QByteArray &original, qint64 timestamp)
{
  QByteArray packet=original;
  if(!timestamp)timestamp=clock_();
  for(int depth=0;depth<=4;++depth) {
    int arrow=packet.indexOf('>'),colon=packet.indexOf(':',arrow+1);
    if(arrow<=0 || arrow>9 || colon<=arrow+1 || packet.contains('\0'))return;
    const auto info=packet.mid(colon+1);
    if(info.startsWith('}')){packet=info.mid(1);continue;}
    XastirDecodedMessage decoded{};
    if(!xastir_decode_message(info.constData(),&decoded) || decoded.kind)return;
    const auto sender=QString::fromLatin1(packet.left(arrow)).trimmed().toUpper();
    const quint64 next=nextMessageKey();
    for(const auto &m:messages_) {
      if(m.sender==sender && m.recipient==QString::fromLatin1(decoded.recipient) &&
         m.text==QString::fromLatin1(decoded.text) && m.id==QString::fromLatin1(decoded.id) &&
         timestamp-m.timestamp < (decoded.id[0]?3600000:30000)) return;
    }
    // Passive import intentionally bypasses delivery, peer-route and ACK state.
    Message m{};m.key=next;m.device=256;m.timestamp_ms=timestamp;m.sec_heard=timestamp/1000;
    m.status=XMSG_HEARD;
    std::snprintf(m.from_call_sign,sizeof(m.from_call_sign),"%s",sender.toLatin1().constData());
    std::snprintf(m.call_sign,sizeof(m.call_sign),"%s",decoded.recipient);
    std::snprintf(m.message_line,sizeof(m.message_line),"%s",decoded.text);
    std::snprintf(m.seq,sizeof(m.seq),"%s",decoded.id);
    m.group=xastir_messages_is_group(core_.get(),decoded.recipient);
    xastir_messages_restore(core_.get(),&m);sync();return;
  }
}

QString MessageService::statusText(int status, const QString &error)
{
    switch (status) {
      case XMSG_QUEUED: return tr("Queued");
      case XMSG_EARLIER: return tr("Waiting for earlier message");
      case XMSG_TIMEOUT: return tr("Timed out — no ACK");
      case XMSG_WAITING: return tr("Waiting: %1").arg(error);
      case XMSG_GROUP_SENT: return tr("Sent (group; no ACK)");
      case XMSG_AWAITING: return tr("Awaiting ACK");
      case XMSG_CANCELLED: return tr("Cancelled");
      case XMSG_ACKED: return tr("Acknowledged");
      case XMSG_REJECTED: return tr("Rejected");
      case XMSG_QUERY_SENT: return tr("Query sent");
      case XMSG_RECEIVED: return tr("Received");
      case XMSG_HEARD: return tr("Heard");
      case XMSG_AUTO_SENT: return tr("Auto reply sent (no ACK)");
      case XMSG_INTERRUPTED: return tr("Interrupted by restart — retry manually");
      case XMSG_AUTO_FAILED: return tr("Auto reply not sent: %1").arg(error);
    }
  return tr("Unknown status");
}
