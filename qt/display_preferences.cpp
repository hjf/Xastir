// SPDX-License-Identifier: GPL-2.0-or-later
#include "display_preferences.h"
#include "aprs_position.h"
#include <QSettings>
#include <cmath>

DisplayPreferences::DisplayPreferences(QObject *parent) : QObject(parent) { restore({}); }
void DisplayPreferences::restore(const PresentationSettings &defaults)
{
  QSettings settings;
  auto value = defaults;
  bool ok = false;
  const int coordinate = settings.value("display/coordinateSystem", defaults.coordinateSystem).toInt(&ok);
  value.coordinateSystem = ok && coordinate >= 0 && coordinate <= 2 ? coordinate : defaults.coordinateSystem;
  auto flag = [&settings](const char *key, bool fallback) {
    const auto text = settings.value(key, fallback).toString().toLower();
    return text == "true" || text == "1" ? true : text == "false" || text == "0" ? false : fallback;
  };
  value.englishUnits = flag("display/englishUnits", defaults.englishUnits);
  value.distanceBearing = flag("display/distanceBearing", defaults.distanceBearing);
  setValue(value);
}
bool DisplayPreferences::save(const PresentationSettings &value)
{
  if (value.coordinateSystem < 0 || value.coordinateSystem > 2) return false;
  QSettings settings;
  settings.setValue("display/coordinateSystem", value.coordinateSystem);
  settings.setValue("display/englishUnits", value.englishUnits);
  settings.setValue("display/distanceBearing", value.distanceBearing);
  settings.sync();
  if (settings.status() != QSettings::NoError) return false;
  setValue(value); return true;
}
void DisplayPreferences::setValue(PresentationSettings value)
{
  if (value.coordinateSystem < 0 || value.coordinateSystem > 2) value.coordinateSystem = 0;
  if (value.coordinateSystem == value_.coordinateSystem && value.englishUnits == value_.englishUnits
      && value.distanceBearing == value_.distanceBearing) return;
  value_ = value; emit changed();
}
QString DisplayPreferences::angle(double degrees, bool latitude) const
{
  if (!std::isfinite(degrees) || std::abs(degrees) > (latitude ? 90 : 180)) return QStringLiteral("—");
  const int formats[] = {CONVERT_DEC_DEG, CONVERT_HP_NORMAL_FORMATED, CONVERT_DMS_NORMAL_FORMATED};
  char text[64];
  if (latitude) xastir_convert_lat_l2s(std::lround((90 - degrees) * 360000), text, sizeof(text), formats[value_.coordinateSystem]);
  else xastir_convert_lon_l2s(std::lround((180 + degrees) * 360000), text, sizeof(text), formats[value_.coordinateSystem]);
  return QString::fromLatin1(text).trimmed();
}
QString DisplayPreferences::latitude(double value) const { return angle(value, true); }
QString DisplayPreferences::longitude(double value) const { return angle(value, false); }
QString DisplayPreferences::position(map::Coordinate coordinate) const { return latitude(coordinate.latitude) + ", " + longitude(coordinate.longitude); }
double DisplayPreferences::convert(double value, XastirDisplayQuantity quantity) const { return xastir_display_value(value, quantity, value_.englishUnits); }
QString DisplayPreferences::unit(XastirDisplayQuantity quantity) const { return QString::fromLatin1(xastir_display_unit(quantity, value_.englishUnits)); }
QString DisplayPreferences::measure(double value, XastirDisplayQuantity quantity, int precision) const
{
  if (!std::isfinite(value)) return QStringLiteral("—");
  return QString(quantity == XASTIR_DISPLAY_TEMPERATURE ? "%1°%2" : "%1 %2")
    .arg(convert(value, quantity), 0, 'f', precision).arg(unit(quantity));
}
QString DisplayPreferences::distance(double meters, int precision, bool adaptive) const
{
  return measure(meters, adaptive && convert(meters, XASTIR_DISPLAY_DISTANCE) < .99
    ? XASTIR_DISPLAY_SHORT_DISTANCE : XASTIR_DISPLAY_DISTANCE, adaptive && convert(meters, XASTIR_DISPLAY_DISTANCE) < .99 ? 0 : precision);
}
