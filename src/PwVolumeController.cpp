#include "PwVolumeController.h"

#include <QDebug>
#include <QHash>
#include <QPointer>

#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/dict.h>

namespace {

// pavucontrol uses cubic mapping for display; this keeps our slider position
// in agreement with what the system mixer shows.
constexpr double kCurve = 3.0;

double userToLinear(double v) {
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    return std::pow(v, kCurve);
}

double linearToUser(double v) {
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    return std::pow(v, 1.0 / kCurve);
}

} // namespace

struct PwVolumeControllerPrivate;

struct NodeEntry {
    uint32_t id = 0;
    pw_proxy *proxy = nullptr;
    spa_hook listener {};
    spa_hook proxyListener {};
    PwVolumeControllerPrivate *parent = nullptr;
};

struct PwVolumeControllerPrivate {
    PwVolumeController *q = nullptr;

    pw_thread_loop *loop = nullptr;
    pw_context     *context = nullptr;
    pw_core        *core = nullptr;
    pw_registry    *registry = nullptr;
    spa_hook        registryListener {};

    QString pidStr;
    QHash<uint32_t, NodeEntry*> nodes;

    // Desired state (linear 0..1 channelVolumes)
    double targetVolumeLinear = 1.0;
    bool   targetMuted = false;

    // Last reported state from PW
    double reportedVolumeLinear = -1.0;
    bool   reportedMuted = false;

    static void onGlobal(void *data, uint32_t id, uint32_t permissions,
                         const char *type, uint32_t version, const spa_dict *props);
    static void onGlobalRemove(void *data, uint32_t id);

    static void onNodeInfo(void *object, const pw_node_info *info);
    static void onNodeParam(void *object, int seq, uint32_t id, uint32_t index,
                            uint32_t next, const spa_pod *param);
    static void onProxyRemoved(void *object);

    void bindNode(uint32_t id);
    void unbindNode(uint32_t id);
    void destroyNode(NodeEntry *e);

    void applyToAllNodes();
    void applyToNode(NodeEntry *e);

    void postReportedVolume(double linear);
    void postReportedMuted(bool muted);
};

static const struct pw_registry_events kRegistryEvents = {
    PW_VERSION_REGISTRY_EVENTS,
    /* global        */ PwVolumeControllerPrivate::onGlobal,
    /* global_remove */ PwVolumeControllerPrivate::onGlobalRemove,
};

static const struct pw_node_events kNodeEvents = {
    PW_VERSION_NODE_EVENTS,
    /* info  */ PwVolumeControllerPrivate::onNodeInfo,
    /* param */ PwVolumeControllerPrivate::onNodeParam,
};

static const struct pw_proxy_events kProxyEvents = {
    PW_VERSION_PROXY_EVENTS,
    /* destroy */ nullptr,
    /* bound   */ nullptr,
    /* removed */ PwVolumeControllerPrivate::onProxyRemoved,
};

void PwVolumeControllerPrivate::onGlobal(void *data, uint32_t id, uint32_t /*permissions*/,
                                         const char *type, uint32_t /*version*/,
                                         const spa_dict *props) {
    auto *d = static_cast<PwVolumeControllerPrivate*>(data);
    if (!type || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
    if (!props) return;

    const char *pidProp    = spa_dict_lookup(props, PW_KEY_APP_PROCESS_ID);
    const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!pidProp || !mediaClass) return;
    if (d->pidStr != QString::fromUtf8(pidProp)) return;
    if (strcmp(mediaClass, "Stream/Output/Audio") != 0) return;

    d->bindNode(id);
}

void PwVolumeControllerPrivate::onGlobalRemove(void *data, uint32_t id) {
    auto *d = static_cast<PwVolumeControllerPrivate*>(data);
    if (d->nodes.contains(id)) d->unbindNode(id);
}

void PwVolumeControllerPrivate::bindNode(uint32_t id) {
    if (nodes.contains(id)) return;

    auto *proxy = static_cast<pw_proxy*>(
        pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Node,
                         PW_VERSION_NODE, sizeof(NodeEntry)));
    if (!proxy) return;

    auto *e = static_cast<NodeEntry*>(pw_proxy_get_user_data(proxy));
    new (e) NodeEntry();
    e->id     = id;
    e->proxy  = proxy;
    e->parent = this;

    pw_node_add_listener(reinterpret_cast<pw_node*>(proxy),
                         &e->listener, &kNodeEvents, e);
    pw_proxy_add_listener(proxy, &e->proxyListener, &kProxyEvents, e);

    uint32_t ids[1] = { SPA_PARAM_Props };
    pw_node_subscribe_params(reinterpret_cast<pw_node*>(proxy), ids, 1);

    nodes.insert(id, e);
    applyToNode(e);
}

void PwVolumeControllerPrivate::unbindNode(uint32_t id) {
    auto it = nodes.find(id);
    if (it == nodes.end()) return;
    NodeEntry *e = it.value();
    nodes.erase(it);
    destroyNode(e);
}

void PwVolumeControllerPrivate::destroyNode(NodeEntry *e) {
    if (!e) return;
    spa_hook_remove(&e->listener);
    spa_hook_remove(&e->proxyListener);
    if (e->proxy) {
        pw_proxy_destroy(e->proxy);
    }
    e->~NodeEntry();
}

void PwVolumeControllerPrivate::onNodeInfo(void *object, const pw_node_info * /*info*/) {
    Q_UNUSED(object);
}

void PwVolumeControllerPrivate::onNodeParam(void *object, int /*seq*/, uint32_t id,
                                            uint32_t /*index*/, uint32_t /*next*/,
                                            const spa_pod *param) {
    auto *e = static_cast<NodeEntry*>(object);
    if (!e || !e->parent) return;
    if (id != SPA_PARAM_Props || !param) return;
    if (!spa_pod_is_object_type(param, SPA_TYPE_OBJECT_Props)) return;

    bool  haveMute = false, muted = false;
    bool  haveVol  = false;
    float vol      = 0.0f;

    spa_pod_prop *prop;
    SPA_POD_OBJECT_FOREACH(reinterpret_cast<const spa_pod_object*>(param), prop) {
        if (prop->key == SPA_PROP_mute) {
            bool tmp = false;
            if (spa_pod_get_bool(&prop->value, &tmp) == 0) {
                muted = tmp;
                haveMute = true;
            }
        } else if (prop->key == SPA_PROP_channelVolumes) {
            float values[8];
            uint32_t n = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, values, 8);
            if (n > 0) {
                vol = values[0];
                haveVol = true;
            }
        } else if (prop->key == SPA_PROP_volume && !haveVol) {
            float tmp = 0.0f;
            if (spa_pod_get_float(&prop->value, &tmp) == 0) {
                vol = tmp;
                haveVol = true;
            }
        }
    }

    if (haveMute && muted != e->parent->reportedMuted) {
        e->parent->reportedMuted = muted;
        e->parent->postReportedMuted(muted);
    }
    if (haveVol && std::fabs(vol - e->parent->reportedVolumeLinear) > 1e-4) {
        e->parent->reportedVolumeLinear = vol;
        e->parent->postReportedVolume(vol);
    }
}

void PwVolumeControllerPrivate::onProxyRemoved(void *object) {
    // The registry sends us a global_remove right before this; nothing to do.
    Q_UNUSED(object);
}

void PwVolumeControllerPrivate::applyToAllNodes() {
    for (auto *e : std::as_const(nodes)) applyToNode(e);
}

void PwVolumeControllerPrivate::applyToNode(NodeEntry *e) {
    if (!e || !e->proxy) return;

    const int channels = 2;
    float vols[channels];
    for (int i = 0; i < channels; ++i)
        vols[i] = static_cast<float>(targetVolumeLinear);

    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);

    spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
    spa_pod_builder_bool(&b, targetMuted);

    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, channels, vols);

    auto *param = static_cast<spa_pod*>(spa_pod_builder_pop(&b, &f));
    pw_node_set_param(reinterpret_cast<pw_node*>(e->proxy), SPA_PARAM_Props, 0, param);
}

void PwVolumeControllerPrivate::postReportedVolume(double linear) {
    QPointer<PwVolumeController> guard(q);
    QMetaObject::invokeMethod(q, [this, guard, linear]() {
        if (!guard) return;
        if (std::fabs(linear - targetVolumeLinear) < 1e-4) return;
        targetVolumeLinear = linear;
        emit q->volumeChanged();
    }, Qt::QueuedConnection);
}

void PwVolumeControllerPrivate::postReportedMuted(bool muted) {
    QPointer<PwVolumeController> guard(q);
    QMetaObject::invokeMethod(q, [this, guard, muted]() {
        if (!guard) return;
        if (targetMuted == muted) return;
        targetMuted = muted;
        emit q->mutedChanged();
    }, Qt::QueuedConnection);
}

PwVolumeController::PwVolumeController(QObject *parent)
    : QObject(parent), d(new PwVolumeControllerPrivate) {
    d->q = this;
    d->pidStr = QString::number(::getpid());

    pw_init(nullptr, nullptr);

    d->loop = pw_thread_loop_new("yamp-pw-vol", nullptr);
    if (!d->loop) {
        qWarning() << "[PwVolumeController] pw_thread_loop_new failed";
        return;
    }

    pw_thread_loop_lock(d->loop);
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context) {
        qWarning() << "[PwVolumeController] pw_context_new failed";
        pw_thread_loop_unlock(d->loop);
        return;
    }
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (!d->core) {
        qWarning() << "[PwVolumeController] pw_context_connect failed";
        pw_thread_loop_unlock(d->loop);
        return;
    }
    d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
    spa_zero(d->registryListener);
    pw_registry_add_listener(d->registry, &d->registryListener, &kRegistryEvents, d);
    pw_thread_loop_unlock(d->loop);

    if (pw_thread_loop_start(d->loop) < 0) {
        qWarning() << "[PwVolumeController] pw_thread_loop_start failed";
    }
}

PwVolumeController::~PwVolumeController() {
    if (d->loop) {
        pw_thread_loop_lock(d->loop);
        for (auto *e : std::as_const(d->nodes)) d->destroyNode(e);
        d->nodes.clear();
        if (d->registry) {
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(d->registry));
            d->registry = nullptr;
        }
        if (d->core)    { pw_core_disconnect(d->core);   d->core = nullptr; }
        if (d->context) { pw_context_destroy(d->context); d->context = nullptr; }
        pw_thread_loop_unlock(d->loop);
        pw_thread_loop_stop(d->loop);
        pw_thread_loop_destroy(d->loop);
        d->loop = nullptr;
    }
    delete d;
}

double PwVolumeController::volume() const {
    return linearToUser(d->targetVolumeLinear);
}

bool PwVolumeController::isMuted() const {
    return d->targetMuted;
}

void PwVolumeController::setVolume(double v) {
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    const double lin = userToLinear(v);
    if (qFuzzyCompare(lin + 1.0, d->targetVolumeLinear + 1.0)) return;
    d->targetVolumeLinear = lin;
    if (d->loop) {
        pw_thread_loop_lock(d->loop);
        d->applyToAllNodes();
        pw_thread_loop_unlock(d->loop);
    }
    emit volumeChanged();
}

void PwVolumeController::setMuted(bool muted) {
    if (d->targetMuted == muted) return;
    d->targetMuted = muted;
    if (d->loop) {
        pw_thread_loop_lock(d->loop);
        d->applyToAllNodes();
        pw_thread_loop_unlock(d->loop);
    }
    emit mutedChanged();
}
