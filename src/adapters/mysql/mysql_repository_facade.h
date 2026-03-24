#pragma once

#include <QSqlDatabase>
#include <QString>

namespace panthera::adapters {

struct DatabaseConnectionSettings {
    QString connectionName;
    QString host;
    QString schema;
    QString username;
    QString password;
    int port {3306};
};

class MySqlRepositoryFacade final {
public:
    MySqlRepositoryFacade();
    ~MySqlRepositoryFacade();

    bool open(const DatabaseConnectionSettings& settings);
    void close();
    bool isOpen() const;
    QSqlDatabase database() const;
    bool initializeSchemaFromFile(const QString& schemaFilePath);
    QString lastError() const;

private:
    QString m_connectionName;
    QString m_lastError;
};

}  // panthera::adapters 命名空间
