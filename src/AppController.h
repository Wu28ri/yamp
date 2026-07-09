#pragma once

#include <QObject>

class PlayerBackend;
class Settings;
class CoverImageProvider;

class AppController : public QObject {
    Q_OBJECT
public:
    AppController(PlayerBackend *backend,
                  Settings *settings,
                  CoverImageProvider *coverProvider,
                  QObject *parent = nullptr);

    void applyInitialSettings();
    void wireSignals();

private:
    PlayerBackend      *m_backend       = nullptr;
    Settings           *m_settings      = nullptr;
    CoverImageProvider *m_coverProvider = nullptr;
};
