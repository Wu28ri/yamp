#include "LibraryWatcher.h"

#include "MusicLibrary.h"
#include "Track.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <taglib/fileref.h>
#include <taglib/tag.h>

namespace {

constexpr int kDebounceMs = 350;

QString makeTechInfo(const QString &filePath, int sampleRate, int bitrate) {
    const double khz = sampleRate / 1000.0;
    QString prefix;
    if (filePath.endsWith(QLatin1String(".flac"), Qt::CaseInsensitive))      prefix = QStringLiteral("FLAC | ");
    else if (filePath.endsWith(QLatin1String(".mp3"), Qt::CaseInsensitive))  prefix = QStringLiteral("MP3 | ");
    return QStringLiteral("%1%2 kHz | %3 kbps")
        .arg(prefix)
        .arg(khz, 0, 'f', 1)
        .arg(bitrate);
}

bool readTrackFromFile(const QString &filePath, Track &t, qint64 &fileSize) {
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return false;
    fileSize = info.size();

    const QByteArray pathBytes = filePath.toUtf8();
    TagLib::FileRef f(pathBytes.constData());

    QString title  = info.fileName();
    QString artist = QStringLiteral("Unknown Artist");
    QString album  = QStringLiteral("Unknown Album");
    QString techInfo;
    int duration = 0;
    int trackNo  = 0;

    if (!f.isNull()) {
        if (auto *tag = f.tag()) {
            const QString tTitle  = QString::fromStdString(tag->title().to8Bit(true));
            const QString tArtist = QString::fromStdString(tag->artist().to8Bit(true));
            const QString tAlbum  = QString::fromStdString(tag->album().to8Bit(true));
            if (!tTitle.isEmpty())  title  = tTitle;
            if (!tArtist.isEmpty()) artist = tArtist;
            if (!tAlbum.isEmpty())  album  = tAlbum;
            trackNo = tag->track();
        }
        if (auto *audio = f.audioProperties()) {
            duration = audio->lengthInSeconds();
            techInfo = makeTechInfo(filePath, audio->sampleRate(), audio->bitrate());
        }
    }

    t.path     = filePath;
    t.title    = title;
    t.artist   = artist;
    t.album    = album;
    t.duration = duration;
    t.techInfo = techInfo;
    t.trackNo  = trackNo;
    return true;
}

bool insertTrackRow(const QString &path) {
    Track t;
    qint64 fileSize = 0;
    if (!readTrackFromFile(path, t, fileSize)) return false;

    const QString searchText = (t.title + QLatin1Char(' ') + t.artist + QLatin1Char(' ') + t.album).toLower();

    QSqlQuery q;
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO tracks "
        "(title, artist, album, path, duration, search_text, track_no, tech_info, file_size) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(t.title);
    q.addBindValue(t.artist);
    q.addBindValue(t.album);
    q.addBindValue(t.path);
    q.addBindValue(t.duration);
    q.addBindValue(searchText);
    q.addBindValue(t.trackNo);
    q.addBindValue(t.techInfo);
    q.addBindValue(fileSize);
    if (!q.exec()) {
        qWarning() << "[LibraryWatcher] insert failed:" << q.lastError().text() << path;
        return false;
    }
    if (q.numRowsAffected() > 0) {
        QSqlDatabase db = QSqlDatabase::database();
        MusicLibrary::linkTrackToArtists(db, q.lastInsertId().toLongLong(), t.artist);
        return true;
    }
    return false;
}

bool deleteTrackRow(const QString &path) {
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));
    q.addBindValue(path);
    if (!q.exec()) {
        qWarning() << "[LibraryWatcher] delete failed:" << q.lastError().text() << path;
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool updateTrackPath(const QString &oldPath, const QString &newPath) {
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE tracks SET path = ? WHERE path = ?"));
    q.addBindValue(newPath);
    q.addBindValue(oldPath);
    if (!q.exec()) {
        qWarning() << "[LibraryWatcher] update path failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool loadDbSignature(const QString &path, qint64 &size, int &duration) {
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT file_size, duration FROM tracks WHERE path = ?"));
    q.addBindValue(path);
    if (!q.exec() || !q.next()) return false;
    size     = q.value(0).toLongLong();
    duration = q.value(1).toInt();
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

QStringList dbPathsInDir(const QString &dir) {
    QStringList out;
    QString prefix = dir;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    QSqlQuery q;
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

QStringList dbPathsUnder(const QString &root) {
    QStringList out;
    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT path FROM tracks WHERE path LIKE ? || '%'"));
    q.addBindValue(prefix);
    if (!q.exec()) return out;
    while (q.next()) out.append(q.value(0).toString());
    return out;
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
    bool anyChange = false;
    for (const QString &raw : loaded) {
        const QString r = QDir(raw).absolutePath();
        if (!QDir(r).exists()) continue;
        if (m_roots.contains(r)) continue;
        m_roots.insert(r);

        watchTreeRecursive(r);

        const QStringList dbBefore = dbPathsUnder(r);
        initialReconcile(r);
        const QStringList dbAfter = dbPathsUnder(r);
        if (dbBefore != dbAfter) anyChange = true;
    }
    if (anyChange) emit libraryChanged();
}

void LibraryWatcher::addRoot(const QString &path) {
    const QString clean = QDir(path).absolutePath();
    if (clean.isEmpty() || !QDir(clean).exists()) return;

    for (const QString &existing : m_roots) {
        if (clean == existing) return;
        if (clean.startsWith(existing + QLatin1Char('/'))) return;
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

    const QStringList dbBefore = dbPathsUnder(clean);
    initialReconcile(clean);
    watchTreeRecursive(clean);
    const QStringList dbAfter = dbPathsUnder(clean);
    if (dbBefore != dbAfter) emit libraryChanged();
}

void LibraryWatcher::onDirectoryChanged(const QString &dir) {
    m_pendingDirs.insert(dir);
    m_debounce->start();
}

void LibraryWatcher::flushPending() {
    if (m_pendingDirs.isEmpty()) return;
    const QSet<QString> pending = m_pendingDirs;
    m_pendingDirs.clear();
    reconcileDirs(pending);
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

void LibraryWatcher::initialReconcile(const QString &root) {
    QSet<QString> diskFiles;
    QDirIterator it(root,
                    {QStringLiteral("*.flac"), QStringLiteral("*.mp3")},
                    QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) diskFiles.insert(it.next());

    const QStringList dbList = dbPathsUnder(root);
    const QSet<QString> dbFiles(dbList.begin(), dbList.end());

    QStringList removals;
    for (const QString &p : dbFiles) if (!diskFiles.contains(p)) removals.append(p);

    QStringList additions;
    for (const QString &p : diskFiles) if (!dbFiles.contains(p)) additions.append(p);

    QHash<QPair<qint64, int>, QString> removalIndex;
    for (const QString &p : removals) {
        qint64 sz = 0;
        int dur = 0;
        if (loadDbSignature(p, sz, dur) && sz > 0) {
            removalIndex.insert(qMakePair(sz, dur), p);
        }
    }

    QSet<QString> matchedRemovals;
    QStringList unmatchedAdditions;
    for (const QString &add : additions) {
        Track t;
        qint64 sz = 0;
        if (!readTrackFromFile(add, t, sz)) continue;
        const auto key = qMakePair(sz, t.duration);
        const auto it2 = removalIndex.find(key);
        if (it2 != removalIndex.end()) {
            updateTrackPath(it2.value(), add);
            matchedRemovals.insert(it2.value());
            removalIndex.erase(it2);
        } else {
            unmatchedAdditions.append(add);
        }
    }

    for (const QString &p : removals) {
        if (!matchedRemovals.contains(p)) deleteTrackRow(p);
    }
    for (const QString &p : unmatchedAdditions) insertTrackRow(p);
}

void LibraryWatcher::reconcileDirs(const QSet<QString> &dirs) {
    QStringList allRemovals;
    QStringList allAdditions;
    QStringList newSubdirsToWatch;

    for (const QString &dir : dirs) {
        QDir qdir(dir);
        if (qdir.exists()) {
            const auto subs = qdir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            for (const auto &s : subs) newSubdirsToWatch.append(s.absoluteFilePath());
        }

        const QStringList diskFiles = listAudioFilesIn(dir);
        const QStringList dbFiles   = dbPathsInDir(dir);
        const QSet<QString> diskSet(diskFiles.begin(), diskFiles.end());
        const QSet<QString> dbSet(dbFiles.begin(), dbFiles.end());
        for (const QString &p : dbSet)   if (!diskSet.contains(p)) allRemovals.append(p);
        for (const QString &p : diskSet) if (!dbSet.contains(p))   allAdditions.append(p);
    }

    for (const QString &sub : newSubdirsToWatch) {
        bool insideRoot = false;
        for (const QString &r : m_roots) {
            if (sub == r || sub.startsWith(r + QLatin1Char('/'))) { insideRoot = true; break; }
        }
        if (insideRoot) watchTreeRecursive(sub);
    }

    QHash<QPair<qint64, int>, QString> removalIndex;
    for (const QString &p : allRemovals) {
        qint64 sz = 0;
        int dur = 0;
        if (loadDbSignature(p, sz, dur) && sz > 0) {
            removalIndex.insert(qMakePair(sz, dur), p);
        }
    }

    QSet<QString> matchedRemovals;
    QStringList unmatchedAdditions;
    for (const QString &add : allAdditions) {
        Track t;
        qint64 sz = 0;
        if (!readTrackFromFile(add, t, sz)) continue;
        const auto key = qMakePair(sz, t.duration);
        const auto it = removalIndex.find(key);
        if (it != removalIndex.end()) {
            updateTrackPath(it.value(), add);
            matchedRemovals.insert(it.value());
            removalIndex.erase(it);
        } else {
            unmatchedAdditions.append(add);
        }
    }

    bool changed = !matchedRemovals.isEmpty();
    for (const QString &p : allRemovals) {
        if (!matchedRemovals.contains(p)) {
            if (deleteTrackRow(p)) changed = true;
        }
    }
    for (const QString &p : unmatchedAdditions) {
        if (insertTrackRow(p)) changed = true;
    }

    if (changed) emit libraryChanged();
}
