// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "map_projection.h"
#include "aprs_symbols.h"
#include "station_view.h"
#include "xastir_config.h"
#include <QPointer>
#include <QCache>
#include <QTimer>
#include <QWidget>

class QLabel;
class TileSource;
class VectorMaps;
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
  QString baseMap() const { return baseMap_; }
  void setBaseMap(const QString &source);
  bool vectorMapReady() const;
  void setVectorLayers(int mask) { vectorLayers_ = mask & 15; update(); }
  int vectorLayers() const { return vectorLayers_; }
  void setVectorFills(bool enabled) { vectorFills_ = enabled; update(); }
  void setVectorLabels(bool enabled) { vectorLabels_ = enabled; update(); }
  bool darkMapMode() const { return darkMode_; }
  void setDarkMapMode(bool enabled);
  bool mapsEnabled() const { return mapsEnabled_; }
  void setMapsEnabled(bool enabled);
  void setMapGrid(bool enabled);
  void setMapBorder(bool enabled);
  void setMapBackground(QColor color) { background_ = color; update(); }
  QString mapAttribution() const;
  QString rasterAttribution() const { return baseMap_ == "raster" ? mapAttribution() : rasterAttribution_; }
  void setMapAttribution(const QString &text);
  void setMapGamma(double gamma);
  void setMapIntensity(double intensity) { intensity_ = intensity; update(); }
  void setTextStyle(int style) { textStyle_ = style; update(); }
  void setIconOutline(int style) { iconOutline_ = style; update(); }
  bool stationVisible(const QString &name) const;
  void setTiming(const TimingSettings &value);
  const TimingSettings &timing() const { return timing_; }

signals:
  void stationClicked(const QString &name);
  void viewChanged();
  void manuallyPanned();
  void baseMapChanged();
  void mapStatusChanged(const QString &message);

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
  void drawGrid(QPainter &painter);
  QColor chromeBackground() const;
  QColor chromeText() const;
  int mapFrameWidth() const;
  void positionAttribution();
  void drawStationOverlays(QPainter &painter, const DataRow &row);
  void drawRange(QPainter &painter, map::Coordinate center, double meters, double start = 0, double span = 360);

  TimingSettings timing_;
  VectorMaps *vectors_;
  QString baseMap_ = "raster", rasterAttribution_;
  int vectorLayers_ = 15;
  bool vectorFills_ = true, vectorLabels_ = true;
  bool mapsEnabled_ = true, grid_ = false, border_ = false, darkMode_ = false;
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
