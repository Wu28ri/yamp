#pragma once

#include "Track.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

class ScanSession;
class LibraryWatcher;

class ScanCoordinator : public QObject {
    Q_OBJECT
public:
    explicit ScanCoordinator(LibraryWatcher *watcher, QObject *parent = nullptr);

    bool active() const { return !m_progresses.isEmpty(); }
    int  progress() const { return m_progressCached; }
    int  total() const { return m_totalCached; }
    QSet<QString> activeRoots() const;

    void startScan(const QString &folderPath);
    void cancelAll();
    void cancelRoot(const QString &folderPath);

signals:
    void progressChanged();
    void newTracksAvailable(const QList<Track> &tracks);
    void batchReady();

private:
    void startScanAttempt(const QString &folderPath, quint64 generation);
    void recomputeTotals();

    LibraryWatcher *m_watcher = nullptr;
    QHash<ScanSession*, QPair<int, int>> m_progresses;
    QHash<QString, quint64> m_startGenerations;
    int m_progressCached = 0;
    int m_totalCached    = 0;
    bool m_activeCached  = false;
};
