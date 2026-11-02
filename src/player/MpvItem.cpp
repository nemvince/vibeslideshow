#include "player/MpvItem.h"

#include <QOpenGLContext>
#include <QMetaObject>
#include <QDebug>
#include <algorithm>

namespace {

void *getProcAddress(void *, const char *name) {
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    return gl ? reinterpret_cast<void *>(gl->getProcAddress(name)) : nullptr;
}

} // namespace

MpvItem::MpvItem(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    setFlag(QQuickItem::ItemHasContents, true);
    {
        std::lock_guard<std::mutex> lock(m_state->mtx);
        m_state->item = this;
    }
    m_mpv = mpv_create();
    if (!m_mpv) {
        qCritical("vibeslideshow: could not create the mpv instance");
        return;
    }
    applyOptions();
    if (mpv_initialize(m_mpv) < 0) {
        qCritical("vibeslideshow: could not initialise mpv");
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    // How much media is already buffered ahead. The show engine waits on this
    // instead of guessing how long a slow drive needs.
    mpv_observe_property(m_mpv, 0, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(m_mpv, &MpvItem::wakeupCb, this);
}

MpvItem::~MpvItem() {
    {
        std::lock_guard<std::mutex> lock(m_state->mtx);
        m_state->dead = true;
        m_state->item = nullptr;
    }
    if (!m_mpv) return;
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    // The renderer frees the render context on the render thread while this item
    // (and the handle) is torn down on the GUI thread; Qt destroys the scene
    // graph's renderers before the window's items, so the context is already
    // gone by the time this runs.
    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

void MpvItem::applyOptions() {
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "idle", "yes");
    mpv_set_option_string(m_mpv, "keep-open", "no");
    // A surface only ever shows pictures: the song plays on Show's audio-only
    // instance, and a second soundtrack would drift against the cuts.
    mpv_set_option_string(m_mpv, "audio", "no");
    mpv_set_option_string(m_mpv, "audio-display", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "256MiB");
    mpv_set_option_string(m_mpv, "demuxer-readahead-secs", "20");
    mpv_set_option_string(m_mpv, "cache", "yes");
    // These surfaces carry no soundtrack, so they MUST NOT sync to audio: with
    // video-sync=audio and no audio track mpv has no clock at all and freezes on
    // the frame it just seeked to. "desync" paces them off their own timer, which
    // is what we want — the show engine already owns the real timeline and
    // corrects drift.
    mpv_set_option_string(m_mpv, "video-sync", "desync");
    mpv_set_option_string(m_mpv, "hr-seek", "yes");
    mpv_set_option_string(m_mpv, "hr-seek-demuxer-offset", "0");
}

void MpvItem::wakeupCb(void *ctx) {
    auto *item = static_cast<MpvItem *>(ctx);
    QMetaObject::invokeMethod(item, "pump", Qt::QueuedConnection);
}

void MpvItem::pump() {
    pollEvents();
    maybeLoad();
    update();
}

void MpvItem::pollEvents() {
    if (!m_mpv) return;
    for (;;) {
        mpv_event *e = mpv_wait_event(m_mpv, 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id != MPV_EVENT_PROPERTY_CHANGE) continue;
        auto *p = static_cast<mpv_event_property *>(e->data);
        if (!p || !p->name) continue;
        const QString name = QString::fromUtf8(p->name);
        if (p->format == MPV_FORMAT_DOUBLE && p->data && name == QLatin1String("time-pos")) {
            m_frameTime = *static_cast<double *>(p->data);
        } else if (p->format == MPV_FORMAT_DOUBLE && p->data && name == QLatin1String("demuxer-cache-duration")) {
            m_buffered = *static_cast<double *>(p->data);
            m_haveBufferInfo = true;
        } else if (p->format == MPV_FORMAT_FLAG && p->data && name == QLatin1String("pause")) {
            m_paused = *static_cast<int *>(p->data) != 0;
        }
    }
}

void MpvItem::loadAndSeek(const QString &path, double t, double readaheadSecs) {
    if (!m_mpv || path.isEmpty()) return;
    m_path = path;
    setReadahead(readaheadSecs);

    m_pendingPath = path;
    m_pendingSeek = t;
    m_pendingLoad = true;
    // The surface should report the new time immediately: a drift correction
    // right after a cut must not compare against the previous clip's clock.
    m_frameTime = t;
    // A new file starts with nothing buffered; the old value is meaningless.
    m_buffered = -1;
    m_haveBufferInfo = false;
    maybeLoad();
}

void MpvItem::setReadahead(double readaheadSecs) {
    if (!m_mpv) return;
    // Readahead defaults assume a local disk. On a network mount, asking for 20 s
    // of buffer means the reader competes with the decoder for a slow link and
    // the picture stalls on the frame it already had.
    const bool remote = m_path.contains(QLatin1String("/gvfs/")) || m_path.startsWith(QLatin1String("//")) ||
                        m_path.startsWith(QLatin1String("/mnt/")) || m_path.startsWith(QLatin1String("/media/"));
    const double secs = readaheadSecs > 0 ? readaheadSecs : (remote ? 4.0 : 20.0);
    const QByteArray secsText = QByteArray::number(secs, 'f', 1);
    mpv_set_property_string(m_mpv, "demuxer-readahead-secs", secsText.constData());
    mpv_set_property_string(m_mpv, "demuxer-max-bytes", remote ? "48MiB" : "256MiB");
}

void MpvItem::maybeLoad() {
    if (!m_mpv || !m_pendingLoad) return;
    // Invariant 1: loading before the render context exists gives "no video" and
    // a permanently black surface.
    if (!m_state->ready.load()) return;
    m_pendingLoad = false;
    const QByteArray path = m_pendingPath.toUtf8();
    // The in-point MUST travel as loadfile's own start option. A separate seek
    // command issued straight after loadfile is silently discarded: at that
    // moment the file is not open yet, so there is nothing to seek.
    //
    // loadfile's argv is: loadfile <url> [<flags> [<index> [<options>]]]. The
    // options string is the FIFTH argument; putting it fourth makes mpv read it
    // as the playlist index ("The loadfile option must be an integer") and the
    // whole load is rejected.
    const QByteArray startVal = QByteArrayLiteral("start=") + QByteArray::number(std::max(0.0, m_pendingSeek), 'f', 3);
    const char *cmd[] = {"loadfile", path.constData(), "replace", "-1", startVal.constData(), nullptr};
    mpv_command(m_mpv, cmd);
}

void MpvItem::clear() {
    m_pendingPath.clear();
    m_pendingLoad = false;
    m_pendingSeek = -1;
    m_frameTime = 0;
    if (!m_mpv) return;
    mpv_command_string(m_mpv, "stop");
}

void MpvItem::seekExact(double t) {
    if (!m_mpv) return;
    const QByteArray v = QByteArray::number(t, 'f', 3);
    const char *cmd[] = {"seek", v.constData(), "absolute+exact", nullptr};
    mpv_command(m_mpv, cmd);
}

void MpvItem::setPaused(bool p) {
    if (!m_mpv) return;
    int f = p ? 1 : 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &f);
}

QQuickFramebufferObject::Renderer *MpvItem::createRenderer() const {
    return new MpvRenderer(const_cast<MpvItem *>(this));
}

// ---------------------------------------------------------------- renderer

MpvRenderer::MpvRenderer(MpvItem *item) : m_state(item->renderState()), m_mpv(item->mpv()) {}

MpvRenderer::~MpvRenderer() {
    if (m_ctx) {
        mpv_render_context_set_update_callback(m_ctx, nullptr, nullptr);
        mpv_render_context_free(m_ctx);
        m_ctx = nullptr;
        m_state->ready = false;
    }
    // The FBO is owned by Qt: it is deleted when the item is resized and on
    // teardown. Freeing it here would be a double free.
    m_fbo = nullptr;
}

void MpvRenderer::onUpdate(void *ctx) {
    auto *r = static_cast<MpvRenderer *>(ctx);
    const std::shared_ptr<MpvRenderState> &st = r->m_state;
    std::lock_guard<std::mutex> lock(st->mtx);
    if (st->dead || !st->item) return;
    QMetaObject::invokeMethod(st->item, "pump", Qt::QueuedConnection);
}

QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject(const QSize &size) {
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    m_fbo = new QOpenGLFramebufferObject(size, fmt);
    return m_fbo;
}

void MpvRenderer::render() {
    if (!m_glReady) {
        initializeOpenGLFunctions();
        m_glReady = true;
    }
    if (!m_ctx) {
        if (!m_mpv) return;
        mpv_opengl_init_params gl{getProcAddress, nullptr};
        int advanced = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&m_ctx, m_mpv, params) < 0) {
            qWarning("vibeslideshow: could not create the mpv render context");
            return;
        }
        mpv_render_context_set_update_callback(m_ctx, &MpvRenderer::onUpdate, this);
        m_state->ready = true;
        // Now that the context exists a deferred load can finally run.
        std::lock_guard<std::mutex> lock(m_state->mtx);
        if (!m_state->dead && m_state->item)
            QMetaObject::invokeMethod(m_state->item, "pump", Qt::QueuedConnection);
    }
    if (!m_fbo || !m_ctx) return;

    // Under ADVANCED_CONTROL mpv maintains its own frame clock and tells us when
    // a new frame is ready. Rendering on every request -- the song clock pumps
    // this at 40 Hz -- desynchronises that clock and the video crawls along at a
    // fraction of real time. Only draw when mpv asks for a frame.
    if (!(mpv_render_context_update(m_ctx) & MPV_RENDER_UPDATE_FRAME)) return;

    m_fbo->bind();
    const QSize px = m_fbo->size();
    // mpv leaves GL state behind that breaks the scene graph's own compositing.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glViewport(0, 0, px.width(), px.height());

    mpv_opengl_fbo fbo{int(m_fbo->handle()), px.width(), px.height(), 0};
    int flip = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_ctx, params);
    mpv_render_context_report_swap(m_ctx);

    m_fbo->release();
}
