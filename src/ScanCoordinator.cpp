#include "ScanCoordinator.h"

#include "LibraryWatcher.h"
#include "ScanSession.h"

#include <QDir>
#include <QTimer>

ScanCoordinator::ScanCoordinator(LibraryWatcher *watcher, QObject *parent)
    : QObject(parent), m_watcher(watcher) {}

QSet<QString> ScanCoordinator::activeRoots() const {
    QSet<QString> roots;
    for (ScanSession *session : m_progresses.keys()) {
        if (!session->cancelled()) roots.insert(QDir(session->rootPath()).absolutePath());
    }
    return roots;
}

void ScanCoordinator::startScan(const QString &folderPath) {
    if (folderPath.isEmpty()) return;
    const QString clean = QDir(folderPath).absolutePath();
    const quint64 generation = m_startGenerations.value(clean) + 1;
    m_startGenerations.insert(clean, generation);
    startScanAttempt(clean, generation);
}

void ScanCoordinator::startScanAttempt(const QString &folderPath, quint64 generation) {
    if (m_startGenerations.value(folderPath) != generation) return;
    if (m_watcher) {
        const auto registration = m_watcher->registerScannedRoot(folderPath);
        if (registration == LibraryWatcher::RegisterResult::Retry) {
            QTimer::singleShot(250, this, [this, folderPath, generation]() {
                startScanAttempt(folderPath, generation);
            });
            return;
        }
        if (registration == LibraryWatcher::RegisterResult::AlreadyCovered) return;
    }

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

    connect(session, &ScanSession::finished, this,
            [this, session](const QString &path, const QList<Track> &newTracks, bool success) {
                if (!session->cancelled() && success) emit newTracksAvailable(newTracks);
                if (session->failed() && m_watcher) m_watcher->rescanRoot(path);
                m_progresses.remove(session);
                recomputeTotals();
            });

    session->start();
}

void ScanCoordinator::cancelAll() {
    for (auto it = m_startGenerations.begin(); it != m_startGenerations.end(); ++it)
        ++it.value();
    for (ScanSession *session : m_progresses.keys()) session->cancel();
}

void ScanCoordinator::cancelRoot(const QString &folderPath) {
    const QString root = QDir(folderPath).absolutePath();
    const QString prefix = root + QLatin1Char('/');
    for (auto it = m_startGenerations.begin(); it != m_startGenerations.end(); ++it) {
        if (it.key() == root || it.key().startsWith(prefix)) ++it.value();
    }
    for (ScanSession *session : m_progresses.keys()) {
        const QString sessionRoot = QDir(session->rootPath()).absolutePath();
        if (sessionRoot == root || sessionRoot.startsWith(prefix)) session->cancel();
    }
}

void ScanCoordinator::recomputeTotals() {
    int progress = 0;
    int total    = 0;
    for (auto it = m_progresses.cbegin(); it != m_progresses.cend(); ++it) {
        progress += it.value().first;
        total    += it.value().second;
    }
    const bool isActive = active();
    if (progress == m_progressCached && total == m_totalCached &&
        isActive == m_activeCached) return;
    m_progressCached = progress;
    m_totalCached    = total;
    m_activeCached   = isActive;
    emit progressChanged();
}
