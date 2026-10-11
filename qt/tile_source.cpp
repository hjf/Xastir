// SPDX-License-Identifier: GPL-2.0-or-later
#include "tile_source.h"
#include "map_projection.h"

#include <QBuffer>
#include <QImageReader>
#include "tile_cache.h"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>

TileSource::TileSource(const TileSourceOptions &options, QObject *parent)
  : QObject(parent), urlTemplate_(options.urlTemplate), authentication_(options.authentication)
{
  clock_.start();
  const QString directory = options.cacheDirectory.isEmpty()
    ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/tiles")
    : options.cacheDirectory;
  auto *cache = new TileCache(directory, &network_);
  network_.setCache(cache);
  updateCacheIdentity();
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
  QString configurationError;
  if (!validate(urlTemplate_, authentication_, &configurationError)) {
    queue_.clear(); emit statusChanged(configurationError); return;
  }
  while (pending_.size() < MaxRequests && !queue_.isEmpty())
  {
    const TileId id = queue_.dequeue();
    QNetworkRequest request{tileUrl(id)};
    if (!authentication_.header.isEmpty())
      request.setRawHeader(authentication_.header, authentication_.prefix + credential().toUtf8());
    if (usesAuthentication())
      request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
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
  if (reply->error() != QNetworkReply::NoError) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // Qt error strings can contain the full URL, including a query/path key.
    if (status == 401 || status == 403) error = tr("Tile server denied access (HTTP %1); check the API key and account permissions.").arg(status);
    else if (status == 429) error = tr("Tile server rate limit exceeded (HTTP 429).");
    else if (reply->error() == QNetworkReply::InsecureRedirectError || reply->error() == QNetworkReply::TooManyRedirectsError)
      error = tr("Tile server redirect was blocked.");
    else if (status >= 400) error = tr("Tile server returned HTTP %1.").arg(status);
    else error = tr("Tile request failed (network error %1).").arg(int(reply->error()));
  }
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
      network_.cache()->remove(tileUrl(id));
    }
  }
  setVisibleTiles(visible);
  emit tileAvailable();
}
void TileSource::setUrlTemplate(const QString &url)
{
  configure(url, {});
}
QString TileSource::credential() const
{
  return authentication_.key.isEmpty() ? qEnvironmentVariable(authentication_.environment.toUtf8().constData()) : authentication_.key;
}
bool TileSource::usesAuthentication() const
{
  return urlTemplate_.contains("{api_key}") || !authentication_.header.isEmpty();
}
QUrl TileSource::tileUrl(const TileId &id) const
{
  QString url = urlTemplate_;
  url.replace("{z}", QString::number(id.zoom)).replace("{x}", QString::number(id.x)).replace("{y}", QString::number(id.y));
  url.replace("{api_key}", QString::fromLatin1(QUrl::toPercentEncoding(credential())));
  return QUrl(url);
}
void TileSource::updateCacheIdentity()
{
  const QByteArray material = usesAuthentication() ? authentication_.header.toLower() + '\0' + authentication_.prefix + credential().toUtf8() : QByteArray();
  static_cast<TileCache *>(network_.cache())->setIdentity(QCryptographicHash::hash(material, QCryptographicHash::Sha256), usesAuthentication() ? credential().toUtf8() : QByteArray());
}
bool TileSource::configure(const QString &url, const TileAuthentication &authentication, QString *error)
{
  if (!validate(url, authentication, error, false)) return false;
  const auto visible = visible_.values();
  setVisibleTiles({});
  urlTemplate_ = url; authentication_ = authentication;
  updateCacheIdentity();
  images_.clear(); retryAfter_.clear(); lastError_.clear();
  setVisibleTiles(visible);
  emit tileAvailable(); return true;
}
bool TileSource::validate(const QString &url, const TileAuthentication &authentication, QString *error, bool requireKey)
{
  auto fail = [error](const QString &message) { if (error) *error = message; return false; };
  QString expanded = url;
  expanded.replace("{z}", "0").replace("{x}", "0").replace("{y}", "0").replace("{api_key}", "key");
  const QUrl parsed(expanded);
  if (!url.contains("{z}") || !url.contains("{x}") || !url.contains("{y}") || expanded.contains('{') || expanded.contains('}') ||
      !parsed.isValid() || !(parsed.scheme() == "https" || parsed.scheme() == "http" || parsed.isLocalFile()) ||
      (!parsed.isLocalFile() && parsed.host().isEmpty()) || !parsed.userInfo().isEmpty() || !parsed.fragment().isEmpty())
    return fail(tr("Enter a valid XYZ URL containing {z}, {x}, {y}, and optionally {api_key}."));
  const bool authenticated = url.contains("{api_key}") || !authentication.header.isEmpty();
  // Credentials may occupy a path or query component, never the URL authority.
  if (QUrl(url).authority().contains("api_key")) return fail(tr("API keys must appear in the URL path or query, not the server name."));
  if (authenticated && parsed.scheme() != "https" &&
      !(parsed.scheme() == "http" && (parsed.host() == "localhost" || QHostAddress(parsed.host()).isLoopback())))
    return fail(tr("API-keyed tile sources require HTTPS (except local loopback servers)."));
  if (!authentication.header.isEmpty()) {
    static const QRegularExpression token(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    const QByteArray name = authentication.header.toLower();
    if (!token.match(QString::fromLatin1(authentication.header)).hasMatch() || name == "host" || name == "content-length" ||
        name == "transfer-encoding" || name == "connection" || name.startsWith("proxy-") || name == "cookie" || name == "accept" || name == "user-agent")
      return fail(tr("Enter a valid API-key header name, such as X-API-Key or Authorization."));
  }
  static const QRegularExpression environmentName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
  if (authenticated && !authentication.environment.isEmpty() && !environmentName.match(authentication.environment).hasMatch())
    return fail(tr("Enter a valid environment variable name."));
  const QString key = authentication.key.isEmpty() ? qEnvironmentVariable(authentication.environment.toUtf8().constData()) : authentication.key;
  static const QRegularExpression controls(QStringLiteral("[\\x00-\\x1f\\x7f]"));
  if (authenticated && (key.size() > 4096 || controls.match(key).hasMatch() || controls.match(QString::fromLatin1(authentication.prefix)).hasMatch()))
    return fail(tr("The API key or header prefix contains invalid characters."));
  if (authenticated && requireKey && key.isEmpty()) return fail(tr("Tile API key is missing. Enter it in Map Chooser or set the configured environment variable."));
  return true;
}
