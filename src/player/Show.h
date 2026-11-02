#pragma once
#include "core/Types.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QList>
#include <mpv/client.h>

class Scheduler;
class Library;
class MpvItem;

// Plays an edit: the song is the master clock, video surfaces crossfade at cut
// boundaries. Only the surfaces' own mpv instances show pictures; a third
// audio-only mpv instance owns the timeline, so a cut can never disturb the song.
//
// More surfaces are attached than the stage can show at once. Each spare one is a
// lookahead slot holding an upcoming shot already open, seeked and decoded, so a
// cut is a swap rather than a file open — which is the only way a quarter-second
// shot can land on time.
class Show : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString songTitle READ songTitle NOTIFY songChanged)
    Q_PROPERTY(QString songPath READ songPath NOTIFY songChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(int cutIndex READ cutIndex NOTIFY cutIndexChanged)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY segmentsChanged)
    Q_PROPERTY(int currentClip READ currentClip NOTIFY currentClipChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY durationChanged)
    // True when the song clock is running but the visible surface stopped
    // producing frames: the file is being fetched, not played.
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
public:
    explicit Show(QObject *parent = nullptr);
    ~Show() override;

    bool playing() const { return m_playing; }
    double position() const { return m_pos; }
    double duration() const { return m_dur; }
    QString positionText() const;
    QString durationText() const;
    double volume() const { return m_volume; }
    void setVolume(double v);
    QString songTitle() const { return m_songTitle; }
    QString songPath() const { return m_song; }
    bool ready() const { return !m_segs.isEmpty(); }
    int cutIndex() const { return m_cutIdx; }
    int segmentCount() const { return m_segs.size(); }
    int currentClip() const { return m_currentClip; }
    bool buffering() const { return m_buffering; }

    void setScheduler(Scheduler *s) { m_sched = s; }
    // Cap the size of every surface's picture, in lines (0 = native). Applies to
    // the surfaces already attached and to any that attach later.
    void setPreviewHeight(int lines);
    void setLibrary(Library *l) { m_lib = l; }

    // QML hands over the video surfaces once they exist. The first starts on
    // screen; the rest are lookahead slots.
    Q_INVOKABLE void attachSurfaces(const QVariantList &surfaces);

    // The prepared edit. Both lists run parallel; fades are milliseconds.
    Q_INVOKABLE void setEdit(const QList<TlSeg> &segs, const QList<double> &cuts, const QList<double> &fades);
    Q_INVOKABLE void setSong(const QString &path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double t);
    // Jump to a segment boundary and stay paused (timeline scrubbing).
    Q_INVOKABLE void cueSegment(int index);

signals:
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void songChanged();
    void readyChanged();
    void bufferingChanged();
    void cutIndexChanged();
    void segmentsChanged();
    void currentClipChanged();
    // `frontIndex` is the surface to show, `fadeMs` how long to take over it.
    void crossfade(int frontIndex, int fadeMs);

private:
    void pollAudio();
    void tick();
    // Open `idx` on a spare surface and show it now.
    void startSegment(int idx, double songTime);
    // Show the shot the song has reached, from an already-warm surface when one
    // holds it: this is the path a cut takes during playback.
    void revealSegment(int idx, double songTime);
    // Replace the picture the visible surface holds with the shot the playhead
    // is over now (after the edit under it changed).
    void refreshPreview();
    // Keep every spare surface loaded with one of the shots coming up next.
    void fillLookahead();
    void releaseSurfaces();
    // Song time → the segment covering it.
    int segmentAt(double songTime) const;
    // Clip time a segment should be showing at `songTime`.
    double targetFor(int idx, double songTime) const;
    // Enough readahead for the shot a warming surface holds, not a whole minute.
    double warmReadahead(int idx) const;
    // Load `segIdx` into `surf` at `clipTime`, recording what it holds.
    void loadInto(int surf, int segIdx, double clipTime, double readaheadSecs);
    // The surface holding segment `idx`'s clip at `clipTime` (within `tolerance`),
    // or -1.
    int surfaceHolding(int idx, double clipTime, double tolerance) const;
    // Index of a surface that may take a new file right now, or -1. The visible
    // one is never a candidate, and neither is one still fading out.
    int spareSurface() const;
    MpvItem *frontSurface() const;
    void setBuffering(bool b);
    // The picture cannot keep up: hold the song instead of running past it.
    void beginWait();
    void resumeFromWait();
    double expectedVideoTime(int idx, double songTime) const;
    QString pathFor(const TlSeg &s, double *dur) const;
    void setPos(double t);
    void emitReadyChanged();

    Scheduler *m_sched = nullptr;
    Library *m_lib = nullptr;
    QList<MpvItem *> m_surfs;
    int m_previewHeight = 0;
    // Per surface: what it holds — the segment index, the clip it was loaded from
    // and the clip time. All three are needed: an edit can keep an index while
    // swapping the clip or moving its in-point, and a stale frame is not warm.
    QList<int> m_holdSeg;
    QList<int> m_holdClip;
    QList<double> m_holdAt;
    // Which surface is on screen now, and which one is still fading out of it.
    int m_frontIdx = -1;
    int m_outgoingIdx = -1;

    mpv_handle *m_audio = nullptr;
    QString m_song;
    QString m_songTitle;
    double m_pos = 0;
    double m_dur = 0;
    double m_volume = 70;
    bool m_playing = false;

    QList<TlSeg> m_segs;
    QList<double> m_cuts;
    QList<double> m_fades;

    int m_cutIdx = 0;          // index of the segment currently on screen
    int m_currentClip = -1;
    // How long the crossfade in progress lasts: loading into a surface that is
    // still fading out would show the next clip through the outgoing one.
    double m_lastFadeMs = 0;
    QTimer m_tick;
    // One-shot: the lookahead could not use a surface that was still fading out,
    // so it comes back once the fade is over.
    QTimer m_fillLater;
    // The drift guard must not fight a cut that is still seeking: right after a
    // swap the surface reports its old time-pos for a frame or two, and chasing
    // that produces a seek storm instead of a correction.
    QElapsedTimer m_sinceCut;
    QElapsedTimer m_sinceCorrection;
    // Stall detection: a surface that has not produced a new frame for a while is
    // fetching, not drifting, and MUST NOT be seeked (a seek on a starved
    // demuxer throws the buffered data away and starts again from the network).
    bool m_buffering = false;
    double m_lastFrameTs = -1;
    QElapsedTimer m_sinceFrameAdvance;
    // Repeated corrections that did not stick: back off instead of hammering.
    int m_seekStreak = 0;
    // Waiting for the file: the song is paused so the picture cannot fall further
    // behind, and `m_resync` marks the single corrective seek issued before the
    // resume so picture and music continue together.
    bool m_waiting = false;
    bool m_resync = false;
    QElapsedTimer m_sinceWaitStart;
};
