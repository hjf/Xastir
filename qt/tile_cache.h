// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QAbstractNetworkCache>
#include <QNetworkDiskCache>

// Qt's HTTP cache stores URL metadata. Use opaque URLs on disk, including a
// credential identity so header-authenticated accounts never share entries.
class TileCache : public QAbstractNetworkCache
{
public:
  TileCache(const QString &directory, QObject *parent);
  void setIdentity(const QByteArray &identity, const QByteArray &secret) { identity_ = identity; secret_ = secret; }
  QNetworkCacheMetaData metaData(const QUrl &url) override;
  void updateMetaData(const QNetworkCacheMetaData &metadata) override;
  QIODevice *data(const QUrl &url) override;
  bool remove(const QUrl &url) override;
  qint64 cacheSize() const override { return disk_.cacheSize(); }
  QIODevice *prepare(const QNetworkCacheMetaData &metadata) override;
  void insert(QIODevice *device) override { disk_.insert(device); }
  void clear() override { disk_.clear(); }
private:
  QUrl storageUrl(const QUrl &url) const;
  QNetworkCacheMetaData storageMetadata(QNetworkCacheMetaData metadata) const;
  QByteArray identity_, secret_;
  QNetworkDiskCache disk_;
};
