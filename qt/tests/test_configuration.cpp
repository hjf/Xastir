// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "beacon_control.h"
#include "message_service.h"
#include "packet_monitor.h"
#include "station_navigator.h"
#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QtTest>

class ConfigurationTest : public QObject
{
  Q_OBJECT
  QTemporaryDir settings_;
  static void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
  }
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("XastirConfigurationTests");
    QCoreApplication::setApplicationName("Configuration");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
  }
  void init() { QSettings().clear(); }
  void defaultsApplyAndRestore() {
    ApplicationOptions options;
    options.config.callsign = "LOCAL";
    {
      MainWindow window(options);
      auto *action = window.findChild<QAction *>("PULDNCF001");
      QVERIFY(action && action->property("implemented").toBool());
      action->trigger();
      auto *dialog = window.findChild<QDialog *>("configureDefaults");
      QVERIFY(dialog && dialog->isVisible());
      dialog->findChild<QCheckBox *>("defaultsAllDisabled")->setChecked(true);
      dialog->findChild<QCheckBox *>("defaultsPositionDisabled")->setChecked(false);
      dialog->findChild<QCheckBox *>("defaultsTrafficUtf8")->setChecked(false);
      accept(dialog);
      QVERIFY(window.findChild<QAction *>("PULDNTNT03")->isChecked());
      QVERIFY(!window.findChild<QAction *>("PULDNTNT05")->isChecked());
      auto *beacons = window.findChild<BeaconControl *>();
      QVERIFY(beacons->transmitDisabled());
      QVERIFY(!beacons->positionDisabled());
      QString error;
      QVERIFY(!window.messageService()->sendQuery("?APRS?", 3, &error));
      QVERIFY(error.contains("All transmission"));
      QVERIFY(!window.findChild<PacketMonitor *>()->trafficUtf8());
      QVERIFY(options.config.agwpe.isEmpty());
    }
    MainWindow restored(options);
    QVERIFY(restored.findChild<BeaconControl *>()->transmitDisabled());
    QVERIFY(!restored.findChild<BeaconControl *>()->positionDisabled());
    QVERIFY(!restored.findChild<PacketMonitor *>()->trafficUtf8());
  }
  void cancelLeavesSettingsAndControlsUnchanged() {
    MainWindow window(ApplicationOptions{});
    window.findChild<QAction *>("PULDNCF001")->trigger();
    auto *defaults = window.findChild<QDialog *>("configureDefaults");
    defaults->findChild<QCheckBox *>("defaultsAllDisabled")->setChecked(true);
    defaults->reject();
    QVERIFY(!window.findChild<BeaconControl *>()->transmitDisabled());
    QVERIFY(!QSettings().contains("transmit/allDisabled"));
    window.findChild<QAction *>("PULDNCF003")->trigger();
    auto *timing = window.findChild<QDialog *>("configureTiming");
    timing->findChild<QSpinBox *>("timingBeacon")->setValue(600);
    timing->findChild<QSpinBox *>("timingRemove")->setValue(1);
    timing->findChild<QSpinBox *>("timingTrailSegmentMinutes")->setValue(5);
    timing->findChild<QSpinBox *>("timingTrailSegmentDegrees")->setValue(2);
    timing->reject();
    QCOMPARE(window.findChild<BeaconControl *>()->intervalSeconds(), 300);
    QVERIFY(!QSettings().contains("beacon/interval"));
    QCOMPARE(window.stationModel()->database()->segment_minutes, 30);
    QCOMPARE(window.stationModel()->database()->segment_distance, 1);
    QVERIFY(!QSettings().contains("timing/removeSeconds"));
  }
  void timingChangesVisibilityAndRestores() {
    ApplicationOptions options;
    {
      MainWindow window(options);
      auto *map = window.findChild<MapWidget *>();
      const auto now = QDateTime::currentSecsSinceEpoch();
      window.stationModel()->applyPacket("REMOTE>APRS:!4903.50N/07201.75W>test", 'I', now - 120);
      window.stationModel()->applyPacket("PLANE>APRS:!4903.50N/07201.75W^test", 'I', now - 600);
      QVERIFY(map->stationVisible("REMOTE"));
      QVERIFY(map->stationVisible("PLANE"));
      auto *action = window.findChild<QAction *>("PULDNCF003");
      QVERIFY(action && action->property("implemented").toBool());
      window.findChild<BeaconControl *>()->open();
      action->trigger();
      auto *dialog = window.findChild<QDialog *>("configureTiming");
      QVERIFY(dialog && dialog->isVisible());
      dialog->findChild<QSpinBox *>("timingClear")->setValue(60);
      dialog->findChild<QSpinBox *>("timingAircraft")->setValue(100);
      dialog->findChild<QSpinBox *>("timingDeadReckoning")->setValue(120);
      dialog->findChild<QSpinBox *>("timingBeacon")->setValue(600);
      accept(dialog);
      QVERIFY(!map->stationVisible("REMOTE"));
      auto selections = map->selections(); selections.old_data = 1; map->setSelections(selections);
      QVERIFY(map->stationVisible("REMOTE"));
      QVERIFY(!map->stationVisible("PLANE"));
      QCOMPARE(window.stationModel()->stations().size(), 2);
      QCOMPARE(map->timing().deadReckoningSeconds, 120);
      QCOMPARE(window.findChild<BeaconControl *>()->intervalSeconds(), 600);
      QCOMPARE(window.findChild<QSpinBox *>("beaconInterval")->value(), 600);
      QVERIFY(window.findChild<BeaconControl *>()->positionDisabled());
      QVERIFY(!QSettings().value("beacon/periodic", false).toBool());
    }
    MainWindow restored(options);
    auto *map = restored.findChild<MapWidget *>();
    QCOMPARE(map->timing().clearSeconds, 60);
    QCOMPARE(map->timing().aircraftSeconds, 100);
    QCOMPARE(map->timing().deadReckoningSeconds, 120);
    QCOMPARE(restored.findChild<BeaconControl *>()->intervalSeconds(), 600);
  }
  void deadReckoningTimeoutChangesRenderedMap() {
    ApplicationOptions options;
    options.center = {49 + 3.5 / 60, -(72 + 1.75 / 60)};
    options.zoom = 12;
    MainWindow window(options);
    auto *map = window.findChild<MapWidget *>();
    map->setMapsEnabled(false);
    auto display = map->displayOptions();
    display.dr_data = 1; display.dr_course = 1; display.dr_arc = 0; display.dr_symbol = 0;
    display.last_heard = 0;
    map->setDisplayOptions(display);
    const auto now = QDateTime::currentSecsSinceEpoch();
    window.stationModel()->applyPacket("REMOTE>APRS:!4903.50N/07201.75W>090/010test", 'I', now - 300);
    QVERIFY(window.stationModel()->record("REMOTE")->heading_valid);
    window.show();
    const auto predicted = map->grab().toImage();
    window.findChild<QAction *>("PULDNCF003")->trigger();
    auto *dialog = window.findChild<QDialog *>("configureTiming");
    dialog->findChild<QSpinBox *>("timingDeadReckoning")->setValue(120);
    accept(dialog);
    QVERIFY(predicted != map->grab().toImage());
    QVERIFY(map->stationVisible("REMOTE"));
  }
  void removalAndTrailTimingApplyAndRestore() {
    ApplicationOptions options;
    options.config.callsign = "LOCAL";
    Station local; local.callsign = "LOCAL";
    options.config.localStation = local;
    {
      MainWindow window(options);
      auto *stations = window.stationModel();
      const auto now = QDateTime::currentSecsSinceEpoch();
      stations->applyPacket("REMOTE>APRS:!4903.50N/07201.75W>old", 'T', now - 120);
      stations->applyPacket("OLDSTATUS>APRS:>No position", 'I', now - 120);
      stations->applyPacket("LOCAL>APRS:;OWNED    *101200z4903.50N/07201.75W>mine", 'T', now - 120);
      stations->applyPacket("MOVING>APRS:!4903.50N/07201.75W>090/010", 'T', now - 120);
      stations->applyPacket("MOVING>APRS:!4903.51N/07201.75W>090/010", 'T', now);
      QVERIFY(stations->record("MOVING")->oldest_trackpoint);
      auto *navigator = window.findChild<StationNavigator *>();
      QVERIFY(navigator->locate("REMOTE", true));
      auto *map = window.findChild<MapWidget *>();
      auto selections = map->selections(); selections.old_data = 1; map->setSelections(selections);
      window.findChild<QAction *>("PULDNCF003")->trigger();
      auto *dialog = window.findChild<QDialog *>("configureTiming");
      dialog->findChild<QSpinBox *>("timingClear")->setValue(60);
      dialog->findChild<QSpinBox *>("timingRemove")->setValue(90);
      dialog->findChild<QSpinBox *>("timingTrailSegmentMinutes")->setValue(1);
      dialog->findChild<QSpinBox *>("timingTrailSegmentDegrees")->setValue(2);
      accept(dialog);
      QTRY_VERIFY(!stations->record("REMOTE"));
      QVERIFY(!stations->record("OLDSTATUS"));
      QVERIFY(stations->record("LOCAL"));
      QVERIFY(stations->record("OWNED"));
      QVERIFY(navigator->followedName().isEmpty());
      QCOMPARE(qint64(stations->record("MOVING")->oldest_trackpoint->sec), now);
      QCOMPARE(stations->database()->segment_minutes, 1);
      QCOMPARE(stations->database()->segment_distance, 2);
      // Future receptions use the configured gap limit in the C trail builder.
      stations->applyPacket("MOVING>APRS:!4903.52N/07201.75W>090/010", 'T', now + 61);
      QVERIFY(stations->record("MOVING")->newest_trackpoint->flag & TR_NEWTRK);
      // Scheduled cleanup also runs while the map window is hidden.
      auto *timer = stations->findChild<QTimer *>("stationMaintenanceTimer");
      QVERIFY(timer && timer->isActive());
      QCOMPARE(timer->interval(), 60000);
      stations->applyPacket("LATE>APRS:>Old replay", 'F', now - 120);
      QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
      QTRY_VERIFY(!stations->record("LATE"));
    }
    MainWindow restored(options);
    const auto timing = restored.findChild<MapWidget *>()->timing();
    QCOMPARE(timing.removeSeconds, 90);
    QCOMPARE(timing.trailSegmentMinutes, 1);
    QCOMPARE(timing.trailSegmentDegrees, 2);
    QCOMPARE(restored.stationModel()->database()->trail_seconds, time_t(60));
  }
  void trailLifetimeFollowsClearTime() {
    StationModel stations;
    TimingSettings timing;
    timing.clearSeconds = 7200;
    timing.removeSeconds = 86400;
    stations.setTiming(timing);
    const auto now = QDateTime::currentSecsSinceEpoch();
    stations.applyPacket("MOBILE>APRS:!4903.50N/07201.75W>090/010", 'T', now - 4000);
    stations.applyPacket("MOBILE>APRS:!4903.51N/07201.75W>090/010", 'T', now);
    QCOMPARE(qint64(stations.record("MOBILE")->oldest_trackpoint->sec), now - 4000);
    timing.clearSeconds = 60;
    stations.setTiming(timing);
    QTRY_COMPARE(qint64(stations.record("MOBILE")->oldest_trackpoint->sec), now);
    QVERIFY(stations.record("MOBILE")->oldest_trackpoint == stations.record("MOBILE")->newest_trackpoint);
  }
  void maintenanceContinuesAcrossBatchesAndReception() {
    StationModel stations;
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (int i = 0; i < 130; ++i)
      stations.applyPacket(QString("S%1>APRS:>Old status").arg(i, 4, 10, QChar('0')).toLatin1(), 'I', now - 86400);
    bool receivedDuringCleanup = false;
    connect(&stations, &StationModel::stationsChanged, &stations, [&] {
      if (receivedDuringCleanup) return;
      receivedDuringCleanup = true;
      stations.applyPacket("A_NEW>APRS:>Just received", 'T', now);
      stations.remove("S0064");
    });
    auto *timer = stations.findChild<QTimer *>("stationMaintenanceTimer");
    QVERIFY(timer && QMetaObject::invokeMethod(timer, "timeout"));
    QTRY_COMPARE(stations.database()->count, 1);
    QVERIFY(receivedDuringCleanup);
    QVERIFY(stations.record("A_NEW"));
  }
  void snapshotReplacementPreservesTrailSettingsAndOwnership() {
    StationModel stations;
    stations.setOwnCallsign("LOCAL");
    TimingSettings timing;
    timing.clearSeconds = 7200; timing.trailSegmentMinutes = 5; timing.trailSegmentDegrees = 3;
    stations.setTiming(timing);
    QFile snapshot(settings_.filePath("stations.json"));
    QVERIFY(snapshot.open(QIODevice::WriteOnly));
    snapshot.write(R"([{"callsign":"Owned","latitude":49,"longitude":-72,"symbol":"/>","kind":"object","source":"LOCAL"}])");
    snapshot.close();
    QVERIFY(stations.loadFile(snapshot.fileName()));
    QCOMPARE(stations.database()->trail_seconds, time_t(7200));
    QCOMPARE(stations.database()->segment_minutes, 5);
    QCOMPARE(stations.database()->segment_distance, 3);
    QVERIFY(stations.record("Owned")->flag & ST_MYOBJITEM);
  }
  void legacyTimingAndInvalidSettings() {
    QFile file(settings_.filePath("legacy.cnf"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("STATION_CALLSIGN:LOCAL\nDEFAULT_STATION_CLEAR:43200\nDEFAULT_AIRCRAFT_CLEAR:120\nDEAD_RECKONING_TIMEOUT:900\nPOSIT_RATE:1800\nTRAFFIC_UTF8:0\nDEFAULT_STATION_REMOVE:86400\nTRAIL_SEGMENT_TIME:45\nTRAIL_SEGMENT_DISTANCE:3\n");
    file.close();
    ApplicationOptions options;
    QString error;
    QVERIFY2(readXastirConfig(file.fileName(), options.config, &error), qPrintable(error));
    QSettings().setValue("timing/clearSeconds", "invalid");
    QSettings().setValue("timing/aircraftSeconds", -1);
    QSettings().setValue("beacon/interval", "invalid");
    QSettings().setValue("timing/removeSeconds", 1209601);
    QSettings().setValue("timing/trailSegmentMinutes", "invalid");
    QSettings().setValue("timing/trailSegmentDegrees", -1);
    MainWindow window(options);
    auto *map = window.findChild<MapWidget *>();
    QCOMPARE(map->timing().clearSeconds, 43200);
    QCOMPARE(map->timing().aircraftSeconds, 120);
    QCOMPARE(map->timing().deadReckoningSeconds, 900);
    QCOMPARE(map->timing().removeSeconds, 86400);
    QCOMPARE(map->timing().trailSegmentMinutes, 45);
    QCOMPARE(map->timing().trailSegmentDegrees, 3);
    QCOMPARE(window.findChild<BeaconControl *>()->intervalSeconds(), 1800);
    QVERIFY(!window.findChild<PacketMonitor *>()->trafficUtf8());
  }
  void trafficDecodingPreservesCaptureBytes() {
    QMainWindow window;
    PacketMonitor monitor(&window);
    const QByteArray packet = QByteArray("REMOTE>APRS:>caf") + QByteArray::fromHex("c3a9");
    const auto path = settings_.filePath("capture.jsonl");
    QVERIFY(monitor.startCapture(path));
    monitor.receive(packet, 3);
    QCOMPARE(monitor.model()->item(0, 2)->text(), QString::fromUtf8(packet));
    monitor.setTrafficUtf8(false);
    monitor.receive(packet, 3);
    QCOMPARE(monitor.model()->item(1, 2)->text(), QString::fromLatin1(packet));
    monitor.stopCapture();
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    for (int i = 0; i < 2; ++i) {
      const auto object = QJsonDocument::fromJson(file.readLine()).object();
      QCOMPARE(QByteArray::fromBase64(object["packet"].toString().toLatin1()), packet);
    }
  }
};
QTEST_MAIN(ConfigurationTest)
#include "test_configuration.moc"
