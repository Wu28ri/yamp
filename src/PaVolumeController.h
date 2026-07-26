#pragma once

#include <QObject>
#include <QString>

struct PaVolumeControllerPrivate;

class PaVolumeController : public QObject {
    Q_OBJECT
public:
    explicit PaVolumeController(QObject *parent = nullptr);
    ~PaVolumeController() override;

    double volume() const;
    bool   isMuted() const;

    void setVolume(double v);
    void setMuted(bool muted);
    void setHardwareDeviceExclusive(const QString &mpvDevice, bool exclusive);
    void setHardwareDeviceVolume(const QString &mpvDevice, double volume, bool muted);

signals:
    void volumeChanged();
    void mutedChanged();
    void hardwareDeviceExclusiveChanged(bool exclusive, bool success);

private:
    PaVolumeControllerPrivate *d;
};
