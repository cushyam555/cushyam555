#include "SongLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>
#include <stdexcept>
namespace bt {
namespace {
void checked(bool ok, const QString &message) {
    if (!ok)
        throw std::runtime_error(message.toUtf8().constData());
}
QString keyFor(const QString &path) {
    auto key = QFileInfo(path).canonicalFilePath();
    if (key.isEmpty())
        key = QFileInfo(path).absoluteFilePath();
#ifdef _WIN32
    key = key.toCaseFolded();
#endif
    return key;
}
bool identical(const QString &a, const QString &b) {
    QFile first(a), second(b);
    if (!first.open(QFile::ReadOnly) || !second.open(QFile::ReadOnly) ||
        first.size() != second.size())
        return false;
    while (!first.atEnd())
        if (first.read(65536) != second.read(65536))
            return false;
    return true;
}
} // namespace
QString applicationDataPath() {
    auto overridePath = qEnvironmentVariable("BT_KARAOKE_DATA_DIR");
    return overridePath.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               : overridePath;
}
SongLibrary::SongLibrary(const QString &file) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    connection = "bt-library-" + QUuid::createUuid().toString();
    database = QSqlDatabase::addDatabase("QSQLITE", connection);
    database.setDatabaseName(file);
    if (!database.open()) {
        auto message = database.lastError().text();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
        throw std::runtime_error(message.toUtf8().constData());
    }
    QSqlQuery q(database);
    checked(q.exec("CREATE TABLE IF NOT EXISTS songs (pathkey TEXT PRIMARY KEY,path TEXT NOT "
                   "NULL,code TEXT,title TEXT,artist TEXT,musicalkey TEXT,bpm REAL,duration REAL)"),
            q.lastError().text());
}
SongLibrary::~SongLibrary() {
    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
}
void SongLibrary::beginBatch() {
    if (!database.transaction())
        throw std::runtime_error(database.lastError().text().toUtf8().constData());
}
void SongLibrary::endBatch() {
    if (!database.commit()) {
        auto message = database.lastError().text();
        database.rollback();
        throw std::runtime_error(message.toUtf8().constData());
    }
}
void SongLibrary::put(const Song &s) {
    QSqlQuery q(database);
    q.prepare("INSERT OR REPLACE INTO songs VALUES (?,?,?,?,?,?,?,?)");
    q.addBindValue(keyFor(s.path));
    q.addBindValue(s.path);
    q.addBindValue(s.id);
    q.addBindValue(s.title);
    q.addBindValue(s.artist);
    q.addBindValue(s.key);
    q.addBindValue(s.bpm);
    q.addBindValue(s.duration);
    checked(q.exec(), q.lastError().text());
}
QVector<Song> SongLibrary::entries() const {
    QSqlQuery q(database);
    checked(q.exec("SELECT path,code,title,artist,musicalkey,bpm,duration FROM songs ORDER BY code "
                   "COLLATE NOCASE,title COLLATE NOCASE"),
            q.lastError().text());
    QVector<Song> songs;
    while (q.next()) {
        Song s;
        s.path = q.value(0).toString();
        s.id = q.value(1).toString();
        s.title = q.value(2).toString();
        s.artist = q.value(3).toString();
        s.key = q.value(4).toString();
        s.bpm = q.value(5).toDouble();
        s.duration = q.value(6).toDouble();
        songs.push_back(s);
    }
    return songs;
}
void SongLibrary::removeMissing() {
    auto songs = entries();
    for (const auto &s : songs)
        if (!QFileInfo::exists(s.path)) {
            QSqlQuery q(database);
            q.prepare("DELETE FROM songs WHERE pathkey=?");
            q.addBindValue(keyFor(s.path));
            checked(q.exec(), q.lastError().text());
        }
}
QString SongLibrary::copyNcn(const QString &midi, const QString &root) {
    auto files = ncnFiles(midi);
    checked(!files.midi.isEmpty() && !files.lyrics.isEmpty() && !files.cursor.isEmpty(),
            "NCN requires matching Song, Lyrics and Cursor files");
    // Validate all source data before creating any destination file.
    readSong(midi);
    auto relative = QDir::cleanPath(files.relative);
    checked(!QDir::isAbsolutePath(relative) && relative != ".." && !relative.startsWith("../"),
            "Invalid NCN relative path");
    QFileInfo info(relative);
    auto base = relative.left(relative.size() - info.suffix().size() - 1);
    QStringList sources{files.midi, files.lyrics, files.cursor};
    QStringList targets{QDir(root).filePath("Song/" + base + ".mid"),
                        QDir(root).filePath("Lyrics/" + base + ".lyr"),
                        QDir(root).filePath("Cursor/" + base + ".cur")};
    for (int i = 0; i < 3; ++i)
        if (QFile::exists(targets[i]))
            checked(identical(sources[i], targets[i]),
                    "NCN filename already exists with different contents: " + targets[i]);
    QStringList created;
    try {
        for (int i = 0; i < 3; ++i) {
            if (QFile::exists(targets[i]))
                continue;
            checked(QDir().mkpath(QFileInfo(targets[i]).absolutePath()),
                    "Cannot create NCN folder");
            QFile source(sources[i]);
            checked(source.open(QFile::ReadOnly), source.errorString());
            QSaveFile target(targets[i]);
            checked(target.open(QFile::WriteOnly), target.errorString());
            while (!source.atEnd()) {
                auto block = source.read(65536);
                checked(source.error() == QFile::NoError, source.errorString());
                checked(target.write(block) == block.size(), target.errorString());
            }
            checked(target.commit(), target.errorString());
            created << targets[i];
        }
        readSong(targets[0]);
    } catch (...) {
        for (const auto &path : created)
            QFile::remove(path);
        throw;
    }
    return targets[0];
}
} // namespace bt
