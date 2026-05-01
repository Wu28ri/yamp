#pragma once

#include "Track.h"

#include <QList>
#include <QObject>
#include <QString>

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(QString rootPath, QObject *parent = nullptr);

    void run();

signals:
    void finished(const QList<Track> &newTracks);

private:
    static QString makeTechInfo(const QString &filePath, int sampleRate, int bitrate);

    QString m_rootPath;
};

namespace MusicLibrary {

QString databasePath();
bool initialize();

}
