// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <optional>

// Reuses the original Xastir artwork and palette, independently of X11/Motif.
class AprsSymbols
{
public:
  AprsSymbols();
  QImage image(QChar table, QChar code, std::optional<double> course = std::nullopt);
  bool contains(QChar table, QChar code) const;

private:
  struct Entry
  {
    QImage image;
    bool directional;
  };
  QHash<QString, Entry> symbols_;
  QCache<QString, QImage> rendered_{1024};
};
