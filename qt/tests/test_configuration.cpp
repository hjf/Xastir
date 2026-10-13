// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "beacon_control.h"
#include "message_service.h"
#include "packet_monitor.h"
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
    timing->reject();
    QCOMPARE(window.findChild<BeaconControl *>()->intervalSeconds(), 300);
    QVERIFY(!QSettings().contains("beacon/interval"));
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
  void legacyTimingAndInvalidSettings() {
    QFile file(settings_.filePath("legacy.cnf"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("STATION_CALLSIGN:LOCAL\nDEFAULT_STATION_CLEAR:43200\nDEFAULT_AIRCRAFT_CLEAR:120\nDEAD_RECKONING_TIMEOUT:900\nPOSIT_RATE:1800\nTRAFFIC_UTF8:0\n");
    file.close();
    ApplicationOptions options;
    QString error;
    QVERIFY2(readXastirConfig(file.fileName(), options.config, &error), qPrintable(error));
    QSettings().setValue("timing/clearSeconds", "invalid");
    QSettings().setValue("timing/aircraftSeconds", -1);
    QSettings().setValue("beacon/interval", "invalid");
    MainWindow window(options);
    auto *map = window.findChild<MapWidget *>();
    QCOMPARE(map->timing().clearSeconds, 43200);
    QCOMPARE(map->timing().aircraftSeconds, 120);
    QCOMPARE(map->timing().deadReckoningSeconds, 900);
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
