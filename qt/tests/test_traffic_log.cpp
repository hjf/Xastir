// SPDX-License-Identifier: GPL-2.0-or-later
#include "traffic_log.h"
#include "packet_monitor.h"
#include "legacy_menus.h"
#include "main_window.h"
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QtTest>

class TrafficLogTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
  static QList<QJsonObject> records(const QString &directory, const QString &prefix) {
    QList<QJsonObject> result;
    for (const auto &name : QDir(directory).entryList({prefix + "-*.jsonl"}, QDir::Files)) {
      QFile file(QDir(directory).filePath(name)); if (!file.open(QIODevice::ReadOnly)) return {};
      while (!file.atEnd()) result.append(QJsonDocument::fromJson(file.readLine()).object());
    }
    return result;
  }
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirLogTests"); QCoreApplication::setApplicationName("TrafficLogs");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void classificationBytesAndDirection() {
    QTemporaryDir output; QMainWindow window; createLegacyMenus(&window);
    TrafficLogControl logs(&window, {}); logs.configure(output.path());
    PacketMonitor monitor(&window); connect(&monitor, &PacketMonitor::packetObserved, &logs, &TrafficLogControl::observe);
    for (auto category : {TrafficLogControl::Tnc, TrafficLogControl::Internet, TrafficLogControl::Messages, TrafficLogControl::Weather}) logs.setEnabled(category, true);
    const QByteArray message("GATE>APRS:}REMOTE>APRS::LOCAL    :hello{01");
    const QByteArray weather("WX>APRS:_10091300c180s005g010t032h50b10000");
    const QByteArray positionWeather("WX>APRS:!4903.51N/07201.74W_090/010g020t032h50b10123");
    monitor.receive(message, 2); monitor.receive(weather, 256); monitor.receive(positionWeather, 0);
    monitor.recordTransmitted("LOCAL>APRS::REMOTE   :ack01", 2);
    monitor.receive("REMOTE>APRS::BLN1     :Notice", 256);
    const QByteArray raw = QByteArray("REMOTE>APRS:>raw ") + char(0xe9); monitor.receive(raw, 2);
    logs.flush();
    const auto tnc = records(output.path(), "tnc"), internet = records(output.path(), "internet");
    QCOMPARE(tnc.size(), 4); QCOMPARE(internet.size(), 2);
    QCOMPARE(QByteArray::fromBase64(tnc[0]["packet"].toString().toLatin1()), message);
    QCOMPARE(QByteArray::fromBase64(tnc.last()["packet"].toString().toLatin1()), raw);
    QCOMPARE(tnc[0]["device"].toInt(), 2); QVERIFY(!tnc[0]["outgoing"].toBool()); QVERIFY(tnc[2]["outgoing"].toBool());
    QVERIFY(tnc[0]["timestamp"].toVariant().toLongLong() > QDateTime::currentMSecsSinceEpoch() - 10000);
    QCOMPARE(records(output.path(), "messages").size(), 3); QCOMPARE(records(output.path(), "weather").size(), 2);
    QVERIFY(!window.findChild<QAction *>("PULDNFI012")->isEnabled());
    QVERIFY(!window.findChild<QAction *>("WX Alert Logging")->isEnabled());
    // Replaying these logs displays queued TX, without classifying it as RX or logging again.
    QSignalSpy replay(&monitor, &PacketMonitor::replayPacket), observed(&monitor, &PacketMonitor::packetObserved);
    const auto path = QDir(output.path()).filePath(QDir(output.path()).entryList({"messages-*.jsonl"}).first());
    QVERIFY(monitor.startReplay(path)); QTRY_VERIFY(!monitor.replaying());
    QCOMPARE(replay.count(), 2); QCOMPARE(observed.count(), 0);
    QCOMPARE(records(output.path(), "messages").size(), 3);
    QVERIFY(monitor.model()->item(monitor.model()->rowCount()-2, 1)->text().contains("TX"));
  }
  void rotationRetentionAndShutdownFlush() {
    QTemporaryDir output;
    const auto old = "tnc-" + QDate::currentDate().addDays(-8).toString(Qt::ISODate) + ".jsonl";
    QFile stale(output.filePath(old)); QVERIFY(stale.open(QIODevice::WriteOnly)); stale.write("old\n"); stale.close();
    QFile other(output.filePath("tnc-2020-01-01-user.jsonl")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("keep"); other.close();
    {
      QMainWindow window; TrafficLogLimits limits; limits.fileBytes = 8192; limits.categoryBytes = 16384;
      TrafficLogControl logs(&window, {}, limits); logs.configure(output.path()); logs.setEnabled(TrafficLogControl::Tnc, true);
      for (int i = 0; i < 120; ++i) { logs.observe("REMOTE>APRS:>" + QByteArray(1000, 'a'), 0, 1700000000123LL + i, false); logs.flush(); }
    }
    QVERIFY(!stale.exists()); QVERIFY(other.exists());
    qint64 total = 0; int files = 0;
    for (const auto &entry : QDir(output.path()).entryInfoList({"tnc-*.jsonl"}, QDir::Files)) {
      if (entry.fileName().contains("-user")) continue;
      total += entry.size(); ++files; QVERIFY(entry.size() <= 8192);
    }
    QVERIFY(files > 1 && files <= 4); QVERIFY(total <= 16384);
    auto result = records(output.path(), "tnc"); QVERIFY(!result.isEmpty());
  }
  void failureDisableAndSupersededFailure() {
    QTemporaryDir output; QFile blocked(output.filePath("blocked"));
    QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("keep"); blocked.close();
    QMainWindow window; createLegacyMenus(&window); TrafficLogControl logs(&window, {});
    logs.configure(blocked.fileName()); logs.setEnabled(TrafficLogControl::Tnc, true); logs.setEnabled(TrafficLogControl::Messages, true);
    QSignalSpy errors(&logs, &TrafficLogControl::errorOccurred);
    logs.observe("REMOTE>APRS::LOCAL    :hello", 0, 1700000000000LL, false); logs.flush();
    QTRY_COMPARE(errors.count(), 2);
    QVERIFY(!logs.enabled(TrafficLogControl::Tnc)); QVERIFY(!logs.enabled(TrafficLogControl::Messages));
    QVERIFY(!window.findChild<QAction *>("Message Logging")->isChecked());
    logs.setEnabled(TrafficLogControl::Tnc, true);
    logs.observe("REMOTE>APRS:>old", 0, 1700000000000LL, false); logs.flush();
    logs.configure(output.path()); // The old error callback is queued, not yet delivered.
    logs.observe("REMOTE>APRS:>new", 0, 1700000000000LL, false); logs.flush();
    QCoreApplication::processEvents(); QVERIFY(logs.enabled(TrafficLogControl::Tnc)); QCOMPARE(errors.count(), 2);
    QCOMPARE(records(output.path(), "tnc").size(), 1);
  }
  void timingCancelApplyAndLegacyRestore() {
    QTemporaryDir output;
    QFile file(output.filePath("legacy.cnf")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("TNC_LOG_DATA:1\nNET_LOG_DATA:1\nLOG_MESSAGE:1\nLOG_WX:1\n"); file.close();
    ApplicationOptions options; QString error; QVERIFY(readXastirConfig(file.fileName(), options.config, &error));
    QCOMPARE(options.config.loggingMask, 15);
    {
      MainWindow window(options); auto *logs = window.findChild<TrafficLogControl *>();
      const auto initial = logs->directory();
      window.findChild<QAction *>("PULDNCF003")->trigger(); auto *dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QLineEdit *>("timingLogDirectory")->setText(output.path()); dialog->reject();
      QCOMPARE(logs->directory(), initial); QVERIFY(!QSettings().contains("logging/directory"));
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      window.findChild<QAction *>("PULDNCF003")->trigger(); dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QLineEdit *>("timingLogDirectory")->setText(output.path());
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
      QCOMPARE(logs->directory(), output.path()); window.findChild<QAction *>("PULDNFI010")->trigger();
      QVERIFY(!logs->enabled(TrafficLogControl::Tnc));
    }
    MainWindow restored(options); auto *logs = restored.findChild<TrafficLogControl *>();
    QCOMPARE(logs->directory(), output.path()); QVERIFY(!logs->enabled(TrafficLogControl::Tnc)); QVERIFY(logs->enabled(TrafficLogControl::Weather));
  }
  void boundedQueueAndDrain() {
    QTemporaryDir output; QMainWindow window; TrafficLogLimits limits; limits.pendingRecords = 1;
    quint64 dropped = 0;
    {
      TrafficLogControl logs(&window, {}, limits); logs.configure(output.path()); logs.setEnabled(TrafficLogControl::Tnc, true);
      for (int i = 0; i < 2000; ++i) {
        logs.observe("REMOTE>APRS:>" + QByteArray(3000, 'a'), 0, 1700000000000LL + i, false);
        QVERIFY(logs.pending() <= 1);
      }
      dropped = logs.dropped(); QVERIFY(dropped > 0);
      // The destructor must drain the final queued write without event delivery.
    }
    QCOMPARE(quint64(records(output.path(), "tnc").size()) + dropped, quint64(2000));
  }
};
QTEST_MAIN(TrafficLogTest)
#include "test_traffic_log.moc"
