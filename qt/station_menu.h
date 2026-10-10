// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "station_model.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <functional>
class QMainWindow;
class MapWidget;
class StationMenu : public QObject {
  Q_OBJECT
public:
  StationMenu(QMainWindow *window, StationModel *stations, MapWidget *map,
              QString callsign, std::optional<Station> local, QString historyDirectory = {});
  void setLocalStation(std::optional<Station> local) { local_ = std::move(local); }
  bool exportKml(const QString &path, QString *error = nullptr) const;
  bool reloadObjects(const QString &path, QString *error = nullptr);
  bool reloadTactical(const QString &path, QString *error = nullptr);
  int importFindu(const QByteArray &data, const QString &callsign, QString *error = nullptr);
private:
  void fetchTrail();
  void showError(const QString &error);
  void clearHistory(const QString &name, bool tactical);
  void confirm(const QString &title, const QString &text, std::function<void()> action);
  QMainWindow *window_;
  StationModel *stations_;
  MapWidget *map_;
  QString callsign_, historyDirectory_;
  std::optional<Station> local_;
  QNetworkAccessManager network_;
};
