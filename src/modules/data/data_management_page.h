#pragma once

#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QWidget>

#include "core/application/application_context.h"
#include "core/services/audit_service.h"

namespace panthera::modules {

// DataManagementPage 是当前患者 / 影像 / 治疗 / 报告工作区的原型壳层。
// 目前界面结构已经按正式模块形态搭好，但底层仍是样例数据与内存上下文联动，
// 还没有接通真实仓储层。
class DataManagementPage final : public QWidget {
    Q_OBJECT

public:
    DataManagementPage(panthera::core::ApplicationContext* context, panthera::core::AuditService* auditService, QWidget* parent = nullptr);

private slots:
    void appendAuditEntry(const panthera::core::AuditEntry& entry);
    void refreshFromContext();

private:
    void populateStaticTables();

    panthera::core::ApplicationContext* m_context {nullptr};
    QTableWidget* m_patientTable {nullptr};
    QTableWidget* m_imagingTable {nullptr};
    QTableWidget* m_treatmentTable {nullptr};
    QTextBrowser* m_reportPreview {nullptr};
    QListWidget* m_auditList {nullptr};
};

}  // panthera::modules 命名空间
