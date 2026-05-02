#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

class LibraryWatcher : public QObject {
    Q_OBJECT
public:
    explicit LibraryWatcher(QObject *parent = nullptr);

    // Load persisted roots from DB, run a quick reconciliation pass, install watchers.
    void start();

    // Add a new root (persisted). Performs initial reconciliation and starts watching.
    void addRoot(const QString &path);

    // Detach all roots from the live watcher and clear m_roots / pending state.
    // Does not touch watch_roots in DB (caller is expected to do that as part
    // of a full library wipe).
    void clearAll();

    QStringList roots() const;

signals:
    // Emitted on the GUI thread after the DB has been mutated by the watcher.
    void libraryChanged();

private slots:
    void onDirectoryChanged(const QString &dir);
    void flushPending();

private:
    void persistRoot(const QString &root);
    QStringList loadRoots();

    void watchTreeRecursive(const QString &root);
    void unwatchTree(const QString &root);

    void initialReconcile(const QString &root);
    void reconcileDirs(const QSet<QString> &dirs);

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QSet<QString> m_pendingDirs;
    QSet<QString> m_roots;
};
