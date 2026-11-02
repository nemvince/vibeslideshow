#pragma once
#include "core/Types.h"
#include <QString>

// ffmpeg-decode a song to mono float PCM at 22050 Hz, then find its beats.
class Analyzer {
public:
    static constexpr int SampleRate = 22050;

    // Decodes with ffmpeg. Empty error string on success.
    static bool decodeAudio(const QString &path, QList<float> &pcm, QString *error);

    // Pure function over PCM: no I/O, no state, safe on any thread.
    static Analysis analyze(const QList<float> &pcm, int sr);
};
