// SPDX-License-Identifier: GPL-2.0-or-later
#include "radio_interface.h"
#include "ax25_core.h"
#include "message_core.h"
QByteArray RadioInterface::prepareFrame(const QByteArray &information, const QString &route, QString *error) const
{
  auto reject = [error](const QString &reason) { if (error) *error = reason; return QByteArray{}; };
  if (blocked_) return reject(tr("All transmission is disabled."));
  if (!transmitEnabled()) return reject(tr("Transmission is disabled for this radio interface."));
  if (!connected()) return reject(tr("Radio interface is not connected."));
  if (information.isEmpty() || information.size() > 256 || information.contains('\r') || information.contains('\n') || information.contains('\0'))
    return reject(tr("Invalid APRS information field."));
  const auto source = callsign().toLatin1(), via = route.toLatin1();
  if (QString::fromLatin1(source) != callsign() || QString::fromLatin1(via) != route || source.contains('\0') || via.contains('\0')
      || !xastir_message_valid_path(via.constData()))
    return reject(tr("Invalid AX.25 callsign or path."));
  QByteArray frame(XASTIR_AX25_MAX_FRAME, Qt::Uninitialized);
  const auto size = xastir_ax25_build(source.constData(), "APXQT1", via.constData(),
    reinterpret_cast<const unsigned char *>(information.constData()), size_t(information.size()),
    reinterpret_cast<unsigned char *>(frame.data()), size_t(frame.size()));
  if (!size) return reject(tr("Invalid AX.25 callsign or path (at most eight digipeaters)."));
  frame.resize(qsizetype(size)); return frame;
}
QByteArray RadioInterface::tnc2(const QByteArray &frame)
{
  QByteArray packet(XASTIR_AX25_MAX_FRAME + 128, Qt::Uninitialized);
  const auto size = xastir_ax25_to_tnc2(reinterpret_cast<const unsigned char *>(frame.constData()), size_t(frame.size()),
    reinterpret_cast<unsigned char *>(packet.data()), size_t(packet.size()));
  packet.resize(qsizetype(size)); return packet;
}
