// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "station_model.h"
#include "station_view.h"
#include <QList>
struct AgwpeSettings {
  QString host;
  quint16 port = 8000;
  QString callsign;
  QByteArray password;
  int deviceIndex = 0;
  bool connectOnStartup = true;
  bool reconnect = true;
  bool transmitEnabled = false;
  quint32 radioPort = 0;
  QString path;
};
struct AprsisSettings {
  QString host = "rotate.aprs2.net";
  quint16 port = 14580;
  QString filter = "r/0/0/100";
  bool enabled = false;
  bool reconnect = true;
  bool transmitEnabled = false;
  QString passcode;
};
struct XastirConfig {
  AprsisSettings aprsis;
  bool transmitDisabled = false;
  bool positionTransmitDisabled = true;
  QString callsign;
  std::optional<Station> localStation;
  QList<AgwpeSettings> agwpe;
  QString autoReplyText;
  std::optional<Selections> selection;
  std::optional<What_to_display> display;
};
bool readXastirConfig(const QString &path, XastirConfig &config, QString *error);
