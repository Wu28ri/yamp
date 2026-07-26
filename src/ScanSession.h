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
    QString rootPath() const { return m_rootPath; }
    bool cancelled() const { return m_cancelled; }
    bool failed() const { return m_failed; }

    void start();
    void cancel();

signals:
    void progressChanged();
    void batchReady();
    void finished(const QString &path, const QList<Track> &newTracks, bool success);

private:
    QString m_rootPath;
    int m_processed = 0;
    int m_total     = 0;
    bool m_cancelled = false;
    bool m_failed = false;

    QThread        *m_thread  = nullptr;
    LibraryScanner *m_scanner = nullptr;
};
