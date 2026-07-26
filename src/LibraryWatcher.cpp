#include "LibraryWatcher.h"

#include "AudioFormats.h"
#include "LibraryDb.h"
#include "MusicLibrary.h"
#include "ScannerHelpers.h"
#include "SqlUtils.h"
#include "Track.h"

#include <QDir>
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

namespace {

constexpr int kDebounceMs = 350;

QStringList listAudioFilesIn(const QString &dir) {
    QStringList out;
    QDir d(dir);
    if (!d.exists()) return out;
    const auto entries = d.entryInfoList(
        AudioFormats::nameFilters(),
        QDir::Files | QDir::NoSymLinks);
    out.reserve(entries.size());
    for (const auto &e : entries) out.append(e.absoluteFilePath());
    return out;
}

bool appendDbPathsInDir(QSqlDatabase &db, const QString &dir, QSet<QString> &out) {
    QString prefix = dir;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT path FROM tracks WHERE path LIKE ? ESCAPE '\\'"));
    q.addBindValue(SqlUtils::prefixPattern(prefix));
    if (!q.exec()) return false;
    while (q.next()) {
        const QString p = q.value(0).toString();
        if (p.lastIndexOf(QLatin1Char('/')) == prefix.size() - 1) {
            out.insert(p);
        }
    }
    return true;
}

bool loadDbPathsUnder(QSqlDatabase &db, const QString &root, QSet<QString> &out) {
    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT path FROM tracks WHERE path LIKE ? ESCAPE '\\'"));
    q.addBindValue(SqlUtils::prefixPattern(prefix));
    if (!q.exec()) return false;
    while (q.next()) out.insert(q.value(0).toString());
    return true;
}

struct ReconcileResult {
    bool changed = false;
    bool success = false;
    bool retry = false;
    QStringList newSubdirsToWatch;
};

struct PreparedTrack {
    Track track;
    qint64 fileSize = 0;
};

struct PreparedTracks {
    QHash<QString, PreparedTrack> tracks;
    QSet<QString> attemptedPaths;
};

PreparedTracks prepareAdditions(const QSet<QString> &diskFiles,
                                const QSet<QString> &dbFiles,
                                const std::atomic_bool &stopping) {
    PreparedTracks result;
    for (const QString &path : diskFiles) {
        if (stopping.load(std::memory_order_relaxed)) break;
        if (dbFiles.contains(path)) continue;
        result.attemptedPaths.insert(path);
        PreparedTrack prepared;
        if (MusicLibrary::readTrackFromFile(path, prepared.track, prepared.fileSize))
            result.tracks.insert(path, prepared);
    }
    return result;
}

struct ApplyResult {
    bool changed = false;
    bool success = true;
    bool retry = false;
};

ApplyResult applyPreparedDiff(QSqlDatabase &db,
                              const QSet<QString> &diskFiles,
                              const QSet<QString> &dbFiles,
                              const PreparedTracks &prepared) {
    ApplyResult result;
    QStringList removals;
    for (const QString &p : dbFiles) if (!diskFiles.contains(p)) removals.append(p);
    QStringList additions;
    for (const QString &p : diskFiles) if (!dbFiles.contains(p)) additions.append(p);
    constexpr int kMaxChangesPerTransaction = 250;

    for (const QString &path : additions) {
        if (!prepared.attemptedPaths.contains(path)) {
            result.success = false;
            result.retry = true;
            return result;
        }
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));
    int changesProcessed = 0;
    for (const QString &path : removals) {
        if (changesProcessed >= kMaxChangesPerTransaction) {
            result.retry = true;
            break;
        }
        del.bindValue(0, path);
        if (!del.exec()) {
            result.success = false;
            return result;
        }
        result.changed = result.changed || del.numRowsAffected() > 0;
        ++changesProcessed;
    }

    ScannerHelpers::TrackInserter inserter(db);
    for (const QString &path : additions) {
        if (changesProcessed >= kMaxChangesPerTransaction) {
            result.retry = true;
            break;
        }
        const auto it = prepared.tracks.constFind(path);
        if (it == prepared.tracks.constEnd()) continue;
        if (!inserter.insert(it->track, it->fileSize)) {
            if (!inserter.hasError()) continue;
            result.success = false;
            return result;
        }
        result.changed = true;
        ++changesProcessed;
    }
    if (!removals.isEmpty()) MusicLibrary::pruneOrphanArtists(db);
    return result;
}

bool beginWriteIfCurrent(QSqlDatabase &db,
                         quint64 expectedGeneration,
                         const std::atomic<quint64> &generation) {
    QSqlQuery begin(db);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) return false;
    if (generation.load(std::memory_order_relaxed) == expectedGeneration) return true;
    db.rollback();
    return false;
}

ReconcileResult reconcileDirsBlocking(QSqlDatabase &db,
                                      const QSet<QString> &dirs,
                                      quint64 expectedGeneration,
                                      const std::atomic<quint64> &generation,
                                      const std::atomic_bool &stopping) {
    ReconcileResult result;

    QSet<QString> diskFiles;
    QSet<QString> dbSnapshot;

    QSet<QString> visited;
    QStringList queue;
    queue.reserve(dirs.size());
    for (const QString &d : dirs) queue.append(d);

    while (!queue.isEmpty()) {
        if (stopping.load(std::memory_order_relaxed)) return result;
        const QString dir = queue.takeFirst();
        if (visited.contains(dir)) continue;
        visited.insert(dir);

        QDir qdir(dir);
        if (qdir.exists()) {
            const auto subs = qdir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            for (const auto &s : subs) {
                const QString subPath = s.absoluteFilePath();
                result.newSubdirsToWatch.append(subPath);
                if (!visited.contains(subPath)) queue.append(subPath);
            }
        }

        const QStringList disk = listAudioFilesIn(dir);
        for (const QString &p : disk) diskFiles.insert(p);
        if (!appendDbPathsInDir(db, dir, dbSnapshot)) {
            result.retry = true;
            return result;
        }
    }

    const PreparedTracks prepared = prepareAdditions(diskFiles, dbSnapshot, stopping);
    if (stopping.load(std::memory_order_relaxed)) return result;

    if (!beginWriteIfCurrent(db, expectedGeneration, generation)) {
        result.retry = generation.load(std::memory_order_relaxed) == expectedGeneration;
        return result;
    }
    QSet<QString> currentDbFiles;
    for (const QString &dir : visited) {
        if (!appendDbPathsInDir(db, dir, currentDbFiles)) {
            db.rollback();
            result.retry = true;
            return result;
        }
    }
    const ApplyResult applied = applyPreparedDiff(db, diskFiles, currentDbFiles, prepared);
    if (!applied.success) {
        db.rollback();
        result.retry = applied.retry ||
                       generation.load(std::memory_order_relaxed) == expectedGeneration;
        return result;
    }
    result.changed = applied.changed;
    result.retry = applied.retry;
    result.success = db.commit();
    if (!result.success) {
        db.rollback();
        result.changed = false;
        result.retry = generation.load(std::memory_order_relaxed) == expectedGeneration;
    }
    return result;
}

}

LibraryWatcher::LibraryWatcher(QObject *parent)
    : QObject(parent),
      m_watcher(new QFileSystemWatcher(this)),
      m_debounce(new QTimer(this)) {
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &LibraryWatcher::flushPending);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &LibraryWatcher::onDirectoryChanged);
}

LibraryWatcher::~LibraryWatcher() {
    m_stopping.store(true, std::memory_order_relaxed);
    m_generation.fetch_add(1, std::memory_order_relaxed);
    m_workerPool.clear();
    m_workerPool.waitForDone();
}

QStringList LibraryWatcher::roots() const {
    return QStringList(m_roots.begin(), m_roots.end());
}

void LibraryWatcher::start() {
    const QStringList loaded = loadRoots();
    for (const QString &raw : loaded) {
        const QString r = QDir(raw).absolutePath();
        if (!QDir(r).exists()) continue;
        if (m_roots.contains(r)) continue;
        m_roots.insert(r);

        watchTreeRecursive(r);
        initialReconcileAsync(r);
    }
}

QString LibraryWatcher::attachRoot(const QString &path, bool *retry) {
    if (retry) *retry = false;
    const QString clean = QDir(path).absolutePath();
    if (clean.isEmpty() || !QDir(clean).exists()) return {};

    for (const QString &existing : m_roots) {
        if (clean == existing) return {};
        if (clean.startsWith(existing + QLatin1Char('/'))) return {};
    }
    const QStringList currentRoots(m_roots.begin(), m_roots.end());
    QStringList coveredRoots;
    for (const QString &existing : currentRoots) {
        if (existing.startsWith(clean + QLatin1Char('/'))) coveredRoots.append(existing);
    }

    QSqlDatabase db = QSqlDatabase::database();
    LibraryDb::NonBlockingWrite nonBlocking(db);
    if (!db.transaction()) {
        if (retry) *retry = true;
        return {};
    }
    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("DELETE FROM watch_roots WHERE path = ?"));
    for (const QString &existing : coveredRoots) {
        remove.bindValue(0, existing);
        if (!remove.exec()) {
            db.rollback();
            if (retry) *retry = true;
            return {};
        }
    }
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT OR IGNORE INTO watch_roots (path) VALUES (?)"));
    insert.addBindValue(clean);
    if (!insert.exec() || !db.commit()) {
        db.rollback();
        if (retry) *retry = true;
        return {};
    }

    if (!coveredRoots.isEmpty())
        m_generation.fetch_add(1, std::memory_order_relaxed);

    for (const QString &existing : coveredRoots) {
        unwatchTree(existing);
        m_roots.remove(existing);
    }

    m_roots.insert(clean);
    return clean;
}

LibraryWatcher::RegisterResult LibraryWatcher::registerScannedRoot(const QString &path) {
    const QString requested = QDir(path).absolutePath();
    for (const QString &root : m_roots) {
        if (requested == root || requested.startsWith(root + QLatin1Char('/')))
            return RegisterResult::AlreadyCovered;
    }
    bool retry = false;
    const QString clean = attachRoot(path, &retry);
    if (clean.isEmpty()) return retry ? RegisterResult::Retry : RegisterResult::AlreadyCovered;
    watchTreeRecursive(clean);
    return RegisterResult::Ready;
}

void LibraryWatcher::onDirectoryChanged(const QString &dir) {
    m_pendingDirs.insert(dir);
    m_debounce->start();
}

void LibraryWatcher::removeRoot(const QString &path) {
    const QString clean = QDir(path).absolutePath();
    if (!m_roots.contains(clean)) return;
    m_generation.fetch_add(1, std::memory_order_relaxed);

    unwatchTree(clean);
    m_roots.remove(clean);

    QSet<QString> remaining;
    const QString prefix = clean + QLatin1Char('/');
    for (const QString &p : std::as_const(m_pendingDirs)) {
        if (p != clean && !p.startsWith(prefix)) remaining.insert(p);
    }
    m_pendingDirs = remaining;
}

void LibraryWatcher::clearAll() {
    m_generation.fetch_add(1, std::memory_order_relaxed);
    if (m_debounce->isActive()) m_debounce->stop();
    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty()) m_watcher->removePaths(watched);
    m_roots.clear();
    m_pendingDirs.clear();
}

void LibraryWatcher::rescanAll(const QSet<QString> &excludedRoots) {
    const QStringList current = roots();
    for (const QString &r : current) {
        if (excludedRoots.contains(r)) continue;
        if (!QDir(r).exists()) continue;
        watchTreeRecursive(r);
        initialReconcileAsync(r);
    }
}

void LibraryWatcher::rescanRoot(const QString &root) {
    const QString clean = QDir(root).absolutePath();
    if (!m_roots.contains(clean) || !QDir(clean).exists()) return;
    watchTreeRecursive(clean);
    initialReconcileAsync(clean);
}

void LibraryWatcher::flushPending() {
    if (m_pendingDirs.isEmpty()) return;
    if (m_reconcileRunning) {
        if (!m_debounce->isActive()) m_debounce->start();
        return;
    }
    const QSet<QString> pending = m_pendingDirs;
    m_pendingDirs.clear();
    m_reconcileRunning = true;
    const quint64 generation = m_generation.load(std::memory_order_relaxed);

    m_workerPool.start([this, pending, generation]() {
        QSqlDatabase db;
        const QString connName = LibraryDb::openScopedConnection(QStringLiteral("lw"), db);
        ReconcileResult result;
        if (!connName.isEmpty()) {
            result = reconcileDirsBlocking(db, pending, generation, m_generation, m_stopping);
        } else {
            result.retry = generation == m_generation.load(std::memory_order_relaxed);
        }
        LibraryDb::closeScopedConnection(connName, db);

        QMetaObject::invokeMethod(this, [this, pending, result, generation]() {
            if (generation == m_generation.load(std::memory_order_relaxed)) {
                if (result.success) {
                    for (const QString &sub : result.newSubdirsToWatch) {
                        bool insideRoot = false;
                        for (const QString &r : m_roots) {
                            if (sub == r || sub.startsWith(r + QLatin1Char('/'))) {
                                insideRoot = true;
                                break;
                            }
                        }
                        if (insideRoot) watchTreeRecursive(sub);
                    }
                    if (result.changed) emit libraryChanged();
                    if (result.retry) m_pendingDirs.unite(pending);
                } else if (result.retry) {
                    m_pendingDirs.unite(pending);
                }
            }
            m_reconcileRunning = false;
            if (!m_pendingDirs.isEmpty() && !m_debounce->isActive()) m_debounce->start();
        }, Qt::QueuedConnection);
    });
}

QStringList LibraryWatcher::loadRoots() {
    QStringList out;
    QSqlQuery q;
    if (!q.exec(QStringLiteral("SELECT path FROM watch_roots"))) return out;
    while (q.next()) out.append(q.value(0).toString());
    return out;
}

void LibraryWatcher::watchTreeRecursive(const QString &root) {
    QStringList toAdd;
    if (QDir(root).exists()) toAdd.append(root);
    QDirIterator it(root,
                    QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) toAdd.append(it.next());

    const QStringList already = m_watcher->directories();
    QSet<QString> alreadySet(already.begin(), already.end());
    QStringList filtered;
    filtered.reserve(toAdd.size());
    for (const QString &p : toAdd) {
        if (!alreadySet.contains(p)) filtered.append(p);
    }
    if (!filtered.isEmpty())
        m_watcher->addPaths(filtered);
}

void LibraryWatcher::unwatchTree(const QString &root) {
    const QStringList watched = m_watcher->directories();
    const QString rootSep = root + QLatin1Char('/');
    QStringList toRemove;
    for (const QString &d : watched) {
        if (d == root || d.startsWith(rootSep)) toRemove.append(d);
    }
    if (!toRemove.isEmpty()) m_watcher->removePaths(toRemove);
}

void LibraryWatcher::initialReconcileAsync(const QString &root) {
    if (m_stopping.load(std::memory_order_relaxed)) return;
    const quint64 generation = m_generation.load(std::memory_order_relaxed);
    m_workerPool.start([this, root, generation]() {
        QSqlDatabase db;
        const QString connName = LibraryDb::openScopedConnection(QStringLiteral("lwInit"), db);
        bool changed = false;
        bool retry = false;
        if (!connName.isEmpty()) {
            QSet<QString> diskFiles;
            QDirIterator it(root,
                            AudioFormats::nameFilters(),
                            QDir::Files | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            while (it.hasNext() && !m_stopping.load(std::memory_order_relaxed))
                diskFiles.insert(it.next());

            QSet<QString> dbSnapshot;
            if (!loadDbPathsUnder(db, root, dbSnapshot)) {
                retry = true;
            }
            const PreparedTracks prepared = prepareAdditions(diskFiles, dbSnapshot, m_stopping);

            if (!retry && !m_stopping.load(std::memory_order_relaxed) &&
                beginWriteIfCurrent(db, generation, m_generation)) {
                QSet<QString> currentDbFiles;
                if (!loadDbPathsUnder(db, root, currentDbFiles)) {
                    db.rollback();
                    retry = true;
                } else {
                    const ApplyResult applied =
                        applyPreparedDiff(db, diskFiles, currentDbFiles, prepared);
                    if (!applied.success || !db.commit()) {
                        db.rollback();
                        retry = generation == m_generation.load(std::memory_order_relaxed);
                    } else {
                        changed = applied.changed;
                        retry = applied.retry;
                    }
                }
            } else if (!retry) {
                retry = generation == m_generation.load(std::memory_order_relaxed);
            }
        } else {
            retry = generation == m_generation.load(std::memory_order_relaxed);
        }
        LibraryDb::closeScopedConnection(connName, db);

        QMetaObject::invokeMethod(this, [this, root, generation, changed, retry]() {
            if (generation != m_generation.load(std::memory_order_relaxed)) return;
            if (changed) emit libraryChanged();
            if (retry && m_roots.contains(root))
                QTimer::singleShot(250, this, [this, root]() { initialReconcileAsync(root); });
        }, Qt::QueuedConnection);
    });
}
