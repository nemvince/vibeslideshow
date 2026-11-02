#pragma once
#include <QAbstractListModel>
#include <QVariantList>
#include "core/Types.h"
#include "core/Params.h"

class Library;
class Scheduler;
class ClipModel;

// The bottom strip: the edit as tiling segments. Owns the NLE operations
// (shorten / lengthen / chop / re-roll in-point / swap / delete) and keeps the
// invariant that segment lengths sum to the song length.
class TimelineModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(int edits READ edits NOTIFY editsChanged)
    Q_PROPERTY(QString selectionInfo READ selectionInfo NOTIFY selectedChanged)
    Q_PROPERTY(QString stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(bool hasTimeline READ hasTimeline NOTIFY countChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // Length the edit was cut to. The player's duration can still be unknown while
    // mpv opens the file; layout must not depend on it.
    Q_PROPERTY(double songDuration READ songDuration NOTIFY songDurationChanged)
    // Every clip operation, described once. The transport toolbar, the right-click
    // menu and the keyboard all read this table, so an operation is added here (and
    // in runCommand) and shows up in all three. Fields:
    //   id     stable name used by runCommand()/commandKey()
    //   label  what the menu says, in the panel's plain voice
    //   glyph  the icon, in the same unicode set IconButton already uses
    //   keys   QKeySequence portable text; "" means no shortcut
    //   group  "timing" | "clip" | "destructive" | "all"; a group change draws a separator
    //   bar    show it in the transport toolbar
    //   needs  "selection" | "timeline" — what has to exist for it to apply
    Q_PROPERTY(QVariantList commands READ commands CONSTANT)
public:
    enum Roles {
        SegIdRole = Qt::UserRole + 1, SegStartRole, SegLenRole, SegInRole,
        SegThumbRole, SegScoreRole, SegNameRole, SegColorRole, SegIndexRole, SegSelectedRole
    };
    static constexpr double TlMin = 0.05;
    static constexpr double TlStep = 0.25;

    explicit TimelineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLibrary(Library *lib) { m_lib = lib; }
    void setScheduler(Scheduler *s) { m_sched = s; }
    // The strip shares the rail's thumbnails rather than decoding its own.
    void setClipModel(ClipModel *cm) { m_clips = cm; }
    void setSongDuration(double d);
    double songDuration() const { return m_songDur; }

    int selected() const { return m_sel; }
    void setSelected(int i);
    int edits() const { return m_edits; }
    bool hasTimeline() const { return !m_segs.isEmpty(); }
    QString selectionInfo() const;
    QString stats() const;
    // Song time where a segment starts, for scrubber tick marks.
    Q_INVOKABLE double cutTime(int row) const;
    // Which segment covers `songTime` (the timeline's own hit test).
    Q_INVOKABLE int indexAt(double songTime) const;

    const QList<TlSeg> &segments() const { return m_segs; }
    // Crossfade for each segment, in milliseconds, parallel to segments().
    const QList<double> &fades() const { return m_fades; }

    // Replace everything with a freshly generated edit.
    Q_INVOKABLE void regenerate(const QList<int> &poolIds);
    // Cut times implied by the segment lengths (prefix sums).
    QList<double> cuts() const;

    Q_INVOKABLE void shorten();
    Q_INVOKABLE void lengthen();
    Q_INVOKABLE void chop();
    Q_INVOKABLE void rerollIn();
    Q_INVOKABLE void swap();
    Q_INVOKABLE void remove();
    // Footage that left the project: every shot cut from it leaves the strip and
    // its time goes to the last shot, so the song stays covered.
    void dropClips(const QList<int> &clipIds);
    // Put a specific clip into a segment (drag-and-drop / new in-point).
    Q_INVOKABLE void replaceSegment(int row, int clipId, double inPoint);
    Q_INVOKABLE void rerollAll();
    Q_INVOKABLE QString thumbForSegment(int index) const;
    // The edit's own history. Every mutating operation pushes a snapshot before it
    // changes anything, so undo/redo restore an exact previous state — the three
    // lists are small and there are no command objects to keep in step.
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Replace the whole edit with one loaded from a document. No history: it is a
    // different edit, not a step inside this one.
    void adoptEdit(const QList<TlSeg> &segs, const QList<double> &fades);

    QVariantList commands() const;
    Q_INVOKABLE QString commandKey(const QString &id) const;
    Q_INVOKABLE void runCommand(const QString &id);

signals:
    void countChanged();
    void songDurationChanged();
    void selectedChanged();
    void editsChanged();
    void statsChanged();
    void changed();   // structure or lengths changed: re-render the stage
    void historyChanged();

private:
    // Everything a mutator can change, captured in one value.
    struct Snapshot {
        QList<TlSeg> segs;
        QList<double> fades;
        QList<int> poolIds;
        int sel = -1;
        int edits = 0;
    };
    static constexpr int kHistoryDepth = 100;
    void pushUndo();          // call after the guards, before the first mutation
    void restore(const Snapshot &snap);
    void clearHistory();
    QList<Snapshot> m_undo, m_redo;
    void touch();
    void applyPool(const QList<int> &poolIds);

    Library *m_lib = nullptr;
    Scheduler *m_sched = nullptr;
    ClipModel *m_clips = nullptr;
    QList<TlSeg> m_segs;
    QList<double> m_fades;   // parallel to m_segs
    QList<int> m_poolIds;
    double m_songDur = 0;
    int m_sel = -1;
    int m_edits = 0;
};
