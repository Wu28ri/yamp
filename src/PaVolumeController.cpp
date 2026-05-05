#include "PaVolumeController.h"

#include <QHash>
#include <QPointer>
#include <QSet>

#include <cmath>
#include <unistd.h>

#include <pulse/pulseaudio.h>
#include <pulse/proplist.h>
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
};

void PaVolumeControllerPrivate::onContextState(pa_context *c, void *ud) {
    auto *d = static_cast<PaVolumeControllerPrivate*>(ud);
    if (pa_context_get_state(c) != PA_CONTEXT_READY) return;
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

    if (!ours) {
        if (d->ourClientIds.contains(info->client)) {
            ours = true;
        } else {
            d->pendingSinkInputs.insert(info->index, info->client);
            if (info->client != PA_INVALID_INDEX) d->requestClientInfo(info->client);
            return;
        }
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
