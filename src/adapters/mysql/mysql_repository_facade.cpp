#include "adapters/mysql/mysql_repository_facade.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace panthera::adapters {

MySqlRepositoryFacade::MySqlRepositoryFacade() = default;

MySqlRepositoryFacade::~MySqlRepositoryFacade()
{
    close();
}

bool MySqlRepositoryFacade::open(const DatabaseConnectionSettings& settings)
{
    m_lastError.clear();
    m_connectionName = settings.connectionName.isEmpty() ? QStringLiteral("PanTheraDefaultConnection") : settings.connectionName;

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), m_connectionName);
    database.setHostName(settings.host);
    database.setPort(settings.port);
    database.setDatabaseName(settings.schema);
    database.setUserName(settings.username);
    database.setPassword(settings.password);

    if (!database.open()) {
        m_lastError = database.lastError().text();
        return false;
    }

    return true;
}

void MySqlRepositoryFacade::close()
{
    if (m_connectionName.isEmpty()) {
        return;
    }

    {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
        if (database.isValid()) {
            database.close();
        }
    }

    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
}

bool MySqlRepositoryFacade::isOpen() const
{
    if (m_connectionName.isEmpty()) {
        return false;
    }

    const QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    return database.isValid() && database.isOpen();
}

QSqlDatabase MySqlRepositoryFacade::database() const
{
    return m_connectionName.isEmpty() ? QSqlDatabase() : QSqlDatabase::database(m_connectionName, false);
}

bool MySqlRepositoryFacade::initializeSchemaFromFile(const QString& schemaFilePath)
{
    if (m_connectionName.isEmpty()) {
        m_lastError = QStringLiteral("Database is not open.");
        return false;
    }

    QFile schemaFile(schemaFilePath);
    if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QStringLiteral("Unable to open schema file: %1").arg(schemaFilePath);
        return false;
    }

    const QString schemaText = QString::fromUtf8(schemaFile.readAll());
    QStringList statements = schemaText.split(';', Qt::SkipEmptyParts);

    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    for (QString statement : statements) {
        statement = statement.trimmed();
        if (statement.isEmpty()) {
            continue;
        }

        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            return false;
        }
    }

    return true;
}

QString MySqlRepositoryFacade::lastError() const
{
    return m_lastError;
}

}  // panthera::adapters 命名空间
