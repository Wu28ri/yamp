#include "AlsaDevicesService.h"

#include <QFile>
#include <QHash>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <alsa/asoundlib.h>

namespace AlsaDevices {

namespace {

QHash<int, QPair<QString, QString>> loadCardsMap() {
    QHash<int, QPair<QString, QString>> cards;
    QFile f(QStringLiteral("/proc/asound/cards"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return cards;

    const QString text = QString::fromUtf8(f.readAll());
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression headRe(QStringLiteral(R"(^\s*(\d+)\s+\[([^\]]+)\]:\s*(.*)$)"));
    for (const QString &line : lines) {
        const auto m = headRe.match(line);
        if (!m.hasMatch()) continue;
        const int n = m.captured(1).toInt();
        const QString shortId = m.captured(2).trimmed();
        QString descriptive = m.captured(3).trimmed();
        const int dashIdx = descriptive.indexOf(QStringLiteral(" - "));
        if (dashIdx > 0 && dashIdx < 24) {
            descriptive = descriptive.mid(dashIdx + 3).trimmed();
        }
        if (descriptive.isEmpty()) descriptive = shortId;
        cards.insert(n, qMakePair(shortId, descriptive));
    }
    return cards;
}

QString extractCardId(const QString &mpvDeviceName) {
    if (!mpvDeviceName.startsWith(QStringLiteral("alsa/hw:CARD="))) return {};
    const QString tail = mpvDeviceName.mid(QStringLiteral("alsa/hw:").size());
    for (const QString &part : tail.split(QLatin1Char(','))) {
        if (part.startsWith(QStringLiteral("CARD="))) {
            return part.mid(5);
        }
    }
    return {};
}

bool elementToZeroDb(snd_mixer_elem_t *elem) {
    bool touched = false;
    if (snd_mixer_selem_has_playback_volume(elem)) {
        long minDb = 0, maxDb = 0;
        if (snd_mixer_selem_get_playback_dB_range(elem, &minDb, &maxDb) == 0) {
            snd_mixer_selem_set_playback_dB_all(elem, maxDb, 0);
        } else {
            long minV = 0, maxV = 0;
            if (snd_mixer_selem_get_playback_volume_range(elem, &minV, &maxV) == 0) {
                snd_mixer_selem_set_playback_volume_all(elem, maxV);
            }
        }
        touched = true;
    }
    if (snd_mixer_selem_has_playback_switch(elem)) {
        snd_mixer_selem_set_playback_switch_all(elem, 1);
    }
    return touched;
}

}

QVariantList list() {
    QVariantList out;
    const auto cards = loadCardsMap();
    if (cards.isEmpty()) return out;

    QFile pcm(QStringLiteral("/proc/asound/pcm"));
    if (!pcm.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QStringList pcmLines = QString::fromUtf8(pcm.readAll()).split(QLatin1Char('\n'));

    QHash<int, int> playbackCount;
    struct Entry { int card; int dev; QString pcmName; };
    QList<Entry> entries;

    for (const QString &raw : pcmLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() < 2) continue;

        const QStringList head = parts.at(0).split(QLatin1Char('-'));
        if (head.size() != 2) continue;
        bool okC = false, okD = false;
        const int card = head.at(0).toInt(&okC);
        const int dev  = head.at(1).toInt(&okD);
        if (!okC || !okD) continue;

        bool hasPlayback = false;
        for (int i = 1; i < parts.size(); ++i) {
            if (parts.at(i).trimmed().startsWith(QStringLiteral("playback"))) {
                hasPlayback = true;
                break;
            }
        }
        if (!hasPlayback) continue;
        if (!cards.contains(card)) continue;

        QString pcmName = parts.value(2).trimmed();
        if (pcmName.isEmpty()) pcmName = parts.value(1).trimmed();

        playbackCount[card]++;
        entries.append({card, dev, pcmName});
    }

    for (const Entry &e : entries) {
        const QString cardName = cards.value(e.card).second;
        const bool multi = playbackCount.value(e.card) > 1;

        QVariantMap entry;
        entry.insert(QStringLiteral("name"),
                     QStringLiteral("alsa/hw:CARD=%1,DEV=%2")
                         .arg(cards.value(e.card).first).arg(e.dev));
        if (multi && !e.pcmName.isEmpty() && e.pcmName != cardName) {
            entry.insert(QStringLiteral("description"),
                         QStringLiteral("%1 — %2  (hw:%3,%4)")
                             .arg(cardName, e.pcmName).arg(e.card).arg(e.dev));
        } else {
            entry.insert(QStringLiteral("description"),
                         QStringLiteral("%1  (hw:%2,%3)")
                             .arg(cardName).arg(e.card).arg(e.dev));
        }
        out.append(entry);
    }

    return out;
}

bool lockToZeroDb(const QString &mpvDeviceName) {
    const QString cardId = extractCardId(mpvDeviceName);
    if (cardId.isEmpty()) return false;

    snd_mixer_t *mixer = nullptr;
    if (snd_mixer_open(&mixer, 0) < 0) return false;

    const QByteArray ctlName = ("hw:CARD=" + cardId).toUtf8();
    bool ok = false;
    do {
        if (snd_mixer_attach(mixer, ctlName.constData()) < 0) break;
        if (snd_mixer_selem_register(mixer, nullptr, nullptr) < 0) break;
        if (snd_mixer_load(mixer) < 0) break;

        int touched = 0;
        for (snd_mixer_elem_t *elem = snd_mixer_first_elem(mixer);
             elem;
             elem = snd_mixer_elem_next(elem)) {
            if (snd_mixer_elem_get_type(elem) != SND_MIXER_ELEM_SIMPLE) continue;
            if (!snd_mixer_selem_is_active(elem)) continue;
            if (elementToZeroDb(elem)) ++touched;
        }
        ok = touched > 0;
    } while (false);

    snd_mixer_close(mixer);
    return ok;
}

}
