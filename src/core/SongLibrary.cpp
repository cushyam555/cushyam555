#include "SongLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
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
void SongLibrary::replaceAll(const QVector<Song> &songs,
                             const std::function<bool(int, int, const QString &)> &progress) {
    beginBatch();
    try {
        QSqlQuery clear(database);
        if (!clear.exec("DELETE FROM songs"))
            throw std::runtime_error(clear.lastError().text().toUtf8().constData());
        for (int i = 0; i < songs.size(); ++i) {
            if (progress && !progress(i, songs.size(), songs[i].path))
                throw std::runtime_error("ยกเลิกการปรับปรุงฐานข้อมูล");
            put(songs[i]);
        }
        if (progress && !progress(songs.size(), songs.size(), QString()))
            throw std::runtime_error("ยกเลิกการปรับปรุงฐานข้อมูล");
        endBatch();
    } catch (...) {
        database.rollback();
        throw;
    }
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
namespace {
void replaceFiles(const QStringList &sources, const QStringList &targets, bool overwrite) {
    for (int i = 0; i < targets.size(); ++i)
        if (QFile::exists(targets[i]) && !overwrite)
            checked(identical(sources[i], targets[i]),
                    "Duplicate filename requires Skip or Overwrite: " + targets[i]);
    auto root = QFileInfo(targets.first()).absolutePath();
    checked(QDir().mkpath(root), "Cannot create song folder");
    QTemporaryDir staging(QDir(root).filePath(".bt-import-XXXXXX"));
    checked(staging.isValid(), "Cannot stage song files");
    QStringList staged, backups, promoted;
    for (int i = 0; i < sources.size(); ++i) {
        auto temp = QDir(staging.path()).filePath(QString("new-%1").arg(i));
        checked(QFile::copy(sources[i], temp), "Cannot stage song file: " + sources[i]);
        staged << temp;
    }
    for (int i = 0; i < targets.size(); ++i)
        backups << QString();
    try {
        for (int i = 0; i < targets.size(); ++i) {
            checked(QDir().mkpath(QFileInfo(targets[i]).absolutePath()),
                    "Cannot create song folder");
            if (QFile::exists(targets[i])) {
                if (!overwrite && identical(sources[i], targets[i]))
                    continue;
                auto backup = QDir(staging.path()).filePath(QString("old-%1").arg(i));
                checked(QFile::rename(targets[i], backup),
                        "Cannot back up existing song: " + targets[i]);
                backups[i] = backup;
            }
            checked(QFile::rename(staged[i], targets[i]),
                    "Cannot replace song file: " + targets[i]);
            promoted << targets[i];
        }
    } catch (...) {
        for (const auto &path : promoted)
            QFile::remove(path);
        bool recovery = true;
        for (int i = 0; i < backups.size(); ++i)
            if (!backups[i].isEmpty() && !QFile::rename(backups[i], targets[i]))
                recovery = false;
        if (!recovery) {
            staging.setAutoRemove(false);
            throw std::runtime_error(
                ("Cannot restore originals; backup retained at " + staging.path())
                    .toUtf8()
                    .constData());
        }
        throw;
    }
}
} // namespace
QString SongLibrary::copyNcn(const QString &midi, const QString &root, bool overwrite) {
    auto files = ncnFiles(midi);
    checked(!files.midi.isEmpty() && !files.lyrics.isEmpty() && !files.cursor.isEmpty(),
            "NCN requires matching Song, Lyrics and Cursor files");
    readSong(midi);
    auto relative = QDir::cleanPath(files.relative);
    checked(!QDir::isAbsolutePath(relative) && relative != ".." && !relative.startsWith("../"),
            "Invalid NCN relative path");
    QFileInfo info(relative);
    auto base = relative.left(relative.size() - info.suffix().size() - 1);
    QStringList targets{QDir(root).filePath("Song/" + base + ".mid"),
                        QDir(root).filePath("Lyrics/" + base + ".lyr"),
                        QDir(root).filePath("Cursor/" + base + ".cur")};
    replaceFiles({files.midi, files.lyrics, files.cursor}, targets, overwrite);
    return targets[0];
}
QString SongLibrary::copyKar(const QString &source, const QString &target, bool overwrite) {
    checked(QFileInfo(source).suffix().compare("kar", Qt::CaseInsensitive) == 0,
            "Only KAR files are accepted");
    readSong(source);
    replaceFiles({source}, {target}, overwrite);
    return target;
}
} // namespace bt
