// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "aprs_symbols.h"
#include "station_view.h"
#include <QPointer>
#include <QCache>
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
  bool mapsEnabled() const { return mapsEnabled_; }
  void setMapsEnabled(bool enabled);
  void setMapGrid(bool enabled) { grid_ = enabled; update(); }
  void setMapBorder(bool enabled) { border_ = enabled; update(); }
  void setMapBackground(QColor color) { background_ = color; update(); }
  QString mapAttribution() const;
  void setMapAttribution(const QString &text);
  void setMapGamma(double gamma);
  void setMapIntensity(double intensity) { intensity_ = intensity; update(); }
  void setTextStyle(int style) { textStyle_ = style; update(); }
  void setIconOutline(int style) { iconOutline_ = style; update(); }
  bool stationVisible(const QString &name) const;

signals:
  void stationClicked(const QString &name);
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
  friend class MapTest;
  struct StationHit { QString name; QRectF bounds; QPointF location; };
  QList<StationHit> stationHits_;
  void refreshTiles();
  void updateView();
  void zoomAt(int zoom, QPointF position);
  QPointF viewportOrigin() const;
  void drawStations(QPainter &painter);
  void drawStationOverlays(QPainter &painter, const DataRow &row);
  void drawRange(QPainter &painter, map::Coordinate center, double meters, double start = 0, double span = 360);

  bool mapsEnabled_ = true, grid_ = false, border_ = false;
  QColor background_{"#e8edf0"};
  double intensity_ = 1, gamma_ = 1;
  QCache<qint64, QPixmap> correctedTiles_{64 * 1024};
  int textStyle_ = 4, iconOutline_ = 0;
  TileSource *tiles_;
  QLabel *attribution_;
  QPointF center_;
  int zoom_;
  QTimer tileTimer_;
  bool dragging_ = false;
  QPointF dragPosition_, pressPosition_;
  bool dragged_ = false;
  int wheelDelta_ = 0;
  QPointer<StationModel> stations_;
  AprsSymbols symbols_;
  Selections selections_{0,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1};
  What_to_display display_{1,0,1,1,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,1,1,1,0,0};
};
