#pragma once

#include <QObject>

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

signals:
    void volumeChanged();
    void mutedChanged();

private:
    PaVolumeControllerPrivate *d;
};
