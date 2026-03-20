#pragma once

#include <QSettings>
#include <QString>

namespace panthera::adapters {

class LocalSettingsStore final {
public:
    LocalSettingsStore();

    QString themeName() const;
    void setThemeName(const QString& themeName);

    QString databaseHost() const;
    void setDatabaseHost(const QString& host);

    QString lastImageRoot() const;
    void setLastImageRoot(const QString& path);

private:
    mutable QSettings m_settings;
};

}  // panthera::adapters 命名空间
