#include "LibraryWatcher.h"

#include "MusicLibrary.h"
#include "Track.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThreadPool>
#include <QUuid>
#include <QVariant>

namespace {

constexpr int kDebounceMs = 350;

int insertTrackRowsBatch(QSqlDatabase &db, const QStringList &paths) {
    if (paths.isEmpty()) return 0;

    QSqlQuery insertTrack(db);
    insertTrack.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO tracks "
        "(title, artist, album, path, duration, search_text, track_no, tech_info, file_size, album_artist) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    QSqlQuery upsertArtist(db);
    upsertArtist.prepare(QStringLiteral("INSERT OR IGNORE INTO artists (name, name_norm) VALUES (?, ?)"));
    QSqlQuery findArtistId(db);
    findArtistId.prepare(QStringLiteral("SELECT id FROM artists WHERE name_norm = ?"));
    QSqlQuery linkTrackArtist(db);
    linkTrackArtist.prepare(QStringLiteral("INSERT OR IGNORE INTO track_artists (track_id, artist_id) VALUES (?, ?)"));

    db.transaction();
    int inserted = 0;
    for (const QString &path : paths) {
        Track t;
        qint64 fileSize = 0;
        if (!MusicLibrary::readTrackFromFile(path, t, fileSize)) continue;

        const QString searchText = (t.title + QLatin1Char(' ') + t.artist + QLatin1Char(' ') + t.album).toLower();
        insertTrack.bindValue(0, t.title);
        insertTrack.bindValue(1, t.artist);
        insertTrack.bindValue(2, t.album);
        insertTrack.bindValue(3, t.path);
        insertTrack.bindValue(4, t.duration);
        insertTrack.bindValue(5, searchText);
        insertTrack.bindValue(6, t.trackNo);
        insertTrack.bindValue(7, t.techInfo);
        insertTrack.bindValue(8, fileSize);
        insertTrack.bindValue(9, t.albumArtist);
        if (insertTrack.exec() && insertTrack.numRowsAffected() > 0) {
            MusicLibrary::linkTrackToArtistsPrepared(
                insertTrack.lastInsertId().toLongLong(),
                t.artist,
                upsertArtist, findArtistId, linkTrackArtist);
            ++inserted;
        }
    }
    db.commit();
    return inserted;
}

int deleteTrackRowsBatch(QSqlDatabase &db, const QStringList &paths) {
    if (paths.isEmpty()) return 0;
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));

    db.transaction();
    int removed = 0;
    for (const QString &p : paths) {
        del.bindValue(0, p);
        if (del.exec()) removed += del.numRowsAffected();
    }
    db.commit();
    if (removed > 0) MusicLibrary::pruneOrphanArtists(db);
    return removed;
}

bool updateTrackPath(QSqlDatabase &db, const QString &oldPath, const QString &newPath) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE tracks SET path = ? WHERE path = ?"));
    q.addBindValue(newPath);
    q.addBindValue(oldPath);
    if (!q.exec()) {
        qWarning() << "[LibraryWatcher] update path failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool loadDbSignature(QSqlQuery &q, const QString &path, qint64 &size, int &duration) {
    q.bindValue(0, path);
    if (!q.exec() || !q.next()) {
        q.finish();
        return false;
    }
    size     = q.value(0).toLongLong();
    duration = q.value(1).toInt();
    q.finish();
    return true;
}

QStringList listAudioFilesIn(const QString &dir) {
    QStringList out;
    QDir d(dir);
    if (!d.exists()) return out;
    const auto entries = d.entryInfoList(
        {QStringLiteral("*.flac"), QStringLiteral("*.mp3")},
        QDir::Files | QDir::NoSymLinks);
    out.reserve(entries.size());
    for (const auto &e : entries) out.append(e.absoluteFilePath());
    return out;
}

QStringList dbPathsInDir(QSqlDatabase &db, const QString &dir) {
    QStringList out;
    QString prefix = dir;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT path FROM tracks WHERE path LIKE ? || '%'"));
    q.addBindValue(prefix);
    if (!q.exec()) {
        qWarning() << "[LibraryWatcher] dbPathsInDir failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QString p = q.value(0).toString();
        if (p.lastIndexOf(QLatin1Char('/')) == prefix.size() - 1) {
            out.append(p);
        }
    }
    return out;
}

QStringList dbPathsUnder(QSqlDatabase &db, const QString &root) {
    QStringList out;
    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT path FROM tracks WHERE path LIKE ? || '%'"));
    q.addBindValue(prefix);
    if (!q.exec()) return out;
    while (q.next()) out.append(q.value(0).toString());
    return out;
}

struct ReconcileResult {
    bool changed = false;
    QStringList newSubdirsToWatch;
};

ReconcileResult reconcileDirsBlocking(QSqlDatabase &db, const QSet<QString> &dirs) {
    ReconcileResult result;
    QStringList allRemovals;
    QStringList allAdditions;

    for (const QString &dir : dirs) {
        QDir qdir(dir);
        if (qdir.exists()) {
            const auto subs = qdir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            for (const auto &s : subs) result.newSubdirsToWatch.append(s.absoluteFilePath());
        }

        const QStringList diskFiles = listAudioFilesIn(dir);
        const QStringList dbFiles   = dbPathsInDir(db, dir);
        const QSet<QString> diskSet(diskFiles.begin(), diskFiles.end());
        const QSet<QString> dbSet(dbFiles.begin(), dbFiles.end());
        for (const QString &p : dbSet)   if (!diskSet.contains(p)) allRemovals.append(p);
        for (const QString &p : diskSet) if (!dbSet.contains(p))   allAdditions.append(p);
    }

    QHash<QPair<qint64, int>, QString> removalIndex;
    {
        QSqlQuery sigQ(db);
        sigQ.prepare(QStringLiteral("SELECT file_size, duration FROM tracks WHERE path = ?"));
        for (const QString &p : allRemovals) {
            qint64 sz = 0;
            int dur = 0;
            if (loadDbSignature(sigQ, p, sz, dur) && sz > 0) {
                removalIndex.insert(qMakePair(sz, dur), p);
            }
        }
    }

    QSet<QString> matchedRemovals;
    QStringList unmatchedAdditions;
    for (const QString &add : allAdditions) {
        Track t;
        qint64 sz = 0;
        if (!MusicLibrary::readTrackFromFile(add, t, sz)) continue;
        const auto key = qMakePair(sz, t.duration);
        const auto it = removalIndex.find(key);
        if (it != removalIndex.end()) {
            updateTrackPath(db, it.value(), add);
            matchedRemovals.insert(it.value());
            removalIndex.erase(it);
        } else {
            unmatchedAdditions.append(add);
        }
    }

    if (!matchedRemovals.isEmpty()) result.changed = true;
    QStringList toDelete;
    for (const QString &p : allRemovals) {
        if (!matchedRemovals.contains(p)) toDelete.append(p);
    }
    if (!toDelete.isEmpty()) {
        if (deleteTrackRowsBatch(db, toDelete) > 0) result.changed = true;
    }
    if (!unmatchedAdditions.isEmpty()) {
        if (insertTrackRowsBatch(db, unmatchedAdditions) > 0) result.changed = true;
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

QString LibraryWatcher::attachRoot(const QString &path) {
    const QString clean = QDir(path).absolutePath();
    if (clean.isEmpty() || !QDir(clean).exists()) return {};

    for (const QString &existing : m_roots) {
        if (clean == existing) return {};
        if (clean.startsWith(existing + QLatin1Char('/'))) return {};
    }
    const QStringList currentRoots(m_roots.begin(), m_roots.end());
    for (const QString &existing : currentRoots) {
        if (existing.startsWith(clean + QLatin1Char('/'))) {
            unwatchTree(existing);
            m_roots.remove(existing);
            QSqlQuery q;
            q.prepare(QStringLiteral("DELETE FROM watch_roots WHERE path = ?"));
            q.addBindValue(existing);
            q.exec();
        }
    }

    m_roots.insert(clean);
    persistRoot(clean);
    return clean;
}

void LibraryWatcher::addRoot(const QString &path) {
    const QString clean = attachRoot(path);
    if (clean.isEmpty()) return;

    watchTreeRecursive(clean);
    initialReconcileAsync(clean);
}

void LibraryWatcher::registerScannedRoot(const QString &path) {
    const QString clean = attachRoot(path);
    if (clean.isEmpty()) return;
    watchTreeRecursive(clean);
}

void LibraryWatcher::onDirectoryChanged(const QString &dir) {
    m_pendingDirs.insert(dir);
    m_debounce->start();
}

void LibraryWatcher::removeRoot(const QString &path) {
    const QString clean = QDir(path).absolutePath();

    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM watch_roots WHERE path = ?"));
    q.addBindValue(clean);
    if (!q.exec()) {
        qWarning() << "[LibraryWatcher] removeRoot persist failed:" << q.lastError().text();
    }

    if (!m_roots.contains(clean)) return;

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
    if (m_debounce->isActive()) m_debounce->stop();
    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty()) m_watcher->removePaths(watched);
    m_roots.clear();
    m_pendingDirs.clear();
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

    QThreadPool::globalInstance()->start([this, pending]() {
        const QString connName = QStringLiteral("yamp_lw_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        ReconcileResult result;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(MusicLibrary::databasePath());
            if (db.open()) {
                QSqlQuery pragma(db);
                pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
                pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
                pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));
                pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
                result = reconcileDirsBlocking(db, pending);
                db.close();
            } else {
                qWarning() << "[LibraryWatcher] reconcile open DB:" << db.lastError().text();
            }
        }
        QSqlDatabase::removeDatabase(connName);

        QMetaObject::invokeMethod(this, [this, result]() {
            for (const QString &sub : result.newSubdirsToWatch) {
                bool insideRoot = false;
                for (const QString &r : m_roots) {
                    if (sub == r || sub.startsWith(r + QLatin1Char('/'))) { insideRoot = true; break; }
                }
                if (insideRoot) watchTreeRecursive(sub);
            }
            m_reconcileRunning = false;
            if (result.changed) emit libraryChanged();
            if (!m_pendingDirs.isEmpty() && !m_debounce->isActive()) m_debounce->start();
        }, Qt::QueuedConnection);
    });
}

void LibraryWatcher::persistRoot(const QString &root) {
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO watch_roots (path) VALUES (?)"));
    q.addBindValue(root);
    if (!q.exec()) qWarning() << "[LibraryWatcher] persistRoot failed:" << q.lastError().text();
}

QStringList LibraryWatcher::loadRoots() {
    QStringList out;
    QSqlQuery q;
    if (!q.exec(QStringLiteral("SELECT path FROM watch_roots"))) {
        qWarning() << "[LibraryWatcher] loadRoots failed:" << q.lastError().text();
        return out;
    }
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
    if (!filtered.isEmpty()) m_watcher->addPaths(filtered);
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
    QThreadPool::globalInstance()->start([this, root]() {
        const QString connName = QStringLiteral("yamp_lw_init_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool changed = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(MusicLibrary::databasePath());
            if (!db.open()) {
                qWarning() << "[LibraryWatcher] initial reconcile open DB:" << db.lastError().text();
            } else {
                QSqlQuery pragma(db);
                pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
                pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
                pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));
                pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

                QSet<QString> diskFiles;
                QDirIterator it(root,
                                {QStringLiteral("*.flac"), QStringLiteral("*.mp3")},
                                QDir::Files | QDir::NoSymLinks,
                                QDirIterator::Subdirectories);
                while (it.hasNext()) diskFiles.insert(it.next());

                const QStringList dbList = dbPathsUnder(db, root);
                const QSet<QString> dbFiles(dbList.begin(), dbList.end());

                QStringList removals;
                for (const QString &p : dbFiles) if (!diskFiles.contains(p)) removals.append(p);

                QStringList additions;
                for (const QString &p : diskFiles) if (!dbFiles.contains(p)) additions.append(p);

                QHash<QPair<qint64, int>, QString> removalIndex;
                {
                    QSqlQuery sigQ(db);
                    sigQ.prepare(QStringLiteral("SELECT file_size, duration FROM tracks WHERE path = ?"));
                    for (const QString &p : removals) {
                        qint64 sz = 0;
                        int dur = 0;
                        if (loadDbSignature(sigQ, p, sz, dur) && sz > 0) {
                            removalIndex.insert(qMakePair(sz, dur), p);
                        }
                    }
                }

                QSet<QString> matchedRemovals;
                QStringList unmatchedAdditions;
                for (const QString &add : additions) {
                    Track t;
                    qint64 sz = 0;
                    if (!MusicLibrary::readTrackFromFile(add, t, sz)) continue;
                    const auto key = qMakePair(sz, t.duration);
                    const auto it2 = removalIndex.find(key);
                    if (it2 != removalIndex.end()) {
                        updateTrackPath(db, it2.value(), add);
                        matchedRemovals.insert(it2.value());
                        removalIndex.erase(it2);
                    } else {
                        unmatchedAdditions.append(add);
                    }
                }
                if (!matchedRemovals.isEmpty()) changed = true;

                QStringList toDelete;
                for (const QString &p : removals) {
                    if (!matchedRemovals.contains(p)) toDelete.append(p);
                }
                if (!toDelete.isEmpty()) {
                    if (deleteTrackRowsBatch(db, toDelete) > 0) changed = true;
                }
                if (!unmatchedAdditions.isEmpty()) {
                    if (insertTrackRowsBatch(db, unmatchedAdditions) > 0) changed = true;
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connName);

        if (changed) {
            QMetaObject::invokeMethod(this, [this]() {
                emit libraryChanged();
            }, Qt::QueuedConnection);
        }
    });
}
