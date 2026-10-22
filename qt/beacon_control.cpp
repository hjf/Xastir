// SPDX-License-Identifier: GPL-2.0-or-later
#include "beacon_control.h"
#include "radio_interface.h"
#include "aprs_position.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
BeaconControl::BeaconControl(QMainWindow *w, XastirConfig config, Interfaces interfaces)
    : QObject(w), window_(w), config_(std::move(config)), interfaces_(std::move(interfaces))
{
  QSettings settings;
  allDisabled_ = settings.value("transmit/allDisabled", config_.transmitDisabled).toBool();
  positionDisabled_ =
      settings.value("transmit/positionDisabled", config_.positionTransmitDisabled).toBool();
  periodic_ = settings.value("beacon/periodic", false).toBool();
  bool validInterval = false;
  const int savedInterval = settings.value("beacon/interval", config_.timing.beaconSeconds).toInt(&validInterval);
  interval_ = validInterval && savedInterval >= 60 && savedInterval <= 86400
    ? savedInterval : config_.timing.beaconSeconds;
  device_ = settings.value("beacon/device", -1).toInt();
  auto bind = [this](const char *id, auto handler)
  {
    auto *a = window_->findChild<QAction *>(id);
    if (!a)
      return;
    disconnect(a, nullptr, window_, nullptr);
    a->setProperty("implemented", true);
    connect(a, &QAction::triggered, this, handler);
  };
  bind("PULDNTNT01", [this] { open(); });
  if (auto *a = w->findChild<QAction *>("PULDNTNT03"))
    a->setChecked(allDisabled_);
  if (auto *a = w->findChild<QAction *>("PULDNTNT05"))
    a->setChecked(positionDisabled_);
  bind("PULDNTNT03",
       [this](bool disabled)
       {
         allDisabled_ = disabled;
         QSettings().setValue("transmit/allDisabled", disabled);
         emit transmitDisabledChanged(disabled);
         schedule();
         refresh();
       });
  bind("PULDNTNT05",
       [this](bool disabled)
       {
         positionDisabled_ = disabled;
         QSettings().setValue("transmit/positionDisabled", disabled);
         schedule();
         refresh();
       });
  connect(&timer_, &QTimer::timeout, this,
          [this]
          {
            QString error;
            if (!sendNow(device_, &error))
              emit statusChanged(tr("Position beacon: %1").arg(error));
          });
  schedule();
}
void BeaconControl::setIntervalSeconds(int seconds)
{
  interval_ = qBound(60, seconds, 86400);
  QSettings().setValue("beacon/interval", interval_);
  schedule();
  emit intervalChanged(interval_);
}
void BeaconControl::configure(const XastirConfig &config)
{
  config_ = config;
  schedule();
  refresh();
}
QByteArray BeaconControl::information(QString *error) const
{
  if (!config_.localStation)
  {
    if (error)
      *error = tr("Configure your station callsign and position in Interface Control.");
    return {};
  }
  const auto &s = *config_.localStation;
  char buffer[256];
  const auto comment = s.comment.toLatin1();
  if (s.comment != QString::fromLatin1(comment) ||
      !xastir_build_position_beacon(s.position.latitude, s.position.longitude,
                                    s.symbolTable.toLatin1(), s.symbolCode.toLatin1(),
                                    comment.constData(), buffer, sizeof(buffer)))
  {
    if (error)
      *error = tr("The configured position, symbol, or comment is invalid for APRS.");
    return {};
  }
  return QByteArray(buffer);
}
bool BeaconControl::sendNow(int device, QString *error)
{
  auto reject = [error](const QString &reason)
  {
    if (error)
      *error = reason;
    return false;
  };
  if (allDisabled_)
    return reject(tr("All transmission is disabled."));
  if (positionDisabled_)
    return reject(tr(
        "Position transmission is disabled. Uncheck Interface → Disable Transmit: My Position."));
  const auto info = information(error);
  if (info.isEmpty())
    return false;
  for (auto *client : interfaces_())
    if (client->deviceIndex() == device)
    {
      if (client->callsign() != config_.callsign)
        return reject(tr("Station callsign does not match the selected radio interface."));
      if (!client->transmit(info, QString(), error))
        return false;
      if (periodic_)
        timer_.start(interval_ * 1000);
      emit statusChanged(tr("Position beacon queued on device %1.").arg(device));
      return true;
    }
  return reject(tr("Select an available radio interface."));
}
void BeaconControl::schedule()
{
  timer_.stop();
  if (periodic_ && !allDisabled_ && !positionDisabled_ && config_.localStation)
    timer_.start(interval_ * 1000);
}
void BeaconControl::refresh()
{
  if (!dialog_)
    return;
  QString error;
  const auto info = information(&error);
  preview_->setText(info.isEmpty() ? error : QString::fromLatin1(info));
  const int selected =
      devices_->currentData().isValid() ? devices_->currentData().toInt() : device_;
  devices_->clear();
  for (auto *client : interfaces_())
  {
    devices_->addItem(client->description(), client->deviceIndex());
  }
  const int index = devices_->findData(selected);
  if (index >= 0)
    devices_->setCurrentIndex(index);
  status_->setText(
      allDisabled_ ? tr("All transmission disabled")
      : positionDisabled_
          ? tr("Position transmission disabled")
          : tr("Ready; selected interface must be connected and have Enable TX checked."));
}
void BeaconControl::open()
{
  if (!dialog_)
  {
    auto *d = new QDialog(window_);
    dialog_ = d;
    d->setWindowTitle(tr("Position Beacon"));
    d->setObjectName("positionBeacon");
    d->resize(600, 280);
    auto *layout = new QVBoxLayout(d);
    preview_ = new QLabel(d);
    preview_->setObjectName("beaconPreview");
    preview_->setTextFormat(Qt::PlainText);
    preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_->setWordWrap(true);
    layout->addWidget(preview_);
    layout->addWidget(new QLabel(tr("Fixed, uncompressed APRS report. Comments follow the legacy "
                                    "63-character packet limit."),
                                 d));
    devices_ = new QComboBox(d);
    devices_->setObjectName("beaconDevice");
    layout->addWidget(devices_);
    auto *periodic = new QCheckBox(tr("Send periodically"), d);
    periodic->setChecked(periodic_);
    layout->addWidget(periodic);
    auto *interval = new QSpinBox(d);
    interval->setObjectName("beaconInterval");
    connect(this, &BeaconControl::intervalChanged, interval, &QSpinBox::setValue);
    interval->setRange(60, 86400);
    interval->setSuffix(tr(" seconds"));
    interval->setValue(interval_);
    layout->addWidget(interval);
    status_ = new QLabel(d);
    status_->setObjectName("beaconStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *apply = new QPushButton(tr("Apply schedule"), d);
    layout->addWidget(apply);
    connect(apply, &QPushButton::clicked, this,
            [this, periodic, interval]
            {
              if (periodic->isChecked() && devices_->currentIndex() < 0)
              {
                status_->setText(tr("Configure a radio interface first."));
                return;
              }
              periodic_ = periodic->isChecked();
              setIntervalSeconds(interval->value());
              device_ = devices_->currentData().toInt();
              QSettings settings;
              settings.setValue("beacon/periodic", periodic_);
              settings.setValue("beacon/interval", interval_);
              settings.setValue("beacon/device", device_);
              schedule();
              status_->setText(tr("Schedule saved. Periodic sending waits one full interval and "
                                  "respects all transmit controls."));
            });
    auto *send = new QPushButton(tr("Send now"), d);
    send->setObjectName("sendBeacon");
    layout->addWidget(send);
    connect(
        send, &QPushButton::clicked, this,
        [this]
        {
          QString error;
          if (!sendNow(devices_->currentIndex() < 0 ? -1 : devices_->currentData().toInt(), &error))
            status_->setText(error);
          else
            status_->setText(tr("Beacon queued for radio transmission."));
        });
    auto *close = new QPushButton(tr("Close"), d);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, d, &QDialog::close);
  }
  refresh();
  dialog_->show();
  dialog_->raise();
  dialog_->activateWindow();
}
