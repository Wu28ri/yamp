#pragma once

#include <QObject>

struct PwVolumeControllerPrivate;

class PwVolumeController : public QObject {
    Q_OBJECT
public:
    explicit PwVolumeController(QObject *parent = nullptr);
    ~PwVolumeController() override;

    // user-facing 0.0..1.0 values (cubic-mapped to linear internally,
    // matching the curve pavucontrol uses for display)
    double volume() const;
    bool   isMuted() const;

    void setVolume(double v);
    void setMuted(bool muted);

signals:
    void volumeChanged();
    void mutedChanged();

private:
    PwVolumeControllerPrivate *d;
};
