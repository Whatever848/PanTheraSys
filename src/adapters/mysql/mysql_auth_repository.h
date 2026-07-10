#pragma once

#include <QString>

#include "core/application/application_context.h"

namespace panthera::adapters {

struct DoctorRegistrationRequest {
    QString username;
    QString password;
    QString doctorName;
    QString department;
    QString title;
    QString licenseNumber;
    QString phone;
};

class MySqlAuthRepository final {
public:
    explicit MySqlAuthRepository(QString connectionName = QStringLiteral("PanTheraClinicalData"));

    bool initializeSchema();
    bool hasAnyActiveUser() const;
    bool registerDoctorAccount(const DoctorRegistrationRequest& request, panthera::core::AuthenticatedOperator* account);
    bool authenticate(const QString& username, const QString& password, panthera::core::AuthenticatedOperator* account);

    QString lastError() const;

private:
    bool ensureColumn(const QString& tableName, const QString& columnName, const QString& definition);
    bool ensureIndex(const QString& tableName, const QString& indexName, const QString& createSql);
    bool ensureForeignKey(const QString& tableName, const QString& constraintName, const QString& alterSql);
    bool loadAccountByUserId(const QString& userId, panthera::core::AuthenticatedOperator* account);
    void setLastError(const QString& error) const;

    QString m_connectionName;
    mutable QString m_lastError;
};

}  // namespace panthera::adapters
