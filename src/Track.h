#pragma once

#include <QString>

struct Track {
    QString path;
    QString title;
    QString artist;
    QString album;
    int duration = 0;
    QString techInfo;
    int trackNo = 0;

    bool isValid() const { return !path.isEmpty(); }
};
