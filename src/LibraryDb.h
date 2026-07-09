#pragma once

#include <QSqlDatabase>
#include <QString>

namespace LibraryDb {

void applyConnectionPragmas(QSqlDatabase &db);

QString openScopedConnection(const QString &purpose, QSqlDatabase &outDb);

void closeScopedConnection(const QString &connName, QSqlDatabase &db);

}
