#pragma once

#include "Track.h"

#include <QList>
#include <QObject>
#include <QString>

class QThread;
class LibraryScanner;

class ScanSession : public QObject {
    Q_OBJECT
public:
    explicit ScanSession(const QString &path, QObject *parent = nullptr);
    ~ScanSession() override;

    int processed() const { return m_processed; }
    int total() const { return m_total; }

    void start();

signals:
    void progressChanged();
    void batchReady();
    void finished(const QString &path, const QList<Track> &newTracks);

private:
    QString m_rootPath;
    int m_processed = 0;
    int m_total     = 0;

    QThread        *m_thread  = nullptr;
    LibraryScanner *m_scanner = nullptr;
};
