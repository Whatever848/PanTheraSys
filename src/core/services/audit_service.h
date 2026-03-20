#pragma once

#include <QObject>
#include <QVector>

#include "core/domain/system_types.h"

namespace panthera::core {

class AuditService final : public QObject {
    Q_OBJECT

public:
    explicit AuditService(QObject* parent = nullptr);

    const QVector<AuditEntry>& entries() const;
    AuditEntry appendEntry(const QString& actor, const QString& category, const QString& details);

signals:
    void entryAppended(const panthera::core::AuditEntry& entry);

private:
    QVector<AuditEntry> m_entries;
};

}  // panthera::core 命名空间
