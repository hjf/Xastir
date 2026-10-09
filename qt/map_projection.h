// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QPointF>

namespace map
{
constexpr int TileSize = 256;
constexpr int MinZoom = 0;
constexpr int MaxZoom = 19;
constexpr double MaxLatitude = 85.0511287798066;

struct Coordinate
{
  double latitude;
  double longitude;
};

double worldSize(int zoom);
QPointF toWorld(Coordinate coordinate, int zoom);
Coordinate fromWorld(QPointF point, int zoom);
QPointF constrainCenter(QPointF point, int zoom);
int wrapTileX(int x, int zoom);
}
