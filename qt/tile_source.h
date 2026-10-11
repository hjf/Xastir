// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QQueue>
#include <QSet>

class QNetworkReply;

struct TileId
{
  int zoom;
  int x;
  int y;

  bool operator==(const TileId &other) const noexcept
  {
    return zoom == other.zoom && x == other.x && y == other.y;
  }
};

inline size_t qHash(const TileId &id, size_t seed = 0) noexcept
{
  return qHashMulti(seed, id.zoom, id.x, id.y);
}

struct TileSourceOptions
{
  QString urlTemplate = QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
  QString cacheDirectory;
};

// Only requested viewport tiles are queued. HTTP cache metadata is managed by Qt.
class TileSource : public QObject
{
  Q_OBJECT

public:
  explicit TileSource(const TileSourceOptions &options, QObject *parent = nullptr);
  ~TileSource() override;
  const QPixmap *tile(const TileId &id);
  QString urlTemplate() const { return urlTemplate_; }
  void setUrlTemplate(const QString &url);
  void reload(bool flushEntireCache = false);
  void setVisibleTiles(const QList<TileId> &tiles);

signals:
  void tileAvailable();
  void statusChanged(const QString &message);

private:
  void pumpQueue();
  void finishRequest(const TileId &id, QNetworkReply *reply);
  void reportStatus();

  static constexpr int MaxRequests = 6;
  QString urlTemplate_;
  QNetworkAccessManager network_;
  QCache<TileId, QPixmap> images_{64 * 1024}; // KiB; 64 MiB decoded image budget.
  QHash<TileId, QNetworkReply *> pending_;
  QQueue<TileId> queue_;
  QSet<TileId> visible_;
  QHash<TileId, qint64> retryAfter_;
  QElapsedTimer clock_;
  QString lastError_;
};
