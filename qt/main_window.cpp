// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "station_model.h"
#include "agwpe_client.h"
#include "legacy_menus.h"
#include "message_service.h"
#include "message_ui.h"
#include <QTimer>

#include <QLabel>
#include <QStatusBar>

MainWindow::MainWindow(const ApplicationOptions &options, QWidget *parent)
  : QMainWindow(parent)
{
  setWindowTitle(options.config.callsign.isEmpty() ? tr("Xastir Qt")
    : tr("Xastir Qt — %1").arg(options.config.callsign));
  resize(1100, 760);
  auto *tiles = new TileSource(options.tiles, this);
  auto *mapWidget = new MapWidget(tiles, options.center, options.zoom, this);
  mapWidget_ = mapWidget;
  stations_ = new StationModel(mapWidget);
  mapWidget->setStationModel(stations_);
  localStation_ = options.config.localStation;
  if (localStation_) stations_->upsert(*localStation_);
  tiles->setParent(mapWidget);
  setCentralWidget(mapWidget);

  createLegacyMenus(this);

  auto *position = new QLabel(this);
  auto *stationCount = new QLabel(this);
  statusBar()->addPermanentWidget(stationCount);
  auto updateCount = [this, stationCount] {
    int stationTotal = 0, objectTotal = 0;
    for (const auto &entry : stations_->stations()) {
      if (entry.kind == StationKind::Station) ++stationTotal;
      else ++objectTotal;
    }
    stationCount->setText(tr("Stations: %1  Objects/items: %2").arg(stationTotal).arg(objectTotal));
  };
  connect(stations_, &StationModel::stationsChanged, stationCount, updateCount);
  updateCount();
  statusBar()->addPermanentWidget(position);
  auto *status = statusBar();
  connect(tiles, &TileSource::statusChanged, status, [status](const QString &message) {
    status->showMessage(message);
  });
  auto updatePosition = [mapWidget, position] {
    const auto center = mapWidget->centerCoordinate();
    position->setText(tr("%1°, %2°   Zoom %3")
      .arg(center.latitude, 0, 'f', 4).arg(center.longitude, 0, 'f', 4).arg(mapWidget->zoomLevel()));
  };
  connect(mapWidget, &MapWidget::viewChanged, this, updatePosition);
  updatePosition();
  statusBar()->showMessage(tr("Loading map…"));
  messages_ = new MessageService(options.config.callsign, this);
  QList<AgwpeClient *> clients;
  if (!options.config.agwpe.isEmpty()) {
    for (const auto &settings : options.config.agwpe) {
      auto *client = new AgwpeClient(settings, stations_, this);
      clients.append(client);
      connect(client, &AgwpeClient::packetReceived, messages_, [this, settings](const QByteArray &packet) {
        messages_->receivePacket(packet, settings.deviceIndex);
      });
      auto *label = new QLabel(tr("AGWPE stopped"), this);
      statusBar()->addWidget(label);
      connect(client, &AgwpeClient::statusChanged, label, &QLabel::setText);
      if (settings.connectOnStartup) QTimer::singleShot(0, client, [client] { client->start(); });
    }
  }
  QList<QPointer<AgwpeClient>> routes;
  for (auto *client : clients) routes.append(client);
  messages_->setTransport([routes](const QByteArray &info, int device, const QString &path, QString *error) {
    for (const auto &client : routes)
      if (client && client->settings().deviceIndex == device) return client->transmit(info, path, error);
    if (error) *error = QObject::tr("AGWPE interface is no longer available.");
    return false;
  });
  new MessageUi(this, messages_, clients, options.config.autoReplyText);
}

bool MainWindow::loadStations(const QString &path, QString *error, bool centerOnStations)
{
  if (!stations_->loadFile(path, error))
    return false;
  if (localStation_) stations_->upsert(*localStation_);
  if (centerOnStations && !stations_->stations().isEmpty())
    mapWidget_->setView(stations_->stations().first().position, mapWidget_->zoomLevel());
  return true;
}
