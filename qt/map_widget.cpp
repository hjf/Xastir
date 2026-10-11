// SPDX-License-Identifier: GPL-2.0-or-later
#include "map_widget.h"
#include "tile_source.h"
#include "vector_maps.h"
#include "station_model.h"
#include "aprs_position.h"

#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QRegion>
#include <QShowEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>

MapWidget::MapWidget(TileSource *tiles, map::Coordinate center, int zoom, QWidget *parent)
  : QWidget(parent), tiles_(tiles), attribution_(new QLabel(this)),
    zoom_(std::clamp(zoom, map::MinZoom, map::MaxZoom))
{
  vectors_ = new VectorMaps(this);
  connect(vectors_, &VectorMaps::changed, this, qOverload<>(&MapWidget::update));
  connect(vectors_, &VectorMaps::statusChanged, this, [this](const QString &source, const QString &message) {
    if (baseMap_ == source) emit mapStatusChanged(message);
  });
  auto *ageTimer = new QTimer(this);
  ageTimer->setInterval(10000);
  connect(ageTimer, &QTimer::timeout, this, qOverload<>(&MapWidget::update)); ageTimer->start();
  center_ = map::toWorld(center, zoom_);
  setObjectName(QStringLiteral("mapCanvas"));
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::OpenHandCursor);
  setMinimumSize(360, 256);
  setAttribute(Qt::WA_OpaquePaintEvent);
  attribution_->setText(QStringLiteral(
    "© <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap</a> contributors"));
  attribution_->setOpenExternalLinks(true);
  attribution_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
  attribution_->setStyleSheet(QStringLiteral("QLabel { background: white; color: #202830; padding: 5px 8px; }"));
  attribution_->adjustSize();
  rasterAttribution_ = mapAttribution();
  tileTimer_.setSingleShot(true);
  tileTimer_.setInterval(75);
  connect(&tileTimer_, &QTimer::timeout, this, &MapWidget::refreshTiles);
  connect(tiles_, &TileSource::tileAvailable, this, qOverload<>(&MapWidget::update));
}

map::Coordinate MapWidget::centerCoordinate() const
{
  return map::fromWorld(center_, zoom_);
}

void MapWidget::setView(map::Coordinate center, int zoom)
{
  zoom_ = std::clamp(zoom, map::MinZoom, map::MaxZoom);
  center_ = map::toWorld(center, zoom_);
  updateView();
}

void MapWidget::setStationModel(StationModel *stations)
{
  if (stations_)
    disconnect(stations_, nullptr, this, nullptr);
  stations_ = stations;
  if (stations_)
  {
    connect(stations_, &StationModel::stationsChanged, this, qOverload<>(&MapWidget::update));
    connect(stations_, &QObject::destroyed, this, qOverload<>(&MapWidget::update));
  }
  update();
}

QPointF MapWidget::coordinateToScreen(map::Coordinate coordinate) const
{
  QPointF delta = map::toWorld(coordinate, zoom_) - center_;
  const double world = map::worldSize(zoom_);
  // Choose the nearest world copy, including when the viewport crosses ±180°.
  delta.rx() -= std::round(delta.x() / world) * world;
  return delta + QPointF(width() / 2.0, height() / 2.0);
}

QPointF MapWidget::viewportOrigin() const
{
  return center_ - QPointF(width() / 2.0, height() / 2.0);
}

void MapWidget::setMapsEnabled(bool enabled)
{
  mapsEnabled_ = enabled;
  if (!enabled) { tileTimer_.stop(); tiles_->setVisibleTiles({}); }
  else refreshTiles();
  update();
}

void MapWidget::refreshTiles()
{
  if (!isVisible() || !mapsEnabled_ || baseMap_ != "raster")
    return;
  const QPointF origin = viewportOrigin();
  const int firstX = int(std::floor(origin.x() / map::TileSize));
  const int firstY = std::max(0, int(std::floor(origin.y() / map::TileSize)));
  const int lastX = int(std::floor((origin.x() + width() - 1) / map::TileSize));
  const int lastY = std::min((1 << zoom_) - 1,
    int(std::floor((origin.y() + height() - 1) / map::TileSize)));
  QList<TileId> visible;
  for (int y = firstY; y <= lastY; ++y)
    for (int x = firstX; x <= lastX; ++x)
      visible.append({zoom_, map::wrapTileX(x, zoom_), y});
  tiles_->setVisibleTiles(visible);
}

void MapWidget::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.fillRect(rect(), background_);
  painter.setOpacity(intensity_);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  const QPointF origin = viewportOrigin();
  const int firstX = int(std::floor(origin.x() / map::TileSize));
  const int firstY = std::max(0, int(std::floor(origin.y() / map::TileSize)));
  const int lastX = int(std::floor((origin.x() + width() - 1) / map::TileSize));
  const int lastY = std::min((1 << zoom_) - 1,
    int(std::floor((origin.y() + height() - 1) / map::TileSize)));
  if (mapsEnabled_ && baseMap_ == "raster") for (int y = firstY; y <= lastY; ++y)
  {
    for (int x = firstX; x <= lastX; ++x)
    {
      const TileId id{zoom_, map::wrapTileX(x, zoom_), y};
      const QRectF target(x * map::TileSize - origin.x(), y * map::TileSize - origin.y(),
                          map::TileSize, map::TileSize);
      if (const QPixmap *image = tiles_->tile(id)) {
        const QPixmap *rendered = image;
        if (gamma_ != 1) {
          rendered = correctedTiles_.object(image->cacheKey());
          if (!rendered) {
            QImage corrected = image->toImage().convertToFormat(QImage::Format_ARGB32);
            int lookup[256];
            for (int i = 0; i < 256; ++i) lookup[i] = qRound(255 * std::pow(i / 255.0, 1 / gamma_));
            for (int y = 0; y < corrected.height(); ++y) {
              auto *line = reinterpret_cast<QRgb *>(corrected.scanLine(y));
              for (int x = 0; x < corrected.width(); ++x) {
                const auto rgb = line[x];
                line[x] = qRgba(lookup[qRed(rgb)], lookup[qGreen(rgb)], lookup[qBlue(rgb)], qAlpha(rgb));
              }
            }
            auto *pixmap = new QPixmap(QPixmap::fromImage(corrected));
            correctedTiles_.insert(image->cacheKey(), pixmap, int((corrected.sizeInBytes() + 1023) / 1024));
            rendered = pixmap;
          }
        }
        painter.drawPixmap(target, *rendered, rendered->rect());
      }
      else
      {
        painter.setPen(QColor("#d5dde2"));
        painter.drawRect(target.adjusted(0, 0, -1, -1));
      }
    }
  }
  const int frame = mapFrameWidth();
  if (mapsEnabled_ && baseMap_ != "raster") {
    const QRect hintBounds = painter.fontMetrics().boundingRect(tr("Drag to pan · Scroll or +/− to zoom")).adjusted(-10,-7,10,7);
    QRegion reserved(attribution_->geometry());
    reserved += QRect(frame + 12, frame + 12, hintBounds.width(), hintBounds.height());
    if (frame) reserved += QRegion(rect()) - QRegion(rect().adjusted(frame,frame,-frame,-frame));
    vectors_->draw(painter, baseMap_, origin, zoom_, size(), vectorLayers_, vectorFills_, vectorLabels_, reserved);
  }
  painter.setOpacity(1);
  painter.save();
  if (frame) painter.setClipRect(rect().adjusted(frame, frame, -frame, -frame));
  drawStations(painter);
  painter.restore();
  drawGrid(painter);
  painter.setPen(darkMode_ ? chromeText() : QColor("#35434e"));
  const QString hint = tr("Drag to pan · Scroll or +/− to zoom");
  const QRect textRect = painter.fontMetrics().boundingRect(hint).adjusted(-10, -7, 10, 7);
  const QRect panel(frame + 12, frame + 12, textRect.width(), textRect.height());
  QColor panelColor = chromeBackground(); panelColor.setAlpha(235);
  painter.fillRect(panel, panelColor);
  painter.drawText(panel, Qt::AlignCenter, hint);
}

bool MapWidget::stationVisible(const QString &name) const
{
  return stations_ && xastir_station_visible(stations_->record(name), &selections_, QDateTime::currentSecsSinceEpoch(), 3600, 3600, 0);
}
void MapWidget::drawRange(QPainter &painter, map::Coordinate center, double meters, double start, double span)
{
  if (!std::isfinite(meters) || meters <= 0) return;
  QPainterPath path;
  QPointF previous = coordinateToScreen(center);
  const double world = map::worldSize(zoom_);
  if (span < 360) path.moveTo(previous);
  for (int i = 0; i <= 72; ++i) {
    map::Coordinate point;
    xastir_destination(center.latitude, center.longitude, start + span * i / 72, meters, &point.latitude, &point.longitude);
    QPointF screen = coordinateToScreen(point);
    screen.rx() += std::round((previous.x() - screen.x()) / world) * world;
    if (i == 0 && span >= 360) path.moveTo(screen); else path.lineTo(screen);
    previous = screen;
  }
  path.closeSubpath(); painter.drawPath(path);
}
void MapWidget::drawStationOverlays(QPainter &painter, const DataRow &row)
{
  painter.save(); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
  const map::Coordinate position{row.latitude, row.longitude};
  const QPointF origin = coordinateToScreen(position);
  const auto now = QDateTime::currentSecsSinceEpoch();
  if (display_.trail && row.oldest_trackpoint) {
    painter.setPen(QPen(QColor("#337ba3"), 2));
    QPointF previous; bool first = true;
    for (const TrackRow *point = row.oldest_trackpoint; point; point = point->next) {
      auto screen = coordinateToScreen({90 - point->trail_lat_pos / 360000.0, point->trail_long_pos / 360000.0 - 180});
      if (!first && !(point->flag & TR_NEWTRK)) {
        const double world = map::worldSize(zoom_);
        screen.rx() += std::round((previous.x() - screen.x()) / world) * world;
        painter.drawLine(previous, screen);
      }
      if (display_.label_all_trackpoints) painter.drawText(screen + QPointF(4, -4), QDateTime::fromSecsSinceEpoch(point->sec).toString("HH:mm:ss"));
      previous = screen; first = false;
    }
  }
  if (display_.ambiguity && row.pos_amb > 0 && row.pos_amb <= 4) {
    const double minutes[] = {0, .1, 1, 10, 60};
    const double offset = minutes[int(row.pos_amb)] / 120;
    painter.setPen(QPen(QColor("#b47d20"), 1, Qt::DashLine));
    const auto a = coordinateToScreen({std::min(90.0, position.latitude + offset), position.longitude - offset});
    const auto b = coordinateToScreen({std::max(-90.0, position.latitude - offset), position.longitude + offset});
    painter.drawRect(QRectF(a, b).normalized());
  }
  if (display_.phg && (!(row.flag & ST_MOVING) || display_.phg_of_moving)) {
    QByteArray phg(row.power_gain);
    if (phg.isEmpty() && display_.default_phg) phg = "PHG3130";
    double range = 0;
    if (phg.size() == 7 && phg.startsWith("PHG")) range = xastir_phg_range(phg[3], phg[4], phg[5]) * 1609.344;
    if (phg.size() == 7 && phg.startsWith("RNG")) range = phg.mid(3).toDouble() * 1609.344;
    painter.setPen(QPen(QColor("#618c45"), 1, Qt::DashLine)); drawRange(painter, position, range);
  }
  if (display_.df_data) {
    const QByteArray signal(row.signal_gain);
    painter.setPen(QPen(QColor("#884a9a"), 1));
    if (signal.size() == 7) drawRange(painter, position, xastir_shg_range(signal[3], signal[4], signal[5]) * 1609.344);
    if (std::strlen(row.NRQ) == 3 && row.bearing[0]) {
      const int widths[] = {0,240,120,64,32,16,8,4,2,1};
      const int quality = row.NRQ[2] - '0';
      const int width = row.NRQ[0] == '0' ? 8 : quality >= 0 && quality <= 9 ? widths[quality] : 0;
      const double meters = (row.NRQ[0] == '0' ? 512 : std::pow(2, row.NRQ[1] - '0')) * 1852;
      const double heading = std::atof(row.bearing) + std::atof(row.course);
      if (width && display_.df_beamwidth_data) drawRange(painter, position, meters, heading - width / 2.0, width);
      if (width && display_.df_bearing_data) {
        map::Coordinate end; xastir_destination(position.latitude, position.longitude, heading, meters, &end.latitude, &end.longitude);
        painter.drawLine(origin, coordinateToScreen(end));
      }
    }
  }
  if (display_.wind_barb && display_.weather && row.weather_data && row.weather_data->wx_course[0] && row.weather_data->wx_speed[0]) {
    painter.translate(origin); painter.rotate(std::atof(row.weather_data->wx_course));
    painter.setPen(QPen(QColor("#266974"), 2));
    const int knots = qRound(std::atof(row.weather_data->wx_speed) / 1.1508 / 5) * 5;
    if (!knots) painter.drawEllipse(QPointF(0, 0), 5, 5);
    else {
      painter.drawLine(QPointF(0, 0), QPointF(0, -40));
      int remaining = std::min(knots, 150); double y = -40;
      while (remaining >= 50) { painter.drawPolygon(QPolygonF{QPointF(0,y),QPointF(12,y+4),QPointF(0,y+8)}); remaining -= 50; y += 10; }
      while (remaining >= 10) { painter.drawLine(QPointF(0,y),QPointF(12,y+6)); remaining -= 10; y += 5; }
      if (remaining >= 5) painter.drawLine(QPointF(0,y),QPointF(6,y+3));
    }
    painter.resetTransform();
  }
  const double speed = std::atof(row.speed);
  const qint64 age = now - row.sec_heard;
  if (display_.dr_data && row.heading_valid && speed > 0 && !row.weather_data && age >= 0 && age <= 600) {
    const double meters = speed * 1852 / 3600 * age;
    long predictedLon, predictedLat;
    xastir_compute_DR_position(row.coord_lon,row.coord_lat,meters/1852,row.heading,&predictedLon,&predictedLat);
    const map::Coordinate predicted{90-predictedLat/360000.0,predictedLon/360000.0-180};
    painter.setPen(QPen(QColor("#9f6161"), 1, Qt::DashLine));
    if (display_.dr_arc) drawRange(painter, position, meters, row.heading - 15, 30);
    if (display_.dr_course) painter.drawLine(origin, coordinateToScreen(predicted));
    if (display_.dr_symbol) {
      const auto icon = symbols_.image(QChar(row.aprs_symbol.special_overlay ? row.aprs_symbol.special_overlay : row.aprs_symbol.aprs_type), QChar(row.aprs_symbol.aprs_symbol), row.heading);
      painter.setOpacity(.5); painter.drawImage(coordinateToScreen(predicted) - QPointF(icon.width()/2,icon.height()/2),icon);
    }
  }
  painter.restore();
}

void MapWidget::drawStations(QPainter &painter)
{
  stationHits_.clear();
  if (!stations_)
    return;
  painter.save();
  QFont labelFont = font();
  labelFont.setBold(true);
  const QFontMetricsF metrics(labelFont);
  const double world = map::worldSize(zoom_);
  struct Label { QString name, text; QPointF point; QImage icon; QRectF bounds; QPointF anchor; };
  QList<Label> labels;
  const DataRow *local = nullptr;
  for (const auto *row = stations_->database()->first; row; row = row->n_next)
    if (row->flag & ST_MYSTATION) { local = row; break; }
  if (display_.aloha_circle && local) {
    aloha_stats stats{};
    const double radius = xastir_aloha_radius(stations_->database(), local->latitude, local->longitude, &stats);
    painter.setPen(QPen(QColor("#be8738"), 2, Qt::DashLine)); painter.setBrush(Qt::NoBrush);
    drawRange(painter, {local->latitude,local->longitude}, radius);
  }
  for (const Station &station : stations_->stations())
  {
    const auto *row = stations_->record(station.callsign);
    if (!stationVisible(station.callsign)) continue;
    drawStationOverlays(painter, *row);
    QStringList lines;
    if (display_.callsign) lines << QString::fromUtf8(row->tactical_call_sign ? row->tactical_call_sign : row->call_sign);
    if (display_.course && row->heading_valid) lines << tr("%1°").arg(row->heading, 0, 'f', 0);
    if (display_.speed && row->speed[0]) lines << (display_.speed_short ? tr("%1k").arg(std::atof(row->speed)*1.852,0,'f',0) : tr("%1 km/h").arg(std::atof(row->speed)*1.852,0,'f',1));
    if (display_.altitude && row->altitude[0]) lines << tr("%1 m").arg(QString::fromLatin1(row->altitude));
    if (display_.last_heard) lines << tr("%1s ago").arg(std::max<qint64>(0,QDateTime::currentSecsSinceEpoch()-row->sec_heard));
    if (display_.dist_bearing && local) {
      double bearing; const double distance=xastir_distance(local->latitude,local->longitude,row->latitude,row->longitude,&bearing);
      lines << tr("%1 km / %2°").arg(distance/1000,0,'f',1).arg(bearing,0,'f',0);
    }
    if (display_.weather && display_.weather_text && row->weather_data) {
      const auto *wx=row->weather_data;
      if (wx->wx_temp[0]) lines << tr("%1°C").arg((std::atof(wx->wx_temp)-32)*5/9,0,'f',1);
      if (!display_.temperature_only) {
        if (wx->wx_speed[0]) lines << tr("Wind %1 km/h").arg(std::atof(wx->wx_speed)*1.609344,0,'f',1);
        if (wx->wx_hum[0]) lines << tr("RH %1%").arg(QString::fromLatin1(wx->wx_hum));
        if (wx->wx_baro[0]) lines << tr("%1 hPa").arg(QString::fromLatin1(wx->wx_baro));
      }
    }
    const QString text = lines.join(" · ");
    const QPointF nearest = coordinateToScreen(station.position);
    if (nearest.y() < -20 || nearest.y() > height() + 20)
      continue;
    const double labelWidth = metrics.horizontalAdvance(text);
    const int firstCopy = int(std::ceil((-labelWidth - 20 - nearest.x()) / world));
    const int lastCopy = int(std::floor((width() + 20 - nearest.x()) / world));
    const QImage icon = symbols_.image(station.symbolTable, station.symbolCode, display_.symbol_rotate ? station.course : std::nullopt);
    for (int copy = firstCopy; copy <= lastCopy; ++copy)
    {
      const QPointF point = nearest + QPointF(copy * world, 0);
      if (display_.symbol || !text.isEmpty())
        labels.append({station.callsign, text, point, icon, {}, {}});
    }
  }

  // Reserve actual locations before placing cards, so a moved card cannot cover
  // another station's dot. QRegion keeps collision queries out of a pairwise loop.
  QRegion locations, occupied;
  for (const auto &label : labels)
    locations += QRect(qRound(label.point.x()) - 4, qRound(label.point.y()) - 4, 8, 8);
  const int frame = mapFrameWidth();
  const QRect available = rect().adjusted(frame + 4, frame + 4, -frame - 4, -frame - 36);
  const int cardHeight = int(std::ceil(std::max(display_.symbol ? 20.0 : 0.0, metrics.height()) + 6));
  for (auto &label : labels) {
    if (!display_.symbol && label.text.isEmpty()) continue;
    const double textWidth = label.text.isEmpty() ? 0 : metrics.horizontalAdvance(label.text) + 4;
    const double cardWidth = (display_.symbol ? 24 : 0) + textWidth + 6;
    const QPointF preferred(label.point.x() - (display_.symbol ? 13 : -10), label.point.y() - cardHeight / 2.0);
    auto place = [&](QPointF topLeft, bool original) {
      QRectF bounds(topLeft, QSizeF(cardWidth, cardHeight));
      const QRect collision = bounds.toAlignedRect();
      if (!available.contains(collision) || occupied.intersects(collision)) return false;
      if (!original && locations.intersects(collision)) return false;
      label.bounds = bounds;
      label.anchor = topLeft + QPointF(display_.symbol ? 13 : -10, cardHeight / 2.0);
      occupied += collision.adjusted(-3, -3, 3, 3);
      return true;
    };
    if (place(preferred, true)) continue;
    // Search nearby slots first, with a fixed viewport-sized budget per card.
    // Ordering is deterministic (the station projection is sorted by callsign).
    const int columns = std::max(1, available.width() / 24);
    const int rows = std::max(1, available.height() / (cardHeight + 6));
    bool placed = false;
    int attempts = 0;
    for (int ring = 1; ring <= std::max(columns, rows) && !placed && attempts < 2048; ++ring) {
      for (int y = -std::min(ring, rows); y <= std::min(ring, rows) && !placed && attempts < 2048; ++y) {
        for (int x = -std::min(ring, columns); x <= std::min(ring, columns) && attempts < 2048; ++x) {
          if (std::max(std::abs(x), std::abs(y)) != ring) continue;
          ++attempts;
          if (place(preferred + QPointF(x * 24, y * (cardHeight + 6)), false)) { placed = true; break; }
        }
      }
    }
  }
  painter.setRenderHint(QPainter::Antialiasing, true);
  // Draw connectors underneath all cards, so they never obscure label text.
  for (const auto &label : labels) {
    if (label.bounds.isEmpty() || QLineF(label.anchor, label.point).length() < 1) continue;
    const QPointF end(std::clamp(label.point.x(), label.bounds.left(), label.bounds.right()),
                      std::clamp(label.point.y(), label.bounds.top(), label.bounds.bottom()));
    painter.setPen(QPen(Qt::white, 3)); painter.drawLine(label.point, end);
    painter.setPen(QPen(QColor("#192d3b"), 1)); painter.drawLine(label.point, end);
  }
  for (const auto &label : labels) {
    if (!label.bounds.isEmpty()) {
      stationHits_.append({label.name, label.bounds, label.point});
      if (QLineF(label.anchor, label.point).length() >= 1)
        painter.fillRect(label.bounds, QColor(255, 255, 255, 225));
      if (display_.symbol) {
        const QPointF corner = label.anchor - QPointF(label.icon.width()/2.0, label.icon.height()/2.0);
        if (iconOutline_) {
          QImage outline(label.icon.size(), QImage::Format_ARGB32_Premultiplied);
          outline.fill(Qt::transparent);
          QPainter ink(&outline); ink.drawImage(0, 0, label.icon);
          ink.setCompositionMode(QPainter::CompositionMode_SourceIn);
          ink.fillRect(outline.rect(), iconOutline_ == 1 ? Qt::black : iconOutline_ == 2 ? Qt::gray : Qt::white);
          ink.end();
          for (const QPoint &offset : {QPoint(-1,0), QPoint(1,0), QPoint(0,-1), QPoint(0,1)})
            painter.drawImage(corner + offset, outline);
        }
        painter.drawImage(corner, label.icon);
      }
      QPainterPath path;
      path.addText(label.anchor + QPointF(14, (metrics.ascent() - metrics.descent()) / 2), labelFont, label.text);
      if (textStyle_ == 2 || textStyle_ == 4) {
        painter.fillRect(path.boundingRect().adjusted(-2,-2,2,2), textStyle_ == 2 ? Qt::black : Qt::white);
      }
      if (textStyle_ == 1 || textStyle_ == 3) {
        painter.save(); painter.translate(textStyle_ == 1 ? 1 : 2, textStyle_ == 1 ? 1 : 2);
        painter.setPen(Qt::NoPen); painter.setBrush(Qt::black); painter.drawPath(path); painter.restore();
      }
      painter.setPen(textStyle_ == 0 ? QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) : QPen(Qt::NoPen));
      painter.setBrush(textStyle_ == 4 ? Qt::black : Qt::white); painter.drawPath(path);
    }
  }
  for (const auto &label : labels) {
    if (label.bounds.isEmpty() || QLineF(label.anchor, label.point).length() >= 1 || !display_.symbol) {
      painter.setPen(QPen(Qt::white, 1.5)); painter.setBrush(QColor("#192d3b"));
      painter.drawEllipse(label.point, 3, 3);
    }
  }
  painter.restore();
}

void MapWidget::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  positionAttribution();
  if (isVisible())
    tileTimer_.start();
}

void MapWidget::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  tileTimer_.start(0);
}

void MapWidget::hideEvent(QHideEvent *event)
{
  tileTimer_.stop();
  tiles_->setVisibleTiles({});
  QWidget::hideEvent(event);
}

void MapWidget::updateView()
{
  center_ = map::constrainCenter(center_, zoom_);
  update();
  if (isVisible())
    tileTimer_.start(75);
  emit viewChanged();
}

void MapWidget::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    dragging_ = true;
    dragged_ = false; pressPosition_ = event->position();
    dragPosition_ = event->position();
    setCursor(Qt::ClosedHandCursor);
    setFocus();
    event->accept();
  }
  else
    QWidget::mousePressEvent(event);
}

void MapWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (dragging_)
  {
    if ((event->position()-pressPosition_).manhattanLength()>4) dragged_=true;
    if (dragged_) emit manuallyPanned();
    center_ -= event->position() - dragPosition_;
    dragPosition_ = event->position();
    updateView();
    event->accept();
  }
  else
    QWidget::mouseMoveEvent(event);
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    if (!dragged_ && stations_) {
      QString selected; double best=225;
      for (const auto &hit : stationHits_)
        if (stationVisible(hit.name) && hit.bounds.contains(event->position())) { selected = hit.name; break; }
      const bool hitLabel = !selected.isEmpty();
      for (const auto &station : stations_->stations()) {
        if (hitLabel) break;
        if (!stationVisible(station.callsign)) continue;
        QPointF delta=coordinateToScreen(station.position)-event->position();
        const double world=map::worldSize(zoom_);
        delta.setX(delta.x()-std::round(delta.x()/world)*world);
        const double squared=delta.x()*delta.x()+delta.y()*delta.y();
        if(squared<=best) {best=squared; selected=station.callsign;}
      }
      if(!selected.isEmpty()) emit stationClicked(selected);
    }
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
  }
  else
    QWidget::mouseReleaseEvent(event);
}

void MapWidget::zoomAt(int zoom, QPointF position)
{
  zoom = std::clamp(zoom, map::MinZoom, map::MaxZoom);
  if (zoom == zoom_)
    return;
  const QPointF offset = position - QPointF(width() / 2.0, height() / 2.0);
  center_ = (center_ + offset) * std::ldexp(1.0, zoom - zoom_) - offset;
  zoom_ = zoom;
  updateView();
}

void MapWidget::wheelEvent(QWheelEvent *event)
{
  wheelDelta_ += event->angleDelta().y();
  const int steps = wheelDelta_ / 120;
  wheelDelta_ %= 120;
  zoomAt(zoom_ + steps, event->position());
  event->accept();
}

void MapWidget::keyPressEvent(QKeyEvent *event)
{
  const QPointF midpoint(width() / 2.0, height() / 2.0);
  switch (event->key())
  {
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomAt(zoom_ + 1, midpoint); return;
    case Qt::Key_Minus: zoomAt(zoom_ - 1, midpoint); return;
    case Qt::Key_Left: center_.rx() -= 128; break;
    case Qt::Key_Right: center_.rx() += 128; break;
    case Qt::Key_Up: center_.ry() -= 128; break;
    case Qt::Key_Down: center_.ry() += 128; break;
    default: QWidget::keyPressEvent(event); return;
  }
  emit manuallyPanned();
  updateView();
}

QString MapWidget::mapAttribution() const
{
  return attribution_->textFormat() == Qt::PlainText ? attribution_->text() : QStringLiteral("© OpenStreetMap contributors");
}
void MapWidget::setMapAttribution(const QString &text)
{
  if (text == QStringLiteral("© OpenStreetMap contributors")) {
    attribution_->setTextFormat(Qt::RichText);
    attribution_->setText(QStringLiteral("© <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap</a> contributors"));
  } else {
    attribution_->setTextFormat(Qt::PlainText);
    attribution_->setText(text);
  }
  attribution_->adjustSize();
  positionAttribution();
}
void MapWidget::setMapGamma(double gamma)
{
  gamma_ = std::isfinite(gamma) ? std::clamp(gamma, 0.1, 5.0) : 1;
  correctedTiles_.clear(); update();
}

int MapWidget::mapFrameWidth() const
{
  // Legacy get_border_width() derives the white margin from the font height.
  return grid_ && border_ ? (fontMetrics().height() * 3 / 2 + 2) & ~1 : 0;
}

void MapWidget::positionAttribution()
{
  const int frame = mapFrameWidth();
  attribution_->move(std::max(frame, width() - frame - attribution_->width() - 8),
                     std::max(frame, height() - frame - attribution_->height() - 8));
}

void MapWidget::setMapGrid(bool enabled)
{
  grid_ = enabled; positionAttribution(); update();
}

void MapWidget::setMapBorder(bool enabled)
{
  border_ = enabled; positionAttribution(); update();
}

void MapWidget::drawGrid(QPainter &painter)
{
  if (!grid_) return;
  painter.save();
  const int frame = mapFrameWidth();
  const QRectF inner = QRectF(rect()).adjusted(frame, frame, -frame, -frame);
  const QPointF origin = viewportOrigin();
  const QFontMetrics metrics = painter.fontMetrics();
  if (frame) {
    // The legacy border is a coordinate band around the viewport, not tile edges.
    painter.fillRect(QRect(0, 0, width(), frame), chromeBackground());
    painter.fillRect(QRect(0, height() - frame, width(), frame), chromeBackground());
    painter.fillRect(QRect(0, frame, frame, height() - 2 * frame), chromeBackground());
    painter.fillRect(QRect(width() - frame, frame, frame, height() - 2 * frame), chromeBackground());
  }
  auto coordinateLabel = [](double value, bool latitude) {
    char text[64];
    if (latitude) xastir_convert_lat_l2s(std::lround((90 - value) * 360000), text, sizeof(text), CONVERT_DEC_DEG);
    else {
      // Label the correct hemisphere even across the dateline or repeated worlds.
      value -= std::floor((value + 180) / 360) * 360;
      xastir_convert_lon_l2s(std::lround((180 + value) * 360000), text, sizeof(text), CONVERT_DEC_DEG);
    }
    return QString::fromLatin1(text).trimmed();
  };
  // Keep grid spacing legible and bounded for every supported zoom.
  const double target = 360.0 * 120 / map::worldSize(zoom_);
  double step = std::pow(10.0, std::floor(std::log10(target)));
  for (double factor : {1.0, 2.0, 5.0, 10.0})
    if (step * factor >= target) { step *= factor; break; }
  const double left = (origin.x() + frame) / map::worldSize(zoom_) * 360 - 180;
  const double right = (origin.x() + width() - frame) / map::worldSize(zoom_) * 360 - 180;
  double lastLongitudeEnd = inner.left();
  const QPen gridPen(darkMode_ ? QColor(188,203,216,160) : QColor(50,70,90,150), 1, Qt::DashLine);
  for (double lon = std::ceil(left / step) * step; lon <= right; lon += step) {
    const double x = (lon + 180) / 360 * map::worldSize(zoom_) - origin.x();
    painter.setPen(gridPen);
    painter.drawLine(QPointF(x, inner.top()), QPointF(x, inner.bottom()));
    if (frame) {
      painter.setPen(chromeText());
      painter.drawLine(QPointF(x, inner.bottom()), QPointF(x, inner.bottom() + 4));
      const QString text = coordinateLabel(lon, false);
      const double w = metrics.horizontalAdvance(text);
      const double start = x - w / 2;
      if (start >= lastLongitudeEnd + 4 && start + w < inner.right()) {
        painter.drawText(QRectF(start, height() - frame + 4, w, frame - 4), Qt::AlignCenter, text);
        lastLongitudeEnd = start + w;
      }
    }
  }
  const double top = map::fromWorld(origin + QPointF(0, frame), zoom_).latitude;
  const double bottom = map::fromWorld(origin + QPointF(0, height() - frame), zoom_).latitude;
  double lastLatitudeEnd = inner.top();
  // Traverse from north to south to test label overlap in screen order.
  for (double lat = std::floor(top / step) * step; lat >= bottom; lat -= step) {
    const double y = coordinateToScreen({lat, centerCoordinate().longitude}).y();
    painter.setPen(gridPen);
    painter.drawLine(QPointF(inner.left(), y), QPointF(inner.right(), y));
    if (frame) {
      painter.setPen(chromeText());
      painter.drawLine(QPointF(inner.left() - 4, y), QPointF(inner.left(), y));
      painter.drawLine(QPointF(inner.right(), y), QPointF(inner.right() + 4, y));
      const QString text = coordinateLabel(lat, true);
      const double w = metrics.horizontalAdvance(text);
      const double start = y - w / 2;
      if (start >= lastLatitudeEnd + 4 && start + w < inner.bottom()) {
        for (double x : {frame / 2.0, width() - frame / 2.0}) {
          painter.save(); painter.translate(x, y); painter.rotate(-90);
          painter.drawText(QRectF(-w / 2, -metrics.height() / 2.0, w, metrics.height()), Qt::AlignCenter, text);
          painter.restore();
        }
        lastLatitudeEnd = start + w;
      }
    }
  }
  if (frame) {
    painter.setPen(chromeText());
    const QString metadata = tr("Latitude/Longitude grid · WGS 84 · %1, %2 to %3, %4")
      .arg(coordinateLabel(top, true), coordinateLabel(left, false), coordinateLabel(bottom, true), coordinateLabel(right, false));
    painter.drawText(QRectF(frame + 4, 0, inner.width() - 8, frame), Qt::AlignCenter,
      metrics.elidedText(metadata, Qt::ElideRight, int(inner.width()) - 8));
  }
  painter.restore();
}

void MapWidget::setBaseMap(const QString &source)
{
  if (source != "raster" && source != "world" && source != "naturalearth") return;
  if (baseMap_ == "raster") rasterAttribution_ = mapAttribution();
  baseMap_ = source;
  emit baseMapChanged();
  if (source == "raster") { setMapAttribution(rasterAttribution_); refreshTiles(); }
  else {
    tileTimer_.stop(); tiles_->setVisibleTiles({});
    setMapAttribution(source == "world" ? "Xastir world map (WU2Z)" : "Made with Natural Earth");
    vectors_->request(source);
    if (vectors_->ready(source)) emit mapStatusChanged(tr("Offline vector map ready"));
  }
  update();
}
bool MapWidget::vectorMapReady() const { return vectors_->ready(baseMap_); }

QColor MapWidget::chromeBackground() const { return darkMode_ ? QColor("#20262b") : QColor(Qt::white); }
QColor MapWidget::chromeText() const { return darkMode_ ? QColor("#e6edf3") : QColor(Qt::black); }
void MapWidget::setDarkMapMode(bool enabled)
{
  darkMode_ = enabled;
  attribution_->setStyleSheet(QString("QLabel { background: %1; color: %2; padding: 5px 8px; }")
    .arg(chromeBackground().name(), darkMode_ ? chromeText().name() : QStringLiteral("#202830")));
  auto colors = attribution_->palette();
  colors.setColor(QPalette::Link, darkMode_ ? QColor("#8dcaff") : QColor(Qt::blue));
  attribution_->setPalette(colors);
  update();
}
