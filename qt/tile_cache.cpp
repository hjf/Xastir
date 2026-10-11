// SPDX-License-Identifier: GPL-2.0-or-later
#include "tile_cache.h"
#include <QCryptographicHash>
#include <QUrl>
TileCache::TileCache(const QString &directory, QObject *parent) : QAbstractNetworkCache(parent)
{
  disk_.setCacheDirectory(directory);
  disk_.setMaximumCacheSize(256 * 1024 * 1024);
}
QUrl TileCache::storageUrl(const QUrl &url) const
{
  const auto hash = QCryptographicHash::hash(identity_ + '\0' + url.toEncoded(), QCryptographicHash::Sha256).toHex();
  return QUrl(QStringLiteral("https://xastir-tile-cache.invalid/") + QString::fromLatin1(hash));
}
QNetworkCacheMetaData TileCache::storageMetadata(QNetworkCacheMetaData metadata) const
{
  metadata.setUrl(storageUrl(metadata.url()));
  // Preserve HTTP freshness/validation metadata, excluding redirects, cookies,
  // or other arbitrary response headers that might echo authentication data.
  QNetworkCacheMetaData::RawHeaderList headers;
  for (const auto &header : metadata.rawHeaders()) {
    const auto name = header.first.toLower();
    if (!secret_.isEmpty() && (header.second.contains(secret_) || header.second.contains(QUrl::toPercentEncoding(QString::fromUtf8(secret_))))) continue;
    if (name == "content-type" || name == "cache-control" || name == "expires" || name == "date" ||
        name == "last-modified" || name == "etag" || name == "age" || name == "vary") headers.append(header);
  }
  metadata.setRawHeaders(headers);
  auto attributes = metadata.attributes();
  attributes.remove(QNetworkRequest::RedirectionTargetAttribute);
  attributes.remove(QNetworkRequest::HttpReasonPhraseAttribute);
  metadata.setAttributes(attributes);
  return metadata;
}
QNetworkCacheMetaData TileCache::metaData(const QUrl &url)
{
  auto metadata = disk_.metaData(storageUrl(url));
  if (metadata.isValid()) metadata.setUrl(url);
  return metadata;
}
void TileCache::updateMetaData(const QNetworkCacheMetaData &metadata) { disk_.updateMetaData(storageMetadata(metadata)); }
QIODevice *TileCache::data(const QUrl &url) { return disk_.data(storageUrl(url)); }
bool TileCache::remove(const QUrl &url) { return disk_.remove(storageUrl(url)); }
QIODevice *TileCache::prepare(const QNetworkCacheMetaData &metadata)
{
  // A sanitized redirect cannot be replayed faithfully without Location.
  if (metadata.attributes().value(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 300) return nullptr;
  return disk_.prepare(storageMetadata(metadata));
}
