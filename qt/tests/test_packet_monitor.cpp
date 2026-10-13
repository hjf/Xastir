// SPDX-License-Identifier: GPL-2.0-or-later
#include "packet_monitor.h"
#include "file_menu.h"
#include <QFileDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QJsonDocument>
#include <QJsonObject>
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
  void fileMenuReplayPreservesTimestamp() {
    ApplicationOptions options;
    MainWindow window(options);
    QTemporaryDir dir;
    QFile file(dir.filePath("recorded.jsonl"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const qint64 timestamp = 1700000000123LL;
    file.write(QJsonDocument(QJsonObject{{"timestamp", timestamp},
      {"packet", QString::fromLatin1(QByteArray("REMOTE>APRS:!4903.50N/07201.75W>recorded").toBase64())}})
      .toJson(QJsonDocument::Compact) + '\n');
    file.close();
    auto *action = window.findChild<QAction *>("PULDNFI002");
    QVERIFY(action && action->property("implemented").toBool());
    action->trigger();
    auto *chooser = window.findChild<QFileDialog *>("openLogFileDialog");
    QVERIFY(chooser);
    QVERIFY(chooser->isVisible());
    // Drive the chooser's selection signal without depending on a native dialog.
    QVERIFY(QMetaObject::invokeMethod(chooser, "fileSelected", Q_ARG(QString, file.fileName())));
    chooser->close();
    auto *monitor = window.findChild<PacketMonitor *>();
    QTRY_VERIFY(!monitor->replaying());
    QCOMPARE(window.stationModel()->stations().value("REMOTE").heardAt, timestamp / 1000);
    QVERIFY(window.messageService()->messages().isEmpty());
  }
  void fileMenuCancelDoesNotReplay() {
    ApplicationOptions options;
    MainWindow window(options);
    window.findChild<QAction *>("PULDNFI002")->trigger();
    auto *chooser = window.findChild<QFileDialog *>("openLogFileDialog");
    QVERIFY(chooser);
    chooser->reject();
    QVERIFY(!window.findChild<PacketMonitor *>()->replaying());
    QVERIFY(window.stationModel()->stations().isEmpty());
  }
  void invalidRecordedTimestampStopsReplay() {
    QMainWindow window;
    PacketMonitor monitor(&window);
    QObject::disconnect(&monitor, &PacketMonitor::errorOccurred, &monitor, nullptr);
    QSignalSpy errors(&monitor, &PacketMonitor::errorOccurred);
    QSignalSpy replay(&monitor, &PacketMonitor::replayPacket);
    QTemporaryDir dir;
    QFile file(dir.filePath("bad.jsonl"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"timestamp", "invalid"},
      {"packet", QString::fromLatin1(QByteArray("REMOTE>APRS:>test").toBase64())}})
      .toJson(QJsonDocument::Compact) + '\n');
    file.close();
    QVERIFY(monitor.startReplay(file.fileName()));
    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(replay.count(), 0);
    QVERIFY(!monitor.replaying());
  }
  void printMenuAndPdf() {
    ApplicationOptions options;
    MainWindow window(options);
    window.show();
    auto *action = window.findChild<QAction *>("PULDNFI015");
    QVERIFY(action && action->property("implemented").toBool());
    action->trigger();
    auto *preview = window.findChild<QPrintPreviewDialog *>("mapPrintPreview");
    QVERIFY(preview);
    QVERIFY(preview->isVisible());
    preview->close();
    QTemporaryDir dir;
    const auto path = dir.filePath("map.pdf");
    QPrinter printer;
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    QPixmap map(640, 480);
    map.fill(Qt::green);
    QVERIFY(FileMenu::printMap(&printer, map));
    QFile pdf(path);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.read(5).startsWith("%PDF-"));
    QVERIFY(pdf.size() > 1000);
    QVERIFY(!FileMenu::printMap(&printer, QPixmap()));
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
