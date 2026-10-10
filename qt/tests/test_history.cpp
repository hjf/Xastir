// SPDX-License-Identifier: GPL-2.0-or-later
#include "history_store.h"
#include "history_ui.h"
#include "main_window.h"
#include "message_core.h"
#include "message_service.h"
#include "station_model.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QJsonObject>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cstdlib>
#include <sqlite3.h>
namespace
{
const QByteArray position = "REMOTE>APRS:!4903.50N/07201.75W>old position";
const QByteArray object = "OWNER>APRS:;TESTOBJ  *101200z4903.50N/07201.75W>object";
const QByteArray kill = "OWNER>APRS:;TESTOBJ  _101200z4903.50N/07201.75W>gone";
qint64 sqlNumber(const QString &path, const char *sql)
{
  sqlite3 *db = nullptr;
  sqlite3_open(path.toUtf8().constData(), &db);
  sqlite3_stmt *s = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &s, nullptr);
  qint64 value = -1;
  if (s && sqlite3_step(s) == SQLITE_ROW)
    value = sqlite3_column_int64(s, 0);
  sqlite3_finalize(s);
  sqlite3_close(db);
  return value;
}
} // namespace
class HistoryTest : public QObject
{
  Q_OBJECT
private slots:
  void restartAndRetention()
  {
    QTemporaryDir dir;
    HistoryOptions opts;
    opts.path = dir.filePath("history.sqlite");
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto old = now - 2 * 86400000;
    {
      HistoryStore store(opts);
      QString error;
      QVERIFY2(store.open(&error), qPrintable(error));
      store.recordPacket("EXPIRED>APRS:!4903.50N/07201.75W>expired", 0, now - 8 * 86400000);
      store.recordPacket(position, 0, old);
      store.recordPacket("TXONLY>APRS:!4903.50N/07201.75W>outgoing", 0, old, true);
      store.recordPacket(object, 0, old + 1000);
      store.recordPacket(kill, 0, old + 2000);
      store.recordPacket("NET>APRS:!4903.50N/07201.75W>internet", 256, old + 3000);
      store.recordPacket("REMOTE>APRS:_10121200c180s010g020t050", 0, old + 4000);
      store.recordPacket("REMOTE>APRS::LOCAL    :permanent{A1", 0, now - 9 * 86400000);
      MessageService service("LOCAL", nullptr, [=] { return now - 9 * 86400000; });
      int transmissions = 0;
      service.setTransport(
          [&](const auto &, int, const auto &, auto *)
          {
            ++transmissions;
            return true;
          });
      service.receivePacket("REMOTE>APRS::LOCAL    :permanent{A1", 0);
      QVERIFY(service.sendMessage("REMOTE", "unfinished", false, 0, "", &error));
      QVERIFY(service.messages().last().pending);
      store.saveMessages(&service);
      store.prune(now);
      store.flush();
      QCOMPARE(store.packets().size(), 6);
      QCOMPARE(store.messages("LOCAL").size(), 2);
      QCOMPARE(sqlNumber(opts.path, "SELECT COUNT(*) FROM message_packets"), qint64(1));
    }
    {
      HistoryStore store(opts);
      QVERIFY(store.open());
      StationModel stations;
      MessageService service("LOCAL");
      int transmissions = 0;
      service.setTransport(
          [&](const auto &, int, const auto &, auto *)
          {
            ++transmissions;
            return true;
          });
      service.setAutoReplyText("away");
      service.setAutoReply(true);
      QVERIFY(store.restore(&stations, &service));
      QCOMPARE(transmissions, 0);
      service.processRetries();
      QCOMPARE(transmissions, 0);
      QVERIFY(stations.record("REMOTE"));
      QCOMPARE(qint64(stations.record("REMOTE")->sec_heard), (old + 4000) / 1000);
      QVERIFY(stations.record("REMOTE")->weather_data);
      QCOMPARE(qint64(stations.record("REMOTE")->weather_data->wx_sec_time), (old + 4000) / 1000);
      QVERIFY(!stations.record("TESTOBJ"));
      QVERIFY(!stations.record("EXPIRED"));
      QVERIFY(!stations.record("TXONLY"));
      QVERIFY(stations.record("NET"));
      QCOMPARE(stations.record("NET")->data_via, 'I');
      QCOMPARE(service.messages().size(), 2);
      QCOMPARE(service.messages().first().text, QString("permanent"));
      auto pending = service.messages().last();
      QVERIFY(!pending.pending);
      QVERIFY(pending.retryable);
      QVERIFY(pending.status.contains("Interrupted"));
      const auto oldId = pending.id;
      QString error;
      QVERIFY(service.sendMessage("REMOTE", "new", false, 0, "", &error));
      QVERIFY(service.messages().last().key > pending.key);
      QVERIFY(service.messages().last().id != oldId);
      QCOMPARE(transmissions, 1);
      MessageService other("OTHER");
      StationModel otherStations;
      QVERIFY(store.restore(&otherStations, &other));
      QVERIFY(other.messages().isEmpty());
    }
  }
  void boundsAndPagination()
  {
    QTemporaryDir dir;
    HistoryOptions opts;
    opts.path = dir.filePath("history.sqlite");
    opts.maxPackets = 3;
    opts.maxBytes = 4096;
    HistoryStore store(opts);
    QVERIFY(store.open());
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < 6; ++i)
      store.recordPacket(position + QByteArray::number(i), i, now + i);
    store.prune(now);
    store.flush();
    const auto latest = store.packets(0, 2);
    QCOMPARE(latest.size(), 2);
    QCOMPARE(latest.first().device, 5);
    auto older = store.packets(latest.last().key, 2);
    QCOMPARE(older.size(), 1);
    QCOMPARE(older.first().device, 3);
    QByteArray large = "LARGE>APRS:" + QByteArray(3000, 'X');
    store.recordPacket(large, 0, now);
    store.recordPacket(large, 1, now);
    store.prune(now);
    store.flush();
    QCOMPARE(store.packets().size(), 1);
    QCOMPARE(store.packets().first().device, 1);
    QVERIFY(sqlNumber(opts.path, "SELECT SUM(length(packet)) FROM packets") <= opts.maxBytes);
    store.prune(now + 8 * 86400000);
    store.flush();
    QVERIFY(store.packets().isEmpty());
  }
  void archiveAndPassiveInternet()
  {
    QTemporaryDir dir;
    HistoryOptions opts;
    opts.path = dir.filePath("history.sqlite");
    HistoryStore store(opts);
    QVERIFY(store.open());
    MessageService service("LOCAL");
    int tx = 0;
    service.setTransport(
        [&](const auto &, int, const auto &, auto *)
        {
          ++tx;
          return true;
        });
    service.setAutoReplyText("away");
    service.setAutoReply(true);
    service.receiveInternetPacket("REMOTE>APRS::LOCAL    :internet{N1");
    QCOMPARE(tx, 0);
    QCOMPARE(service.messages().size(), 1);
    service.receiveInternetPacket("REMOTE>APRS::LOCAL    :internet{N1");
    QCOMPARE(service.messages().size(), 1);
    store.saveMessages(&service);
    QString error;
    QVERIFY(service.sendMessage("REMOTE", "test", false, 0, "", &error));
    const auto key = service.messages().last().key;
    service.receiveInternetPacket("REMOTE>APRS::LOCAL    :ack" +
                                  service.messages().last().id.toLatin1());
    QVERIFY(service.messages().last().pending);
    store.saveMessages(&service);
    service.receivePacket("REMOTE>APRS::LOCAL    :ack" + service.messages().last().id.toLatin1(),
                          0);
    store.saveMessages(&service);
    store.flush();
    auto archive = store.messages("LOCAL", 0, 1);
    QCOMPARE(archive.size(), 1);
    QVERIFY(!archive.first().toObject()["pending"].toBool());
    auto older = store.messages("LOCAL", qint64(key), 1);
    QCOMPARE(older.first().toObject()["text"].toString(), QString("internet"));
    service.clearOutgoing();
    service.receiveInternetPacket("REMOTE>APRS::LOCAL    :next{N2");
    QVERIFY(service.messages().last().key > key);
    store.saveMessages(&service);
    store.flush();
    QCOMPARE(store.messages("LOCAL").size(), 3);
  }
  void internetReplyDoesNotUseRfRoute()
  {
    MessageService service("LOCAL");
    QList<int> devices;
    QStringList paths;
    service.setTransport(
        [&](const auto &, int device, const auto &path, auto *)
        {
          devices.append(device);
          paths.append(path);
          return true;
        });
    QString error;
    QVERIFY(service.sendMessage("REMOTE", "RF conversation", false, 0, "WIDE1-1", &error));
    devices.clear();
    paths.clear();
    service.receiveInternetPacket("REMOTE>APRS,TCPIP*::LOCAL    :internet{X1", 0, true);
    QCOMPARE(devices, QList<int>{256});
    QCOMPARE(paths, QStringList{QString()});
  }
  void archiveBeyondMemoryLimit()
  {
    QTemporaryDir dir;
    HistoryOptions opts;
    opts.path = dir.filePath("archive.sqlite");
    const auto now = QDateTime::currentMSecsSinceEpoch();
    {
      HistoryStore store(opts);
      QVERIFY(store.open());
      MessageService service("LOCAL");
      for (int batch = 0; batch < 3; ++batch)
      {
        QJsonArray records;
        for (int i = 0; i < 700; ++i)
        {
          const int key = batch * 700 + i + 1;
          records.append(QJsonObject{{"key", QString::number(key)},
                                     {"timestamp", QString::number(now - 9 * 86400000)},
                                     {"sender", "REMOTE"},
                                     {"recipient", "LOCAL"},
                                     {"text", QString::number(key)},
                                     {"status", XMSG_HEARD},
                                     {"device", 256}});
        }
        service.restoreArchive(records, "00", quint64((batch + 1) * 700 + 1));
        store.saveMessages(&service);
      }
      store.prune(now);
      store.flush();
      QCOMPARE(sqlNumber(opts.path, "SELECT COUNT(*) FROM messages"), qint64(2100));
      QCOMPARE(store.messages("LOCAL", 2, 1).first().toObject()["text"].toString(), QString("1"));
    }
    HistoryStore reopened(opts);
    QVERIFY(reopened.open());
    StationModel stations;
    MessageService service("LOCAL");
    QVERIFY(reopened.restore(&stations, &service));
    QCOMPARE(service.messages().size(), 2000);
    QCOMPARE(service.messages().first().key, quint64(101));
    QCOMPARE(service.nextMessageKey(), quint64(2101));
  }
  void survivesUncleanExit()
  {
    QTemporaryDir dir;
    const auto path = dir.filePath("crash.sqlite");
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--crash-writer", path});
    QVERIFY(child.waitForFinished(15000));
    QCOMPARE(child.exitCode(), 0);
    HistoryOptions opts;
    opts.path = path;
    HistoryStore store(opts);
    QVERIFY(store.open());
    StationModel stations;
    MessageService service("LOCAL");
    QVERIFY(store.restore(&stations, &service));
    QVERIFY(stations.record("REMOTE"));
    QCOMPARE(service.messages().size(), 1);
  }
  void liveWindowRestart()
  {
    QTemporaryDir dir;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    ApplicationOptions opts;
    opts.historyEnabled = true;
    opts.history.path = dir.filePath("live.sqlite");
    opts.tiles.urlTemplate = "http://127.0.0.1:1/{z}/{x}/{y}.png";
    opts.config.callsign = "LOCAL";
    AgwpeSettings radio;
    radio.host = "127.0.0.1";
    radio.port = server.serverPort();
    radio.callsign = "LOCAL";
    radio.deviceIndex = 0;
    radio.connectOnStartup = true;
    radio.transmitEnabled = true;
    opts.config.agwpe.append(radio);
    qint64 heard = 0;
    {
      MainWindow window(opts);
      QTRY_VERIFY(server.hasPendingConnections());
      auto *peer = server.nextPendingConnection();
      QByteArray payload(1, '\0');
      for (const auto &call : {QByteArray("APRS  "), QByteArray("REMOTE")})
      {
        for (char c : call)
          payload += char(c << 1);
        payload += char(call == "REMOTE" ? 0x61 : 0x60);
      }
      payload += char(3);
      payload += char(0xf0);
      payload += "!4903.50N/07201.75W>live";
      QByteArray frame(36, '\0');
      frame[4] = 'K';
      qToLittleEndian<quint32>(quint32(payload.size()), frame.data() + 28);
      peer->write(frame + payload);
      peer->flush();
      QTRY_VERIFY(window.stationModel()->record("REMOTE"));
      heard = window.stationModel()->record("REMOTE")->sec_heard;
      window.historyStore()->flush();
      QCOMPARE(window.historyStore()->packets().size(), 1);
      QVERIFY(!window.historyStore()->packets().first().outgoing);
    }
    opts.config.agwpe.clear();
    MainWindow restored(opts);
    QVERIFY(restored.stationModel()->record("REMOTE"));
    QCOMPARE(qint64(restored.stationModel()->record("REMOTE")->sec_heard), heard);
  }
  void exclusiveWriter()
  {
    QTemporaryDir dir;
    HistoryOptions opts;
    opts.path = dir.filePath("locked.sqlite");
    HistoryStore first(opts);
    QVERIFY(first.open());
    HistoryStore second(opts);
    QString error;
    QVERIFY(!second.open(&error));
    QVERIFY(error.contains("another application instance"));
  }
  void invalidDatabaseAndHistoryMenu()
  {
    QTemporaryDir dir;
    HistoryOptions bad;
    bad.path = dir.filePath("bad.sqlite");
    QFile f(bad.path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not sqlite");
    f.close();
    HistoryStore broken(bad);
    QString error;
    QVERIFY(!broken.open(&error));
    QVERIFY(!error.isEmpty());
    ApplicationOptions opts;
    opts.historyEnabled = true;
    opts.history.path = dir.filePath("good.sqlite");
    opts.tiles.urlTemplate = "http://127.0.0.1:1/{z}/{x}/{y}.png";
    MainWindow window(opts);
    auto *action = window.findChild<QAction *>("actionHistory");
    QVERIFY(action);
    action->trigger();
    QVERIFY(window.findChild<QDialog *>("historyDialog"));
  }
};
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setOrganizationName("XastirTests");
  app.setApplicationName("HistoryTest");
  if (argc == 3 && QByteArray(argv[1]) == "--crash-writer")
  {
    HistoryOptions opts;
    opts.path = QString::fromLocal8Bit(argv[2]);
    HistoryStore store(opts);
    if (!store.open())
      return 2;
    store.recordPacket(position, 0);
    MessageService service("LOCAL");
    service.receiveInternetPacket("REMOTE>APRS::LOCAL    :survive{X1");
    store.saveMessages(&service);
    store.flush();
    std::_Exit(0); // Deliberately skip destructors and SQLite clean shutdown.
  }
  HistoryTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "test_history.moc"
