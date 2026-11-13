#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

struct Track {
    Q_GADGET
    Q_PROPERTY(QString path        MEMBER path)
    Q_PROPERTY(QString title       MEMBER title)
    Q_PROPERTY(QString artist      MEMBER artist)
    Q_PROPERTY(QString albumArtist MEMBER albumArtist)
    Q_PROPERTY(QString album       MEMBER album)
    Q_PROPERTY(int     duration    MEMBER duration)
    Q_PROPERTY(QString techInfo    MEMBER techInfo)
    Q_PROPERTY(int     trackNo     MEMBER trackNo)
    Q_PROPERTY(int     discNo      MEMBER discNo)
    Q_PROPERTY(int     year        MEMBER year)
    Q_PROPERTY(QString genre       MEMBER genre)

public:
    QString path;
    QString title;
    QString artist;
    QString albumArtist;
    QString album;
    int duration = 0;
    QString techInfo;
    int trackNo = 0;
    int discNo = 0;
    int year = 0;
    QString genre;
    QStringList artistTags;

    bool isValid() const { return !path.isEmpty(); }
};

Q_DECLARE_METATYPE(Track)
