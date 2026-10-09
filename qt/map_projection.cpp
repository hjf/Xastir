// SPDX-License-Identifier: GPL-2.0-or-later
#include "map_projection.h"

#include <algorithm>
#include <cmath>

namespace map
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
}

double worldSize(int zoom)
{
  return std::ldexp(double(TileSize), zoom);
}

QPointF constrainCenter(QPointF point, int zoom)
{
  const double size = worldSize(zoom);
  double x = std::fmod(point.x(), size);
  if (x < 0)
    x += size;
  return {x, std::clamp(point.y(), 0.0, size)};
}

QPointF toWorld(Coordinate coordinate, int zoom)
{
  const double size = worldSize(zoom);
  const double latitude = std::clamp(coordinate.latitude, -MaxLatitude, MaxLatitude);
  const double radians = latitude * Pi / 180.0;
  return constrainCenter({(coordinate.longitude + 180.0) / 360.0 * size,
                         (1.0 - std::asinh(std::tan(radians)) / Pi) / 2.0 * size}, zoom);
}

Coordinate fromWorld(QPointF point, int zoom)
{
  const double size = worldSize(zoom);
  point = constrainCenter(point, zoom);
  return {std::atan(std::sinh(Pi * (1.0 - 2.0 * point.y() / size))) * 180.0 / Pi,
          point.x() / size * 360.0 - 180.0};
}

int wrapTileX(int x, int zoom)
{
  const int count = 1 << zoom;
  const int wrapped = x % count;
  return wrapped < 0 ? wrapped + count : wrapped;
}
}
