#include "modules/data/data_management_page.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

DataManagementPage::DataManagementPage(ApplicationContext* context, AuditService* auditService, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(16);

    auto* tabs = new QTabWidget();
    m_patientTable = new QTableWidget(0, 5);
    m_patientTable->setHorizontalHeaderLabels({QStringLiteral("患者ID"), QStringLiteral("姓名"), QStringLiteral("年龄"), QStringLiteral("性别"), QStringLiteral("诊断")});
    m_patientTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_imagingTable = new QTableWidget(0, 4);
    m_imagingTable->setHorizontalHeaderLabels({QStringLiteral("影像ID"), QStringLiteral("患者ID"), QStringLiteral("类型"), QStringLiteral("路径")});
    m_imagingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_treatmentTable = new QTableWidget(0, 6);
    m_treatmentTable->setHorizontalHeaderLabels({QStringLiteral("治疗ID"), QStringLiteral("患者ID"), QStringLiteral("方案ID"), QStringLiteral("总能量(J)"), QStringLiteral("总时长(s)"), QStringLiteral("状态")});
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    m_reportPreview = new QTextBrowser();

    tabs->addTab(m_patientTable, QStringLiteral("患者信息"));
    tabs->addTab(m_imagingTable, QStringLiteral("影像数据"));
    tabs->addTab(m_treatmentTable, QStringLiteral("治疗数据"));
    tabs->addTab(m_reportPreview, QStringLiteral("治疗报告"));
    rootLayout->addWidget(tabs, 3);

    auto* auditPanel = new QVBoxLayout();
    m_auditList = new QListWidget();
    auditPanel->addWidget(m_auditList);
    rootLayout->addLayout(auditPanel, 1);

    populateStaticTables();
    refreshFromContext();

    connect(auditService, &AuditService::entryAppended, this, &DataManagementPage::appendAuditEntry);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::activePlanChanged, this, &DataManagementPage::refreshFromContext);
}

void DataManagementPage::appendAuditEntry(const AuditEntry& entry)
{
    m_auditList->insertItem(0, QStringLiteral("%1 | %2 | %3").arg(entry.occurredAt.toString(QStringLiteral("hh:mm:ss")), entry.category, entry.details));
}

void DataManagementPage::refreshFromContext()
{
    // 当前页面还是“静态种子数据 + 当前会话上下文”的混合模式。
    // 等仓储层接通后，这里应改为把查询结果绑定到界面控件上。
    if (m_context->hasSelectedPatient()) {
        const PatientRecord& patient = m_context->selectedPatient();
        m_reportPreview->setHtml(
            QStringLiteral("<h2>治疗报告</h2><p><b>患者：</b>%1 (%2)</p><p><b>诊断：</b>%3</p>")
                .arg(patient.name, patient.id, patient.diagnosis));
    } else {
        m_reportPreview->setHtml(QStringLiteral("<h2>治疗报告</h2><p>当前未选择患者。</p>"));
    }

    if (m_context->hasActivePlan()) {
        const TherapyPlan& plan = m_context->activePlan();
        const int row = m_treatmentTable->rowCount();
        if (row == 0) {
            m_treatmentTable->insertRow(0);
            for (int column = 0; column < m_treatmentTable->columnCount(); ++column) {
                m_treatmentTable->setItem(0, column, new QTableWidgetItem());
            }
        }

        int totalPoints = 0;
        double duration = 0.0;
        for (const TherapySegment& segment : plan.segments) {
            totalPoints += segment.points.size();
            duration += segment.plannedDurationSeconds;
        }

        m_treatmentTable->item(0, 0)->setText(QStringLiteral("SIM-TX-001"));
        m_treatmentTable->item(0, 1)->setText(plan.patientId);
        m_treatmentTable->item(0, 2)->setText(plan.id);
        m_treatmentTable->item(0, 3)->setText(QString::number(totalPoints * plan.plannedPowerWatts * 0.3, 'f', 0));
        m_treatmentTable->item(0, 4)->setText(QString::number(duration, 'f', 1));
        m_treatmentTable->item(0, 5)->setText(toDisplayString(plan.approvalState));

        m_reportPreview->append(QStringLiteral("<p><b>方案：</b>%1</p><p><b>模式：</b>%2</p><p><b>状态：</b>%3</p>")
                                    .arg(plan.id, toDisplayString(plan.pattern), toDisplayString(plan.approvalState)));
    }
}

void DataManagementPage::populateStaticTables()
{
    // 先填充样例行，便于在真实持久化未接入前验证信息架构和界面布局。
    m_patientTable->insertRow(0);
    m_patientTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("P2026001")));
    m_patientTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("张三")));
    m_patientTable->setItem(0, 2, new QTableWidgetItem(QStringLiteral("42")));
    m_patientTable->setItem(0, 3, new QTableWidgetItem(QStringLiteral("女")));
    m_patientTable->setItem(0, 4, new QTableWidgetItem(QStringLiteral("右乳浸润性导管癌 II 期")));

    m_imagingTable->insertRow(0);
    m_imagingTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("IMG-001")));
    m_imagingTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("P2026001")));
    m_imagingTable->setItem(0, 2, new QTableWidgetItem(QStringLiteral("超声图像")));
    m_imagingTable->setItem(0, 3, new QTableWidgetItem(QStringLiteral("runtime/images/P2026001/series-01")));
}

}  // panthera::modules 命名空间
