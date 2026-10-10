// SPDX-License-Identifier: GPL-2.0-or-later
#include "map_widget.h"
#include "tile_source.h"
#include "station_model.h"

#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
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

void MapWidget::refreshTiles()
{
  if (!isVisible())
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
  painter.fillRect(rect(), QColor("#e8edf0"));
  const QPointF origin = viewportOrigin();
  const int firstX = int(std::floor(origin.x() / map::TileSize));
  const int firstY = std::max(0, int(std::floor(origin.y() / map::TileSize)));
  const int lastX = int(std::floor((origin.x() + width() - 1) / map::TileSize));
  const int lastY = std::min((1 << zoom_) - 1,
    int(std::floor((origin.y() + height() - 1) / map::TileSize)));
  for (int y = firstY; y <= lastY; ++y)
  {
    for (int x = firstX; x <= lastX; ++x)
    {
      const TileId id{zoom_, map::wrapTileX(x, zoom_), y};
      const QRectF target(x * map::TileSize - origin.x(), y * map::TileSize - origin.y(),
                          map::TileSize, map::TileSize);
      if (const QPixmap *image = tiles_->tile(id))
        painter.drawPixmap(target, *image, image->rect());
      else
      {
        painter.setPen(QColor("#d5dde2"));
        painter.drawRect(target.adjusted(0, 0, -1, -1));
      }
    }
  }
  drawStations(painter);
  painter.setPen(QColor("#35434e"));
  const QString hint = tr("Drag to pan · Scroll or +/− to zoom");
  const QRect textRect = painter.fontMetrics().boundingRect(hint).adjusted(-10, -7, 10, 7);
  const QRect panel(12, 12, textRect.width(), textRect.height());
  painter.fillRect(panel, QColor(255, 255, 255, 235));
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
  if (!stations_)
    return;
  painter.save();
  QFont labelFont = font();
  labelFont.setBold(true);
  const QFontMetricsF metrics(labelFont);
  const double world = map::worldSize(zoom_);
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
      const QPoint anchor(qRound(point.x()), qRound(point.y()));
      if (display_.symbol) painter.drawImage(anchor - QPoint(icon.width() / 2, icon.height() / 2), icon);
      QPainterPath label;
      label.addText(QPointF(anchor.x() + 14, anchor.y() + (metrics.ascent() - metrics.descent()) / 2),
                    labelFont, text);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.setBrush(QColor("#192d3b"));
      painter.drawPath(label);
      painter.setPen(Qt::NoPen);
      painter.drawPath(label);
    }
  }
  painter.restore();
}

void MapWidget::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  attribution_->move(std::max(0, width() - attribution_->width() - 8),
                     std::max(0, height() - attribution_->height() - 8));
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
    if (event->position() != dragPosition_) emit manuallyPanned();
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
