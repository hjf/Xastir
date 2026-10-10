// SPDX-License-Identifier: GPL-2.0-or-later
#include "packet_monitor.h"
#include "main_window.h"
#include "station_model.h"
#include "message_service.h"
#include <QAction>
#include <QDialog>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
class PacketMonitorTest : public QObject {
  Q_OBJECT
private slots:
  void captureReplayAndBoundedHistory() {
    QMainWindow window;
    PacketMonitor monitor(&window);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("packets.jsonl");
    QString error;
    QVERIFY(monitor.startCapture(path, &error));
    QByteArray packet("REMOTE>APRS:!4903.50N/07201.75W>test");
    packet += char(0xe9);
    monitor.receive(packet, 3);
    monitor.stopCapture();
    QSignalSpy replay(&monitor, &PacketMonitor::replayPacket);
    QVERIFY(monitor.startReplay(path, &error));
    QVERIFY(!monitor.startCapture(path, &error));
    QTRY_COMPARE(replay.count(), 1);
    QCOMPARE(replay.first().first().toByteArray(), packet);
    QTRY_VERIFY(!monitor.replaying());
    for (int i = 0; i < 2100; ++i) monitor.receive("REMOTE>APRS:>status", 0);
    QCOMPARE(monitor.model()->rowCount(), 2000);
    QVERIFY(monitor.startCapture(path, &error));
    QVERIFY(!monitor.startReplay(path, &error));
    monitor.stopCapture();
  }
  void replayUpdatesMapWithoutMessaging() {
    ApplicationOptions options;
    MainWindow window(options);
    auto *monitor = window.findChild<PacketMonitor *>();
    QVERIFY(monitor);
    auto *action = window.findChild<QAction *>("PULDNVI002");
    QVERIFY(action && action->property("implemented").toBool());
    action->trigger();
    QVERIFY(window.findChild<QDialog *>("incomingDataDialog"));
    QTemporaryDir dir;
    QFile file(dir.filePath("packets.log"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("REMOTE>APRS:!4903.50N/07201.75W>test\nREMOTE>APRS::ALL      :hello{01\n");
    file.close();
    QVERIFY(monitor->startReplay(file.fileName()));
    QTRY_VERIFY(!monitor->replaying());
    QVERIFY(window.stationModel()->stations().contains("REMOTE"));
    QVERIFY(window.messageService()->messages().isEmpty());
    QCOMPARE(monitor->model()->rowCount(), 2);
  }
  void malformedReplayStops() {
    QMainWindow window;
    PacketMonitor monitor(&window);
    // Capture errors directly without opening the normal UI error box.
    QObject::disconnect(&monitor, &PacketMonitor::errorOccurred, &monitor, nullptr);
    QSignalSpy errors(&monitor, &PacketMonitor::errorOccurred);
    QTemporaryDir dir;
    QFile file(dir.filePath("bad.log"));
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray(17000, 'x')); file.close();
    QVERIFY(monitor.startReplay(file.fileName()));
    QTRY_COMPARE(errors.count(), 1);
    QVERIFY(!monitor.replaying());
    QCOMPARE(monitor.model()->rowCount(), 0);
  }
};
QTEST_MAIN(PacketMonitorTest)
#include "test_packet_monitor.moc"
