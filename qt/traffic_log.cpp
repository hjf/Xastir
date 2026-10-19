// SPDX-License-Identifier: GPL-2.0-or-later
#include "traffic_log.h"
#include "aprs_decoder.h"
#include "message_core.h"
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QRegularExpression>
#include <algorithm>

namespace {
constexpr const char *prefixes[] = {"tnc", "internet", "messages", "weather"};
constexpr const char *ids[] = {"PULDNFI010", "PULDNFI011", "Message Logging", "PULDNFI013"};
QString setting(int i) { return QString("logging/%1").arg(prefixes[i]); }
QString appendLog(const QString &directory, int category, const QByteArray &line, const TrafficLogLimits &limits)
{
  if (!QDir().mkpath(directory)) return TrafficLogControl::tr("Could not create log directory: %1").arg(directory);
  QDir dir(directory);
  const auto today = QDateTime::currentDateTimeUtc().date();
  const auto prefix = QString::fromLatin1(prefixes[category]) + '-';
  const auto base = prefix + today.toString(Qt::ISODate);
  const auto path = dir.filePath(base + ".jsonl");
  if (QFileInfo(path).size() + line.size() > limits.fileBytes) {
    for (int i = 3; i >= 1; --i) {
      const auto target = dir.filePath(base + QString("-%1.jsonl").arg(i));
      const auto source = i == 1 ? path : dir.filePath(base + QString("-%1.jsonl").arg(i - 1));
      if (QFile::exists(target) && !QFile::remove(target)) return TrafficLogControl::tr("Could not rotate log: %1").arg(target);
      if (QFile::exists(source) && !QFile::rename(source, target)) return TrafficLogControl::tr("Could not rotate log: %1").arg(source);
    }
  }
  QFile file(path); const bool existed = file.exists();
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return file.errorString();
  if (!existed) file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (file.write(line) != line.size() || !file.flush()) return file.errorString();
  file.close();
  auto files = dir.entryInfoList({prefix + "*.jsonl"}, QDir::Files, QDir::Name);
  const QRegularExpression owned("^" + prefix + "[0-9]{4}-[0-9]{2}-[0-9]{2}(-[1-3])?\\.jsonl$");
  files.erase(std::remove_if(files.begin(), files.end(), [&](const QFileInfo &entry) {
    return !owned.match(entry.fileName()).hasMatch();
  }), files.end());
  std::sort(files.begin(), files.end(), [&](const QFileInfo &a, const QFileInfo &b) {
    const auto first = a.fileName(), second = b.fileName();
    const auto firstDate = first.mid(prefix.size(), 10), secondDate = second.mid(prefix.size(), 10);
    if (firstDate != secondDate) return firstDate < secondDate;
    // Rotations retain their write timestamps. Higher suffixes are older.
    auto rotation = [](const QString &name) { return name.endsWith("-3.jsonl") ? 3 : name.endsWith("-2.jsonl") ? 2 : name.endsWith("-1.jsonl") ? 1 : 0; };
    return rotation(first) > rotation(second);
  });
  qint64 total = 0;
  for (const auto &entry : files) total += entry.size();
  for (const auto &entry : files) {
    const auto date = QDate::fromString(entry.fileName().mid(prefix.size(), 10), Qt::ISODate);
    if (!date.isValid()) continue;
    if (date < today.addDays(-6) || total > limits.categoryBytes) {
      if (!QFile::remove(entry.absoluteFilePath())) return TrafficLogControl::tr("Could not prune log: %1").arg(entry.fileName());
      total -= entry.size();
    }
  }
  return {};
}
}
TrafficLogControl::TrafficLogControl(QMainWindow *window, const XastirConfig &config, TrafficLogLimits limits)
  : QObject(window), worker_(new QObject), limits_(limits)
{
  limits_.fileBytes = qMax<qint64>(8192, limits_.fileBytes);
  limits_.categoryBytes = qMax(limits_.fileBytes * 2, limits_.categoryBytes);
  limits_.pendingRecords = qMax(1, limits_.pendingRecords);
  limits_.pendingBytes = qMax<qint64>(8192, limits_.pendingBytes);
  QSettings settings;
  const auto fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
  directory_ = settings.value("logging/directory", fallback).toString().trimmed();
  if (directory_.isEmpty()) directory_ = fallback;
  worker_->moveToThread(&thread_); connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater); thread_.start();
  connect(this, &TrafficLogControl::errorOccurred, window, [window](const QString &error) { window->statusBar()->showMessage(error, 30000); });
  for (int i = 0; i < 4; ++i) {
    const auto category = Category(1 << i);
    if (settings.value(setting(i), bool(config.loggingMask & category)).toBool()) mask_ |= category;
    actions_[i] = window->findChild<QAction *>(ids[i]);
    if (auto *action = actions_[i]) {
      disconnect(action, nullptr, window, nullptr); action->setProperty("implemented", true);
      action->setChecked(mask_ & category);
      action->setToolTip(tr("Record live RX and queued TX in replayable JSONL files. Configure the directory under Timing."));
      connect(action, &QAction::triggered, this, [this, category](bool value) { setEnabled(category, value); });
    }
  }
  for (const char *id : {"PULDNFI012", "WX Alert Logging"}) if (auto *action = window->findChild<QAction *>(id)) {
    disconnect(action, nullptr, window, nullptr); action->setEnabled(false); action->setProperty("unavailable", true);
    action->setToolTip(tr("Requires the unfinished gateway or weather-alert backend."));
  }
}
TrafficLogControl::~TrafficLogControl() { flush(); thread_.quit(); thread_.wait(); }
void TrafficLogControl::flush() { QMetaObject::invokeMethod(worker_, [] {}, Qt::BlockingQueuedConnection); }
void TrafficLogControl::configure(const QString &directory)
{
  ++generation_; directory_ = directory.trimmed(); QSettings().setValue("logging/directory", directory_);
}
void TrafficLogControl::setEnabled(Category category, bool value)
{
  if (category != Tnc && category != Internet && category != Messages && category != Weather) return;
  ++generation_;
  for (int i = 0; i < 4; ++i) if ((1 << i) == category) {
    if (value) mask_ |= category; else mask_ &= ~category;
    QSettings().setValue(setting(i), value); if (actions_[i]) actions_[i]->setChecked(value);
  }
}
void TrafficLogControl::observe(const QByteArray &packet, int device, qint64 timestamp, bool outgoing)
{
  if (!mask_ || packet.isEmpty() || packet.size() > 4096) return;
  int mask = device == 256 ? Internet : Tnc;
  if (mask_ & Messages) {
    char sender[10]; XastirDecodedMessage decoded{};
    if (!packet.contains('\0') && xastir_decode_message_packet(packet.constData(), sender, sizeof(sender), &decoded)) mask |= Messages;
  }
  if (mask_ & Weather) {
    const auto position = decodeAprs(packet);
    if (decodeAprsWeather(packet) || (position && position->station.weather)) mask |= Weather;
  }
  mask &= mask_; if (!mask) return;
  const auto line = QJsonDocument(QJsonObject{{"timestamp", timestamp}, {"device", device}, {"outgoing", outgoing},
    {"packet", QString::fromLatin1(packet.toBase64())}}).toJson(QJsonDocument::Compact) + '\n';
  if (pending_.load() >= limits_.pendingRecords || pendingBytes_.load() + line.size() > limits_.pendingBytes) {
    ++dropped_; if (dropped_ == 1 || dropped_ % 1000 == 0) emit errorOccurred(tr("Traffic logging queue full; dropped %1 records.").arg(dropped_));
    return;
  }
  ++pending_; pendingBytes_ += line.size(); const auto directory = directory_; const auto generation = generation_;
  QMetaObject::invokeMethod(worker_, [this, line, mask, directory, timestamp, generation] {
    write(line, mask, directory, timestamp, generation); --pending_; pendingBytes_ -= line.size();
  }, Qt::QueuedConnection);
}
void TrafficLogControl::write(const QByteArray &line, int mask, const QString &directory, qint64, quint64 generation)
{
  if (workerGeneration_ != generation) { workerGeneration_ = generation; failedMask_ = 0; }
  for (int i = 0; i < 4; ++i) if ((mask & (1 << i)) && !(failedMask_ & (1 << i))) {
    const auto error = directory.isEmpty() ? tr("Choose a traffic log directory.") : appendLog(directory, i, line, limits_);
    if (!error.isEmpty()) {
      failedMask_ |= 1 << i;
      QMetaObject::invokeMethod(this, [this, i, error, generation] {
        if (generation != generation_) return;
        // Keep this generation: other category failures from the same write
        // must still disable their affected toggles.
        mask_ &= ~(1 << i); QSettings().setValue(setting(i), false);
        if (actions_[i]) actions_[i]->setChecked(false);
        emit errorOccurred(tr("%1 logging disabled: %2").arg(QString::fromLatin1(prefixes[i]), error));
      }, Qt::QueuedConnection);
    }
  }
}
