#include "LibraryWatcher.h"

#include "AudioFormats.h"
#include "LibraryDb.h"
#include "MusicLibrary.h"
#include "ScannerHelpers.h"
#include "SqlUtils.h"
#include "Track.h"

#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSocketNotifier>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace {

constexpr int kDebounceMs = 350;

bool isWithin(const QString &path, const QString &root) {
    return path == root || path.startsWith(root.endsWith(QLatin1Char('/'))
                                          ? root : root + QLatin1Char('/'));
}

struct FileState {
    qint64 size = 0;
    qint64 modifiedTime = 0;

    bool operator==(const FileState &) const = default;
};

using FileStates = QHash<QString, FileState>;

bool loadDbFilesUnder(QSqlDatabase &db, const QString &root, FileStates &out) {
    const auto bounds = SqlUtils::pathPrefixBounds(root);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT path, file_size, file_mtime FROM tracks WHERE path >= ? AND path < ?"));
    q.addBindValue(bounds.first);
    q.addBindValue(bounds.second);
    if (!q.exec()) return false;
    while (q.next()) {
        out.insert(q.value(0).toString(),
                   FileState{q.value(1).toLongLong(), q.value(2).toLongLong()});
    }
    return true;
}

struct ReconcileResult {
    bool changed = false;
    bool success = false;
    bool retry = false;
    QSet<QString> newSubdirsToWatch;
};

struct PreparedTrack {
    Track track;
    qint64 fileSize = 0;
    qint64 modifiedTime = 0;
};

struct PreparedTracks {
    QHash<QString, PreparedTrack> tracks;
    QSet<QString> attemptedPaths;
};

PreparedTracks prepareWrites(const FileStates &diskFiles,
                             const FileStates &dbFiles,
                             const std::atomic_bool &stopping) {
    PreparedTracks result;
    for (auto it = diskFiles.cbegin(); it != diskFiles.cend(); ++it) {
        if (stopping.load(std::memory_order_relaxed)) break;
        const QString &path = it.key();

        const auto dbIt = dbFiles.constFind(path);
        if (dbIt != dbFiles.constEnd() && dbIt.value() == it.value()) continue;

        result.attemptedPaths.insert(path);
        PreparedTrack prepared;
        if (MusicLibrary::readTrackFromFile(path, prepared.track, prepared.fileSize,
                                            prepared.modifiedTime)) {
            result.tracks.insert(path, prepared);
        }
    }
    return result;
}

struct ApplyResult {
    bool changed = false;
    bool success = true;
    bool retry = false;
};

ApplyResult applyPreparedDiff(QSqlDatabase &db,
                              const FileStates &diskFiles,
                              const FileStates &dbFiles,
                              const PreparedTracks &prepared) {
    ApplyResult result;
    QStringList removals;
    for (auto it = dbFiles.cbegin(); it != dbFiles.cend(); ++it) {
        if (!diskFiles.contains(it.key())) removals.append(it.key());
    }
    QStringList writes;
    for (auto it = diskFiles.cbegin(); it != diskFiles.cend(); ++it) {
        const auto dbIt = dbFiles.constFind(it.key());
        if (dbIt == dbFiles.constEnd() || dbIt.value() != it.value())
            writes.append(it.key());
    }

    for (const QString &path : writes) {
        if (!prepared.attemptedPaths.contains(path)) {
            result.success = false;
            result.retry = true;
            return result;
        }
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));
    for (const QString &path : removals) {
        del.bindValue(0, path);
        if (!del.exec()) {
            result.success = false;
            return result;
        }
        result.changed = result.changed || del.numRowsAffected() > 0;
    }

    ScannerHelpers::TrackInserter inserter(db);
    for (const QString &path : writes) {
        const auto it = prepared.tracks.constFind(path);
        if (it == prepared.tracks.constEnd()) {
            if (dbFiles.contains(path)) {
                del.bindValue(0, path);
                if (del.exec() && del.numRowsAffected() > 0) {
                    result.changed = true;
                }
            }
            continue;
        }

        const QFileInfo current(path);
        const FileState currentState{current.size(),
                                     current.lastModified().toMSecsSinceEpoch()};
        if (!current.exists() || !current.isFile() ||
            currentState != diskFiles.value(path)) {
            result.retry = true;
            continue;
        }

        const auto writeResult = inserter.upsert(
            it->track, it->fileSize, it->modifiedTime);
        if (writeResult == ScannerHelpers::TrackWriteResult::Error) {
            result.success = false;
            return result;
        }
        result.changed = true;
    }

    if (result.changed) MusicLibrary::pruneOrphanArtists(db);
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
    FileStates diskFiles;
    FileStates dbSnapshot;

    QStringList sortedDirs = dirs.values();
    std::sort(sortedDirs.begin(), sortedDirs.end());
    QStringList effectiveDirs;
    for (const QString &d : sortedDirs) {
        bool covered = false;
        for (const QString &parent : effectiveDirs) {
            if (d.startsWith(parent + QLatin1Char('/'))) {
                covered = true;
                break;
            }
        }
        if (!covered) effectiveDirs.append(d);
    }

    for (const QString &dir : effectiveDirs) {
        if (stopping.load(std::memory_order_relaxed)) return result;
        QDir qdir(dir);
        if (qdir.exists()) {
            result.newSubdirsToWatch.insert(dir);
            QDirIterator entries(dir,
                                 QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                 QDirIterator::Subdirectories);
            while (entries.hasNext()) {
                if (stopping.load(std::memory_order_relaxed)) return result;
                entries.next();
                const QFileInfo info = entries.fileInfo();
                if (info.isDir()) {
                    result.newSubdirsToWatch.insert(info.filePath());
                } else if (info.isFile() && AudioFormats::isAudioFile(info.fileName())) {
                    diskFiles.insert(info.absoluteFilePath(),
                                     FileState{info.size(),
                                               info.lastModified().toMSecsSinceEpoch()});
                }
            }
        }
        if (!loadDbFilesUnder(db, dir, dbSnapshot)) {
            result.retry = true;
            return result;
        }
    }

    const PreparedTracks prepared = prepareWrites(diskFiles, dbSnapshot, stopping);
    if (stopping.load(std::memory_order_relaxed)) return result;

    if (!beginWriteIfCurrent(db, expectedGeneration, generation)) {
        result.retry = generation.load(std::memory_order_relaxed) == expectedGeneration;
        return result;
    }

    FileStates currentDbFiles;
    for (const QString &dir : effectiveDirs) {
        if (!loadDbFilesUnder(db, dir, currentDbFiles)) {
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
    m_workerPool.setMaxThreadCount(1);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &LibraryWatcher::flushPending);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &LibraryWatcher::onDirectoryChanged);
#ifdef Q_OS_LINUX
    m_inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotifyFd >= 0) {
        m_inotifyNotifier = new QSocketNotifier(m_inotifyFd, QSocketNotifier::Read, this);
        connect(m_inotifyNotifier, &QSocketNotifier::activated,
                this, &LibraryWatcher::drainInotifyEvents);
    }
#endif
}

LibraryWatcher::~LibraryWatcher() {
    m_stopping.store(true, std::memory_order_relaxed);
    m_generation.fetch_add(1, std::memory_order_relaxed);
    if (m_debounce->isActive()) m_debounce->stop();
    m_workerPool.clear();
    m_workerPool.waitForDone();
#ifdef Q_OS_LINUX
    if (m_inotifyNotifier) {
        m_inotifyNotifier->setEnabled(false);
    }
    if (m_inotifyFd >= 0) {
        ::close(m_inotifyFd);
        m_inotifyFd = -1;
    }
#endif
}

QStringList LibraryWatcher::roots() const {
    return QStringList(m_roots.begin(), m_roots.end());
}

void LibraryWatcher::start() {
    const QStringList loaded = loadRoots();
    QStringList initialTopDirs;
    for (const QString &raw : loaded) {
        const QString r = QDir(raw).absolutePath();
        if (!QDir(r).exists()) continue;
        if (m_roots.contains(r)) continue;
        m_roots.insert(r);
        initialTopDirs.append(r);
        m_initialWatchPending.insert(r);
        m_pendingDirs.insert(r);
    }
    if (!initialTopDirs.isEmpty()) {
        watchDirectories(initialTopDirs);
    }
    if (!m_pendingDirs.isEmpty()) {
        flushPending();
    }
}

QString LibraryWatcher::attachRoot(const QString &path, bool *retry) {
    if (retry) *retry = false;
    const QString clean = QDir(path).absolutePath();
    if (clean.isEmpty() || !QDir(clean).exists()) return {};

    for (const QString &existing : m_roots) {
        if (isWithin(clean, existing)) return {};
    }
    const QStringList currentRoots(m_roots.begin(), m_roots.end());
    QStringList coveredRoots;
    for (const QString &existing : currentRoots) {
        if (isWithin(existing, clean)) coveredRoots.append(existing);
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
        if (isWithin(requested, root))
            return RegisterResult::AlreadyCovered;
    }
    bool retry = false;
    const QString clean = attachRoot(path, &retry);
    if (clean.isEmpty()) return retry ? RegisterResult::Retry : RegisterResult::AlreadyCovered;
    watchDirectories({clean});
    scheduleWatchTreeAsync(clean);
    return RegisterResult::Ready;
}

void LibraryWatcher::onDirectoryChanged(const QString &dir) {
    if (QDir(dir).exists()) {
        m_pendingDirs.insert(dir);
    } else {
        for (const QString &root : m_roots) {
            if (isWithin(dir, root)) {
                m_pendingDirs.insert(dir);
                break;
            }
        }
    }
    m_debounce->start();
}

void LibraryWatcher::removeRoot(const QString &path) {
    const QString clean = QDir(path).absolutePath();
    if (!m_roots.contains(clean)) return;
    m_generation.fetch_add(1, std::memory_order_relaxed);

    unwatchTree(clean);
    m_roots.remove(clean);
    m_initialWatchPending.remove(clean);

    QSqlDatabase db = QSqlDatabase::database();
    LibraryDb::NonBlockingWrite nonBlocking(db);
    if (db.transaction()) {
        QSqlQuery remove(db);
        remove.prepare(QStringLiteral("DELETE FROM watch_roots WHERE path = ?"));
        remove.bindValue(0, clean);
        if (remove.exec()) {
            db.commit();
        } else {
            db.rollback();
        }
    }

    m_pendingDirs.removeIf([&clean](const QString &dir) { return isWithin(dir, clean); });
}

void LibraryWatcher::clearAll() {
    m_generation.fetch_add(1, std::memory_order_relaxed);
    if (m_debounce->isActive()) m_debounce->stop();
    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty()) m_watcher->removePaths(watched);
    clearInotifyWatches();
    m_roots.clear();
    m_initialWatchPending.clear();
    m_pendingDirs.clear();
}

void LibraryWatcher::rescanAll(const QSet<QString> &excludedRoots) {
    const QStringList current = roots();
    QStringList topDirs;
    for (const QString &r : current) {
        if (excludedRoots.contains(r)) continue;
        if (!QDir(r).exists()) continue;
        topDirs.append(r);
        m_initialWatchPending.insert(r);
        m_pendingDirs.insert(r);
    }
    if (!topDirs.isEmpty()) {
        watchDirectories(topDirs);
    }
    if (!m_pendingDirs.isEmpty()) {
        flushPending();
    }
}

void LibraryWatcher::rescanRoot(const QString &root) {
    const QString clean = QDir(root).absolutePath();
    if (!m_roots.contains(clean) || !QDir(clean).exists()) return;
    watchDirectories({clean});
    m_initialWatchPending.insert(clean);
    m_pendingDirs.insert(clean);
    flushPending();
}

void LibraryWatcher::flushPending() {
    if (m_pendingDirs.isEmpty()) return;
    if (m_reconcileRunning) {
        if (!m_debounce->isActive()) m_debounce->start();
        return;
    }
    QStringList dirs(m_pendingDirs.begin(), m_pendingDirs.end());
    std::sort(dirs.begin(), dirs.end(), [](const QString &left, const QString &right) {
        return left.size() < right.size();
    });
    QSet<QString> pending;
    for (const QString &dir : dirs) {
        if (std::none_of(pending.cbegin(), pending.cend(), [&dir](const QString &parent) {
                return isWithin(dir, parent);
            })) pending.insert(dir);
    }
    QSet<QString> initialCovered;
    for (const QString &dir : pending) {
        if (m_initialWatchPending.contains(dir)) {
            initialCovered.insert(dir);
            m_initialWatchPending.remove(dir);
        }
    }
    m_pendingDirs.clear();
    m_reconcileRunning = true;
    const quint64 generation = m_generation.load(std::memory_order_relaxed);

    m_workerPool.start([this, pending, initialCovered, generation]() {
        QSqlDatabase db;
        const QString connName = LibraryDb::openScopedConnection(QStringLiteral("lw"), db);
        ReconcileResult result;
        if (!connName.isEmpty()) {
            result = reconcileDirsBlocking(db, pending, generation, m_generation, m_stopping);
        } else {
            result.retry = generation == m_generation.load(std::memory_order_relaxed);
        }
        LibraryDb::closeScopedConnection(connName, db);

        QMetaObject::invokeMethod(this, [this, pending, initialCovered, result, generation]() {
            if (generation == m_generation.load(std::memory_order_relaxed)) {
                QStringList directories;
                if (result.success) {
                    for (const QString &sub : result.newSubdirsToWatch) {
                        if (std::any_of(m_roots.cbegin(), m_roots.cend(), [&sub](const QString &root) {
                                return isWithin(sub, root);
                            })) directories.append(sub);
                    }
                }
                const QStringList newlyWatched = watchDirectories(directories);
                for (const QString &added : newlyWatched) {
                    const bool fromInitialWalk = std::any_of(
                        initialCovered.cbegin(), initialCovered.cend(),
                        [&added](const QString &initRoot) { return isWithin(added, initRoot); });
                    if (!fromInitialWalk) m_pendingDirs.insert(added);
                }
                if (result.success) {
                    if (result.changed) emit libraryChanged();
                    if (result.retry) {
                        m_initialWatchPending.unite(initialCovered);
                        m_pendingDirs.unite(pending);
                    }
                } else if (result.retry) {
                    m_initialWatchPending.unite(initialCovered);
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

QStringList LibraryWatcher::watchDirectories(const QStringList &paths) {
    const QStringList already = m_watcher->directories();
    QSet<QString> alreadySet(already.begin(), already.end());
    QStringList stale;
    for (const QString &path : already) {
        if (!QDir(path).exists()) {
            stale.append(path);
            alreadySet.remove(path);
        }
    }
    if (!stale.isEmpty()) {
        m_watcher->removePaths(stale);
        removeInotifyWatches(stale);
    }
    QStringList filtered;
    for (const QString &path : paths) {
        if (!alreadySet.contains(path) && QDir(path).exists()) filtered.append(path);
    }
    if (!filtered.isEmpty()) {
        const QStringList failed = m_watcher->addPaths(filtered);
        if (!failed.isEmpty()) {
            qWarning() << "[LibraryWatcher] could not watch directories" << failed;
            const QSet<QString> failedSet(failed.begin(), failed.end());
            filtered.removeIf([&failedSet](const QString &path) {
                return failedSet.contains(path);
            });
        }
        addInotifyWatches(filtered);
    }
    return filtered;
}

void LibraryWatcher::scheduleWatchTreeAsync(const QString &root) {
    QPointer<LibraryWatcher> guard(this);
    const quint64 expectedGen = m_generation.load(std::memory_order_relaxed);
    QThreadPool::globalInstance()->start([this, guard, root, expectedGen]() {
        if (!guard || m_stopping.load(std::memory_order_relaxed)) return;
        QStringList paths;
        if (QDir(root).exists()) paths.append(root);
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (!guard || m_stopping.load(std::memory_order_relaxed) ||
                m_generation.load(std::memory_order_relaxed) != expectedGen) {
                return;
            }
            paths.append(it.next());
        }
        if (!guard || paths.isEmpty()) return;
        QMetaObject::invokeMethod(guard, [this, guard, root, expectedGen, paths = std::move(paths)]() {
            if (!guard || m_stopping.load(std::memory_order_relaxed)) return;
            if (m_generation.load(std::memory_order_relaxed) != expectedGen) return;
            if (!m_roots.contains(root)) return;
            watchDirectories(paths);
        }, Qt::QueuedConnection);
    });
}

void LibraryWatcher::unwatchTree(const QString &root) {
    const QStringList watched = m_watcher->directories();
    QStringList toRemove;
    for (const QString &d : watched) {
        if (isWithin(d, root)) toRemove.append(d);
    }
    if (!toRemove.isEmpty()) m_watcher->removePaths(toRemove);
    removeInotifyWatches(toRemove);
}

void LibraryWatcher::addInotifyWatches(const QStringList &paths) {
#ifdef Q_OS_LINUX
    if (m_inotifyFd < 0) return;
    constexpr uint32_t kMask = IN_CLOSE_WRITE | IN_ATTRIB | IN_MOVED_TO |
                               IN_DELETE_SELF | IN_MOVE_SELF;
    for (const QString &dir : paths) {
        if (m_dirToWd.contains(dir)) continue;
        const QByteArray encoded = dir.toUtf8();
        const int wd = ::inotify_add_watch(m_inotifyFd, encoded.constData(), kMask);
        if (wd >= 0) {
            m_dirToWd.insert(dir, wd);
            m_wdToDir.insert(wd, dir);
        }
    }
#else
    Q_UNUSED(paths);
#endif
}

void LibraryWatcher::removeInotifyWatches(const QStringList &paths) {
#ifdef Q_OS_LINUX
    if (m_inotifyFd < 0) return;
    for (const QString &dir : paths) {
        const auto it = m_dirToWd.constFind(dir);
        if (it == m_dirToWd.constEnd()) continue;
        const int wd = it.value();
        ::inotify_rm_watch(m_inotifyFd, wd);
        m_wdToDir.remove(wd);
        m_dirToWd.erase(it);
    }
#else
    Q_UNUSED(paths);
#endif
}

void LibraryWatcher::clearInotifyWatches() {
#ifdef Q_OS_LINUX
    if (m_inotifyFd < 0) return;
    for (auto it = m_dirToWd.cbegin(); it != m_dirToWd.cend(); ++it) {
        ::inotify_rm_watch(m_inotifyFd, it.value());
    }
    m_dirToWd.clear();
    m_wdToDir.clear();
#endif
}

void LibraryWatcher::drainInotifyEvents() {
#ifdef Q_OS_LINUX
    if (m_inotifyFd < 0) return;
    alignas(inotify_event) char buffer[8192];
    while (true) {
        const ssize_t bytesRead = ::read(m_inotifyFd, buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            if (bytesRead < 0 && errno == EINTR) continue;
            break;
        }
        ssize_t offset = 0;
        while (offset < bytesRead) {
            const auto *ev = reinterpret_cast<const inotify_event *>(buffer + offset);
            offset += static_cast<ssize_t>(sizeof(inotify_event)) + ev->len;

            if (ev->mask & IN_Q_OVERFLOW) {
                rescanAll();
                continue;
            }
            const QString dir = m_wdToDir.value(ev->wd);
            if (ev->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                if (!dir.isEmpty()) {
                    m_wdToDir.remove(ev->wd);
                    m_dirToWd.remove(dir);
                    onDirectoryChanged(dir);
                }
                continue;
            }
            if (dir.isEmpty()) continue;
            if (ev->mask & (IN_CLOSE_WRITE | IN_ATTRIB | IN_MOVED_TO)) {
                const bool isDir = (ev->mask & IN_ISDIR) != 0;
                const QString fileName = ev->len > 0 ? QString::fromUtf8(ev->name) : QString();
                if (isDir || fileName.isEmpty() || AudioFormats::isAudioFile(fileName)) {
                    onDirectoryChanged(dir);
                }
            }
        }
    }
#endif
}
