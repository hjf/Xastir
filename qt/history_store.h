// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>
class QLockFile;
struct sqlite3;
class StationModel;
class MessageService;
struct HistoryPacket
{
  qint64 key, timestamp;
  int device;
  QByteArray packet;
  bool outgoing = false;
};
struct HistoryOptions
{
  QString path;
  int retentionDays = 7;
  qint64 maxPackets = 100000;
  qint64 maxBytes = 64 * 1024 * 1024;
};
// Only the worker thread touches SQLite. GUI-thread methods queue durable writes.
class HistoryStore : public QObject
{
  Q_OBJECT
public:
  explicit HistoryStore(HistoryOptions options, QObject *parent = nullptr);
  ~HistoryStore() override;
  bool open(QString *error = nullptr);
  bool restore(StationModel *, MessageService *, QString *error = nullptr);
  void recordPacket(const QByteArray &, int device, qint64 timestamp = 0, bool outgoing = false);
  void saveMessages(const MessageService *);
  void flush();
  void prune(qint64 now = 0);
  QList<HistoryPacket> packets(qint64 before = 0, int limit = 200);
  QJsonArray messages(const QString &identity, qint64 before = 0, int limit = 200);
  const QString &path() const { return options_.path; }
  static QString defaultPath();
signals:
  void errorOccurred(const QString &error);

private:
  bool execute(const char *sql);
  void reportError();
  void pruneWorker(qint64 now);
  QThread thread_;
  QObject *worker_ = nullptr;
  sqlite3 *db_ = nullptr;
  HistoryOptions options_;
  std::unique_ptr<QLockFile> lock_;
  QTimer pruneTimer_;
  QHash<QString, QByteArray> savedMessages_;
  std::atomic<int> queued_{0};
  quint64 written_ = 0;
  bool opened_ = false;
};
