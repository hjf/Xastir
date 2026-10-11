// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPainterPath>
#include <QColor>
#include <QRegion>
#include <QThread>
#include <QSet>
#include <QHash>
#include <memory>
class QPainter;
struct VectorShape {
  QPainterPath path;
  QString label;
  QPointF labelPoint;
  QColor stroke, fill;
  int width = 1, layer = 0;
};
struct VectorScene { QList<VectorShape> shapes; QString credit; };
class VectorMaps : public QObject
{
  Q_OBJECT
public:
  explicit VectorMaps(QObject *parent = nullptr);
  ~VectorMaps() override;
  static std::shared_ptr<VectorScene> readBinary(const QByteArray &bytes, QString *error = nullptr);
  static std::shared_ptr<VectorScene> readShapefile(const QString &path, int layer, QString *error = nullptr);
  void request(const QString &source);
  bool ready(const QString &source) const { return scenes_.contains(source); }
  void draw(QPainter &painter, const QString &source, QPointF origin, int zoom,
            QSize viewport, int layers, bool fills, bool labels, const QRegion &reserved = {}) const;
signals:
  void changed();
  void statusChanged(const QString &source, const QString &message);
private:
  friend class MapTest;
  QHash<QString, std::shared_ptr<VectorScene>> scenes_;
  QList<QThread *> threads_;
  QSet<QString> loading_;
};
