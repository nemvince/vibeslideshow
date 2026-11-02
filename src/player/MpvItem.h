#pragma once
#include <QQuickFramebufferObject>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QSize>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <atomic>
#include <memory>
#include <mutex>

class MpvItem;

// What the render thread may publish and the GUI thread may read. Sharing this
// instead of poking at the item keeps the two threads off each other's objects:
// the renderer is destroyed by the scene graph, which is not always before the
// item it belongs to.
struct MpvRenderState {
    std::atomic<bool> ready{false};   // render context exists; loadfile is allowed
    std::atomic<bool> dead{false};    // item is going away; the renderer must not touch it
    // Height of the last framebuffer the render thread created, in device pixels.
    // Published so the preview cap can be observed instead of assumed.
    std::atomic<int> fboHeight{0};
    // The cap in device pixels (0 = native), read by the render thread. Kept in
    // pixels because the render thread has no item to ask about the screen.
    std::atomic<int> capPixels{0};
    // The item the renderer posts "a new frame is ready" to. Guarded because the
    // item dies on the GUI thread while the renderer runs on the render thread.
    std::mutex mtx;
    MpvItem *item = nullptr;
};

// A single mpv instance rendered into a Qt Quick framebuffer object.
//
// Three invariants were measured the hard way and must not be "simplified":
//  1. The mpv render context only exists after the scene graph's first render
//     pass, so `loadfile` is deferred until then (no render context = "no video").
//  2. mpv must be told to draw into *our* FBO id and viewport; asking the
//     context for its default FBO yields 0 and mpv paints the window backbuffer
//     instead, which the scene graph never composites.
//  3. The FBO size is in device pixels while the item is in logical pixels.
//
// Video surfaces never play audio: the song owns the soundtrack on its own mpv
// instance in Show, so a cut can never perturb the timeline.
class MpvItem : public QQuickFramebufferObject {
    Q_OBJECT
public:
    explicit MpvItem(QQuickItem *parent = nullptr);
    ~MpvItem() override;

    Renderer *createRenderer() const override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;

    // Load a file and seek it to `t` in one ordered command batch. Safe to call
    // before the render context exists: the load waits for it. `readaheadSecs`
    // overrides how much mpv buffers ahead; negative means the default for where
    // the file lives (a surface only being kept warm needs far less than one that
    // is playing).
    void loadAndSeek(const QString &path, double t, double readaheadSecs = -1);
    // Change how far mpv reads ahead on the file this surface holds. Negative
    // restores the default for the file's location.
    void setReadahead(double readaheadSecs);
    // Blank this surface (show stopped).
    void clear();
    // Jump to an exact frame time.
    void seekExact(double t);
    void setPaused(bool p);
    bool paused() const { return m_paused; }
    // Ken Burns at playback: mpv's own video-zoom / video-pan-x / video-pan-y.
    // zoom is log2 (mpv's unit), so 1.1× is 0.1375.
    void setView(double zoomLog2, double panX, double panY);
    double viewZoom() const { return m_viewZoom; }   // read back for tests
    // Current frame time in seconds (mpv's own clock for this surface).
    double frameTime() const { return m_frameTime; }
    // Seconds of media buffered ahead of the current position (mpv's demuxer
    // cache). Negative when mpv has not reported yet.
    double bufferedSecs() const { return m_buffered; }
    bool hasBufferInfo() const { return m_haveBufferInfo; }
    // Cap the size of this surface's picture, in lines (0 = native). Two things
    // follow: the decoded frame is scaled down before it reaches the GPU, which is
    // where a 4K file's cost per cut lives, and the render target shrinks to match
    // on a display big enough for the cap to bind. Neither upscales.
    void setPreviewHeight(int lines);
    // Height of the framebuffer in device pixels, as last created by the render
    // thread. 0 until the surface has rendered once.
    int renderHeight() const { return m_state->fboHeight.load(); }
    // The cap as it reaches the render thread, in device pixels (0 = uncapped).
    int capPixels() const { return m_state->capPixels.load(); }
    // The filter chain mpv holds after the last cap change, as mpv reports it.
    // Read back rather than assumed: a rejected filter would be invisible.
    QString previewFilter() const { return m_previewFilter; }
    // Shrink a framebuffer request to the cap, keeping the aspect. Static and pure
    // so the arithmetic can be checked without a GPU.
    static QSize cappedTarget(const QSize &size, int capPixels);

    // Ask Qt for one more frame on this surface. With ADVANCED_CONTROL mpv only
    // advances its decoder when render() runs, so the show engine pumps the
    // visible surface off the song clock instead of relying on wakeups alone.
    void requestFrame() { update(); }

    std::shared_ptr<MpvRenderState> renderState() const { return m_state; }
    int previewHeight() const { return m_previewHeight; }
    mpv_handle *mpv() const { return m_mpv; }

public slots:
    void pump();

private:
    friend class MpvRenderer;
    void applyOptions();
    // Recompute the device-pixel cap from the item's screen; 0 when uncapped.
    void updateCapPixels();
    void maybeLoad();
    void pollEvents();
    static void wakeupCb(void *ctx);

    mpv_handle *m_mpv = nullptr;
    std::shared_ptr<MpvRenderState> m_state = std::make_shared<MpvRenderState>();

    QString m_pendingPath;
    bool m_pendingLoad = false;
    double m_pendingSeek = -1;
    // What this surface holds, for deciding the readahead its location allows.
    QString m_path;

    int m_previewHeight = 0;
    QString m_previewFilter;
    double m_frameTime = 0;
    double m_buffered = -1;
    bool m_haveBufferInfo = false;
    bool m_paused = true;
    double m_viewZoom = 0;
};

class MpvRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
public:
    explicit MpvRenderer(MpvItem *item);
    ~MpvRenderer() override;

    void render() override;
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override;

private:
    std::shared_ptr<MpvRenderState> m_state;
    mpv_handle *m_mpv = nullptr;
    QOpenGLFramebufferObject *m_fbo = nullptr;
    mpv_render_context *m_ctx = nullptr;
    bool m_glReady = false;
    static void onUpdate(void *ctx);
};
