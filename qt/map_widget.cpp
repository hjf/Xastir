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
#include <algorithm>
#include <cmath>

MapWidget::MapWidget(TileSource *tiles, map::Coordinate center, int zoom, QWidget *parent)
  : QWidget(parent), tiles_(tiles), attribution_(new QLabel(this)),
    zoom_(std::clamp(zoom, map::MinZoom, map::MaxZoom))
{
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

void MapWidget::drawStations(QPainter &painter)
{
  if (!stations_)
    return;
  painter.save();
  QFont labelFont = font();
  labelFont.setBold(true);
  const QFontMetricsF metrics(labelFont);
  const double world = map::worldSize(zoom_);
  for (const Station &station : stations_->stations())
  {
    const QPointF nearest = coordinateToScreen(station.position);
    if (nearest.y() < -20 || nearest.y() > height() + 20)
      continue;
    const double labelWidth = metrics.horizontalAdvance(station.callsign);
    const int firstCopy = int(std::ceil((-labelWidth - 20 - nearest.x()) / world));
    const int lastCopy = int(std::floor((width() + 20 - nearest.x()) / world));
    const QImage icon = symbols_.image(station.symbolTable, station.symbolCode, station.course);
    for (int copy = firstCopy; copy <= lastCopy; ++copy)
    {
      const QPointF point = nearest + QPointF(copy * world, 0);
      const QPoint anchor(qRound(point.x()), qRound(point.y()));
      painter.drawImage(anchor - QPoint(icon.width() / 2, icon.height() / 2), icon);
      QPainterPath label;
      label.addText(QPointF(anchor.x() + 14, anchor.y() + (metrics.ascent() - metrics.descent()) / 2),
                    labelFont, station.callsign);
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
  updateView();
}
