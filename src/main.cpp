#include <QGuiApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <cstdio>
#include <clocale>

#include "player/MpvItem.h"
#include "player/Show.h"
#include "ui/ClipModel.h"
#include "ui/TimelineModel.h"
#include "ui/Studio.h"

#ifndef VIBESLIDESHOW_VERSION
#define VIBESLIDESHOW_VERSION "0.0.0"
#endif

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
