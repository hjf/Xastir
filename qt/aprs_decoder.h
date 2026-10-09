// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "station_model.h"
struct AprsUpdate { Station station; bool removed = false; };
std::optional<AprsUpdate> decodeAprs(const QByteArray &packet, int depth = 0);
QByteArray decodeAgwpeAx25(const QByteArray &payload);
