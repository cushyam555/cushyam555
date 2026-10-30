#include "StartupLog.h"
#include "plugins/VstEffect.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QPixmap>
#include <QSaveFile>
#include <QTimer>
#include <cstring>
int main(int argc, char **argv) {
    QCoreApplication::setApplicationName("BT Karaoke");
    QCoreApplication::setOrganizationName("BT Studio");
    QCoreApplication::setApplicationVersion("0.2.0");
    bt::installStartupLog();
    try {
        bool probe = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--audio-probe") == 0)
                probe = true;
        if (probe) {
            QCoreApplication app(argc, argv);
            bt::Engine engine;
            QString error;
            bt::startupLog("BT_AUDIO_PROBE initializing device");
            bool ready = engine.startAudio(error);
            engine.stopAudio();
            bt::startupLog(ready ? "BT_AUDIO_PROBE PASS" : "BT_AUDIO_PROBE FAIL: " + error);
            return ready ? 0 : 4;
        }
        QApplication app(argc, argv);
        const auto args = app.arguments();
        if (args.contains("--scan-vst3")) {
            int i = args.indexOf("--scan-vst3");
            if (i + 1 >= args.size())
                return 2;
            auto json = QJsonDocument(bt::scanVst3(args[i + 1])).toJson(QJsonDocument::Compact);
            if (i + 2 < args.size()) {
                QSaveFile out(args[i + 2]);
                if (!out.open(QFile::WriteOnly) || out.write(json) < 0 || !out.commit())
                    return 3;
            } else
                std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
            return 0;
        }
        const bool smoke = args.contains("--smoke-test"), startup = args.contains("--startup-test");
        const bool safe = args.contains("--safe-mode") || smoke;
        bt::startupLog(safe ? "Constructing window in safe mode"
                            : "Constructing window before audio initialization");
        bt::MainWindow w(safe);
        w.setWindowIcon(QIcon(":/bt.ico"));
        if (args.contains("--mixer"))
            w.selectPage(1);
        w.show();
        bt::startupLog("BT_WINDOW_SHOWN");
        if (smoke)
            QTimer::singleShot(400, &app, [&] {
                bool ready = w.isVisible() && w.smokeReady();
                int shot = args.indexOf("--screenshot");
                if (shot >= 0 && shot + 1 < args.size())
                    ready = w.grab().save(args[shot + 1]) && ready;
                bt::startupLog(ready ? "BT_UI_SMOKE PASS" : "BT_UI_SMOKE FAIL");
                app.exit(ready ? 0 : 1);
            });
        if (startup) {
            auto *timer = new QTimer(&app);
            timer->setInterval(100);
            auto ticks = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, &w, [&, timer, ticks] {
                ++*ticks;
                if (w.startupSettled() || *ticks >= 180) {
                    timer->stop();
                    bool ready = w.isVisible() && w.smokeReady();
                    bt::startupLog(ready ? "BT_STARTUP_VISIBLE PASS" : "BT_STARTUP_VISIBLE FAIL");
                    app.exit(ready ? 0 : 1);
                }
            });
            timer->start();
        }
        return app.exec();
    } catch (const std::exception &e) {
        bt::startupLog(QString("Startup exception: ") + e.what());
        return 1;
    }
}
