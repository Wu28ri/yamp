#pragma once

#include <QMetaType>
#include <QString>

struct Track {
    Q_GADGET
    Q_PROPERTY(QString path     MEMBER path)
    Q_PROPERTY(QString title    MEMBER title)
    Q_PROPERTY(QString artist   MEMBER artist)
    Q_PROPERTY(QString album    MEMBER album)
    Q_PROPERTY(int     duration MEMBER duration)
    Q_PROPERTY(QString techInfo MEMBER techInfo)
    Q_PROPERTY(int     trackNo  MEMBER trackNo)

public:
    QString path;
    QString title;
    QString artist;
    QString album;
    int duration = 0;
    QString techInfo;
    int trackNo = 0;

    Q_INVOKABLE bool isValid() const { return !path.isEmpty(); }
};

Q_DECLARE_METATYPE(Track)
