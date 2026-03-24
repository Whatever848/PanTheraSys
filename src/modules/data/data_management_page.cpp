#include "modules/data/data_management_page.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

QString toPatientListText(const PatientRecord& patient)
{
    return QStringLiteral("%1（%2岁）\nID:%3 | %4").arg(patient.name).arg(patient.age).arg(patient.id).arg(patient.diagnosis);
}

bool matchesPatientKeyword(const PatientRecord& patient, const QString& keyword)
{
    if (keyword.trimmed().isEmpty()) {
        return true;
    }

    return patient.id.contains(keyword, Qt::CaseInsensitive)
        || patient.name.contains(keyword, Qt::CaseInsensitive)
        || patient.diagnosis.contains(keyword, Qt::CaseInsensitive);
}

QString wrapValue(const QString& value)
{
    return value.toHtmlEscaped();
}

}  // namespace

DataManagementPage::DataManagementPage(
    ApplicationContext* context,
    AuditService* auditService,
    const IClinicalDataRepository* clinicalDataRepository,
    QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_clinicalDataRepository(clinicalDataRepository)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(16);

    auto* patientIndexCard = new QGroupBox(QStringLiteral("患者索引"));
    auto* patientIndexLayout = new QVBoxLayout(patientIndexCard);
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索姓名/ID/诊断"));
    m_patientList = new QListWidget();
    patientIndexLayout->addWidget(m_searchEdit);
    patientIndexLayout->addWidget(m_patientList, 1);
    rootLayout->addWidget(patientIndexCard, 1);

    auto* contentLayout = new QVBoxLayout();

    auto* patientDetailCard = new QGroupBox(QStringLiteral("患者详情"));
    auto* detailLayout = new QFormLayout(patientDetailCard);
    m_patientIdValue = new QLabel(QStringLiteral("--"));
    m_patientNameValue = new QLabel(QStringLiteral("--"));
    m_patientAgeValue = new QLabel(QStringLiteral("--"));
    m_patientGenderValue = new QLabel(QStringLiteral("--"));
    m_patientDiagnosisValue = new QLabel(QStringLiteral("--"));
    m_patientDiagnosisValue->setWordWrap(true);
    detailLayout->addRow(QStringLiteral("患者ID"), m_patientIdValue);
    detailLayout->addRow(QStringLiteral("患者姓名"), m_patientNameValue);
    detailLayout->addRow(QStringLiteral("患者年龄"), m_patientAgeValue);
    detailLayout->addRow(QStringLiteral("患者性别"), m_patientGenderValue);
    detailLayout->addRow(QStringLiteral("诊断结果"), m_patientDiagnosisValue);
    contentLayout->addWidget(patientDetailCard);

    auto* tabs = new QTabWidget();
    m_imagingTable = new QTableWidget(0, 4);
    m_imagingTable->setHorizontalHeaderLabels({QStringLiteral("影像ID"), QStringLiteral("类型"), QStringLiteral("采集日期"), QStringLiteral("存储路径")});
    m_imagingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_imagingTable->verticalHeader()->setVisible(false);
    m_imagingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_imagingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_treatmentTable = new QTableWidget(0, 6);
    m_treatmentTable->setHorizontalHeaderLabels(
        {QStringLiteral("治疗ID"), QStringLiteral("方案ID"), QStringLiteral("病灶类型"), QStringLiteral("总能量 (J)"), QStringLiteral("总时长 (s)"), QStringLiteral("状态")});
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_treatmentTable->verticalHeader()->setVisible(false);
    m_treatmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treatmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_reportPreview = new QTextBrowser();

    tabs->addTab(m_imagingTable, QStringLiteral("影像数据"));
    tabs->addTab(m_treatmentTable, QStringLiteral("治疗数据"));
    tabs->addTab(m_reportPreview, QStringLiteral("治疗报告"));
    contentLayout->addWidget(tabs, 1);

    auto* auditCard = new QGroupBox(QStringLiteral("审计日志"));
    auto* auditLayout = new QVBoxLayout(auditCard);
    m_auditList = new QListWidget();
    auditLayout->addWidget(m_auditList);
    contentLayout->addWidget(auditCard, 1);

    rootLayout->addLayout(contentLayout, 3);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &DataManagementPage::filterPatients);
    connect(m_patientList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (current == nullptr) {
            return;
        }
        selectPatientById(current->data(Qt::UserRole).toString());
    });
    if (auditService != nullptr) {
        connect(auditService, &AuditService::entryAppended, this, &DataManagementPage::appendAuditEntry);
    }
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::selectedPatientCleared, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::activePlanChanged, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::activePlanCleared, this, &DataManagementPage::refreshFromContext);

    populatePatientList();
    if (!m_context->hasSelectedPatient() && m_patientList->count() > 0) {
        m_patientList->setCurrentRow(0);
    } else {
        refreshFromContext();
    }
}

void DataManagementPage::appendAuditEntry(const AuditEntry& entry)
{
    m_auditList->insertItem(
        0,
        QStringLiteral("%1 | %2 | %3").arg(entry.occurredAt.toString(QStringLiteral("hh:mm:ss")), entry.category, entry.details));
}

void DataManagementPage::refreshFromContext()
{
    if (!m_context->hasSelectedPatient()) {
        refreshPatientOverview(nullptr);
        fillImagingTable({});
        fillTreatmentTable({});
        m_reportPreview->setHtml(QStringLiteral("<h2>治疗报告</h2><p>当前未选择患者。</p>"));
        return;
    }

    const PatientRecord& patient = m_context->selectedPatient();
    refreshPatientOverview(&patient);

    for (int row = 0; row < m_patientList->count(); ++row) {
        QListWidgetItem* item = m_patientList->item(row);
        if (item->data(Qt::UserRole).toString() != patient.id) {
            continue;
        }

        if (m_patientList->currentRow() != row) {
            const QSignalBlocker blocker(m_patientList);
            m_patientList->setCurrentRow(row);
        }
        break;
    }

    const QVector<ImageSeriesRecord> imageSeries =
        m_clinicalDataRepository == nullptr ? QVector<ImageSeriesRecord> {} : m_clinicalDataRepository->listImageSeriesForPatient(patient.id);
    const QVector<TreatmentSessionRecord> treatmentSessions =
        m_clinicalDataRepository == nullptr ? QVector<TreatmentSessionRecord> {} : m_clinicalDataRepository->listTreatmentSessionsForPatient(patient.id);
    const QVector<TreatmentReportRecord> reports =
        m_clinicalDataRepository == nullptr ? QVector<TreatmentReportRecord> {} : m_clinicalDataRepository->listTreatmentReportsForPatient(patient.id);

    fillImagingTable(imageSeries);
    fillTreatmentTable(treatmentSessions);

    QString reportHtml = buildReportHtml(patient, reports);
    if (m_context->hasActivePlan() && m_context->activePlan().patientId == patient.id) {
        const TherapyPlan& plan = m_context->activePlan();
        reportHtml += QStringLiteral(
            "<hr/><h3>当前工作站活动方案</h3><p><b>方案ID：</b>%1</p><p><b>状态：</b>%2</p><p><b>模式：</b>%3</p><p><b>功率：</b>%4 W</p>")
                          .arg(wrapValue(plan.id))
                          .arg(wrapValue(toDisplayString(plan.approvalState)))
                          .arg(wrapValue(toDisplayString(plan.pattern)))
                          .arg(plan.plannedPowerWatts, 0, 'f', 0);
    }
    m_reportPreview->setHtml(reportHtml);
}

void DataManagementPage::filterPatients(const QString& keyword)
{
    populatePatientList(keyword);
}

void DataManagementPage::populatePatientList(const QString& keyword)
{
    m_patientList->clear();
    if (m_clinicalDataRepository == nullptr) {
        return;
    }

    const QVector<PatientRecord> patients = m_clinicalDataRepository->listPatients();
    for (const PatientRecord& patient : patients) {
        if (!matchesPatientKeyword(patient, keyword)) {
            continue;
        }

        auto* item = new QListWidgetItem(toPatientListText(patient), m_patientList);
        item->setData(Qt::UserRole, patient.id);
    }

    if (!m_context->hasSelectedPatient()) {
        return;
    }

    for (int row = 0; row < m_patientList->count(); ++row) {
        if (m_patientList->item(row)->data(Qt::UserRole).toString() != m_context->selectedPatient().id) {
            continue;
        }

        const QSignalBlocker blocker(m_patientList);
        m_patientList->setCurrentRow(row);
        return;
    }
}

void DataManagementPage::selectPatientById(const QString& patientId)
{
    if (patientId.isEmpty()) {
        return;
    }

    if (m_context->hasSelectedPatient() && m_context->selectedPatient().id == patientId) {
        return;
    }

    if (m_clinicalDataRepository == nullptr) {
        return;
    }

    PatientRecord patient;
    if (!m_clinicalDataRepository->findPatientById(patientId, &patient)) {
        return;
    }

    m_context->selectPatient(patient);
}

void DataManagementPage::refreshPatientOverview(const PatientRecord* patient)
{
    if (patient == nullptr) {
        m_patientIdValue->setText(QStringLiteral("--"));
        m_patientNameValue->setText(QStringLiteral("--"));
        m_patientAgeValue->setText(QStringLiteral("--"));
        m_patientGenderValue->setText(QStringLiteral("--"));
        m_patientDiagnosisValue->setText(QStringLiteral("--"));
        return;
    }

    m_patientIdValue->setText(patient->id);
    m_patientNameValue->setText(patient->name);
    m_patientAgeValue->setText(QString::number(patient->age));
    m_patientGenderValue->setText(patient->gender);
    m_patientDiagnosisValue->setText(patient->diagnosis);
}

void DataManagementPage::fillImagingTable(const QVector<ImageSeriesRecord>& imageSeries)
{
    m_imagingTable->setRowCount(imageSeries.size());
    for (int row = 0; row < imageSeries.size(); ++row) {
        const ImageSeriesRecord& series = imageSeries[row];
        m_imagingTable->setItem(row, 0, new QTableWidgetItem(series.id));
        m_imagingTable->setItem(row, 1, new QTableWidgetItem(series.type));
        m_imagingTable->setItem(row, 2, new QTableWidgetItem(series.acquisitionDate.toString(QStringLiteral("yyyy-MM-dd"))));
        m_imagingTable->setItem(row, 3, new QTableWidgetItem(series.storagePath));
    }
}

void DataManagementPage::fillTreatmentTable(const QVector<TreatmentSessionRecord>& treatmentSessions)
{
    m_treatmentTable->setRowCount(treatmentSessions.size());
    for (int row = 0; row < treatmentSessions.size(); ++row) {
        const TreatmentSessionRecord& session = treatmentSessions[row];
        m_treatmentTable->setItem(row, 0, new QTableWidgetItem(session.id));
        m_treatmentTable->setItem(row, 1, new QTableWidgetItem(session.planId));
        m_treatmentTable->setItem(row, 2, new QTableWidgetItem(session.lesionType));
        m_treatmentTable->setItem(row, 3, new QTableWidgetItem(QString::number(session.totalEnergyJ, 'f', 0)));
        m_treatmentTable->setItem(row, 4, new QTableWidgetItem(QString::number(session.totalDurationSeconds, 'f', 1)));
        m_treatmentTable->setItem(row, 5, new QTableWidgetItem(session.status));
    }
}

QString DataManagementPage::buildReportHtml(const PatientRecord& patient, const QVector<TreatmentReportRecord>& reports) const
{
    QString html =
        QStringLiteral("<h2>治疗报告</h2><p><b>患者ID：</b>%1</p><p><b>患者姓名：</b>%2</p><p><b>患者年龄：</b>%3</p><p><b>诊断结果：</b>%4</p>")
            .arg(wrapValue(patient.id))
            .arg(wrapValue(patient.name))
            .arg(patient.age)
            .arg(wrapValue(patient.diagnosis));

    if (reports.isEmpty()) {
        html += QStringLiteral("<p>当前无已归档治疗报告。</p>");
        return html;
    }

    const TreatmentReportRecord& latestReport = reports.first();
    html += QStringLiteral("<p><b>报告标题：</b>%1</p><p><b>生成时间：</b>%2</p>")
                .arg(wrapValue(latestReport.title))
                .arg(wrapValue(latestReport.generatedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
    html += latestReport.contentHtml;
    return html;
}

}  // namespace panthera::modules
