#pragma once

#include "Track.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTimer>

class ScanSession;
class LibraryWatcher;

class ScanCoordinator : public QObject {
    Q_OBJECT
public:
    explicit ScanCoordinator(LibraryWatcher *watcher, QObject *parent = nullptr);

    bool active() const { return !m_progresses.isEmpty(); }
    int  progress() const { return m_progressCached; }
    int  total() const { return m_totalCached; }

    void startScan(const QString &folderPath);

signals:
    void progressChanged();
    void newTracksAvailable(const QList<Track> &tracks);
    void batchReady();
    void scanFinished();

private:
    void recomputeTotals();

    LibraryWatcher *m_watcher = nullptr;
    QHash<ScanSession*, QPair<int, int>> m_progresses;
    int m_progressCached = 0;
    int m_totalCached    = 0;
};
