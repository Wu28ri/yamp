#include "ScanCoordinator.h"

#include "LibraryWatcher.h"
#include "ScanSession.h"

ScanCoordinator::ScanCoordinator(LibraryWatcher *watcher, QObject *parent)
    : QObject(parent), m_watcher(watcher) {}

void ScanCoordinator::startScan(const QString &folderPath) {
    if (folderPath.isEmpty()) return;

    auto *session = new ScanSession(folderPath, this);
    m_progresses.insert(session, qMakePair(0, 0));
    recomputeTotals();

    connect(session, &ScanSession::progressChanged, this, [this, session]() {
        auto it = m_progresses.find(session);
        if (it == m_progresses.end()) return;
        it->first  = session->processed();
        it->second = session->total();
        recomputeTotals();
    });

    connect(session, &ScanSession::batchReady, this, &ScanCoordinator::batchReady);

    if (m_watcher) m_watcher->registerScannedRoot(folderPath);

    connect(session, &ScanSession::finished, this,
            [this, session](const QString &, const QList<Track> &newTracks) {
                emit newTracksAvailable(newTracks);
                m_progresses.remove(session);
                recomputeTotals();
                emit scanFinished();
            });

    session->start();
}

void ScanCoordinator::recomputeTotals() {
    int progress = 0;
    int total    = 0;
    for (auto it = m_progresses.cbegin(); it != m_progresses.cend(); ++it) {
        progress += it.value().first;
        total    += it.value().second;
    }
    if (progress == m_progressCached && total == m_totalCached) return;
    m_progressCached = progress;
    m_totalCached    = total;
    emit progressChanged();
}
