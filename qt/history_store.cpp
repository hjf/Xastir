// SPDX-License-Identifier: GPL-2.0-or-later
#include "history_store.h"
#include "aprs_decoder.h"
#include "message_core.h"
#include "message_service.h"
#include "station_model.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <functional>
#include <sqlite3.h>
namespace
{
struct Statement
{
  sqlite3_stmt *value = nullptr;
  Statement(sqlite3 *db, const char *sql) { sqlite3_prepare_v2(db, sql, -1, &value, nullptr); }
  ~Statement() { sqlite3_finalize(value); }
  void text(int n, const QByteArray &v)
  {
    sqlite3_bind_text(value, n, v.constData(), int(v.size()), SQLITE_TRANSIENT);
  }
  void number(int n, qint64 v) { sqlite3_bind_int64(value, n, v); }
};
QByteArray bytes(sqlite3_stmt *s, int n)
{
  return QByteArray(static_cast<const char *>(sqlite3_column_blob(s, n)),
                    sqlite3_column_bytes(s, n));
}
bool messagePacket(const QByteArray &packet)
{
  char sender[10]; XastirDecodedMessage decoded{};
  return !packet.contains('\0') && xastir_decode_message_packet(packet.constData(), sender, sizeof(sender), &decoded);
}

} // namespace
QString HistoryStore::defaultPath()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/history.sqlite";
}
HistoryStore::HistoryStore(HistoryOptions options, QObject *parent)
    : QObject(parent), options_(std::move(options))
{
  if (options_.path.isEmpty())
    options_.path = defaultPath();
  options_.retentionDays = qBound(1, options_.retentionDays, 365);
  options_.maxPackets = qMax(qint64(1), options_.maxPackets);
  options_.maxBytes = qMax(qint64(4096), options_.maxBytes);
  worker_ = new QObject;
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  thread_.start();
  pruneTimer_.setInterval(60000);
  connect(&pruneTimer_, &QTimer::timeout, this, [this] { prune(); });
}
HistoryStore::~HistoryStore()
{
  pruneTimer_.stop();
  QMetaObject::invokeMethod(
      worker_,
      [this]
      {
        if (db_)
        {
          sqlite3_close(db_);
          db_ = nullptr;
        }
      },
      Qt::BlockingQueuedConnection);
  thread_.quit();
  thread_.wait();
}
bool HistoryStore::execute(const char *sql)
{
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}
void HistoryStore::reportError()
{
  const QString error = QString::fromUtf8(sqlite3_errmsg(db_));
  QMetaObject::invokeMethod(
      this, [this, error] { emit errorOccurred(tr("History database: %1").arg(error)); },
      Qt::QueuedConnection);
}
bool HistoryStore::open(QString *error)
{
  if (opened_)
    return true;
  if (!QDir().mkpath(QFileInfo(options_.path).absolutePath()))
  {
    if (error)
      *error = tr("Cannot create history directory.");
    return false;
  }
  if (!lock_)
  {
    lock_ = std::make_unique<QLockFile>(options_.path + ".lock");
    lock_->setStaleLockTime(0);
  }
  if (!lock_->isLocked() && !lock_->tryLock(0))
  {
    if (error)
      *error = tr("History database is in use by another application instance, or its lock cannot "
                  "be created.");
    return false;
  }
  QString reason;
  QMetaObject::invokeMethod(
      worker_,
      [this, &reason]
      {
        if (db_)
        {
          sqlite3_close(db_);
          db_ = nullptr;
        }
        if (sqlite3_open_v2(options_.path.toUtf8().constData(), &db_,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        {
          reason = QString::fromUtf8(sqlite3_errmsg(db_));
          sqlite3_close(db_);
          db_ = nullptr;
          return;
        }
        sqlite3_busy_timeout(db_, 2000);
        Statement version(db_, "PRAGMA user_version");
        if (!version.value || sqlite3_step(version.value) != SQLITE_ROW ||
            sqlite3_column_int(version.value, 0) > 1)
        {
          reason = tr("Unsupported history database version.");
          return;
        }
        sqlite3_reset(version.value);
        if (!execute(
                "PRAGMA auto_vacuum=INCREMENTAL; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; "
                "PRAGMA journal_size_limit=4194304;"
                "CREATE TABLE IF NOT EXISTS packets(id INTEGER PRIMARY KEY, timestamp INTEGER NOT "
                "NULL, device INTEGER NOT NULL, packet BLOB NOT NULL, outgoing INTEGER NOT NULL "
                "DEFAULT 0);"
                "CREATE INDEX IF NOT EXISTS packets_time ON packets(timestamp);"
                "CREATE TABLE IF NOT EXISTS message_packets(id INTEGER PRIMARY KEY, timestamp "
                "INTEGER NOT NULL, device INTEGER NOT NULL, packet BLOB NOT NULL, outgoing INTEGER "
                "NOT NULL DEFAULT 0);"
                "CREATE TABLE IF NOT EXISTS messages(identity TEXT NOT NULL, id INTEGER NOT NULL, "
                "timestamp INTEGER NOT NULL, record TEXT NOT NULL, PRIMARY KEY(identity,id));"
                "CREATE TABLE IF NOT EXISTS identities(identity TEXT PRIMARY KEY, counter TEXT NOT "
                "NULL, next_key INTEGER NOT NULL); PRAGMA user_version=1;"))
        {
          reason = QString::fromUtf8(sqlite3_errmsg(db_));
          return;
        }
        opened_ = true;
        pruneWorker(QDateTime::currentMSecsSinceEpoch());
      },
      Qt::BlockingQueuedConnection);
  if (error)
    *error = reason;
  if (opened_)
    pruneTimer_.start();
  else
    lock_->unlock();
  return opened_;
}
void HistoryStore::recordPacket(const QByteArray &packet, int device, qint64 timestamp,
                                bool outgoing)
{
  if (!opened_ || packet.isEmpty() || packet.size() > 4096 || packet.contains('\0'))
    return;
  if (!timestamp)
    timestamp = QDateTime::currentMSecsSinceEpoch();
  if (queued_.load() >= 8192)
  {
    emit errorOccurred(tr("History writer overloaded; packet not saved."));
    return;
  }
  ++queued_;
  QMetaObject::invokeMethod(
      worker_,
      [this, packet, device, timestamp, outgoing]
      {
        Statement insert(db_,
                         "INSERT INTO packets(timestamp,device,packet,outgoing) VALUES(?,?,?,?)");
        if (!insert.value)
        {
          reportError();
          --queued_;
          return;
        }
        insert.number(1, timestamp);
        insert.number(2, device);
        sqlite3_bind_blob(insert.value, 3, packet.constData(), int(packet.size()),
                          SQLITE_TRANSIENT);
        insert.number(4, outgoing);
        bool ok = execute("BEGIN IMMEDIATE");
        ok = ok && sqlite3_step(insert.value) == SQLITE_DONE;
        if (ok && messagePacket(packet))
        {
          Statement permanent(
              db_, "INSERT INTO message_packets(timestamp,device,packet,outgoing) VALUES(?,?,?,?)");
          if (permanent.value)
          {
            permanent.number(1, timestamp);
            permanent.number(2, device);
            sqlite3_bind_blob(permanent.value, 3, packet.constData(), int(packet.size()),
                              SQLITE_TRANSIENT);
            permanent.number(4, outgoing);
            ok = sqlite3_step(permanent.value) == SQLITE_DONE;
          }
          else
            ok = false;
        }
        if (!ok || !execute("COMMIT"))
        {
          reportError();
          execute("ROLLBACK");
        }
        --queued_;
        // Bound high-volume feeds between the minute-based age sweeps.
        if (++written_ % 256 == 0)
          pruneWorker(QDateTime::currentMSecsSinceEpoch());
      },
      Qt::QueuedConnection);
}
void HistoryStore::saveMessages(const MessageService *service)
{
  if (!opened_)
    return;
  const QString identity = service->callsign();
  QJsonArray changed;
  const quint64 next = service->nextMessageKey();
  QSet<QString> currentKeys;
  for (const auto &v : service->archiveRecords())
  {
    const auto o = v.toObject();
    const QString key = identity + ":" + o["key"].toString();
    const auto json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    currentKeys.insert(key);
    if (savedMessages_.value(key) != json)
    {
      changed.append(o);
      savedMessages_.insert(key, json);
    }
  }
  for (auto i = savedMessages_.begin(); i != savedMessages_.end();)
  {
    if (!currentKeys.contains(i.key()))
      i = savedMessages_.erase(i);
    else
      ++i;
  }
  const auto counter = service->messageCounter().toLatin1();
  QMetaObject::invokeMethod(
      worker_,
      [this, identity, changed, counter, next]
      {
        bool ok = execute("BEGIN IMMEDIATE");
        Statement row(db_, "INSERT INTO messages VALUES(?,?,?,?) ON CONFLICT(identity,id) DO "
                           "UPDATE SET timestamp=excluded.timestamp, record=excluded.record");
        if (!row.value)
          ok = false;
        for (const auto &v : changed)
        {
          if (!ok)
            break;
          const auto o = v.toObject();
          sqlite3_reset(row.value);
          sqlite3_clear_bindings(row.value);
          row.text(1, identity.toUtf8());
          row.number(2, o["key"].toString().toLongLong());
          row.number(3, o["timestamp"].toString().toLongLong());
          row.text(4, QJsonDocument(o).toJson(QJsonDocument::Compact));
          ok = sqlite3_step(row.value) == SQLITE_DONE;
        }
        Statement meta(db_,
                       "INSERT INTO identities VALUES(?,?,?) ON CONFLICT(identity) DO UPDATE SET "
                       "counter=excluded.counter,next_key=MAX(next_key,excluded.next_key)");
        if (!meta.value)
          ok = false;
        if (ok)
        {
          meta.text(1, identity.toUtf8());
          meta.text(2, counter);
          meta.number(3, qint64(next));
          ok = sqlite3_step(meta.value) == SQLITE_DONE;
        }
        if (!ok || !execute("COMMIT"))
        {
          reportError();
          execute("ROLLBACK");
          QMetaObject::invokeMethod(this, [this] { savedMessages_.clear(); }, Qt::QueuedConnection);
        }
      },
      Qt::QueuedConnection);
}
void HistoryStore::flush()
{
  QMetaObject::invokeMethod(worker_, [] {}, Qt::BlockingQueuedConnection);
}
void HistoryStore::pruneWorker(qint64 now)
{
  Statement age(db_, "DELETE FROM packets WHERE timestamp < ?");
  if (!age.value)
  {
    reportError();
    return;
  }
  age.number(1, now - qint64(options_.retentionDays) * 86400000);
  if (sqlite3_step(age.value) != SQLITE_DONE)
    reportError();
  Statement count(
      db_,
      "DELETE FROM packets WHERE id <= (SELECT id FROM packets ORDER BY id DESC LIMIT 1 OFFSET ?)");
  if (count.value)
  {
    count.number(1, options_.maxPackets);
    if (sqlite3_step(count.value) != SQLITE_DONE)
      reportError();
  }
  Statement total(db_, "SELECT COALESCE(SUM(length(packet)),0) FROM packets");
  if (!total.value || sqlite3_step(total.value) != SQLITE_ROW)
  {
    reportError();
    return;
  }
  qint64 excess = sqlite3_column_int64(total.value, 0) - options_.maxBytes;
  sqlite3_reset(total.value);
  if (excess > 0)
  {
    Statement oldest(db_, "SELECT id,length(packet) FROM packets ORDER BY id");
    qint64 last = 0;
    if (oldest.value)
      while (excess > 0 && sqlite3_step(oldest.value) == SQLITE_ROW)
      {
        last = sqlite3_column_int64(oldest.value, 0);
        excess -= sqlite3_column_int64(oldest.value, 1);
      }
    sqlite3_reset(oldest.value);
    Statement remove(db_, "DELETE FROM packets WHERE id<=?");
    if (remove.value)
    {
      remove.number(1, last);
      if (sqlite3_step(remove.value) != SQLITE_DONE)
        reportError();
    }
  }
  if (!execute("PRAGMA incremental_vacuum(256)"))
    reportError();
  sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
}
void HistoryStore::prune(qint64 now)
{
  if (opened_)
    QMetaObject::invokeMethod(
        worker_, [this, now] { pruneWorker(now ? now : QDateTime::currentMSecsSinceEpoch()); },
        Qt::QueuedConnection);
}
QList<HistoryPacket> HistoryStore::packets(qint64 before, int limit)
{
  QList<HistoryPacket> result;
  if (!opened_)
    return result;
  QMetaObject::invokeMethod(
      worker_,
      [this, before, limit, &result]
      {
        Statement q(db_, "SELECT id,timestamp,device,packet,outgoing FROM packets WHERE (?=0 OR "
                         "id<?) ORDER BY id DESC LIMIT ?");
        if (!q.value)
        {
          reportError();
          return;
        }
        q.number(1, before);
        q.number(2, before);
        q.number(3, qBound(1, limit, 1000));
        int rc;
        while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
          result.append({sqlite3_column_int64(q.value, 0), sqlite3_column_int64(q.value, 1),
                         sqlite3_column_int(q.value, 2), bytes(q.value, 3),
                         bool(sqlite3_column_int(q.value, 4))});
        if (rc != SQLITE_DONE)
          reportError();
      },
      Qt::BlockingQueuedConnection);
  return result;
}
QJsonArray HistoryStore::messages(const QString &identity, qint64 before, int limit)
{
  QJsonArray result;
  if (!opened_)
    return result;
  QMetaObject::invokeMethod(
      worker_,
      [this, identity, before, limit, &result]
      {
        Statement q(db_, "SELECT record FROM messages WHERE identity=? AND (?=0 OR id<?) ORDER BY "
                         "id DESC LIMIT ?");
        if (!q.value)
        {
          reportError();
          return;
        }
        q.text(1, identity.toUtf8());
        q.number(2, before);
        q.number(3, before);
        q.number(4, qBound(1, limit, 2000));
        int rc;
        while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
          result.append(QJsonDocument::fromJson(bytes(q.value, 0)).object());
        if (rc != SQLITE_DONE)
          reportError();
      },
      Qt::BlockingQueuedConnection);
  return result;
}
bool HistoryStore::restore(StationModel *stations, MessageService *service, QString *error)
{
  if (!opened_)
  {
    if (error)
      *error = tr("History database is not open.");
    return false;
  }
  prune();
  flush();
  QList<HistoryPacket> batch;
  qint64 after = 0;
  bool ok = true;
  {
    QSignalBlocker block(stations);
    do
    {
      batch.clear();
      QMetaObject::invokeMethod(
          worker_,
          [this, after, &batch, &ok]
          {
            Statement q(db_, "SELECT id,timestamp,device,packet,outgoing FROM packets WHERE id>? "
                             "AND outgoing=0 ORDER BY id LIMIT 512");
            if (!q.value)
            {
              ok = false;
              return;
            }
            q.number(1, after);
            int rc;
            while ((rc = sqlite3_step(q.value)) == SQLITE_ROW)
              batch.append({sqlite3_column_int64(q.value, 0), sqlite3_column_int64(q.value, 1),
                            sqlite3_column_int(q.value, 2), bytes(q.value, 3),
                            bool(sqlite3_column_int(q.value, 4))});
            if (rc != SQLITE_DONE)
              ok = false;
          },
          Qt::BlockingQueuedConnection);
      for (const auto &p : batch)
      {
        stations->applyPacket(p.packet, p.device == 256 ? 'I' : 'T', p.timestamp / 1000);
        after = p.key;
      }
    } while (ok && batch.size() == 512);
  }
  emit stations->stationsChanged();
  auto descending = messages(service->callsign(), 0, 2000);
  QJsonArray ascending;
  for (auto i = descending.size(); i > 0; --i)
    ascending.append(descending[i - 1]);
  QString counter = "00";
  quint64 next = 1;
  QMetaObject::invokeMethod(
      worker_,
      [this, service, &counter, &next]
      {
        Statement q(db_, "SELECT counter,next_key FROM identities WHERE identity=?");
        if (!q.value)
          return;
        q.text(1, service->callsign().toUtf8());
        if (sqlite3_step(q.value) == SQLITE_ROW)
        {
          counter = QString::fromLatin1(bytes(q.value, 0));
          next = quint64(sqlite3_column_int64(q.value, 1));
        }
      },
      Qt::BlockingQueuedConnection);
  service->restoreArchive(ascending, counter, next);
  if (!ok && error)
    *error = tr("Unable to read saved traffic.");
  return ok;
}
