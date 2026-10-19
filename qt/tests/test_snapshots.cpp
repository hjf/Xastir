// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "snapshot_control.h"
#include "kml_snapshot.h"
#include "station_model.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

class SnapshotTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirSnapshotTests");
    QCoreApplication::setApplicationName("Snapshots");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void menuCapturesMapAndBoundsQueue() {
    QTemporaryDir output;
    MainWindow window(ApplicationOptions{});
    auto *map = window.findChild<MapWidget *>();
    map->setMapsEnabled(false);
    window.show();
    auto *snapshots = window.findChild<SnapshotControl *>();
    QVERIFY(snapshots);
    snapshots->configure(output.path(), 1);
    QSignalSpy saved(snapshots, &SnapshotControl::saved);
    auto *action = window.findChild<QAction *>("PULDNFI014");
    QVERIFY(action && action->property("implemented").toBool());
    QVERIFY(!action->isChecked());
    const auto expected = map->grab().toImage();
    action->trigger();
    QVERIFY(snapshots->enabled());
    QVERIFY(snapshots->busy());
    for (int i = 0; i < 100; ++i) snapshots->captureNow();
    QTRY_COMPARE(saved.count(), 1);
    const QString path = output.filePath("snapshot.png");
    QCOMPARE(saved.first().first().toString(), path);
    const QImage image(path);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), expected.size());
    QCOMPARE(image.convertToFormat(QImage::Format_RGB32), expected.convertToFormat(QImage::Format_RGB32));
    QCOMPARE(QDir(output.path()).entryList(QDir::Files).size(), 1);
    auto *timer = snapshots->findChild<QTimer *>("pngSnapshotTimer");
    QVERIFY(timer && timer->isActive());
    QCOMPARE(timer->interval(), 60000);
    // Drive real periodic timer delivery without waiting a production minute.
    map->setMapBackground(Qt::green);
    timer->setInterval(20);
    QTRY_VERIFY(saved.count() >= 2);
    action->trigger();
    QVERIFY(!snapshots->enabled());
    QVERIFY(!timer->isActive());
    QTRY_VERIFY(!snapshots->busy());
    const auto count = saved.count();
    snapshots->captureNow();
    QTest::qWait(50);
    QCOMPARE(saved.count(), count);
    QCOMPARE(QDir(output.path()).entryList(QDir::Files), QStringList{"snapshot.png"});
    QVERIFY(QImage(path).convertToFormat(QImage::Format_RGB32) != image.convertToFormat(QImage::Format_RGB32));
  }
  void timingCancelApplyAndRestart() {
    QTemporaryDir output;
    ApplicationOptions options;
    {
      MainWindow window(options);
      auto *snapshots = window.findChild<SnapshotControl *>();
      const auto initial = snapshots->directory();
      window.findChild<QAction *>("PULDNCF003")->trigger();
      auto *dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QLineEdit *>("timingSnapshotDirectory")->setText(output.path());
      dialog->findChild<QSpinBox *>("timingSnapshotInterval")->setValue(2);
      dialog->reject();
      QCOMPARE(snapshots->directory(), initial);
      QCOMPARE(snapshots->intervalMinutes(), 5);
      QVERIFY(!QSettings().contains("snapshots/directory"));
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      window.findChild<QAction *>("PULDNCF003")->trigger();
      dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QLineEdit *>("timingSnapshotDirectory")->setText(output.path());
      dialog->findChild<QSpinBox *>("timingSnapshotInterval")->setValue(2);
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
      QCOMPARE(snapshots->directory(), output.path());
      QCOMPARE(snapshots->intervalMinutes(), 2);
      QVERIFY(!snapshots->enabled());
      window.findChild<QAction *>("PULDNFI014")->trigger();
      QVERIFY(snapshots->enabled());
      QVERIFY(!snapshots->busy()); // Hidden windows are not captured.
    }
    MainWindow restored(options);
    restored.findChild<MapWidget *>()->setMapsEnabled(false);
    auto *snapshots = restored.findChild<SnapshotControl *>();
    QCOMPARE(snapshots->directory(), output.path());
    QCOMPARE(snapshots->intervalMinutes(), 2);
    QVERIFY(restored.findChild<QAction *>("PULDNFI014")->isChecked());
    QSignalSpy saved(snapshots, &SnapshotControl::saved);
    restored.show();
    QTRY_COMPARE(saved.count(), 1);
  }
  void failureDisablesToggleAndPreservesExistingTarget() {
    QTemporaryDir output;
    const auto target = output.filePath("snapshot.png");
    QVERIFY(QDir().mkpath(target));
    QFile marker(target + "/keep.txt");
    QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("keep"); marker.close();
    MainWindow window(ApplicationOptions{});
    window.findChild<MapWidget *>()->setMapsEnabled(false);
    window.show();
    auto *snapshots = window.findChild<SnapshotControl *>();
    snapshots->configure(output.path(), 1);
    QSignalSpy errors(snapshots, &SnapshotControl::errorOccurred);
    window.findChild<QAction *>("PULDNFI014")->trigger();
    QTRY_COMPARE(errors.count(), 1);
    QVERIFY(!snapshots->enabled());
    QVERIFY(!window.findChild<QAction *>("PULDNFI014")->isChecked());
    QVERIFY(!QSettings().value("snapshots/enabled").toBool());
    QVERIFY(marker.open(QIODevice::ReadOnly)); QCOMPARE(marker.readAll(), QByteArray("keep"));
    QCOMPARE(QDir(output.path()).entryList(QDir::Files).size(), 0);
  }
  void reconfigureIgnoresSupersededFailure() {
    QTemporaryDir output;
    const auto blocked = output.filePath("not-a-directory");
    QFile file(blocked); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); file.close();
    MainWindow window(ApplicationOptions{});
    window.findChild<MapWidget *>()->setMapsEnabled(false);
    window.show();
    auto *snapshots = window.findChild<SnapshotControl *>();
    snapshots->configure(blocked, 1);
    QSignalSpy errors(snapshots, &SnapshotControl::errorOccurred);
    QSignalSpy saved(snapshots, &SnapshotControl::saved);
    snapshots->setEnabled(true);
    QVERIFY(snapshots->busy());
    snapshots->configure(output.filePath("valid"), 2);
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(errors.count(), 0);
    QVERIFY(snapshots->enabled());
    QVERIFY(!QImage(output.filePath("valid/snapshot.png")).isNull());
  }
  void legacyOptionsAndInvalidInterval() {
    QTemporaryDir output;
    QFile file(output.filePath("legacy.cnf"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("STATION_CALLSIGN:LOCAL\nSNAPSHOT_INTERVAL:7\nSNAPSHOTS_ENABLED:1\n"); file.close();
    ApplicationOptions options;
    QString error;
    QVERIFY2(readXastirConfig(file.fileName(), options.config, &error), qPrintable(error));
    QSettings().setValue("snapshots/directory", output.path());
    QSettings().setValue("snapshots/intervalMinutes", "invalid");
    MainWindow window(options);
    auto *snapshots = window.findChild<SnapshotControl *>();
    QCOMPARE(snapshots->intervalMinutes(), 7);
    QVERIFY(snapshots->enabled());
    QVERIFY(!QFile::exists(output.filePath("snapshot.png")));
  }
  void shutdownFinishesOutstandingWrite() {
    QTemporaryDir output;
    {
      MainWindow window(ApplicationOptions{});
      window.findChild<MapWidget *>()->setMapsEnabled(false);
      window.show();
      auto *snapshots = window.findChild<SnapshotControl *>();
      snapshots->configure(output.path(), 1);
      snapshots->setEnabled(true);
      QVERIFY(snapshots->busy());
    }
    QVERIFY(!QImage(output.filePath("snapshot.png")).isNull());
  }
  void kmlCapturesHiddenDataAndPeriodicReplacement() {
    QTemporaryDir output;
    MainWindow window(ApplicationOptions{});
    auto *stations = window.findChild<StationModel *>();
    stations->applyPacket("REMOTE>APRS:!4903.50N/07201.75W>first & <text>", 'T', 100);
    stations->applyPacket("REMOTE>APRS:!4903.51N/07201.74W>second", 'T', 110);
    stations->applyPacket("OWNER>APRS:;Aid Point*092345z4903.50N/07201.75Wh", 'T', 110);
    stations->applyPacket("OWNER>APRS:)Item!4903.51N/07201.74W-", 'T', 110);
    stations->applyPacket("STATUS>APRS:>Ready", 'T', 110);
    auto *snapshots = window.findChild<SnapshotControl *>();
    auto *kml = window.findChild<KmlSnapshotControl *>();
    snapshots->configure(output.path(), 1);
    QSignalSpy saved(kml, &KmlSnapshotControl::saved);
    auto *action = window.findChild<QAction *>("PULDNFI016");
    QVERIFY(action && action->property("implemented").toBool());
    action->trigger();
    QVERIFY(kml->busy());
    for (int i = 0; i < 100; ++i) kml->captureNow();
    QTRY_COMPARE(saved.count(), 1);
    QFile file(output.filePath("snapshot.kml")); QVERIFY(file.open(QIODevice::ReadOnly));
    auto data = file.readAll(); file.close();
    QXmlStreamReader xml(data); int count = 0, lines = 0;
    while (!xml.atEnd()) { xml.readNext(); if (xml.isStartElement()) { if (xml.name() == "Placemark") ++count; if (xml.name() == "LineString") ++lines; } }
    QVERIFY(!xml.hasError()); QCOMPARE(count, 3); QCOMPARE(lines, 1);
    QVERIFY(!data.contains("STATUS")); QVERIFY(data.contains("Aid Point"));
    auto *timer = kml->findChild<QTimer *>("kmlSnapshotTimer");
    QCOMPARE(timer->interval(), 60000);
    stations->applyPacket("OWNER>APRS:)Item_", 'T', 120);
    timer->setInterval(20);
    QTRY_VERIFY(saved.count() >= 2);
    action->trigger(); QTRY_VERIFY(!kml->busy());
    QVERIFY(!timer->isActive()); QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("<name>Item</name>"));
    QCOMPARE(QDir(output.path()).entryList(QDir::Files), QStringList{"snapshot.kml"});
  }
  void kmlFailureRestartAndSupersededCapture() {
    QTemporaryDir output;
    const auto blocked = output.filePath("blocked");
    QFile marker(blocked); QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("keep"); marker.close();
    {
      MainWindow window(ApplicationOptions{});
      auto *png = window.findChild<SnapshotControl *>();
      auto *kml = window.findChild<KmlSnapshotControl *>();
      QSignalSpy saved(kml, &KmlSnapshotControl::saved), errors(kml, &KmlSnapshotControl::errorOccurred);
      png->configure(blocked, 1); kml->setEnabled(true);
      QTRY_COMPARE(errors.count(), 1);
      QVERIFY(!kml->enabled()); QVERIFY(!window.findChild<QAction *>("PULDNFI016")->isChecked());
      kml->setEnabled(true); png->configure(output.path(), 2);
      QTRY_COMPARE(saved.count(), 1); QCOMPARE(errors.count(), 1);
      QVERIFY(kml->enabled()); QVERIFY(QSettings().value("snapshots/kmlEnabled").toBool());
      QCOMPARE(kml->findChild<QTimer *>("kmlSnapshotTimer")->interval(), 120000);
    }
    MainWindow restored(ApplicationOptions{});
    auto *kml = restored.findChild<KmlSnapshotControl *>();
    QVERIFY(kml->enabled());
    QSignalSpy saved(kml, &KmlSnapshotControl::saved); QTRY_COMPARE(saved.count(), 1);
    QVERIFY(marker.open(QIODevice::ReadOnly)); QCOMPARE(marker.readAll(), QByteArray("keep"));
  }
  void kmlWriterEscapesAndBoundsTrailCopy() {
    StationModel stations;
    QList<Station> points;
    for (int i = 0; i < 1100; ++i) {
      Station s; s.callsign = "REMOTE"; s.position = {49 + i * .00001, -72}; s.heardAt = 10000 + i;
      points.append(s);
    }
    QString error; QVERIFY2(stations.importTrail(points, &error), qPrintable(error));
    const auto *row = stations.record("REMOTE"); QVERIFY(row);
    const auto full = copyKmlStation(row); const auto bounded = copyKmlStation(row, 1000);
    QVERIFY(full.trail.size() > 1000); QCOMPARE(bounded.trail.size(), 1000);
    QCOMPARE(bounded.trail.last().latitude, full.trail.last().latitude);
    KmlStation record; record.name = "A & B"; record.description = "<test>";
    record.trail = {{1, 2, 0, false}, {2, 3, 0, false}, {3, 4, 0, true}, {4, 5, 0, false}};
    QTemporaryDir output; QVERIFY(writeKmlFile(output.filePath("test.kml"), {record}, &error));
    QFile file(output.filePath("test.kml")); QVERIFY(file.open(QIODevice::ReadOnly)); auto data = file.readAll();
    QVERIFY(data.contains("A &amp; B")); QVERIFY(data.contains("&lt;test&gt;")); QCOMPARE(data.count("<LineString>"), 2);
    QXmlStreamReader xml(data); while (!xml.atEnd()) xml.readNext(); QVERIFY(!xml.hasError());
  }
};
QTEST_MAIN(SnapshotTest)
#include "test_snapshots.moc"
