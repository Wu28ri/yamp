#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>

class QFileSystemWatcher;
class QTimer;

class LibraryWatcher : public QObject {
    Q_OBJECT
public:
    enum class RegisterResult { Ready, AlreadyCovered, Retry };

    explicit LibraryWatcher(QObject *parent = nullptr);
    ~LibraryWatcher() override;

    void start();

    RegisterResult registerScannedRoot(const QString &path);

    void removeRoot(const QString &path);

    void clearAll();

    void rescanAll(const QSet<QString> &excludedRoots = {});
    void rescanRoot(const QString &root);

    QStringList roots() const;

signals:
    void libraryChanged();

private slots:
    void onDirectoryChanged(const QString &dir);
    void flushPending();

private:
    QStringList loadRoots();
    QString attachRoot(const QString &path, bool *retry);

    void watchTreeRecursive(const QString &root);
    void unwatchTree(const QString &root);

    void initialReconcileAsync(const QString &root);

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QSet<QString> m_pendingDirs;
    QSet<QString> m_roots;
    QThreadPool m_workerPool;
    bool m_reconcileRunning = false;
    std::atomic<quint64> m_generation{0};
    std::atomic_bool m_stopping{false};
};
