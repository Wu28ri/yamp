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

    void start();

    void addRoot(const QString &path);

    void registerScannedRoot(const QString &path);

    void removeRoot(const QString &path);

    void clearAll();

    QStringList roots() const;

signals:
    void libraryChanged();

private slots:
    void onDirectoryChanged(const QString &dir);
    void flushPending();

private:
    void persistRoot(const QString &root);
    QStringList loadRoots();
    QString attachRoot(const QString &path);

    void watchTreeRecursive(const QString &root);
    void unwatchTree(const QString &root);

    void initialReconcileAsync(const QString &root);

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QSet<QString> m_pendingDirs;
    QSet<QString> m_roots;
    bool m_reconcileRunning = false;
};
