// SPDX-License-Identifier: GPL-2.0-or-later
#include "agwpe_client.h"
#include "aprs_decoder.h"
#include <QNetworkProxy>
#include <QSignalBlocker>
#include <QtEndian>
#include <algorithm>
#include <QRegularExpression>

AgwpeClient::AgwpeClient(AgwpeSettings settings, StationModel *stations, QObject *parent)
  : QObject(parent), settings_(std::move(settings)), stations_(stations)
{
  socket_.setProxy(QNetworkProxy::NoProxy);
  retry_.setSingleShot(true); timeout_.setSingleShot(true);
  connect(&retry_, &QTimer::timeout, this, &AgwpeClient::connectToServer);
  connect(&timeout_, &QTimer::timeout, this, [this] { fail(tr("Connection timed out")); });
  connect(&socket_, &QTcpSocket::connected, this, [this] {
    timeout_.stop(); retryDelay_ = 2000;
    socket_.setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    if (!settings_.password.isEmpty()) {
      QByteArray login(510, '\0');
      const QByteArray call = settings_.callsign.section('-', 0, 0).toUpper().toLatin1().left(254);
      const QByteArray password = settings_.password.left(254);
      std::copy(call.begin(), call.end(), login.begin());
      std::copy(password.begin(), password.end(), login.begin() + 255);
      send('P', login);
    }
    send('R'); send('G'); send('k');
    emit statusChanged(tr("AGWPE %1:%2 — connected, receiving").arg(settings_.host).arg(settings_.port));
  });
  connect(&socket_, &QTcpSocket::readyRead, this, &AgwpeClient::receive);
  connect(&socket_, &QTcpSocket::errorOccurred, this, [this] { fail(socket_.errorString()); });
  connect(&socket_, &QTcpSocket::disconnected, this, [this] { if (running_) fail(tr("Disconnected")); });
}
AgwpeClient::~AgwpeClient()
{
  socket_.disconnect(this); socket_.abort();
}
void AgwpeClient::start()
{
  if (running_) return;
  running_ = true; retryDelay_ = 2000; connectToServer();
}
void AgwpeClient::stop()
{
  running_ = false; retry_.stop(); timeout_.stop(); input_.clear();
  { const QSignalBlocker blocker(&socket_); socket_.abort(); }
  emit statusChanged(tr("AGWPE stopped"));
}
void AgwpeClient::connectToServer()
{
  if (!running_) return;
  input_.clear();
  emit statusChanged(tr("AGWPE %1:%2 — connecting").arg(settings_.host).arg(settings_.port));
  timeout_.start(10000); socket_.connectToHost(settings_.host, settings_.port);
}
void AgwpeClient::fail(const QString &reason)
{
  timeout_.stop(); input_.clear();
  { const QSignalBlocker blocker(&socket_); socket_.abort(); }
  if (!running_) return;
  emit statusChanged(tr("AGWPE: %1%2").arg(reason, settings_.reconnect ? tr(" — retrying") : QString()));
  if (settings_.reconnect) {
    retry_.start(retryDelay_); retryDelay_ = std::min(30000, retryDelay_ * 2);
  }
}
void AgwpeClient::send(char kind, const QByteArray &payload)
{
  QByteArray frame(36, '\0'); frame[4] = kind;
  qToLittleEndian<quint32>(quint32(payload.size()), frame.data() + 28);
  socket_.write(frame + payload);
}
void AgwpeClient::receive()
{
  if (processing_ || !running_) return;
  if (input_.size() + socket_.bytesAvailable() > 1024 * 1024) { fail(tr("Receive buffer exceeded limit")); return; }
  input_ += socket_.readAll();
  processing_ = true;
  int offset = 0, frames = 0;
  while (input_.size() - offset >= 36 && frames++ < 128) {
    const auto length = qFromLittleEndian<quint32>(input_.constData() + offset + 28);
    if (length > 65536) { processing_ = false; fail(tr("Invalid AGWPE frame length")); return; }
    if (input_.size() - offset < 36 + int(length)) break;
    const char kind = input_[offset + 4];
    const QByteArray payload = input_.mid(offset + 36, length);
    offset += 36 + int(length);
    if (kind != 'K') continue;
    const QByteArray packet = decodeAgwpeAx25(payload);
    if (packet.isEmpty()) continue;
    ++packets_; emit packetReceived(packet);
    const auto update = decodeAprs(packet);
    if (update && stations_) {
      if (update->removed) stations_->remove(update->station.callsign, update->station.kind);
      else stations_->upsert(update->station);
      ++updates_;
    }
  }
  input_.remove(0, offset); processing_ = false;
  if (offset) emit statusChanged(tr("AGWPE %1:%2 — receiving (%3 packets, %4 positions)")
    .arg(settings_.host).arg(settings_.port).arg(packets_).arg(updates_));
  if (frames > 128 && input_.size() >= 36) QTimer::singleShot(0, this, &AgwpeClient::receive);
}

bool AgwpeClient::transmit(const QByteArray &information, const QString &path, QString *error)
{
  auto reject = [error](const QString &reason) { if (error) *error = reason; return false; };
  if (!settings_.transmitEnabled) return reject(tr("Transmission is disabled for this AGWPE device."));
  if (!connected()) return reject(tr("AGWPE is not connected."));
  const QRegularExpression callPattern(QStringLiteral("^[A-Z0-9]{1,6}(-([0-9]|1[0-5]))?$"));
  const QString from = settings_.callsign.toUpper();
  if (!callPattern.match(from).hasMatch()) return reject(tr("Configure a valid AX.25 station callsign."));
  if (information.isEmpty() || information.size() > 256 || information.contains('\r')
      || information.contains('\n') || information.contains('\0'))
    return reject(tr("Invalid APRS information field."));
  const QString route = (path.isNull() ? settings_.path : path).trimmed().toUpper();
  const QStringList digis = route.isEmpty() ? QStringList{} : route.split(',');
  if (digis.size() > 8) return reject(tr("An AX.25 path may contain at most eight digipeaters."));
  QByteArray payload;
  if (!digis.isEmpty()) {
    payload += char(digis.size());
    for (const auto &digi : digis) {
      const QString call = digi.trimmed();
      if (!callPattern.match(call).hasMatch()) return reject(tr("Invalid digipeater in path."));
      payload += call.toLatin1().leftJustified(10, '\0');
    }
  }
  payload += information;
  QByteArray frame(36, '\0');
  qToLittleEndian<quint32>(settings_.radioPort, frame.data());
  frame[4] = digis.isEmpty() ? 'M' : 'V'; frame[6] = char(0xf0);
  const QByteArray source = from.toLatin1();
  std::copy(source.begin(), source.end(), frame.begin() + 8);
  const QByteArray destination("APXQT1");
  std::copy(destination.begin(), destination.end(), frame.begin() + 18);
  qToLittleEndian<quint32>(quint32(payload.size()), frame.data() + 28);
  if (socket_.bytesToWrite() > 65536) return reject(tr("AGWPE transmit buffer is full."));
  if (socket_.write(frame + payload) != frame.size() + payload.size())
    return reject(tr("Cannot queue the packet to AGWPE."));
  return true;
}
