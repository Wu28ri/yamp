#include "ScanSession.h"

#include "MusicLibrary.h"

#include <QThread>

ScanSession::ScanSession(const QString &path, QObject *parent)
    : QObject(parent), m_rootPath(path) {}

ScanSession::~ScanSession() {
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

void ScanSession::cancel() {
    m_cancelled = true;
    if (m_thread) m_thread->requestInterruption();
}

void ScanSession::start() {
    m_thread  = new QThread;
    m_scanner = new LibraryScanner(m_rootPath);
    m_scanner->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_scanner, &LibraryScanner::run);

    connect(m_scanner, &LibraryScanner::countDetermined, this, [this](int total) {
        m_total = total;
        emit progressChanged();
    });

    connect(m_scanner, &LibraryScanner::progress, this, [this](int processed, int total) {
        m_processed = processed;
        m_total     = total;
        emit progressChanged();
    });

    connect(m_scanner, &LibraryScanner::batchReady, this, &ScanSession::batchReady);

    connect(m_scanner, &LibraryScanner::finished, this,
            [this](const QList<Track> &newTracks, bool success) {
        m_failed = !success && !m_cancelled;
        emit finished(m_rootPath, newTracks, success);
        m_thread->quit();
    });
    connect(m_thread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]() {
        QThread *t = m_thread;
        m_thread = nullptr;
        if (t) t->deleteLater();
        deleteLater();
    });

    m_thread->start();
}
