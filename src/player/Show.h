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

// Plays an edit: the song is the master clock, two video surfaces crossfade at
// cut boundaries. Only the surfaces' own mpv instances show pictures; a third
// audio-only mpv instance owns the timeline, so a cut can never disturb the song.
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
    void setLibrary(Library *l) { m_lib = l; }

    // QML hands over the two video surfaces once they exist.
    Q_INVOKABLE void attachSurfaces(QObject *a, QObject *b);

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
    // The incoming surface (`incomingIsA`) must fade in over `fadeMs`.
    void crossfade(bool incomingIsA, int fadeMs);

private:
    void pollAudio();
    void tick();
    void startSegment(int idx, double songTime);
    void preloadNext(int idx);
    void maybePreload();
    void revealCut(int next);
    void setBuffering(bool b);
    double expectedVideoTime(int idx, double songTime) const;
    QString pathFor(const TlSeg &s, double *dur) const;
    void setPos(double t);
    void emitReadyChanged();

    Scheduler *m_sched = nullptr;
    Library *m_lib = nullptr;
    MpvItem *m_a = nullptr;
    MpvItem *m_b = nullptr;

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
    bool m_aIsFront = true;
    int m_preloaded = -1;      // segment index resident on the back surface
    // The next segment to load onto the back surface, and how long the current
    // crossfade lasts: loading into a surface that is still fading out would
    // show the next clip through the outgoing one.
    int m_preloadPending = -1;
    double m_lastFadeMs = 0;
    QTimer m_tick;
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
};
