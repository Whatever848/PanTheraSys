#include "core/services/audit_service.h"

namespace panthera::core {

AuditService::AuditService(QObject* parent)
    : QObject(parent)
{
}

const QVector<AuditEntry>& AuditService::entries() const
{
    return m_entries;
}

AuditEntry AuditService::appendEntry(const QString& actor, const QString& category, const QString& details)
{
    AuditEntry entry {
        QDateTime::currentDateTime(),
        actor,
        category,
        details
    };

    m_entries.push_back(entry);
    emit entryAppended(entry);
    return entry;
}

}  // panthera::core 命名空间
