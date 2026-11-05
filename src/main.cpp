#include <QGuiApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QCoreApplication>
#include <QEventLoop>
#include <QRegularExpression>
#include <cstdio>
#include <clocale>

#include "core/Document.h"
#include "core/Exporter.h"
#include "core/Params.h"
#include "core/RenderPlan.h"
#include "player/MpvItem.h"
#include "player/Show.h"
#include "ui/ClipModel.h"
#include "ui/TimelineModel.h"
#include "ui/Studio.h"

#ifndef VIBESLIDESHOW_VERSION
#define VIBESLIDESHOW_VERSION "0.0.0"
#endif

namespace {

// `vibeslideshow --render project.vibes --out film.mp4`. Handled before any GUI
// object exists: a build machine has no display, and QGuiApplication aborts
// without a platform plugin.
int runRender(int argc, char *argv[], const QString &project, const QString &out,
              const QString &quality, const QString &size) {
    QCoreApplication app(argc, argv);
    // ffmpeg and the filter expressions this builds are formatted in the C locale.
    std::setlocale(LC_NUMERIC, "C");
    if (out.isEmpty()) {
        std::fprintf(stderr, "--render needs --out <file.mp4>\n");
        return 2;
    }
    Document doc;
    QString err;
    if (!loadDocument(project, &doc, &err)) {
        std::fprintf(stderr, "%s\n", qPrintable(err));
        return 1;
    }
    // The project carries its own render settings; a flag overrides them.
    const Params saved = Params::fromMap(doc.params);
    Exporter::Options opt;
    opt.quality = saved.exportQuality <= 0   ? Exporter::Quality::Draft
                  : saved.exportQuality >= 2 ? Exporter::Quality::High
                                             : Exporter::Quality::Balanced;
    opt.size = QSize(std::max(2, saved.exportW), std::max(2, saved.exportH));
    opt.fps = std::max(1, saved.exportFps);
    if (quality == QLatin1String("draft")) opt.quality = Exporter::Quality::Draft;
    else if (quality == QLatin1String("high")) opt.quality = Exporter::Quality::High;
    else if (quality.isEmpty() || quality == QLatin1String("balanced")) {
        // "balanced" is the project's own choice only when it said so.
        if (quality == QLatin1String("balanced")) opt.quality = Exporter::Quality::Balanced;
    } else {
        std::fprintf(stderr, "unknown --quality %s (draft, balanced or high)\n", qPrintable(quality));
        return 2;
    }
    if (!size.isEmpty()) {
        static const QRegularExpression re(QStringLiteral("^(\\d+)x(\\d+)$"));
        const QRegularExpressionMatch m = re.match(size);
        if (!m.hasMatch() || m.captured(1).toInt() <= 0 || m.captured(2).toInt() <= 0) {
            std::fprintf(stderr, "unknown --size %s (WIDTHxHEIGHT, e.g. 1920x1080)\n", qPrintable(size));
            return 2;
        }
        opt.size = QSize(m.captured(1).toInt(), m.captured(2).toInt());
    }

    Exporter exporter;
    QEventLoop loop;
    bool ok = false;
    QString message;
    QObject::connect(&exporter, &Exporter::progress, &loop, [](double frac, const QString &what) {
        std::fprintf(stderr, "\r%3d %%  %s", int(frac * 100), qPrintable(what));
        std::fflush(stderr);
    });
    QObject::connect(&exporter, &Exporter::finished, &loop, [&](bool good, const QString &m) {
        ok = good;
        message = m;
        loop.quit();
    });
    exporter.start(renderSegsFromDocument(doc), doc.song, out, opt);
    loop.exec();
    std::fprintf(stderr, "\r          \r");   // wipe the progress line
    if (!ok) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
        return 1;
    }
    std::printf("%s\n", qPrintable(message));
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // Answer --version before anything else: constructing QGuiApplication needs a
    // platform plugin, and a CI runner (or an ssh session) has no display to give
    // it one — that aborted with SIGABRT before it could print the version.
    for (int i = 1; i < argc; i++) {
        const QByteArray arg(argv[i]);
        if (arg == "--version" || arg == "-v") {
            std::printf("vibeslideshow %s\n", VIBESLIDESHOW_VERSION);
            return 0;
        }
    }

    // --render: a batch job, no window. Parsed and run before QGuiApplication for
    // the same reason as --version above.
    bool wantRender = false;
    QString project, out, quality, size;
    for (int i = 1; i < argc; i++) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        const bool hasNext = i + 1 < argc;
        if (arg == QLatin1String("--render")) {
            wantRender = true;
            if (hasNext) project = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QLatin1String("--out") && hasNext) {
            out = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QLatin1String("--quality") && hasNext) {
            quality = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QLatin1String("--size") && hasNext) {
            size = QString::fromLocal8Bit(argv[++i]);
        }
    }
    if (wantRender) return runRender(argc, argv, project, out, quality, size);

    // MUST happen before the app is constructed: with the Vulkan RHI the content
    // of a QQuickFramebufferObject never reaches the screen (silent black video).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);

    // mpv parses numbers with the C locale and refuses anything else. This MUST
    // come after QGuiApplication: Qt itself calls setlocale(LC_ALL, "") in its
    // constructor and would undo an earlier call.
    std::setlocale(LC_NUMERIC, "C");

    QCoreApplication::setApplicationName(QStringLiteral("vibeslideshow"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VIBESLIDESHOW_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("vibeslideshow"));
    QGuiApplication::setOrganizationName(QStringLiteral("vibeslideshow"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Theme is a QML singleton, MpvItem a creatable type. Everything else the
    // QML needs arrives as a context property from Studio below.
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Theme.qml")), "Vibes", 1, 0, "Theme");
    qmlRegisterType<MpvItem>("Vibes", 1, 0, "MpvItem");

    Studio studio;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("studio"), &studio);
    engine.rootContext()->setContextProperty(QStringLiteral("clips"), studio.clipModel());
    engine.rootContext()->setContextProperty(QStringLiteral("timeline"), studio.timeline());
    engine.rootContext()->setContextProperty(QStringLiteral("showCtl"), studio.show());

    // A packaged bundle keeps its QML modules in qml/ next to the executable, and
    // nothing tells Qt that. Without this the import path list starts at the
    // resource system, where QtQuick.Controls exists but its plugin cannot: the
    // module resolves, the plugin does not, and the app dies at the second line of
    // Studio.qml with "plugin qtquickcontrols2plugin not found". Harmless when
    // there is no such directory (a build tree, a system Qt).
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Studio.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical("vibeslideshow: qrc:/qml/Studio.qml failed to load");
        return 1;
    }

    if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) w->show();

    return app.exec();
}
