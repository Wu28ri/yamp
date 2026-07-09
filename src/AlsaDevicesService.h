#pragma once

#include <QString>
#include <QVariantList>

namespace AlsaDevices {

QVariantList list();
bool lockToZeroDb(const QString &mpvDeviceName);

}
