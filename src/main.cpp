#include "plugins/VstEffect.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QPixmap>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("BT Karaoke");
    app.setOrganizationName("BT Studio");
    app.setApplicationVersion("0.1.0");
    auto args = app.arguments();
    try {
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
        bool smoke = args.contains("--smoke-test");
        bt::MainWindow w(smoke);
        w.setWindowIcon(QIcon(":/bt.ico"));
        if (args.contains("--mixer"))
            w.selectPage(1);
        w.show();
        if (smoke)
            QTimer::singleShot(400, &app, [&] {
                bool ready = w.isVisible() && w.smokeReady();
                int shot = args.indexOf("--screenshot");
                if (shot >= 0 && shot + 1 < args.size())
                    ready = w.grab().save(args[shot + 1]) && ready;
                std::fprintf(stderr, "BT_UI_SMOKE %s\n", ready ? "PASS" : "FAIL");
                app.exit(ready ? 0 : 1);
            });
        return app.exec();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "BT Karaoke: %s\n", e.what());
        return 1;
    }
}
