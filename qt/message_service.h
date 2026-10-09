// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QHash>
#include <functional>

struct AprsMessage {
  quint64 key = 0;
  QString sender, recipient, text, id, path;
  QString status;
  bool outgoing = false, group = false, pending = false, retryable = false;
  int device = -1, attempts = 0;
  qint64 timestamp = 0, nextRetry = 0;
};

class MessageService : public QObject {
  Q_OBJECT
public:
  using Transport = std::function<bool(const QByteArray &, int, const QString &, QString *)>;
  using Clock = std::function<qint64()>;
  explicit MessageService(QString callsign, QObject *parent = nullptr, Clock clock = {});
  void setTransport(Transport transport) { transport_ = std::move(transport); }
  const QList<AprsMessage> &messages() const { return messages_; }
  const QString &callsign() const { return callsign_; }
  bool sendMessage(QString recipient, QString text, bool group, int device,
                   QString path, QString *error = nullptr);
  bool sendQuery(const QByteArray &query, int device, QString *error = nullptr);
  void receivePacket(const QByteArray &packet, int device);
  void processRetries();
  void cancel(quint64 key);
  void retry(quint64 key);
  void clearOutgoing();
  bool setAutoReplyText(QString text, QString *error = nullptr);
  const QString &autoReplyText() const { return autoText_; }
  void setAutoReply(bool enabled);
  bool autoReply() const { return autoReply_; }
  void setSatelliteMode(bool enabled) { satellite_ = enabled; }
  bool satelliteMode() const { return satellite_; }
  bool addGroup(QString group);
  const QSet<QString> &groups() const { return groups_; }
  static bool validPath(const QString &path, QString *error = nullptr);
  static bool validText(const QString &text, QString *error = nullptr);
signals:
  void changed();
  void incomingMessage(const QString &peer, bool group);
  void autoReplyChanged(bool enabled);
  void operationError(const QString &error);
private:
  void attempt(quint64 key);
  void acknowledge(const QString &peer, const QString &id, bool rejected);
  QByteArray messageInformation(const QString &recipient, const QString &body) const;
  QString allocateId();
  void trimHistory();
  QString callsign_, autoText_;
  Transport transport_;
  Clock clock_;
  QTimer timer_;
  QList<AprsMessage> messages_;
  QSet<QString> groups_{"ALL", "CQ", "QST"};
  QHash<QString, qint64> seen_, ackTimes_, autoTimes_;
  QHash<QString, QString> owedAck_;
  QSet<QString> replyAckPeers_;
  QHash<QString, QPair<int, QString>> routes_;
  quint64 nextKey_ = 1;
  unsigned nextId_ = 0;
  bool autoReply_ = false, satellite_ = false;
};
