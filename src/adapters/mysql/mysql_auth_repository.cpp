#include "adapters/mysql/mysql_auth_repository.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <utility>

namespace panthera::adapters {
namespace {

constexpr int kPasswordIterations = 120000;
constexpr int kPasswordSaltBytes = 16;
constexpr int kPasswordHashBytes = 32;

QSqlDatabase authDatabase(const QString& connectionName)
{
    return QSqlDatabase::database(connectionName, false);
}

QString normalizedUsername(const QString& username)
{
    return username.trimmed().toLower();
}

QString roleIdForRole(panthera::core::RoleType role)
{
    switch (role) {
    case panthera::core::RoleType::Operator:
        return QStringLiteral("operator");
    case panthera::core::RoleType::Physician:
        return QStringLiteral("physician");
    case panthera::core::RoleType::Engineer:
        return QStringLiteral("engineer");
    case panthera::core::RoleType::Administrator:
        return QStringLiteral("administrator");
    }
    return QStringLiteral("physician");
}

panthera::core::RoleType roleFromId(const QString& roleId)
{
    const QString normalized = roleId.trimmed().toLower();
    if (normalized == QStringLiteral("operator")) {
        return panthera::core::RoleType::Operator;
    }
    if (normalized == QStringLiteral("engineer")) {
        return panthera::core::RoleType::Engineer;
    }
    if (normalized == QStringLiteral("administrator")) {
        return panthera::core::RoleType::Administrator;
    }
    return panthera::core::RoleType::Physician;
}

bool execSql(QSqlDatabase database, const QString& sql, QString* error)
{
    QSqlQuery query(database);
    if (!query.exec(sql)) {
        if (error != nullptr) {
            *error = query.lastError().text();
        }
        return false;
    }
    return true;
}

QByteArray randomBytes(int byteCount)
{
    QByteArray bytes(byteCount, Qt::Uninitialized);
    for (int index = 0; index < byteCount; ++index) {
        bytes[index] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    return bytes;
}

QByteArray intToBigEndian(int value)
{
    QByteArray bytes(4, Qt::Uninitialized);
    bytes[0] = static_cast<char>((value >> 24) & 0xff);
    bytes[1] = static_cast<char>((value >> 16) & 0xff);
    bytes[2] = static_cast<char>((value >> 8) & 0xff);
    bytes[3] = static_cast<char>(value & 0xff);
    return bytes;
}

QByteArray pbkdf2Sha256(const QByteArray& password, const QByteArray& salt, int iterations, int outputBytes)
{
    const int digestBytes = QCryptographicHash::hashLength(QCryptographicHash::Sha256);
    const int blockCount = (outputBytes + digestBytes - 1) / digestBytes;
    QByteArray output;
    output.reserve(blockCount * digestBytes);

    for (int blockIndex = 1; blockIndex <= blockCount; ++blockIndex) {
        QByteArray u = QMessageAuthenticationCode::hash(salt + intToBigEndian(blockIndex), password, QCryptographicHash::Sha256);
        QByteArray block = u;
        for (int iteration = 1; iteration < iterations; ++iteration) {
            u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha256);
            for (int index = 0; index < block.size(); ++index) {
                block[index] = static_cast<char>(static_cast<unsigned char>(block[index]) ^ static_cast<unsigned char>(u[index]));
            }
        }
        output += block;
    }

    return output.left(outputBytes);
}

QString makePasswordRecord(const QString& password)
{
    const QByteArray salt = randomBytes(kPasswordSaltBytes);
    const QByteArray hash = pbkdf2Sha256(password.toUtf8(), salt, kPasswordIterations, kPasswordHashBytes);
    return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
        .arg(kPasswordIterations)
        .arg(QString::fromLatin1(salt.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
        .arg(QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}

bool verifyPasswordRecord(const QString& password, const QString& passwordRecord)
{
    const QStringList parts = passwordRecord.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts.at(0) != QStringLiteral("pbkdf2-sha256")) {
        return false;
    }

    bool ok = false;
    const int iterations = parts.at(1).toInt(&ok);
    if (!ok || iterations < 1) {
        return false;
    }

    const QByteArray salt = QByteArray::fromBase64(parts.at(2).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray expected = QByteArray::fromBase64(parts.at(3).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (salt.isEmpty() || expected.isEmpty()) {
        return false;
    }

    const QByteArray actual = pbkdf2Sha256(password.toUtf8(), salt, iterations, expected.size());
    if (actual.size() != expected.size()) {
        return false;
    }

    unsigned char diff = 0;
    for (int index = 0; index < actual.size(); ++index) {
        diff |= static_cast<unsigned char>(actual[index]) ^ static_cast<unsigned char>(expected[index]);
    }
    return diff == 0;
}

}  // namespace

MySqlAuthRepository::MySqlAuthRepository(QString connectionName)
    : m_connectionName(std::move(connectionName))
{
}

bool MySqlAuthRepository::initializeSchema()
{
    m_lastError.clear();
    QSqlDatabase database = authDatabase(m_connectionName);
    if (!database.isValid() || !database.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QString error;
    const QStringList statements {
        QStringLiteral("CREATE TABLE IF NOT EXISTS role ("
                       "id VARCHAR(64) PRIMARY KEY,"
                       "name VARCHAR(64) NOT NULL,"
                       "description VARCHAR(255) NULL"
                       ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS doctor_profile ("
                       "id VARCHAR(64) PRIMARY KEY,"
                       "full_name VARCHAR(128) NOT NULL,"
                       "department VARCHAR(128) NOT NULL,"
                       "title_name VARCHAR(128) NOT NULL,"
                       "license_number VARCHAR(64) NULL,"
                       "phone VARCHAR(64) NULL,"
                       "is_active TINYINT(1) NOT NULL DEFAULT 1,"
                       "created_at DATETIME NOT NULL,"
                       "updated_at DATETIME NOT NULL,"
                       "UNIQUE KEY uk_doctor_license (license_number)"
                       ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS user_account ("
                       "id VARCHAR(64) PRIMARY KEY,"
                       "username VARCHAR(64) NOT NULL UNIQUE,"
                       "display_name VARCHAR(128) NOT NULL,"
                       "role_id VARCHAR(64) NOT NULL,"
                       "doctor_id VARCHAR(64) NULL,"
                       "password_hash VARCHAR(255) NOT NULL,"
                       "is_active TINYINT(1) NOT NULL DEFAULT 1,"
                       "created_at DATETIME NOT NULL,"
                       "updated_at DATETIME NOT NULL,"
                       "last_login_at DATETIME NULL,"
                       "CONSTRAINT fk_user_role FOREIGN KEY (role_id) REFERENCES role(id),"
                       "CONSTRAINT fk_user_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id)"
                       ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral("INSERT INTO role (id, name, description) VALUES "
                       "('operator', '操作员', '设备操作与治疗执行人员'),"
                       "('physician', '医生', '临床医生账号'),"
                       "('engineer', '工程师', '设备维护与工程调试人员'),"
                       "('administrator', '管理员', '系统管理人员') "
                       "ON DUPLICATE KEY UPDATE name = VALUES(name), description = VALUES(description)")
    };

    for (const QString& statement : statements) {
        if (!execSql(database, statement, &error)) {
            setLastError(error);
            return false;
        }
    }

    if (!ensureColumn(QStringLiteral("user_account"), QStringLiteral("doctor_id"), QStringLiteral("doctor_id VARCHAR(64) NULL AFTER role_id"))
        || !ensureColumn(QStringLiteral("user_account"), QStringLiteral("last_login_at"), QStringLiteral("last_login_at DATETIME NULL AFTER updated_at"))
        || !ensureIndex(QStringLiteral("user_account"), QStringLiteral("idx_user_account_doctor"), QStringLiteral("CREATE INDEX idx_user_account_doctor ON user_account(doctor_id)"))
        || !ensureForeignKey(QStringLiteral("user_account"), QStringLiteral("fk_user_doctor"), QStringLiteral("ALTER TABLE user_account ADD CONSTRAINT fk_user_doctor FOREIGN KEY (doctor_id) REFERENCES doctor_profile(id)"))) {
        return false;
    }

    return true;
}

bool MySqlAuthRepository::hasAnyActiveUser() const
{
    QSqlDatabase database = authDatabase(m_connectionName);
    if (!database.isValid() || !database.isOpen()) {
        return false;
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM user_account WHERE is_active = 1"))) {
        return false;
    }
    return query.next() && query.value(0).toInt() > 0;
}

bool MySqlAuthRepository::registerDoctorAccount(const DoctorRegistrationRequest& request, panthera::core::AuthenticatedOperator* account)
{
    m_lastError.clear();
    QSqlDatabase database = authDatabase(m_connectionName);
    if (!database.isValid() || !database.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    const QString username = normalizedUsername(request.username);
    const QString doctorName = request.doctorName.trimmed();
    if (username.isEmpty() || request.password.size() < 8 || doctorName.isEmpty()) {
        setLastError(QStringLiteral("账号、医生姓名不能为空，密码至少 8 位。"));
        return false;
    }

    QSqlQuery existsQuery(database);
    existsQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM user_account WHERE username = :username"));
    existsQuery.bindValue(QStringLiteral(":username"), username);
    if (!existsQuery.exec() || !existsQuery.next()) {
        setLastError(existsQuery.lastError().text());
        return false;
    }
    if (existsQuery.value(0).toInt() > 0) {
        setLastError(QStringLiteral("账号已存在。"));
        return false;
    }

    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString doctorId = QStringLiteral("D-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString userId = QStringLiteral("U-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    if (!database.transaction()) {
        setLastError(database.lastError().text());
        return false;
    }

    QSqlQuery doctorQuery(database);
    doctorQuery.prepare(QStringLiteral(
        "INSERT INTO doctor_profile "
        "(id, full_name, department, title_name, license_number, phone, is_active, created_at, updated_at) "
        "VALUES (:id, :full_name, :department, :title_name, :license_number, :phone, 1, :created_at, :updated_at)"));
    doctorQuery.bindValue(QStringLiteral(":id"), doctorId);
    doctorQuery.bindValue(QStringLiteral(":full_name"), doctorName);
    doctorQuery.bindValue(QStringLiteral(":department"), request.department.trimmed().isEmpty() ? QStringLiteral("未设置科室") : request.department.trimmed());
    doctorQuery.bindValue(QStringLiteral(":title_name"), request.title.trimmed().isEmpty() ? QStringLiteral("医生") : request.title.trimmed());
    const QString licenseNumber = request.licenseNumber.trimmed();
    doctorQuery.bindValue(QStringLiteral(":license_number"), licenseNumber.isEmpty() ? QVariant() : QVariant(licenseNumber));
    doctorQuery.bindValue(QStringLiteral(":phone"), request.phone.trimmed());
    doctorQuery.bindValue(QStringLiteral(":created_at"), now);
    doctorQuery.bindValue(QStringLiteral(":updated_at"), now);
    if (!doctorQuery.exec()) {
        database.rollback();
        setLastError(doctorQuery.lastError().text());
        return false;
    }

    QSqlQuery userQuery(database);
    userQuery.prepare(QStringLiteral(
        "INSERT INTO user_account "
        "(id, username, display_name, role_id, doctor_id, password_hash, is_active, created_at, updated_at, last_login_at) "
        "VALUES (:id, :username, :display_name, :role_id, :doctor_id, :password_hash, 1, :created_at, :updated_at, :last_login_at)"));
    userQuery.bindValue(QStringLiteral(":id"), userId);
    userQuery.bindValue(QStringLiteral(":username"), username);
    userQuery.bindValue(QStringLiteral(":display_name"), doctorName);
    userQuery.bindValue(QStringLiteral(":role_id"), roleIdForRole(panthera::core::RoleType::Physician));
    userQuery.bindValue(QStringLiteral(":doctor_id"), doctorId);
    userQuery.bindValue(QStringLiteral(":password_hash"), makePasswordRecord(request.password));
    userQuery.bindValue(QStringLiteral(":created_at"), now);
    userQuery.bindValue(QStringLiteral(":updated_at"), now);
    userQuery.bindValue(QStringLiteral(":last_login_at"), now);
    if (!userQuery.exec()) {
        database.rollback();
        setLastError(userQuery.lastError().text());
        return false;
    }

    if (!database.commit()) {
        setLastError(database.lastError().text());
        return false;
    }

    return loadAccountByUserId(userId, account);
}

bool MySqlAuthRepository::authenticate(const QString& username, const QString& password, panthera::core::AuthenticatedOperator* account)
{
    m_lastError.clear();
    QSqlDatabase database = authDatabase(m_connectionName);
    if (!database.isValid() || !database.isOpen()) {
        setLastError(QStringLiteral("MySQL connection is not open."));
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, password_hash "
        "FROM user_account "
        "WHERE username = :username AND is_active = 1"));
    query.bindValue(QStringLiteral(":username"), normalizedUsername(username));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("账号或密码不正确。"));
        return false;
    }

    const QString userId = query.value(0).toString();
    const QString passwordHash = query.value(1).toString();
    if (!verifyPasswordRecord(password, passwordHash)) {
        setLastError(QStringLiteral("账号或密码不正确。"));
        return false;
    }

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral("UPDATE user_account SET last_login_at = :last_login_at WHERE id = :id"));
    updateQuery.bindValue(QStringLiteral(":last_login_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    updateQuery.bindValue(QStringLiteral(":id"), userId);
    if (!updateQuery.exec()) {
        setLastError(updateQuery.lastError().text());
        return false;
    }

    return loadAccountByUserId(userId, account);
}

QString MySqlAuthRepository::lastError() const
{
    return m_lastError;
}

bool MySqlAuthRepository::ensureColumn(const QString& tableName, const QString& columnName, const QString& definition)
{
    QSqlDatabase database = authDatabase(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND COLUMN_NAME = :column_name"));
    query.bindValue(QStringLiteral(":table_name"), tableName);
    query.bindValue(QStringLiteral(":column_name"), columnName);
    if (!query.exec() || !query.next()) {
        setLastError(query.lastError().text());
        return false;
    }
    if (query.value(0).toInt() > 0) {
        return true;
    }

    QString error;
    if (!execSql(database, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, definition), &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool MySqlAuthRepository::ensureIndex(const QString& tableName, const QString& indexName, const QString& createSql)
{
    QSqlDatabase database = authDatabase(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM information_schema.STATISTICS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND INDEX_NAME = :index_name"));
    query.bindValue(QStringLiteral(":table_name"), tableName);
    query.bindValue(QStringLiteral(":index_name"), indexName);
    if (!query.exec() || !query.next()) {
        setLastError(query.lastError().text());
        return false;
    }
    if (query.value(0).toInt() > 0) {
        return true;
    }

    QString error;
    if (!execSql(database, createSql, &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool MySqlAuthRepository::ensureForeignKey(const QString& tableName, const QString& constraintName, const QString& alterSql)
{
    QSqlDatabase database = authDatabase(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM information_schema.TABLE_CONSTRAINTS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name "
        "AND CONSTRAINT_NAME = :constraint_name AND CONSTRAINT_TYPE = 'FOREIGN KEY'"));
    query.bindValue(QStringLiteral(":table_name"), tableName);
    query.bindValue(QStringLiteral(":constraint_name"), constraintName);
    if (!query.exec() || !query.next()) {
        setLastError(query.lastError().text());
        return false;
    }
    if (query.value(0).toInt() > 0) {
        return true;
    }

    QString error;
    if (!execSql(database, alterSql, &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool MySqlAuthRepository::loadAccountByUserId(const QString& userId, panthera::core::AuthenticatedOperator* account)
{
    if (account == nullptr) {
        return true;
    }

    QSqlDatabase database = authDatabase(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT u.id, u.username, u.display_name, u.role_id, u.doctor_id, "
        "d.full_name, d.department, d.title_name, d.license_number "
        "FROM user_account u "
        "LEFT JOIN doctor_profile d ON d.id = u.doctor_id "
        "WHERE u.id = :id AND u.is_active = 1"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    if (!query.next()) {
        setLastError(QStringLiteral("账号不存在或已停用。"));
        return false;
    }

    account->userId = query.value(0).toString();
    account->username = query.value(1).toString();
    account->displayName = query.value(2).toString();
    account->role = roleFromId(query.value(3).toString());
    account->doctorId = query.value(4).toString();
    account->doctorName = query.value(5).toString();
    account->department = query.value(6).toString();
    account->title = query.value(7).toString();
    account->licenseNumber = query.value(8).toString();
    return true;
}

void MySqlAuthRepository::setLastError(const QString& error) const
{
    m_lastError = error;
}

}  // namespace panthera::adapters
