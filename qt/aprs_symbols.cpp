// SPDX-License-Identifier: GPL-2.0-or-later
#include "aprs_symbols.h"

#include <QFile>
#include <QPainter>
#include <QTransform>
#include <cmath>
#include <stdexcept>

namespace
{
QRgb color(char pixel)
{
  // Palette indices from draw_symbols.c, with X11 RGB values from main.c.
  switch (pixel)
  {
    case 'q': return qRgb(0x00, 0x00, 0x00);
    case 'm': return qRgb(0xff, 0xff, 0xff);
    case 'c': return qRgb(0xcc, 0xcc, 0xcc);
    case 'j': return qRgb(0xee, 0x00, 0x00);
    case 'h': return qRgb(0x00, 0xbf, 0xff);
    case 'l': return qRgb(0x00, 0x00, 0xcd);
    case 'k': return qRgb(0x00, 0xcd, 0x00);
    case 'g': return qRgb(0x00, 0x00, 0x8b);
    case '#': return qRgb(0xff, 0xff, 0x00);
    case 'p': return qRgb(0x45, 0x45, 0x45);
    case 'i': return qRgb(0x00, 0x64, 0x00);
    case 'n': return qRgb(0x87, 0x87, 0x87);
    case 'a': return qRgb(0xcd, 0x66, 0x00);
    case 'o': return qRgb(0x59, 0x59, 0x59);
    case 'f': return qRgb(0xcd, 0x33, 0x33);
    case 'b': return qRgb(0xa0, 0x20, 0xf0);
    case 'e': return qRgb(0xff, 0x40, 0x40);
    case 'd': return qRgb(0xcd, 0x00, 0x00);
    case 'r': return qRgb(0x32, 0xcd, 0x32);
    default: return qRgba(0, 0, 0, 0);
  }
}

QString key(QChar table, QChar code)
{
  return QString(table) + code;
}
}

AprsSymbols::AprsSymbols()
{
  // Ensure the resource object is linked when this code lives in a static library.
  Q_INIT_RESOURCE(aprs_symbols);
  QFile file(QStringLiteral(":/aprs/symbols.dat"));
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Embedded APRS symbol artwork is missing");
  QChar table;
  while (!file.atEnd())
  {
    const QByteArray line = file.readLine();
    if (line.startsWith("TABLE ") && line.size() > 6)
      table = QChar::fromLatin1(line[6]);
    else if (line.startsWith("APRS ") && line.size() > 5)
    {
      QImage image(20, 20, QImage::Format_ARGB32_Premultiplied);
      for (int y = 0; y < 20; ++y)
      {
        const QByteArray row = file.readLine();
        if (row.size() < 20)
          throw std::runtime_error("Embedded APRS symbol artwork is malformed");
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < 20; ++x)
          pixels[x] = color(row[x]);
      }
      symbols_.insert(key(table, QChar::fromLatin1(line[5])),
                      {image, line.size() > 19 && line[19] == 'l'});
    }
  }
}

bool AprsSymbols::contains(QChar table, QChar code) const
{
  if (table != '/' && table != '\\')
    table = '\\';
  return symbols_.contains(key(table, code));
}

QImage AprsSymbols::image(QChar table, QChar code, std::optional<double> course)
{
  QChar overlay;
  if (table != '/' && table != '\\')
  {
    overlay = table;
    table = '\\';
  }
  auto it = symbols_.constFind(key(table, code));
  if (it == symbols_.cend())
    it = symbols_.constFind(QStringLiteral("!#")); // Xastir's unknown-symbol artwork.
  if (it == symbols_.cend())
    return {};
  const int rotation = it->directional && course && std::isfinite(*course)
    ? (int(std::round(*course / 90.0)) * 90 + 90) % 360 : 0;
  const QString cacheKey = key(table, code) + overlay + QString::number(rotation);
  if (const QImage *cached = rendered_.object(cacheKey))
    return *cached;
  QImage rendered = rotation == 0 ? it->image
    : it->image.transformed(QTransform().rotate(rotation), Qt::FastTransformation);
  const auto overlayIt = symbols_.constFind(key('#', overlay));
  if (overlayIt != symbols_.cend())
  {
    QPainter painter(&rendered);
    painter.drawImage(0, 0, overlayIt->image);
  }
  rendered_.insert(cacheKey, new QImage(rendered));
  return rendered;
}
