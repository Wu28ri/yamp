#include "LibraryDb.h"

#include "MusicLibrary.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace LibraryDb {

NonBlockingWrite::NonBlockingWrite(const QSqlDatabase &db) : m_db(db) {
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("PRAGMA busy_timeout=0"));
}

NonBlockingWrite::~NonBlockingWrite() {
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
}

void applyConnectionPragmas(QSqlDatabase &db) {
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));
    pragma.exec(QStringLiteral("PRAGMA cache_size=-32000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
}

QString openScopedConnection(const QString &purpose, QSqlDatabase &outDb) {
    const QString name = QStringLiteral("yamp_%1_%2")
        .arg(purpose, QUuid::createUuid().toString(QUuid::WithoutBraces));
    outDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    outDb.setDatabaseName(MusicLibrary::databasePath());
    if (!outDb.open()) {
        qWarning() << "[LibraryDb] failed to open scoped connection"
                   << purpose << outDb.lastError().text();
        outDb = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        return {};
    }
    applyConnectionPragmas(outDb);
    return name;
}

void closeScopedConnection(const QString &connName, QSqlDatabase &db) {
    if (db.isOpen()) db.close();
    db = QSqlDatabase();
    if (!connName.isEmpty()) QSqlDatabase::removeDatabase(connName);
}

}
