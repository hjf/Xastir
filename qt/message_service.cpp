// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_service.h"
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>

namespace {
bool fail(QString *error, const QString &text) { if (error) *error = text; return false; }
bool validId(const QString &id) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9]{1,5}$"));
  return pattern.match(id).hasMatch();
}
bool validRecipient(const QString &name) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9][A-Z0-9-]{0,8}$"));
  return pattern.match(name).hasMatch();
}
}
MessageService::MessageService(QString callsign, QObject *parent, Clock clock)
  : QObject(parent), callsign_(callsign.trimmed().toUpper()), clock_(std::move(clock))
{
  if (!clock_) clock_ = [] { return QDateTime::currentMSecsSinceEpoch(); };
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, &MessageService::processRetries);
  timer_.start();
}
bool MessageService::validPath(const QString &path, QString *error)
{
  if (path.trimmed().isEmpty()) return true;
  const auto digis = path.trimmed().toUpper().split(',');
  static const QRegularExpression call(QStringLiteral("^[A-Z0-9]{1,6}(-([0-9]|1[0-5]))?$"));
  if (digis.size() > 8) return fail(error, tr("An AX.25 path may contain at most eight digipeaters."));
  for (const auto &digi : digis)
    if (!call.match(digi.trimmed()).hasMatch()) return fail(error, tr("Invalid digipeater in path."));
  return true;
}
bool MessageService::validText(const QString &text, QString *error)
{
  if (text.trimmed().isEmpty() || text.size() > 67)
    return fail(error, tr("Message text must contain 1–67 ASCII characters."));
  for (QChar c : text)
    if (c.unicode() < 32 || c.unicode() > 126 || c == '|' || c == '~' || c == '{')
      return fail(error, tr("Use printable ASCII text without |, ~, or {."));
  return true;
}
QByteArray MessageService::messageInformation(const QString &recipient, const QString &body) const
{
  return ':' + recipient.toLatin1().leftJustified(9, ' ') + ':' + body.toLatin1();
}
QString MessageService::allocateId()
{
  for (int n = 0; n < 1296; ++n) {
    const QString id = QString::number(nextId_++ % 1296, 36).toUpper().rightJustified(2, '0');
    bool used = false;
    for (const auto &message : messages_)
      if (message.outgoing && message.pending && message.id == id) { used = true; break; }
    if (!used) return id;
  }
  return {};
}
bool MessageService::sendMessage(QString recipient, QString text, bool group, int device,
                                QString path, QString *error)
{
  recipient = recipient.trimmed().toUpper();
  if (!validRecipient(callsign_)) return fail(error, tr("Configure your station callsign first."));
  if (!validRecipient(recipient)) return fail(error, tr("Recipient must be 1–9 letters, digits or hyphens."));
  if (!validText(text, error) || !validPath(path, error)) return false;
  group = group || groups_.contains(recipient);
  if (!transport_ || device < 0) return fail(error, tr("Select a configured AGWPE interface."));
  if (std::count_if(messages_.cbegin(), messages_.cend(), [](const AprsMessage &m) { return m.pending; }) >= 100)
    return fail(error, tr("The outgoing queue is full (100 pending messages)."));
  AprsMessage message;
  message.key = nextKey_++; message.sender = callsign_; message.recipient = recipient;
  message.text = text; message.group = group; message.device = device; message.path = path;
  message.timestamp = clock_(); message.outgoing = true; message.pending = true; message.retryable = true;
  message.status = tr("Queued");
  if (!group) {
    message.id = allocateId();
    if (message.id.isEmpty()) return fail(error, tr("No message identifiers are available."));
  } else addGroup(recipient);
  setAutoReply(false); // A manual message indicates that the operator has returned.
  if (!group) routes_.insert(recipient, {device, path});
  messages_.append(message);
  attempt(message.key); trimHistory(); emit changed();
  return true;
}
void MessageService::attempt(quint64 key)
{
  for (auto &message : messages_) {
    if (message.key != key || !message.pending) continue;
    if (!message.group) {
      for (const auto &earlier : messages_)
        if (earlier.key < key && earlier.pending && !earlier.group && earlier.recipient == message.recipient) {
          message.status = tr("Waiting for earlier message"); message.nextRetry = clock_() + 1000; return;
        }
    }
    if (message.attempts >= 18) {
      message.pending = false; message.status = tr("Timed out — no ACK"); return;
    }
    QString body = message.text;
    if (!message.group) {
      body += '{' + message.id;
      // Advertise Reply-ACK capability; piggyback an ACK only to a capable peer.
      body += '}';
      if (replyAckPeers_.contains(message.recipient) && owedAck_.value(message.recipient).size() <= 2)
        body += owedAck_.value(message.recipient);
    }
    QString error;
    if (!transport_ || !transport_(messageInformation(message.recipient, body), message.device, message.path, &error)) {
      message.status = tr("Waiting: %1").arg(error.isEmpty() ? tr("No transport") : error);
      message.nextRetry = clock_() + 7000; return;
    }
    ++message.attempts;
    if (message.group) { message.pending = false; message.status = tr("Sent (group; no ACK)"); }
    else {
      message.status = tr("Awaiting ACK");
      message.nextRetry = clock_() + std::min<qint64>(600000, 7000LL << std::min(message.attempts - 1, 10));
    }
    return;
  }
}
void MessageService::processRetries()
{
  QList<quint64> due;
  for (const auto &message : messages_)
    if (message.pending && message.nextRetry <= clock_()) due.append(message.key);
  for (auto key : due) attempt(key);
  if (!due.isEmpty()) emit changed();
}
void MessageService::cancel(quint64 key)
{
  for (auto &message : messages_) if (message.key == key && message.pending) {
    message.pending = false; message.status = tr("Cancelled"); emit changed(); return;
  }
}
void MessageService::retry(quint64 key)
{
  for (auto &message : messages_) if (message.key == key && message.outgoing && message.retryable) {
    if (!message.pending && std::count_if(messages_.cbegin(), messages_.cend(), [](const AprsMessage &m) { return m.pending; }) >= 100) {
      emit operationError(tr("The outgoing queue is full (100 pending messages).")); return;
    }
    if (message.status == tr("Acknowledged") || message.status == tr("Rejected")) return;
    message.pending = true; message.attempts = 0; attempt(key); emit changed(); return;
  }
}
void MessageService::clearOutgoing()
{
  messages_.removeIf([](const AprsMessage &message) { return message.outgoing; });
  emit changed();
}
void MessageService::acknowledge(const QString &peer, const QString &id, bool rejected)
{
  for (auto &message : messages_)
    if (message.outgoing && message.pending && !message.group && message.recipient == peer && message.id == id) {
      message.pending = false; message.status = rejected ? tr("Rejected") : tr("Acknowledged");
      emit changed(); return;
    }
}
bool MessageService::sendQuery(const QByteArray &query, int device, QString *error)
{
  if (query != "?APRS?" && query != "?IGATE?" && query != "?WX?")
    return fail(error, tr("Unsupported station query."));
  if (!transport_ || device < 0) return fail(error, tr("Select a configured AGWPE interface."));
  if (!transport_(query, device, QString(), error)) return false;
  setAutoReply(false);
  AprsMessage message;
  message.key = nextKey_++; message.sender = callsign_; message.recipient = "APRS";
  message.text = QString::fromLatin1(query); message.outgoing = true; message.group = true;
  message.device = device; message.timestamp = clock_(); message.status = tr("Query sent");
  messages_.append(message); trimHistory(); emit changed(); return true;
}
bool MessageService::setAutoReplyText(QString text, QString *error)
{
  if (!validText("AA:" + text, error)) return false;
  if (text.trimmed().isEmpty()) return fail(error, tr("Enter an auto reply message."));
  autoText_ = std::move(text); return true;
}
void MessageService::setAutoReply(bool enabled)
{
  if (autoReply_ == enabled) return;
  autoReply_ = enabled; emit autoReplyChanged(enabled);
}
bool MessageService::addGroup(QString group)
{
  group = group.trimmed().toUpper();
  if (!validRecipient(group)) return false;
  groups_.insert(group); return true;
}
void MessageService::trimHistory()
{
  while (messages_.size() > 2000) {
    auto it = std::find_if(messages_.begin(), messages_.end(), [](const AprsMessage &m) { return !m.pending; });
    if (it == messages_.end()) break;
    messages_.erase(it);
  }
  const qint64 cutoff = clock_() - 3600000;
  for (auto *cache : {&seen_, &ackTimes_, &autoTimes_}) {
    for (auto it = cache->begin(); it != cache->end(); )
      if (it.value() < cutoff) it = cache->erase(it); else ++it;
  }
}
void MessageService::receivePacket(const QByteArray &original, int device)
{
  QByteArray packet = original;
  int arrow = -1, colon = -1;
  for (int depth = 0; depth <= 4; ++depth) {
    arrow = packet.indexOf('>'); colon = packet.indexOf(':', arrow + 1);
    if (arrow <= 0 || colon <= arrow) return;
    if (packet.mid(colon + 1, 1) != "}") break;
    if (depth == 4) return;
    packet = packet.mid(colon + 2);
  }
  const QString sender = QString::fromLatin1(packet.left(arrow)).trimmed().toUpper();
  const QByteArray data = packet.mid(colon + 1);
  if (!validRecipient(sender) || data.size() < 11 || data[0] != ':' || data[10] != ':') return;
  const QString recipient = QString::fromLatin1(data.mid(1, 9)).trimmed().toUpper();
  if (!validRecipient(recipient)) return;
  QString text = QString::fromLatin1(data.mid(11));
  while (text.endsWith('\r') || text.endsWith('\n')) text.chop(1);
  const bool toMe = !callsign_.isEmpty() && recipient == callsign_;
  if (text.startsWith("ack") || text.startsWith("rej")) {
    const QString suffix = text.mid(3);
    const QString ack = suffix.section('}', 0, 0);
    const QString reply = suffix.contains('}') ? suffix.section('}', 1) : QString();
    if (validId(ack) && suffix.count('}') <= 1 && (reply.isEmpty() || validId(reply))) {
      if (toMe) acknowledge(sender, ack, text.startsWith("rej"));
      return;
    }
  }
  QString id, replyId, originalId;
  const auto marker = text.indexOf('{');
  if (marker >= 0) {
    const QString suffix = text.mid(marker + 1);
    if (suffix.count('}') > 1) return;
    originalId = suffix;
    id = suffix.section('}', 0, 0);
    if (!validId(id)) return;
    if (suffix.contains('}')) {
      replyId = suffix.section('}', 1);
      if (!replyId.isEmpty() && !validId(replyId)) return;
      if (toMe) replyAckPeers_.insert(sender);
    }
    text.truncate(marker);
  }
  // Auto-answer messages can carry a Reply-ACK without requesting an ACK.
  if (marker < 0 && text.startsWith("AA:") && text.contains('}')) {
    const auto end = text.lastIndexOf('}');
    const QString reply = text.mid(end + 1);
    if (validId(reply)) { replyId = reply; text.truncate(end); }
  }
  if (!validText(text)) return;
  if (sender == callsign_) {
    for (const auto &message : messages_)
      if (message.outgoing && message.recipient == recipient && message.text == text && message.id == id) return;
  }
  if (toMe && !replyId.isEmpty()) acknowledge(sender, replyId, false);
  const qint64 now = clock_();
  const QString duplicateKey = sender + '>' + recipient + ':' + id + ':' + text;
  const bool duplicate = seen_.contains(duplicateKey)
    && (id.isEmpty() ? now - seen_.value(duplicateKey) < 30000 : now - seen_.value(duplicateKey) < 3600000);
  seen_.insert(duplicateKey, now);
  const auto route = routes_.value(sender, qMakePair(device, QString()));
  if (toMe && sender != callsign_ && !id.isEmpty()) {
    owedAck_.insert(sender, id);
    const QString ackKey = sender + ':' + id;
    if (!satellite_ && (!ackTimes_.contains(ackKey) || now - ackTimes_.value(ackKey) >= 30000)) {
      QString error;
      if (transport_ && transport_(messageInformation(sender, "ack" + originalId), route.first, route.second, &error))
        ackTimes_.insert(ackKey, now);
    }
  }
  if (!duplicate) {
    AprsMessage message;
    message.key = nextKey_++; message.sender = sender; message.recipient = recipient;
    message.text = text; message.id = id; message.timestamp = now; message.device = device;
    message.group = groups_.contains(recipient) && !toMe; message.status = toMe ? tr("Received") : tr("Heard");
    messages_.append(message); trimHistory(); emit changed();
    if (toMe || groups_.contains(recipient)) emit incomingMessage(toMe ? sender : recipient, !toMe);
  }
  if (toMe && !duplicate && autoReply_ && sender != callsign_ && !autoText_.isEmpty()
      && !text.startsWith("AA:") && (!autoTimes_.contains(sender) || now - autoTimes_.value(sender) >= 60000)) {
    autoTimes_.insert(sender, now);
    QString body = "AA:" + autoText_;
    if (replyAckPeers_.contains(sender) && !owedAck_.value(sender).isEmpty()
        && owedAck_.value(sender).size() <= 2) body += '}' + owedAck_.value(sender);
    QString error;
    const bool sent = transport_ && transport_(messageInformation(sender, body), route.first, route.second, &error);
    AprsMessage reply;
    reply.key = nextKey_++; reply.sender = callsign_; reply.recipient = sender;
    reply.text = "AA:" + autoText_; reply.outgoing = true; reply.timestamp = now;
    reply.device = route.first; reply.path = route.second; reply.attempts = sent ? 1 : 0;
    reply.status = sent ? tr("Auto reply sent (no ACK)") : tr("Auto reply not sent: %1").arg(error);
    messages_.append(reply); trimHistory(); emit changed();
  }
}
