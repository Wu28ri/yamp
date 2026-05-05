#pragma once

#include <QString>
#include <limits>

struct Track {
    QString path;
    QString title;
    QString artist;
    QString albumArtist;
    QString album;
    int duration = 0;
    QString techInfo;
    int trackNo = 0;

    double rgTrackGainDb = std::numeric_limits<double>::quiet_NaN();
    double rgAlbumGainDb = std::numeric_limits<double>::quiet_NaN();
    double rgTrackPeak   = std::numeric_limits<double>::quiet_NaN();
    double rgAlbumPeak   = std::numeric_limits<double>::quiet_NaN();

    bool isValid() const { return !path.isEmpty(); }
};
