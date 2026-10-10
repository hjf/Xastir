// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "aprs_symbols.h"
#include "station_view.h"
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
  const Selections &selections() const { return selections_; }
  const What_to_display &displayOptions() const { return display_; }
  void setSelections(const Selections &value) { selections_ = value; update(); }
  void setDisplayOptions(const What_to_display &value) { display_ = value; update(); }
  bool stationVisible(const QString &name) const;

signals:
  void viewChanged();
  void manuallyPanned();

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
  void drawStationOverlays(QPainter &painter, const DataRow &row);
  void drawRange(QPainter &painter, map::Coordinate center, double meters, double start = 0, double span = 360);

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
  Selections selections_{0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1};
  What_to_display display_{1,0,1,1,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,1,1,1,0,0};
};
