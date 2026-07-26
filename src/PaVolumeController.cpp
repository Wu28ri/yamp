#include "PaVolumeController.h"

#include <QHash>
#include <QPointer>
#include <QSet>

#include <cmath>
#include <utility>
#include <unistd.h>

#include <pulse/pulseaudio.h>
#include <pulse/proplist.h>
#include <pulse/timeval.h>
namespace {

pa_volume_t userToPa(double v) {
    if (v <= 0.0) return PA_VOLUME_MUTED;
    if (v >= 1.0) return PA_VOLUME_NORM;
    return static_cast<pa_volume_t>(std::round(PA_VOLUME_NORM * v));
}

double paToUser(pa_volume_t v) {
    if (v == PA_VOLUME_MUTED) return 0.0;
    double linear = static_cast<double>(v) / PA_VOLUME_NORM;
    if (linear >= 1.0) return 1.0;
    return linear;
}

}

struct PaVolumeControllerPrivate {
    PaVolumeController *q = nullptr;

    pa_threaded_mainloop *mainloop = nullptr;
    pa_context           *context = nullptr;
    bool                  subscribed = false;

    QString pidStr;

    QSet<uint32_t> ourClientIds;
    QSet<uint32_t> knownClientIds;
    QHash<uint32_t, uint32_t> pendingSinkInputs;
    QHash<uint32_t, uint8_t>  ourSinkInputs;

    double targetUser = 1.0;
    bool   targetMuted = false;

    double reportedUser = -1.0;
    bool   reportedMuted = false;

    quint64 exclusiveGeneration = 0;
    QString exclusiveCardName;
    QString exclusiveProfile;
    QString pendingExclusiveDevice;
    int pendingExclusive = -1;
    int activeExclusive = -1;

    static void onContextState(pa_context *c, void *ud);
    static void onSubscribe(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *ud);
    static void onSinkInputInfo(pa_context *c, const pa_sink_input_info *info, int eol, void *ud);
    static void onClientInfo(pa_context *c, const pa_client_info *info, int eol, void *ud);

    void requestClientInfo(uint32_t clientId);
    void rescanPendingSinkInputs();

    void applyToSinkInput(uint32_t idx, uint8_t channels);
    void applyToAll();

    void postReportedVolume(double user);
    void postReportedMuted(bool muted);
    void postExclusiveResult(bool exclusive, bool success);
};

struct PaExclusiveRequest {
    PaVolumeControllerPrivate *d = nullptr;
    quint64 generation = 0;
    QString cardId;
    QString cardName;
    QString profile;
    bool exclusive = false;
};

struct PaHardwareVolumeRequest {
    QString cardId;
    QString deviceId;
    double volume = 1.0;
    bool muted = false;
    bool applied = false;
};

namespace {

QString cardIdFromMpvDevice(const QString &device) {
    const QString marker = QStringLiteral("CARD=");
    const qsizetype start = device.indexOf(marker);
    if (start < 0) return {};
    const qsizetype valueStart = start + marker.size();
    const qsizetype end = device.indexOf(QLatin1Char(','), valueStart);
    return device.mid(valueStart, end < 0 ? -1 : end - valueStart);
}

QString deviceIdFromMpvDevice(const QString &device) {
    const QString marker = QStringLiteral("DEV=");
    const qsizetype start = device.indexOf(marker);
    if (start < 0) return {};
    const qsizetype valueStart = start + marker.size();
    const qsizetype end = device.indexOf(QLatin1Char(','), valueStart);
    return device.mid(valueStart, end < 0 ? -1 : end - valueStart);
}

void onExclusiveProfileSet(pa_context *, int success, void *userdata) {
    auto *request = static_cast<PaExclusiveRequest*>(userdata);
    auto *d = request->d;
    if (request->generation == d->exclusiveGeneration) {
        d->activeExclusive = -1;
        if (success) {
            if (request->exclusive) {
                d->exclusiveCardName = request->cardName;
                d->exclusiveProfile = request->profile;
            } else {
                d->exclusiveCardName.clear();
                d->exclusiveProfile.clear();
            }
        }
        d->postExclusiveResult(request->exclusive, success != 0);
    }
    delete request;
}

void onExclusiveCardInfo(pa_context *context, const pa_card_info *info,
                         int eol, void *userdata) {
    auto *request = static_cast<PaExclusiveRequest*>(userdata);
    if (request->generation != request->d->exclusiveGeneration) {
        if (eol) delete request;
        return;
    }
    if (!eol && info && request->cardName.isEmpty() && info->proplist) {
        const char *alsaId = pa_proplist_gets(info->proplist, "alsa.id");
        if (alsaId && request->cardId == QString::fromUtf8(alsaId)) {
            request->cardName = QString::fromUtf8(info->name);
            if (info->active_profile)
                request->profile = QString::fromUtf8(info->active_profile->name);
        }
        return;
    }
    if (!eol) return;
    if (request->cardName.isEmpty() || request->profile.isEmpty()) {
        request->d->activeExclusive = -1;
        request->d->postExclusiveResult(true, false);
        delete request;
        return;
    }
    request->d->exclusiveCardName = request->cardName;
    request->d->exclusiveProfile = request->profile;
    pa_operation *operation = pa_context_set_card_profile_by_name(
        context, request->cardName.toUtf8().constData(), "off",
        &onExclusiveProfileSet, request);
    if (operation) {
        pa_operation_unref(operation);
    } else {
        request->d->activeExclusive = -1;
        request->d->postExclusiveResult(true, false);
        delete request;
    }
}

void onHardwareSinkInfo(pa_context *context, const pa_sink_info *info,
                        int eol, void *userdata) {
    auto *request = static_cast<PaHardwareVolumeRequest*>(userdata);
    if (!eol && info && !request->applied && info->proplist) {
        const char *alsaId = pa_proplist_gets(info->proplist, "alsa.id");
        const char *alsaDevice = pa_proplist_gets(info->proplist, "alsa.device");
        const bool deviceMatches = request->deviceId.isEmpty() ||
            (alsaDevice && request->deviceId == QString::fromUtf8(alsaDevice));
        if (alsaId && request->cardId == QString::fromUtf8(alsaId) && deviceMatches) {
            pa_cvolume value;
            pa_cvolume_set(&value, info->volume.channels, userToPa(request->volume));
            if (pa_operation *operation = pa_context_set_sink_volume_by_index(
                    context, info->index, &value, nullptr, nullptr)) {
                pa_operation_unref(operation);
            }
            if (pa_operation *operation = pa_context_set_sink_mute_by_index(
                    context, info->index, request->muted ? 1 : 0, nullptr, nullptr)) {
                pa_operation_unref(operation);
            }
            request->applied = true;
        }
    }
    if (eol) delete request;
}

struct SyncRestore {
    pa_threaded_mainloop *mainloop = nullptr;
    bool done = false;
    bool timedOut = false;
    pa_time_event *timer = nullptr;
};

void onSyncRestore(pa_context *, int, void *userdata) {
    auto *restore = static_cast<SyncRestore*>(userdata);
    restore->done = true;
    pa_threaded_mainloop_signal(restore->mainloop, 0);
}

void onSyncRestoreTimeout(pa_mainloop_api *, pa_time_event *,
                          const timeval *, void *userdata) {
    auto *restore = static_cast<SyncRestore*>(userdata);
    restore->timedOut = true;
    restore->done = true;
    pa_threaded_mainloop_signal(restore->mainloop, 0);
}

}

void PaVolumeControllerPrivate::onContextState(pa_context *c, void *ud) {
    auto *d = static_cast<PaVolumeControllerPrivate*>(ud);
    const pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        if (d->activeExclusive >= 0) {
            const bool exclusive = d->activeExclusive != 0;
            d->activeExclusive = -1;
            ++d->exclusiveGeneration;
            d->postExclusiveResult(exclusive, false);
        }
        if (d->pendingExclusive >= 0) {
            const bool exclusive = d->pendingExclusive != 0;
            d->pendingExclusive = -1;
            d->pendingExclusiveDevice.clear();
            d->postExclusiveResult(exclusive, false);
        }
        return;
    }
    if (state != PA_CONTEXT_READY) return;
    if (d->subscribed) return;
    d->subscribed = true;

    pa_context_set_subscribe_callback(c, &onSubscribe, d);
    if (auto *op = pa_context_subscribe(c,
            (pa_subscription_mask_t)(PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_CLIENT),
            nullptr, nullptr)) {
        pa_operation_unref(op);
    }
    if (auto *op = pa_context_get_sink_input_info_list(c, &onSinkInputInfo, d)) {
        pa_operation_unref(op);
    }
    if (d->pendingExclusive >= 0) {
        const QString device = std::exchange(d->pendingExclusiveDevice, {});
        const bool exclusive = d->pendingExclusive != 0;
        d->pendingExclusive = -1;
        QPointer<PaVolumeController> guard(d->q);
        QMetaObject::invokeMethod(d->q, [guard, device, exclusive]() {
            if (guard) guard->setHardwareDeviceExclusive(device, exclusive);
        }, Qt::QueuedConnection);
    }
}

void PaVolumeControllerPrivate::onSubscribe(pa_context *c,
                                            pa_subscription_event_type_t t,
                                            uint32_t idx, void *ud) {
    auto *d = static_cast<PaVolumeControllerPrivate*>(ud);
    const auto facility = (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
    const auto type     = (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK);

    if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
            d->ourSinkInputs.remove(idx);
            d->pendingSinkInputs.remove(idx);
            return;
        }
        if (auto *op = pa_context_get_sink_input_info(c, idx, &onSinkInputInfo, d)) {
            pa_operation_unref(op);
        }
    } else if (facility == PA_SUBSCRIPTION_EVENT_CLIENT) {
        if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
            d->ourClientIds.remove(idx);
            d->knownClientIds.remove(idx);
            return;
        }
        d->requestClientInfo(idx);
    }
}

void PaVolumeControllerPrivate::requestClientInfo(uint32_t clientId) {
    if (knownClientIds.contains(clientId)) return;
    knownClientIds.insert(clientId);
    if (auto *op = pa_context_get_client_info(context, clientId, &onClientInfo, this)) {
        pa_operation_unref(op);
    }
}

void PaVolumeControllerPrivate::onClientInfo(pa_context *,
                                             const pa_client_info *info,
                                             int eol, void *ud) {
    if (eol > 0 || !info || !info->proplist) return;
    auto *d = static_cast<PaVolumeControllerPrivate*>(ud);

    const char *pid = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
    if (!pid) return;
    if (d->pidStr != QString::fromUtf8(pid)) {
        d->ourClientIds.remove(info->index);
        return;
    }
    d->ourClientIds.insert(info->index);
    d->rescanPendingSinkInputs();
}

void PaVolumeControllerPrivate::onSinkInputInfo(pa_context *c,
                                                const pa_sink_input_info *info,
                                                int eol, void *ud) {
    if (eol > 0 || !info) return;
    auto *d = static_cast<PaVolumeControllerPrivate*>(ud);

    bool ours = false;
    if (info->proplist) {
        const char *pid = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
        if (pid && d->pidStr == QString::fromUtf8(pid)) ours = true;
    }

    if (!ours && !d->ourClientIds.contains(info->client)) {
        d->pendingSinkInputs.insert(info->index, info->client);
        if (info->client != PA_INVALID_INDEX) d->requestClientInfo(info->client);
        return;
    }

    const bool wasOurs = d->ourSinkInputs.contains(info->index);
    d->ourSinkInputs.insert(info->index, info->volume.channels);
    d->pendingSinkInputs.remove(info->index);

    const double user = paToUser(pa_cvolume_avg(&info->volume));
    if (std::fabs(user - d->reportedUser) > 1e-4) {
        d->reportedUser = user;
        d->postReportedVolume(user);
    }
    if (info->mute != d->reportedMuted) {
        d->reportedMuted = info->mute;
        d->postReportedMuted(info->mute);
    }

    if (!wasOurs) d->applyToSinkInput(info->index, info->volume.channels);
    Q_UNUSED(c);
}

void PaVolumeControllerPrivate::rescanPendingSinkInputs() {
    if (!context) return;
    for (auto it = pendingSinkInputs.constBegin(); it != pendingSinkInputs.constEnd(); ++it) {
        if (!ourClientIds.contains(it.value())) continue;
        if (auto *op = pa_context_get_sink_input_info(context, it.key(), &onSinkInputInfo, this)) {
            pa_operation_unref(op);
        }
    }
}

void PaVolumeControllerPrivate::applyToSinkInput(uint32_t idx, uint8_t channels) {
    if (!context) return;
    if (channels < 1) channels = 2;
    if (channels > PA_CHANNELS_MAX) channels = PA_CHANNELS_MAX;

    pa_cvolume cv;
    pa_cvolume_set(&cv, channels, userToPa(targetUser));

    if (auto *op = pa_context_set_sink_input_volume(context, idx, &cv, nullptr, nullptr)) {
        pa_operation_unref(op);
    }
    if (auto *op = pa_context_set_sink_input_mute(context, idx, targetMuted ? 1 : 0,
                                                  nullptr, nullptr)) {
        pa_operation_unref(op);
    }
}

void PaVolumeControllerPrivate::applyToAll() {
    for (auto it = ourSinkInputs.constBegin(); it != ourSinkInputs.constEnd(); ++it) {
        applyToSinkInput(it.key(), it.value());
    }
}

void PaVolumeControllerPrivate::postReportedVolume(double user) {
    QPointer<PaVolumeController> guard(q);
    QMetaObject::invokeMethod(q, [this, guard, user]() {
        if (!guard) return;
        if (std::fabs(user - targetUser) < 1e-4) return;
        targetUser = user;
        emit q->volumeChanged();
    }, Qt::QueuedConnection);
}

void PaVolumeControllerPrivate::postReportedMuted(bool muted) {
    QPointer<PaVolumeController> guard(q);
    QMetaObject::invokeMethod(q, [this, guard, muted]() {
        if (!guard) return;
        if (targetMuted == muted) return;
        targetMuted = muted;
        emit q->mutedChanged();
    }, Qt::QueuedConnection);
}

void PaVolumeControllerPrivate::postExclusiveResult(bool exclusive, bool success) {
    QPointer<PaVolumeController> guard(q);
    QMetaObject::invokeMethod(q, [guard, exclusive, success]() {
        if (guard) emit guard->hardwareDeviceExclusiveChanged(exclusive, success);
    }, Qt::QueuedConnection);
}

PaVolumeController::PaVolumeController(QObject *parent)
    : QObject(parent), d(new PaVolumeControllerPrivate) {
    d->q = this;
    d->pidStr = QString::number(::getpid());

    d->mainloop = pa_threaded_mainloop_new();
    if (!d->mainloop) {
        qWarning("[PaVolumeController] pa_threaded_mainloop_new failed");
        return;
    }
    pa_threaded_mainloop_set_name(d->mainloop, "yamp-pa-vol");

    pa_threaded_mainloop_lock(d->mainloop);
    pa_mainloop_api *api = pa_threaded_mainloop_get_api(d->mainloop);
    d->context = pa_context_new(api, "yamp");
    if (!d->context) {
        qWarning("[PaVolumeController] pa_context_new failed");
        pa_threaded_mainloop_unlock(d->mainloop);
        return;
    }
    pa_context_set_state_callback(d->context, &PaVolumeControllerPrivate::onContextState, d);
    if (pa_context_connect(d->context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        qWarning("[PaVolumeController] pa_context_connect failed: %s",
                 pa_strerror(pa_context_errno(d->context)));
    }
    pa_threaded_mainloop_unlock(d->mainloop);

    if (pa_threaded_mainloop_start(d->mainloop) < 0) {
        qWarning("[PaVolumeController] pa_threaded_mainloop_start failed");
    }
}

PaVolumeController::~PaVolumeController() {
    if (d->mainloop) {
        if (d->context) {
            pa_threaded_mainloop_lock(d->mainloop);
            ++d->exclusiveGeneration;
            if (!d->exclusiveCardName.isEmpty() && !d->exclusiveProfile.isEmpty() &&
                pa_context_get_state(d->context) == PA_CONTEXT_READY) {
                SyncRestore restore{d->mainloop};
                pa_operation *operation = pa_context_set_card_profile_by_name(
                    d->context, d->exclusiveCardName.toUtf8().constData(),
                    d->exclusiveProfile.toUtf8().constData(), &onSyncRestore, &restore);
                if (operation) {
                    pa_mainloop_api *api = pa_threaded_mainloop_get_api(d->mainloop);
                    timeval deadline;
                    pa_timeval_add(pa_gettimeofday(&deadline), PA_USEC_PER_SEC);
                    restore.timer = api->time_new(
                        api, &deadline, &onSyncRestoreTimeout, &restore);
                    while (!restore.done && restore.timer)
                        pa_threaded_mainloop_wait(d->mainloop);
                    if (restore.timer) api->time_free(restore.timer);
                    if (restore.timedOut || !restore.done) pa_operation_cancel(operation);
                    pa_operation_unref(operation);
                }
            }
            pa_context_disconnect(d->context);
            pa_context_unref(d->context);
            d->context = nullptr;
            pa_threaded_mainloop_unlock(d->mainloop);
        }
        pa_threaded_mainloop_stop(d->mainloop);
        pa_threaded_mainloop_free(d->mainloop);
    }
    delete d;
}

double PaVolumeController::volume() const {
    return d->targetUser;
}

bool PaVolumeController::isMuted() const {
    return d->targetMuted;
}

void PaVolumeController::setVolume(double v) {
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    if (qFuzzyCompare(v + 1.0, d->targetUser + 1.0)) return;
    d->targetUser = v;
    if (d->mainloop) {
        pa_threaded_mainloop_lock(d->mainloop);
        d->applyToAll();
        pa_threaded_mainloop_unlock(d->mainloop);
    }
    emit volumeChanged();
}

void PaVolumeController::setMuted(bool muted) {
    if (d->targetMuted == muted) return;
    d->targetMuted = muted;
    if (d->mainloop) {
        pa_threaded_mainloop_lock(d->mainloop);
        d->applyToAll();
        pa_threaded_mainloop_unlock(d->mainloop);
    }
    emit mutedChanged();
}

void PaVolumeController::setHardwareDeviceExclusive(const QString &mpvDevice,
                                                    bool exclusive) {
    if (!d->mainloop || !d->context) {
        emit hardwareDeviceExclusiveChanged(exclusive, false);
        return;
    }
    pa_threaded_mainloop_lock(d->mainloop);
    const pa_context_state_t state = pa_context_get_state(d->context);
    if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        pa_threaded_mainloop_unlock(d->mainloop);
        QMetaObject::invokeMethod(this, [this, exclusive]() {
            emit hardwareDeviceExclusiveChanged(exclusive, false);
        }, Qt::QueuedConnection);
        return;
    }
    if (state != PA_CONTEXT_READY) {
        d->pendingExclusiveDevice = mpvDevice;
        d->pendingExclusive = exclusive ? 1 : 0;
        pa_threaded_mainloop_unlock(d->mainloop);
        return;
    }
    const quint64 generation = ++d->exclusiveGeneration;
    auto *request = new PaExclusiveRequest;
    request->d = d;
    request->generation = generation;
    request->exclusive = exclusive;

    pa_operation *operation = nullptr;
    if (exclusive) {
        request->cardId = cardIdFromMpvDevice(mpvDevice);
        if (!request->cardId.isEmpty() &&
            pa_context_get_state(d->context) == PA_CONTEXT_READY) {
            operation = pa_context_get_card_info_list(
                d->context, &onExclusiveCardInfo, request);
        }
    } else if (!d->exclusiveCardName.isEmpty() && !d->exclusiveProfile.isEmpty() &&
               pa_context_get_state(d->context) == PA_CONTEXT_READY) {
        request->cardName = d->exclusiveCardName;
        request->profile = d->exclusiveProfile;
        operation = pa_context_set_card_profile_by_name(
            d->context, request->cardName.toUtf8().constData(),
            request->profile.toUtf8().constData(), &onExclusiveProfileSet, request);
    } else {
        delete request;
        pa_threaded_mainloop_unlock(d->mainloop);
        emit hardwareDeviceExclusiveChanged(false, true);
        return;
    }
    if (operation) {
        d->activeExclusive = exclusive ? 1 : 0;
        pa_operation_unref(operation);
    } else {
        d->activeExclusive = -1;
        delete request;
        QMetaObject::invokeMethod(this, [this, exclusive]() {
            emit hardwareDeviceExclusiveChanged(exclusive, false);
        }, Qt::QueuedConnection);
    }
    pa_threaded_mainloop_unlock(d->mainloop);
}

void PaVolumeController::setHardwareDeviceVolume(const QString &mpvDevice,
                                                 double volume, bool muted) {
    if (!d->mainloop || !d->context) return;
    const QString cardId = cardIdFromMpvDevice(mpvDevice);
    if (cardId.isEmpty()) return;

    pa_threaded_mainloop_lock(d->mainloop);
    if (pa_context_get_state(d->context) != PA_CONTEXT_READY) {
        pa_threaded_mainloop_unlock(d->mainloop);
        return;
    }
    auto *request = new PaHardwareVolumeRequest;
    request->cardId = cardId;
    request->deviceId = deviceIdFromMpvDevice(mpvDevice);
    request->volume = qBound(0.0, volume, 1.0);
    request->muted = muted;
    if (pa_operation *operation = pa_context_get_sink_info_list(
            d->context, &onHardwareSinkInfo, request)) {
        pa_operation_unref(operation);
    } else {
        delete request;
    }
    pa_threaded_mainloop_unlock(d->mainloop);
}
