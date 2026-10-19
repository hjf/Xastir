// SPDX-License-Identifier: GPL-2.0-or-later
#include "configuration_ui.h"
#include "beacon_control.h"
#include "map_widget.h"
#include "packet_monitor.h"
#include "snapshot_control.h"
#include "traffic_log.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

ConfigurationUi::ConfigurationUi(QMainWindow *window, MapWidget *map, PacketMonitor *monitor,
                                 BeaconControl *beacons, SnapshotControl *snapshots, TrafficLogControl *logs, const XastirConfig &config)
  : QObject(window), window_(window), map_(map), monitor_(monitor), beacons_(beacons), snapshots_(snapshots), logs_(logs)
{
  auto *preferences = map_->preferences();
  preferences->restore(config.presentation);
  for (const char *id : {"PULDNCF002", "PULDNUT001", "PULDNDB001"}) {
    if (auto *action = window_->findChild<QAction *>(id)) {
      disconnect(action, nullptr, window_, nullptr); action->setProperty("implemented", true);
      connect(action, &QAction::triggered, this, [this, id](bool checked) {
        if (QByteArray(id) == "PULDNCF002") { coordinates(); return; }
        auto value = map_->preferences()->value();
        if (QByteArray(id) == "PULDNUT001") value.englishUnits = checked;
        else value.distanceBearing = checked;
        applyPresentation(value);
      });
    }
  }
  auto sync = [this, preferences] {
    if (auto *action = window_->findChild<QAction *>("PULDNUT001")) action->setChecked(preferences->value().englishUnits);
    if (auto *action = window_->findChild<QAction *>("PULDNDB001")) action->setChecked(preferences->value().distanceBearing);
  };
  connect(preferences, &DisplayPreferences::changed, this, sync); sync();
  QSettings settings;
  auto bounded = [&settings](const char *key, int fallback, int minimum, int maximum) {
    bool ok = false;
    const int value = settings.value(key, fallback).toInt(&ok);
    return ok && value >= minimum && value <= maximum ? value : fallback;
  };
  auto value = config.timing;
  value.clearSeconds = bounded("timing/clearSeconds", value.clearSeconds, 1, 604800);
  value.aircraftSeconds = bounded("timing/aircraftSeconds", value.aircraftSeconds, 0, 604800);
  value.deadReckoningSeconds = bounded("timing/deadReckoningSeconds", value.deadReckoningSeconds, 1, 2592000);
  value.removeSeconds = bounded("timing/removeSeconds", value.removeSeconds, 1, 1209600);
  value.trailSegmentMinutes = bounded("timing/trailSegmentMinutes", value.trailSegmentMinutes, 0, 720);
  value.trailSegmentDegrees = bounded("timing/trailSegmentDegrees", value.trailSegmentDegrees, 0, 45);
  value.beaconSeconds = beacons_->intervalSeconds();
  map_->setTiming(value);
  connect(beacons_, &BeaconControl::intervalChanged, this, [this](int seconds) {
    auto value = map_->timing(); value.beaconSeconds = seconds; map_->setTiming(value);
  });
  monitor_->setTrafficUtf8(settings.value("defaults/trafficUtf8", config.trafficUtf8).toBool());
  for (const char *id : {"PULDNCF001", "PULDNCF003"}) {
    if (auto *action = window->findChild<QAction *>(id)) {
      disconnect(action, nullptr, window, nullptr);
      action->setProperty("implemented", true);
      connect(action, &QAction::triggered, this,
              [this, timing = QByteArray(id) == "PULDNCF003"] { if (timing) this->timing(); else defaults(); });
    }
  }
}

void ConfigurationUi::applyPresentation(PresentationSettings value)
{
  if (map_->preferences()->save(value)) return;
  window_->findChild<QAction *>("PULDNUT001")->setChecked(map_->preferences()->value().englishUnits);
  window_->findChild<QAction *>("PULDNDB001")->setChecked(map_->preferences()->value().distanceBearing);
  QMessageBox::warning(window_, tr("Display preferences"), tr("Could not save display preferences."));
}

void ConfigurationUi::coordinates()
{
  if (coordinates_) { coordinates_->show(); coordinates_->raise(); return; }
  auto *dialog = new QDialog(window_); coordinates_ = dialog;
  dialog->setObjectName("configureCoordinates"); dialog->setWindowTitle(tr("Coordinate System"));
  dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowModality(Qt::WindowModal);
  auto *form = new QFormLayout(dialog);
  auto *choice = new QComboBox(dialog); choice->setObjectName("coordinateSystem");
  choice->addItems({tr("Decimal degrees"), tr("Degrees and decimal minutes"), tr("Degrees, minutes and seconds")});
  choice->setCurrentIndex(map_->preferences()->value().coordinateSystem); form->addRow(tr("Display format"), choice);
  auto *preview = new QLabel(dialog); preview->setObjectName("coordinatePreview"); form->addRow(preview);
  auto showPreview = [this, choice, preview] {
    DisplayPreferences temporary;
    auto value = map_->preferences()->value(); value.coordinateSystem = choice->currentIndex(); temporary.setValue(value);
    preview->setText(temporary.position(map_->centerCoordinate()));
  };
  connect(choice, &QComboBox::currentIndexChanged, dialog, showPreview); showPreview();
  auto *note = new QLabel(tr("Applies to displayed positions. Coordinate entry remains in decimal degrees. UTM and MGRS are not implemented yet."), dialog);
  note->setWordWrap(true); form->addRow(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog); form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, [this, choice, dialog] {
    auto value = map_->preferences()->value(); value.coordinateSystem = choice->currentIndex();
    if (!map_->preferences()->save(value)) { QMessageBox::warning(dialog, tr("Coordinate System"), tr("Could not save display preferences.")); return; }
    dialog->accept();
  });
  dialog->resize(440, 220); dialog->show();
}

bool ConfigurationUi::save()
{
  QSettings settings;
  settings.sync();
  if (settings.status() == QSettings::NoError) return true;
  QMessageBox::warning(window_, tr("Configuration"), tr("Could not save configuration."));
  return false;
}

void ConfigurationUi::defaults()
{
  if (defaults_) { defaults_->show(); defaults_->raise(); return; }
  auto *dialog = new QDialog(window_);
  defaults_ = dialog;
  dialog->setObjectName("configureDefaults");
  dialog->setWindowTitle(tr("Configure Defaults"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  auto *form = new QFormLayout(dialog);
  auto checkbox = [dialog, form](const QString &text, const char *name, bool checked) {
    auto *box = new QCheckBox(text, dialog);
    box->setObjectName(name); box->setChecked(checked); form->addRow(box); return box;
  };
  auto *all = checkbox(tr("Disable all transmission"), "defaultsAllDisabled", beacons_->transmitDisabled());
  auto *position = checkbox(tr("Disable position transmission"), "defaultsPositionDisabled", beacons_->positionDisabled());
  auto *utf8 = checkbox(tr("Decode incoming traffic as UTF-8"), "defaultsTrafficUtf8", monitor_->trafficUtf8());
  auto *note = new QLabel(tr("Transmit controls also appear in the Interface menu. Each interface must have TX enabled; APRS-IS messages also require a verified login. Position beacons use AGWPE.\n\nText decoding affects new rows in Incoming Data; captured packet bytes are preserved.\n\nOther legacy defaults, including station transmission modes, IGate, bulletin popups, and alternate networks, are not implemented yet."), dialog);
  note->setWordWrap(true); form->addRow(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, all, position, utf8] {
    QSettings settings;
    settings.setValue("transmit/allDisabled", all->isChecked());
    settings.setValue("transmit/positionDisabled", position->isChecked());
    settings.setValue("defaults/trafficUtf8", utf8->isChecked());
    if (!save()) return;
    for (const auto &entry : {qMakePair("PULDNTNT03", all->isChecked()), qMakePair("PULDNTNT05", position->isChecked())}) {
      auto *action = window_->findChild<QAction *>(entry.first);
      if (action && action->isChecked() != entry.second) action->trigger();
    }
    monitor_->setTrafficUtf8(utf8->isChecked());
    window_->statusBar()->showMessage(tr("Defaults saved and applied."), 5000);
    dialog->accept();
  });
  dialog->resize(520, 300);
  dialog->show();
}

void ConfigurationUi::timing()
{
  if (timing_) { timing_->show(); timing_->raise(); return; }
  auto *dialog = new QDialog(window_);
  timing_ = dialog;
  dialog->setObjectName("configureTiming");
  dialog->setWindowTitle(tr("Configure Timing"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(dialog);
  auto *scroll = new QScrollArea(dialog);
  scroll->setWidgetResizable(true);
  auto *contents = new QWidget(scroll);
  auto *form = new QFormLayout(contents);
  scroll->setWidget(contents); layout->addWidget(scroll);
  auto spin = [dialog, form](const QString &label, const char *name, int minimum, int maximum, int value) {
    auto *box = new QSpinBox(dialog);
    box->setObjectName(name); box->setRange(minimum, maximum); box->setValue(value);
    box->setSuffix(tr(" seconds")); form->addRow(label, box); return box;
  };
  const auto value = map_->timing();
  auto *clear = spin(tr("Station clear time"), "timingClear", 1, 604800, value.clearSeconds);
  auto *remove = spin(tr("Station removal time"), "timingRemove", 1, 1209600, value.removeSeconds);
  auto *aircraft = spin(tr("Aircraft clear time"), "timingAircraft", 0, 604800, value.aircraftSeconds);
  aircraft->setSpecialValueText(tr("Disabled"));
  auto *dr = spin(tr("Dead-reckoning timeout"), "timingDeadReckoning", 1, 2592000, value.deadReckoningSeconds);
  auto *beacon = spin(tr("Position beacon interval"), "timingBeacon", 60, 86400, beacons_->intervalSeconds());
  auto *segmentTime = new QSpinBox(dialog);
  segmentTime->setObjectName("timingTrailSegmentMinutes");
  segmentTime->setRange(0, 720); segmentTime->setSuffix(tr(" minutes"));
  segmentTime->setValue(value.trailSegmentMinutes);
  form->addRow(tr("Maximum time between trail points"), segmentTime);
  auto *segmentDistance = new QSpinBox(dialog);
  segmentDistance->setObjectName("timingTrailSegmentDegrees");
  segmentDistance->setRange(0, 45); segmentDistance->setSuffix(tr(" degrees"));
  segmentDistance->setValue(value.trailSegmentDegrees);
  form->addRow(tr("Maximum trail latitude/longitude gap"), segmentDistance);
  auto *snapshotInterval = new QSpinBox(dialog);
  snapshotInterval->setObjectName("timingSnapshotInterval");
  snapshotInterval->setRange(1, 30); snapshotInterval->setSuffix(tr(" minutes"));
  snapshotInterval->setValue(snapshots_->intervalMinutes());
  form->addRow(tr("Snapshot interval"), snapshotInterval);
  auto *directory = new QLineEdit(snapshots_->directory(), dialog);
  directory->setObjectName("timingSnapshotDirectory");
  auto *browse = new QPushButton(tr("Browse…"), dialog);
  auto *directoryRow = new QHBoxLayout;
  directoryRow->addWidget(directory); directoryRow->addWidget(browse);
  form->addRow(tr("Snapshot directory"), directoryRow);
  connect(browse, &QPushButton::clicked, dialog, [dialog, directory] {
    const auto path = QFileDialog::getExistingDirectory(dialog, tr("Snapshot directory"), directory->text());
    if (!path.isEmpty()) directory->setText(path);
  });
  auto *logDirectory = new QLineEdit(logs_->directory(), dialog);
  logDirectory->setObjectName("timingLogDirectory");
  auto *logBrowse = new QPushButton(tr("Browse…"), dialog);
  auto *logRow = new QHBoxLayout; logRow->addWidget(logDirectory); logRow->addWidget(logBrowse);
  form->addRow(tr("Traffic log directory"), logRow);
  connect(logBrowse, &QPushButton::clicked, dialog, [dialog, logDirectory] {
    const auto path = QFileDialog::getExistingDirectory(dialog, tr("Traffic log directory"), logDirectory->text());
    if (!path.isEmpty()) logDirectory->setText(path);
  });
  auto *note = new QLabel(tr("Clear time hides stations and expires trail points. Display Old Data bypasses hiding, but not removal or trail expiry. Aircraft use the legacy five-times-clear cutoff.\n\nRemoval deletes received station state from memory; your station and owned objects/items remain. Cleanup runs every minute. Traffic history and messages are retained independently.\n\nTrail gap limits apply to new points. Zero permits only points with the same time or coordinate; it does not disable the limit.\n\nChanging the beacon interval waits one full interval; periodic sending and TX must be enabled separately in Position Beacon and Interface Control.\n\nGhosting, GPS and serial pacing are not implemented here yet. Object/item schedules are configured under Station → Manage Own Objects/Items. Enable PNG or KML Snapshots in the File menu to refresh snapshot.png or snapshot.kml. KML uses station data even when the map is hidden."), dialog);
  note->setWordWrap(true); form->addRow(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, clear, remove, aircraft, dr, beacon, segmentTime, segmentDistance, snapshotInterval, directory, logDirectory] {
    if (directory->text().trimmed().isEmpty() || logDirectory->text().trimmed().isEmpty()) {
      QMessageBox::warning(dialog, tr("Directories"), tr("Choose snapshot and traffic log directories."));
      return;
    }
    QSettings settings;
    settings.setValue("timing/clearSeconds", clear->value());
    settings.setValue("timing/removeSeconds", remove->value());
    settings.setValue("timing/trailSegmentMinutes", segmentTime->value());
    settings.setValue("timing/trailSegmentDegrees", segmentDistance->value());
    settings.setValue("timing/aircraftSeconds", aircraft->value());
    settings.setValue("timing/deadReckoningSeconds", dr->value());
    settings.setValue("beacon/interval", beacon->value());
    settings.setValue("snapshots/directory", directory->text().trimmed());
    settings.setValue("snapshots/intervalMinutes", snapshotInterval->value());
    settings.setValue("logging/directory", logDirectory->text().trimmed());
    if (!save()) return;
    auto value = map_->timing();
    value.clearSeconds = clear->value(); value.aircraftSeconds = aircraft->value();
    value.removeSeconds = remove->value();
    value.trailSegmentMinutes = segmentTime->value(); value.trailSegmentDegrees = segmentDistance->value();
    value.deadReckoningSeconds = dr->value(); value.beaconSeconds = beacon->value();
    map_->setTiming(value);
    beacons_->setIntervalSeconds(beacon->value());
    snapshots_->configure(directory->text(), snapshotInterval->value());
    logs_->configure(logDirectory->text());
    window_->statusBar()->showMessage(tr("Timing saved and applied."), 5000);
    dialog->accept();
  });
  dialog->resize(600, 640);
  dialog->show();
}
