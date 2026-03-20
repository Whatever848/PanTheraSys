#include "adapters/config/local_settings_store.h"

namespace panthera::adapters {

LocalSettingsStore::LocalSettingsStore()
    : m_settings(QStringLiteral("PanTheraSys"), QStringLiteral("PanTheraConsole"))
{
}

QString LocalSettingsStore::themeName() const
{
    return m_settings.value(QStringLiteral("ui/themeName"), QStringLiteral("midnight-clinical")).toString();
}

void LocalSettingsStore::setThemeName(const QString& themeName)
{
    m_settings.setValue(QStringLiteral("ui/themeName"), themeName);
}

QString LocalSettingsStore::databaseHost() const
{
    return m_settings.value(QStringLiteral("database/host"), QStringLiteral("127.0.0.1")).toString();
}

void LocalSettingsStore::setDatabaseHost(const QString& host)
{
    m_settings.setValue(QStringLiteral("database/host"), host);
}

QString LocalSettingsStore::lastImageRoot() const
{
    return m_settings.value(QStringLiteral("storage/lastImageRoot"), QStringLiteral("runtime/images")).toString();
}

void LocalSettingsStore::setLastImageRoot(const QString& path)
{
    m_settings.setValue(QStringLiteral("storage/lastImageRoot"), path);
}

}  // panthera::adapters 命名空间
