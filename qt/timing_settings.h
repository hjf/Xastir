// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

struct TimingSettings {
  int clearSeconds = 3600;
  int aircraftSeconds = 0;
  int deadReckoningSeconds = 600;
  int beaconSeconds = 300;
  int removeSeconds = 7200;
  int trailSegmentMinutes = 30;
  int trailSegmentDegrees = 1;
};
