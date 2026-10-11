// SPDX-License-Identifier: GPL-2.0-or-later
#include "vector_maps.h"
#include "vector_map.h"
#include "map_projection.h"
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QRegion>
#include <cmath>
#include <cstdio>

namespace {
struct Builder { VectorScene *scene; int layer; };
int appendFeature(void *context, const XastirMapFeature *feature)
{
  if (QThread::currentThread()->isInterruptionRequested()) return 0;
  auto *builder = static_cast<Builder *>(context);
  VectorShape shape;
  shape.layer = builder->layer;
  shape.label = QString::fromUtf8(feature->label);
  shape.labelPoint = map::toWorld({feature->label_latitude, feature->label_longitude}, 0);
  shape.width = feature->line_width;
  // Match the basic bundled layers' intent (boundaries black, water blue).
  shape.stroke = shape.layer >= 2 ? QColor("#3979a5") : QColor("#475254");
  if (shape.layer == 4) {
    switch (feature->color) {
      case 3: shape.stroke = QColor("#26699e"); break;
      case 9: shape.stroke = Qt::black; break;
      case 10: shape.stroke = QColor("#777777"); break;
      case 11: shape.stroke = QColor("#b95a62"); break;
      case 16: shape.stroke = QColor("#777777"); break;
      default: break;
    }
  }
  if (feature->polygon) shape.fill = shape.layer == 2 ? QColor("#c1dced") : QColor("#e4e8d5");
  shape.path.setFillRule(Qt::OddEvenFill);
  // Unwrap each part continuously; keep polygon holes in the outer ring's
  // world copy. This avoids a spurious line across the map at ±180 degrees.
  double reference = 0;
  bool haveReference = false;
  for (int part = 0; part < feature->part_count; ++part) {
    const size_t start = feature->parts[part];
    const size_t end = part + 1 < feature->part_count ? size_t(feature->parts[part+1]) : feature->point_count;
    QPointF previous;
    for (size_t i = start; i < end; ++i) {
      const auto &coordinate = feature->points[i];
      QPointF point = map::toWorld({coordinate.latitude, coordinate.longitude}, 0);
      if (i == start) {
        if (haveReference) point.rx() += std::round((reference - point.x()) / 256) * 256;
        else { reference = point.x(); haveReference = true; }
        shape.path.moveTo(point);
      } else {
        point.rx() += std::round((previous.x() - point.x()) / 256) * 256;
        shape.path.lineTo(point);
      }
      previous = point;
    }
    if (feature->polygon) shape.path.closeSubpath();
  }
  builder->scene->shapes.append(std::move(shape));
  return 1;
}
}
VectorMaps::VectorMaps(QObject *parent) : QObject(parent) { Q_INIT_RESOURCE(offline_maps); }
VectorMaps::~VectorMaps()
{
  for (auto *thread : threads_) thread->requestInterruption();
  for (auto *thread : threads_) thread->wait();
}
void VectorMaps::request(const QString &source)
{
  if (source == "raster" || scenes_.contains(source) || loading_.contains(source)) return;
  loading_.insert(source);
  emit statusChanged(source, tr("Loading offline vector maps…"));
  auto *thread = QThread::create([this, source] {
    auto scene = std::make_shared<VectorScene>();
    char message[256] = "";
    bool ok = true;
    if (source == "world") {
      QFile file(":/maps/worldhi.map"); ok = file.open(QIODevice::ReadOnly);
      const QByteArray bytes = file.readAll();
      QString error;
      if (ok) { scene = readBinary(bytes, &error); ok = bool(scene); }
      if (!ok) snprintf(message, sizeof(message), "%s", error.toUtf8().constData());
      if (!scene) scene = std::make_shared<VectorScene>();
      scene->credit = "Xastir world map (WU2Z)";
    } else if (source == "naturalearth") {
      QTemporaryDir directory;
      ok = directory.isValid();
      const QStringList names = {"ne_50m_admin_0_countries_lakes", "ne_50m_coastline", "ne_50m_lakes", "ne_50m_rivers_lake_centerlines"};
      for (int layer = 0; layer < names.size() && ok; ++layer) {
        const QString base = names[layer];
        for (const auto &suffix : {".shp", ".shx", ".dbf"}) {
          QFile input(":/maps/NaturalEarthVector/" + base + suffix);
          QFile output(directory.path() + "/" + base + suffix);
          ok = input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly);
          if (!ok) break;
          const QByteArray bytes = input.readAll();
          ok = output.write(bytes) == bytes.size();
          if (!ok) break;
        }
        QString error;
        if (ok) {
          const auto data = readShapefile(directory.path() + "/" + base + ".shp", layer, &error);
          ok = bool(data);
          if (ok) scene->shapes.append(data->shapes);
          else snprintf(message, sizeof(message), "%s", error.toUtf8().constData());
        }
      }
      scene->credit = "Made with Natural Earth";
    } else ok = false;
    const QString error = QString::fromUtf8(message);
    QMetaObject::invokeMethod(this, [this, source, scene, ok, error] {
      loading_.remove(source);
      if (ok) scenes_.insert(source, scene);
      emit statusChanged(source, ok ? tr("Offline vector map ready") : tr("Offline map unavailable: %1").arg(error.isEmpty() ? tr("Could not read bundled map data") : error));
      emit changed();
    }, Qt::QueuedConnection);
  });
  thread->setParent(this); threads_.append(thread);
  connect(thread, &QThread::finished, this, [this, thread] {
    threads_.removeAll(thread); thread->deleteLater();
  });
  thread->start();
}
void VectorMaps::draw(QPainter &painter, const QString &source, QPointF origin, int zoom,
                      QSize viewport, int layers, bool fills, bool labels, const QRegion &reserved) const
{
  const auto scene = scenes_.value(source);
  if (!scene) return;
  const double scale = std::ldexp(1.0, zoom), world = map::worldSize(zoom);
  const QRectF view(origin / scale, QSizeF(viewport.width()/scale, viewport.height()/scale));
  painter.save(); painter.setRenderHint(QPainter::Antialiasing);
  QRegion occupied = reserved;
  for (const auto &shape : scene->shapes) {
    if (shape.layer < 4 && !(layers & (1 << shape.layer))) continue;
    const QRectF bounds = shape.path.boundingRect();
    if (bounds.bottom() < view.top() || bounds.top() > view.bottom()) continue;
    const int first = int(std::ceil((view.left() - bounds.right()) / 256));
    const int last = int(std::floor((view.right() - bounds.left()) / 256));
    for (int copy = first; copy <= last; ++copy) {
      painter.save(); painter.translate(copy * world - origin.x(), -origin.y()); painter.scale(scale, scale);
      QPen pen(shape.stroke, shape.width); pen.setCosmetic(true); painter.setPen(pen);
      painter.setBrush(fills && shape.fill.isValid() ? QBrush(shape.fill) : QBrush(Qt::NoBrush));
      painter.drawPath(shape.path); painter.restore();
    }
  }
  // Draw text after every polygon, otherwise a neighboring country's fill can
  // cover a label even though label collision detection reserved its rectangle.
  if (labels) for (const auto &shape : scene->shapes) {
    if (shape.layer < 4 && !(layers & (1 << shape.layer))) continue;
    if (labels && !shape.label.isEmpty() && zoom >= 2 && zoom <= 8) {
      QPointF point = shape.labelPoint * scale - origin;
      point.rx() += std::round((viewport.width()/2.0 - point.x()) / world) * world;
      const QRectF box = painter.fontMetrics().boundingRect(shape.label).translated(point.toPoint()).adjusted(-3,-2,3,2);
      if (QRectF(QPointF(0,0), viewport).contains(box) && !occupied.intersects(box.toAlignedRect())) {
        painter.fillRect(box, QColor(255,255,255,200)); painter.setPen(QColor("#334043"));
        painter.drawText(point, shape.label); occupied += box.toAlignedRect();
      }
    }
  }
  painter.restore();
}

std::shared_ptr<VectorScene> VectorMaps::readBinary(const QByteArray &bytes, QString *error)
{
  auto scene = std::make_shared<VectorScene>();
  Builder builder{scene.get(), 4}; char message[256] = "";
  if (!xastir_read_binary_map(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(), appendFeature, &builder, message, sizeof(message))) {
    if (error) *error = QString::fromUtf8(message);
    return {};
  }
  return scene;
}
std::shared_ptr<VectorScene> VectorMaps::readShapefile(const QString &path, int layer, QString *error)
{
  auto scene = std::make_shared<VectorScene>();
  Builder builder{scene.get(), layer}; char message[256] = "";
  if (!xastir_read_shapefile(QFile::encodeName(path).constData(), appendFeature, &builder, message, sizeof(message))) {
    if (error) *error = QString::fromUtf8(message);
    return {};
  }
  return scene;
}
