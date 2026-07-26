#pragma once

#include <QString>
#include <QVariantList>

namespace AlsaDevices {

QVariantList list();
bool hardwareVolume(const QString &mpvDeviceName, qreal &volume, bool &muted);
bool setHardwareVolume(const QString &mpvDeviceName, qreal volume);
bool setHardwareMuted(const QString &mpvDeviceName, bool muted);

}
