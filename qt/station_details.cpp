// SPDX-License-Identifier: GPL-2.0-or-later
#include "station_details.h"
#include "map_widget.h"
#include "message_ui.h"
#include "station_model.h"
#include "station_navigator.h"
#include "station_views.h"
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <cstdlib>
StationDetails::StationDetails(QMainWindow *w, StationModel *s, MapWidget *m, StationNavigator *n,
                               MessageUi *messages, QString own)
    : QObject(w), window_(w), stations_(s), map_(m), navigator_(n), messages_(messages), own_(own)
{
  connect(m, &MapWidget::stationClicked, this, &StationDetails::open);
  connect(s, &StationModel::stationsChanged, this, &StationDetails::refresh);
  auto *views = new StationViews(w, s, n, own, m->preferences());
  connect(views, &StationViews::detailsRequested, this, &StationDetails::open);
  connect(m->preferences(), &DisplayPreferences::changed, this, &StationDetails::refresh);
}
void StationDetails::open(const QString &name)
{
  if (!stations_->record(name))
    return;
  name_ = name;
  if (!dialog_)
  {
    dialog_ = new QDialog(window_);
    dialog_->setObjectName("stationDetails");
    dialog_->resize(540, 400);
    auto *layout = new QVBoxLayout(dialog_);
    text_ = new QLabel(dialog_);
    text_->setObjectName("stationDetailsText");
    text_->setTextFormat(Qt::PlainText);
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *scroll = new QScrollArea(dialog_);
    scroll->setWidgetResizable(true);
    scroll->setWidget(text_);
    text_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(scroll);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    auto add = [&](const QString &label, auto callback)
    {
      auto *b = new QPushButton(label, dialog_);
      buttons->addWidget(b);
      connect(b, &QPushButton::clicked, this, callback);
    };
    add(tr("Send Message"),
        [this]
        {
          const auto *r = stations_->record(name_);
          if (r)
            messages_->openConversation(
                (r->flag & (ST_OBJECT | ST_ITEM)) ? QString::fromLatin1(r->origin) : name_);
        });
    add(tr("Track"), [this] { navigator_->locate(name_, true); });
    add(tr("Show Trail"),
        [this]
        {
          auto d = map_->displayOptions();
          d.trail = 1;
          map_->setDisplayOptions(d);
          navigator_->locate(name_, false);
        });
    add(tr("Close"), [this] { dialog_->close(); });
  }
  refresh();
  dialog_->show();
  dialog_->raise();
  dialog_->activateWindow();
}
void StationDetails::refresh()
{
  if (!dialog_)
    return;
  dialog_->setWindowTitle(tr("Details — %1").arg(name_));
  const auto *r = stations_->record(name_);
  if (!r)
  {
    text_->setText(tr("This station or object is no longer in the database."));
    return;
  }
  const auto station = stations_->stations().value(name_);
  QStringList lines{tr("%1: %2").arg((r->flag & ST_OBJECT) ? tr("Object")
                                     : (r->flag & ST_ITEM) ? tr("Item")
                                                           : tr("Station"),
                                     name_)};
  if (r->origin[0])
    lines << tr("Owner: %1").arg(QString::fromLatin1(r->origin));
  lines << tr("Last heard: %1")
               .arg(QDateTime::fromSecsSinceEpoch(r->sec_heard).toString(Qt::ISODate));
  if (r->position_valid)
    lines << tr("Position: %1").arg(map_->preferences()->position(station.position));
  else
    lines << tr("Position: not yet received");
  if (r->position_valid && stations_->stations().contains(own_))
  {
    const auto local = stations_->stations().value(own_);
    double bearing = 0;
    const double distance =
        xastir_distance(local.position.latitude, local.position.longitude,
                        station.position.latitude, station.position.longitude, &bearing);
    lines << tr("Distance / bearing: %1 / %2°")
                 .arg(map_->preferences()->distance(distance))
                 .arg(bearing, 0, 'f', 0);
  }
  if (r->course[0])
    lines << tr("Course: %1°").arg(QString::fromLatin1(r->course));
  if (r->speed[0])
    lines << tr("Speed: %1").arg(map_->preferences()->measure(std::atof(r->speed), XASTIR_DISPLAY_SPEED));
  if (r->altitude[0])
    lines << tr("Altitude: %1").arg(map_->preferences()->measure(std::atof(r->altitude), XASTIR_DISPLAY_ALTITUDE));
  if (r->node_path_ptr)
    lines << tr("Path: %1").arg(QString::fromLatin1(r->node_path_ptr));
  for (auto *c = r->comment_data; c; c = c->next)
    if (c->text_ptr)
      lines << tr("Comment: %1").arg(QString::fromUtf8(c->text_ptr));
  for (auto *s = r->status_data; s; s = s->next)
    if (s->text_ptr)
      lines << tr("Status (%1): %2")
        .arg(QDateTime::fromSecsSinceEpoch(s->sec_heard).toString(Qt::ISODate),
             QString::fromLatin1(s->text_ptr));
  if (r->weather_data)
  {
    const auto *wx = r->weather_data;
    auto measure = [this](const char *text, XastirDisplayQuantity quantity) {
      bool ok = false; const double value = QByteArray(text).toDouble(&ok);
      return ok ? map_->preferences()->measure(value, quantity, quantity == XASTIR_DISPLAY_PRESSURE ? 2 : 1) : QStringLiteral("—");
    };
    lines << tr("Weather: %1; wind %2 from %3°; gust %4; humidity %5%; pressure %6")
                 .arg(measure(wx->wx_temp, XASTIR_DISPLAY_TEMPERATURE), measure(wx->wx_speed, XASTIR_DISPLAY_WIND),
                      QString::fromLatin1(wx->wx_course), measure(wx->wx_gust, XASTIR_DISPLAY_WIND),
                      QString::fromLatin1(wx->wx_hum), measure(wx->wx_baro, XASTIR_DISPLAY_PRESSURE));
  }
  text_->setText(lines.join('\n'));
}
