// SPDX-License-Identifier: GPL-2.0-or-later
#include "tile_source.h"
#include "map_projection.h"

#include <QBuffer>
#include <QImageReader>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>

TileSource::TileSource(const TileSourceOptions &options, QObject *parent)
  : QObject(parent), urlTemplate_(options.urlTemplate)
{
  clock_.start();
  const QString directory = options.cacheDirectory.isEmpty()
    ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles")
    : options.cacheDirectory;
  auto *cache = new QNetworkDiskCache(&network_);
  cache->setCacheDirectory(directory);
  cache->setMaximumCacheSize(256 * 1024 * 1024);
  network_.setCache(cache);
}

TileSource::~TileSource()
{
  // Stop callbacks before the network manager and the other members are destroyed.
  for (QNetworkReply *reply : pending_)
  {
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
  }
}

const QPixmap *TileSource::tile(const TileId &id)
{
  return images_.object(id);
}

void TileSource::setVisibleTiles(const QList<TileId> &tiles)
{
  visible_ = QSet<TileId>(tiles.begin(), tiles.end());
  // Disconnect before aborting: abort() can synchronously emit finished().
  for (auto it = pending_.begin(); it != pending_.end();)
  {
    if (!visible_.contains(it.key()))
    {
      QNetworkReply *reply = it.value();
      disconnect(reply, nullptr, this, nullptr);
      it = pending_.erase(it);
      reply->abort();
      reply->deleteLater();
    }
    else
      ++it;
  }
  for (auto it = retryAfter_.begin(); it != retryAfter_.end();)
  {
    if (!visible_.contains(it.key()))
      it = retryAfter_.erase(it);
    else
      ++it;
  }
  queue_.clear();
  QSet<TileId> queued;
  for (const TileId &id : tiles)
  {
    if (!tile(id) && !pending_.contains(id) && !queued.contains(id)
        && retryAfter_.value(id, 0) <= clock_.elapsed())
    {
      queue_.enqueue(id);
      queued.insert(id);
    }
  }
  pumpQueue();
}

void TileSource::pumpQueue()
{
  while (pending_.size() < MaxRequests && !queue_.isEmpty())
  {
    const TileId id = queue_.dequeue();
    QString url = urlTemplate_;
    url.replace(QStringLiteral("{z}"), QString::number(id.zoom));
    url.replace(QStringLiteral("{x}"), QString::number(id.x));
    url.replace(QStringLiteral("{y}"), QString::number(id.y));
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "XastirQt/0.1 (+https://github.com/Xastir/Xastir)");
    request.setRawHeader("Accept", "image/png");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setTransferTimeout(15000);
    QNetworkReply *reply = network_.get(request);
    pending_.insert(id, reply);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
      finishRequest(id, reply);
    });
  }
  reportStatus();
}

void TileSource::finishRequest(const TileId &id, QNetworkReply *reply)
{
  pending_.remove(id);
  QString error;
  if (reply->error() != QNetworkReply::NoError)
    error = reply->errorString();
  else
  {
    QByteArray bytes = reply->readAll();
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png");
    // This renderer uses standard 256-pixel XYZ raster tiles.
    if (reader.size() != QSize(map::TileSize, map::TileSize))
      error = tr("The tile server returned an invalid map image");
    else
    {
      const QImage image = reader.read();
      if (image.isNull())
        error = tr("Could not decode a map tile: %1").arg(reader.errorString());
      else
      {
        auto *pixmap = new QPixmap(QPixmap::fromImage(image));
        const int cost = int((image.sizeInBytes() + 1023) / 1024);
        images_.insert(id, pixmap, cost);
        retryAfter_.remove(id);
        emit tileAvailable();
      }
    }
  }
  if (!error.isEmpty())
  {
    lastError_ = error;
    retryAfter_.insert(id, clock_.elapsed() + 30000);
    // Discard malformed cached responses so a later retry can replace them.
    if (network_.cache())
      network_.cache()->remove(reply->url());
  }
  reply->deleteLater();
  QTimer::singleShot(0, this, &TileSource::pumpQueue);
}

void TileSource::reportStatus()
{
  if (!retryAfter_.isEmpty())
    emit statusChanged(tr("Some map tiles are unavailable: %1").arg(lastError_));
  else if (!pending_.isEmpty() || !queue_.isEmpty())
    emit statusChanged(tr("Loading map tiles (%1 remaining)…").arg(pending_.size() + queue_.size()));
  else
    emit statusChanged(tr("Map ready"));
}

void TileSource::reload(bool flushEntireCache)
{
  const auto visible = visible_.values();
  setVisibleTiles({});
  images_.clear(); retryAfter_.clear(); lastError_.clear();
  if (network_.cache()) {
    if (flushEntireCache) network_.cache()->clear();
    else for (const auto &id : visible) {
      QString url = urlTemplate_;
      url.replace("{z}", QString::number(id.zoom));
      url.replace("{x}", QString::number(id.x));
      url.replace("{y}", QString::number(id.y));
      network_.cache()->remove(QUrl(url));
    }
  }
  setVisibleTiles(visible);
  emit tileAvailable();
}
void TileSource::setUrlTemplate(const QString &url)
{
  if (urlTemplate_ == url) return;
  const auto visible = visible_.values();
  setVisibleTiles({});
  urlTemplate_ = url;
  images_.clear(); retryAfter_.clear(); lastError_.clear();
  setVisibleTiles(visible);
  emit tileAvailable();
}
