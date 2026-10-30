#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <cstdio>
#include <mutex>
namespace bt {
inline QString &startupLogPath() {
    static QString path;
    return path;
}
inline void startupLog(const QString &text) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    QFile file(startupLogPath());
    if (file.open(QFile::WriteOnly | QFile::Append)) {
        auto line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " [" +
                     QString::number(QCoreApplication::applicationPid()) + "] " + text + "\n")
                        .toUtf8();
        file.write(line);
        file.flush();
    }
    auto bytes = text.toUtf8();
    std::fprintf(stderr, "%s\n", bytes.constData());
    std::fflush(stderr);
}
inline void installStartupLog() {
#ifdef _WIN32
    QString root = qEnvironmentVariable("LOCALAPPDATA");
#else
    QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
    QString dir = root + "/BTStudio/BTKaraoke/logs";
    if (root.isEmpty() || !QDir().mkpath(dir)) {
        dir = QDir::tempPath() + "/BT-Karaoke-logs";
        QDir().mkpath(dir);
    }
    startupLogPath() = dir + "/startup.log";
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
        startupLog((type == QtFatalMsg ? "FATAL: " : "") + message);
    });
    startupLog("BT Karaoke process starting");
}
} // namespace bt
