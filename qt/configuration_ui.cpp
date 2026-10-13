// SPDX-License-Identifier: GPL-2.0-or-later
#include "configuration_ui.h"
#include "beacon_control.h"
#include "map_widget.h"
#include "packet_monitor.h"
#include "snapshot_control.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>

ConfigurationUi::ConfigurationUi(QMainWindow *window, MapWidget *map, PacketMonitor *monitor,
                                 BeaconControl *beacons, SnapshotControl *snapshots, const XastirConfig &config)
  : QObject(window), window_(window), map_(map), monitor_(monitor), beacons_(beacons), snapshots_(snapshots)
{
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
  auto *form = new QFormLayout(dialog);
  auto spin = [dialog, form](const QString &label, const char *name, int minimum, int maximum, int value) {
    auto *box = new QSpinBox(dialog);
    box->setObjectName(name); box->setRange(minimum, maximum); box->setValue(value);
    box->setSuffix(tr(" seconds")); form->addRow(label, box); return box;
  };
  const auto value = map_->timing();
  auto *clear = spin(tr("Station clear time"), "timingClear", 1, 604800, value.clearSeconds);
  auto *aircraft = spin(tr("Aircraft clear time"), "timingAircraft", 0, 604800, value.aircraftSeconds);
  aircraft->setSpecialValueText(tr("Disabled"));
  auto *dr = spin(tr("Dead-reckoning timeout"), "timingDeadReckoning", 1, 2592000, value.deadReckoningSeconds);
  auto *beacon = spin(tr("Position beacon interval"), "timingBeacon", 60, 86400, beacons_->intervalSeconds());
  auto *snapshotInterval = new QSpinBox(dialog);
  snapshotInterval->setObjectName("timingSnapshotInterval");
  snapshotInterval->setRange(1, 30); snapshotInterval->setSuffix(tr(" minutes"));
  snapshotInterval->setValue(snapshots_->intervalMinutes());
  form->addRow(tr("PNG snapshot interval"), snapshotInterval);
  auto *directory = new QLineEdit(snapshots_->directory(), dialog);
  directory->setObjectName("timingSnapshotDirectory");
  auto *browse = new QPushButton(tr("Browse…"), dialog);
  auto *directoryRow = new QHBoxLayout;
  directoryRow->addWidget(directory); directoryRow->addWidget(browse);
  form->addRow(tr("PNG snapshot directory"), directoryRow);
  connect(browse, &QPushButton::clicked, dialog, [dialog, directory] {
    const auto path = QFileDialog::getExistingDirectory(dialog, tr("PNG snapshot directory"), directory->text());
    if (!path.isEmpty()) directory->setText(path);
  });
  auto *note = new QLabel(tr("Clear times affect map visibility, not stored history. Display Old Data bypasses station clearing. Aircraft use the legacy five-times-clear cutoff.\n\nChanging the beacon interval waits one full interval; periodic sending and TX must be enabled separately in Position Beacon and Interface Control.\n\nGhosting, station removal, GPS, object transmission, serial pacing, and trail timing are not implemented here yet.\n\nEnable PNG Snapshots in the File menu to refresh snapshot.png in the selected directory."), dialog);
  note->setWordWrap(true); form->addRow(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, clear, aircraft, dr, beacon, snapshotInterval, directory] {
    if (directory->text().trimmed().isEmpty()) {
      QMessageBox::warning(dialog, tr("PNG snapshots"), tr("Choose a snapshot directory."));
      return;
    }
    QSettings settings;
    settings.setValue("timing/clearSeconds", clear->value());
    settings.setValue("timing/aircraftSeconds", aircraft->value());
    settings.setValue("timing/deadReckoningSeconds", dr->value());
    settings.setValue("beacon/interval", beacon->value());
    settings.setValue("snapshots/directory", directory->text().trimmed());
    settings.setValue("snapshots/intervalMinutes", snapshotInterval->value());
    if (!save()) return;
    auto value = map_->timing();
    value.clearSeconds = clear->value(); value.aircraftSeconds = aircraft->value();
    value.deadReckoningSeconds = dr->value(); value.beaconSeconds = beacon->value();
    map_->setTiming(value);
    beacons_->setIntervalSeconds(beacon->value());
    snapshots_->configure(directory->text(), snapshotInterval->value());
    window_->statusBar()->showMessage(tr("Timing saved and applied."), 5000);
    dialog->accept();
  });
  dialog->resize(540, 360);
  dialog->show();
}
