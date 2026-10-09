// SPDX-License-Identifier: GPL-2.0-or-later
#include "main_window.h"
#include "map_widget.h"
#include "station_model.h"

#include <QAction>
#include <QLabel>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

MainWindow::MainWindow(const ApplicationOptions &options, QWidget *parent)
  : QMainWindow(parent)
{
  setWindowTitle(tr("Xastir Qt"));
  resize(1100, 760);
  auto *tiles = new TileSource(options.tiles, this);
  auto *mapWidget = new MapWidget(tiles, options.center, options.zoom, this);
  mapWidget_ = mapWidget;
  stations_ = new StationModel(mapWidget);
  mapWidget->setStationModel(stations_);
  tiles->setParent(mapWidget);
  setCentralWidget(mapWidget);

  auto *fileMenu = menuBar()->addMenu(tr("&File"));
  auto *openAction = fileMenu->addAction(tr("&Open Stations…"));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, [this] {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Station Snapshot"), {},
                                                    tr("Station JSON (*.json)"));
    if (path.isEmpty())
      return;
    QString error;
    if (!loadStations(path, &error, true))
      QMessageBox::warning(this, tr("Cannot Load Stations"), error);
    else if (!stations_->stations().isEmpty() && mapWidget_->zoomLevel() < 12)
      mapWidget_->setView(mapWidget_->centerCoordinate(), 12);
  });
  fileMenu->addSeparator();
  auto *exitAction = fileMenu->addAction(tr("E&xit"));
  exitAction->setObjectName(QStringLiteral("actionExit"));
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, &QWidget::close);

  auto *position = new QLabel(this);
  auto *stationCount = new QLabel(this);
  statusBar()->addPermanentWidget(stationCount);
  auto updateCount = [this, stationCount] {
    stationCount->setText(tr("Stations: %1").arg(stations_->stations().size()));
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
}

bool MainWindow::loadStations(const QString &path, QString *error, bool centerOnStations)
{
  if (!stations_->loadFile(path, error))
    return false;
  if (centerOnStations && !stations_->stations().isEmpty())
    mapWidget_->setView(stations_->stations().first().position, mapWidget_->zoomLevel());
  return true;
}
