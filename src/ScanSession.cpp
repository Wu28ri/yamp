#include "ScanSession.h"

#include "MusicLibrary.h"

#include <QThread>

ScanSession::ScanSession(const QString &path, QObject *parent)
    : QObject(parent), m_rootPath(path) {}

void ScanSession::start() {
    m_thread  = new QThread(this);
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

    connect(m_scanner, &LibraryScanner::finished, this, [this](const QList<Track> &newTracks) {
        emit finished(m_rootPath, newTracks);
        m_thread->quit();
    });
    connect(m_thread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread,  &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this,      &QObject::deleteLater);

    m_thread->start();
}
