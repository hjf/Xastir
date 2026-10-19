// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QJsonArray>
#include <QTimer>
#include <memory>
struct XastirMessages;
#include <functional>

struct AprsMessage {
  quint64 key = 0;
  QString sender, recipient, text, id, path;
  QString status;
  bool outgoing = false, group = false, pending = false, retryable = false;
  int device = -1, attempts = 0;
  int bulletin = 0;
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
  QJsonArray archiveRecords() const;
  QString messageCounter() const;
  quint64 nextMessageKey() const;
  void restoreArchive(const QJsonArray &records, const QString &counter, quint64 nextKey);
  void receiveInternetPacket(const QByteArray &packet, qint64 timestamp = 0, bool bidirectional = false);
  bool setAutoReplyText(QString text, QString *error = nullptr);
  const QString &autoReplyText() const { return autoText_; }
  void setAutoReply(bool enabled);
  bool autoReply() const { return autoReply_; }
  void setSatelliteMode(bool enabled);
  bool satelliteMode() const;
  bool addGroup(QString group);
  const QSet<QString> &groups() const { return groups_; }
  static QString statusText(int status, const QString &error = {});
  static bool validPath(const QString &path, QString *error = nullptr);
  static bool validText(const QString &text, QString *error = nullptr);
signals:
  void changed();
  void incomingMessage(const QString &peer, bool group);
  void autoReplyChanged(bool enabled);
  void operationError(const QString &error);
private:
  void sync();
  static int transmit(void *, const char *, int, const char *, char *, size_t);
  static void incoming(void *, const char *, int);
  QString callsign_, autoText_;
  Transport transport_;
  Clock clock_;
  QTimer timer_;
  std::unique_ptr<XastirMessages, void (*)(XastirMessages *)> core_;
  QList<AprsMessage> messages_; // UI projection; delivery state lives in C.
  QSet<QString> groups_{"ALL", "CQ", "QST"};
  quint64 revision_ = 0;
  bool autoReply_ = false;
};
