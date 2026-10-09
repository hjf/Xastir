// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "aprs_symbols.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class TileSource;
class StationModel;
class QPainter;

class MapWidget : public QWidget
{
  Q_OBJECT

public:
  MapWidget(TileSource *tiles, map::Coordinate center, int zoom, QWidget *parent = nullptr);
  map::Coordinate centerCoordinate() const;
  int zoomLevel() const { return zoom_; }
  void setView(map::Coordinate center, int zoom);
  void setStationModel(StationModel *stations);
  QPointF coordinateToScreen(map::Coordinate coordinate) const;

signals:
  void viewChanged();

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void refreshTiles();
  void updateView();
  void zoomAt(int zoom, QPointF position);
  QPointF viewportOrigin() const;
  void drawStations(QPainter &painter);

  TileSource *tiles_;
  QLabel *attribution_;
  QPointF center_;
  int zoom_;
  QTimer tileTimer_;
  bool dragging_ = false;
  QPointF dragPosition_;
  int wheelDelta_ = 0;
  QPointer<StationModel> stations_;
  AprsSymbols symbols_;
};
