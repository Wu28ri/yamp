#pragma once

#include "Track.h"

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(QString rootPath, QObject *parent = nullptr);

    void run();

signals:
    void finished(const QList<Track> &newTracks);

private:
    static QString makeTechInfo(const QString &filePath, int sampleRate, int bitrate, int bitDepth);

    QString m_rootPath;
};

namespace MusicLibrary {

QString databasePath();
bool initialize();

QString normalizeArtistName(const QString &raw);
QStringList splitArtists(const QString &raw);
void linkTrackToArtists(QSqlDatabase &db, qint64 trackId, const QString &rawArtists);

}
