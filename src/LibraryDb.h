#pragma once

#include <QSqlDatabase>
#include <QString>

namespace LibraryDb {

class NonBlockingWrite {
public:
    explicit NonBlockingWrite(const QSqlDatabase &db);
    ~NonBlockingWrite();

private:
    QSqlDatabase m_db;
};

void applyConnectionPragmas(QSqlDatabase &db);

QString openScopedConnection(const QString &purpose, QSqlDatabase &outDb);

void closeScopedConnection(const QString &connName, QSqlDatabase &db);

}
