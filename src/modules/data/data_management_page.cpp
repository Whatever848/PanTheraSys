#include "modules/data/data_management_page.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

QString toPatientListText(const PatientRecord& patient)
{
    return QStringLiteral("%1（%2岁）\nID:%3 | %4")
        .arg(patient.name)
        .arg(patient.age)
        .arg(patient.id)
        .arg(patient.diagnosis);
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

QGroupBox* createContentCard(const QString& title, QWidget* body)
{
    auto* card = new QGroupBox(title);
    auto* layout = new QVBoxLayout(card);
    layout->addWidget(body);
    return card;
}

QString normalizeHeader(const QString& value)
{
    QString normalized = value.trimmed().toLower();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char('-'));
    return normalized;
}

void clearLayout(QLayout* layout)
{
    if (layout == nullptr) {
        return;
    }

    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

}  // namespace

DataManagementPage::DataManagementPage(
    ApplicationContext* context,
    AuditService* auditService,
    IClinicalDataRepository* clinicalDataRepository,
    QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_auditService(auditService)
    , m_clinicalDataRepository(clinicalDataRepository)
    , m_clinicalDataService(clinicalDataRepository)
{
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(18);

    auto* patientIndexCard = new QGroupBox(QStringLiteral("患者索引"));
    patientIndexCard->setObjectName(QStringLiteral("dataSidebarCard"));
    auto* patientIndexLayout = new QVBoxLayout(patientIndexCard);
    patientIndexLayout->setContentsMargins(10, 10, 10, 10);
    patientIndexLayout->setSpacing(12);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName(QStringLiteral("patientSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索姓名/ID"));
    m_searchEdit->setClearButtonEnabled(true);

    m_patientList = new QListWidget();
    m_patientList->setObjectName(QStringLiteral("patientList"));

    m_sidebarActionsWidget = new QWidget();
    auto* sidebarActionsLayout = new QHBoxLayout(m_sidebarActionsWidget);
    sidebarActionsLayout->setContentsMargins(0, 0, 0, 0);
    sidebarActionsLayout->setSpacing(10);

    m_addPatientButton = new QPushButton(QStringLiteral("添加"));
    m_addPatientButton->setObjectName(QStringLiteral("sidebarActionButton"));

    m_batchImportButton = new QPushButton(QStringLiteral("批量导入"));
    m_batchImportButton->setObjectName(QStringLiteral("sidebarActionButton"));

    sidebarActionsLayout->addWidget(m_addPatientButton);
    sidebarActionsLayout->addWidget(m_batchImportButton);

    patientIndexLayout->addWidget(m_searchEdit);
    patientIndexLayout->addWidget(m_patientList, 1);
    patientIndexLayout->addWidget(m_sidebarActionsWidget);
    rootLayout->addWidget(patientIndexCard, 1);

    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_sectionStack = new QStackedWidget();
    m_sectionStack->setObjectName(QStringLiteral("dataSectionStack"));

    auto* patientInfoPage = new QWidget();
    patientInfoPage->setObjectName(QStringLiteral("patientInfoPage"));
    auto* patientInfoLayout = new QVBoxLayout(patientInfoPage);
    patientInfoLayout->setContentsMargins(0, 0, 0, 0);
    patientInfoLayout->setSpacing(0);

    auto* patientDetailPanel = new QFrame();
    patientDetailPanel->setObjectName(QStringLiteral("patientDetailPanel"));
    auto* patientDetailLayout = new QVBoxLayout(patientDetailPanel);
    patientDetailLayout->setContentsMargins(28, 24, 28, 24);
    patientDetailLayout->setSpacing(22);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    m_editButton = new QPushButton(QStringLiteral("修改信息"));
    m_editButton->setObjectName(QStringLiteral("panelActionButton"));

    m_panelTitleLabel = new QLabel(QStringLiteral("患者详细档案"));
    m_panelTitleLabel->setObjectName(QStringLiteral("patientPanelTitle"));
    m_panelTitleLabel->setAlignment(Qt::AlignCenter);

    m_deleteButton = new QPushButton(QStringLiteral("删除"));
    m_deleteButton->setObjectName(QStringLiteral("panelDangerButton"));

    headerLayout->addWidget(m_editButton, 0, Qt::AlignLeft);
    headerLayout->addStretch();
    headerLayout->addWidget(m_panelTitleLabel, 0, Qt::AlignCenter);
    headerLayout->addStretch();
    headerLayout->addWidget(m_deleteButton, 0, Qt::AlignRight);
    patientDetailLayout->addLayout(headerLayout);

    auto* detailLayout = new QFormLayout();
    detailLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    detailLayout->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    detailLayout->setHorizontalSpacing(24);
    detailLayout->setVerticalSpacing(22);

    m_patientIdValue = new QLineEdit();
    m_patientIdValue->setReadOnly(true);
    m_patientIdValue->setObjectName(QStringLiteral("patientReadOnlyField"));
    m_patientIdValue->setFixedWidth(260);

    m_patientNameEdit = new QLineEdit();
    m_patientNameEdit->setFixedWidth(260);

    m_patientAgeSpin = new QSpinBox();
    m_patientAgeSpin->setRange(0, 150);
    m_patientAgeSpin->setFixedWidth(260);

    m_patientDiagnosisEdit = new QPlainTextEdit();
    m_patientDiagnosisEdit->setMinimumWidth(610);
    m_patientDiagnosisEdit->setMinimumHeight(260);
    m_patientDiagnosisEdit->setTabChangesFocus(true);

    detailLayout->addRow(QStringLiteral("患者ID"), m_patientIdValue);
    detailLayout->addRow(QStringLiteral("患者姓名"), m_patientNameEdit);
    detailLayout->addRow(QStringLiteral("患者年龄"), m_patientAgeSpin);
    detailLayout->addRow(QStringLiteral("诊断结果"), m_patientDiagnosisEdit);
    patientDetailLayout->addLayout(detailLayout);
    patientInfoLayout->addWidget(patientDetailPanel, 1);

    auto* imagingPage = new QWidget();
    imagingPage->setObjectName(QStringLiteral("imagingPage"));
    auto* imagingPageLayout = new QVBoxLayout(imagingPage);
    imagingPageLayout->setContentsMargins(0, 0, 0, 0);
    imagingPageLayout->setSpacing(0);

    auto* imagingPanel = new QFrame();
    imagingPanel->setObjectName(QStringLiteral("imagingPanel"));
    auto* imagingLayout = new QVBoxLayout(imagingPanel);
    imagingLayout->setContentsMargins(28, 22, 16, 22);
    imagingLayout->setSpacing(16);

    m_imagingPatientNameLabel = new QLabel(QStringLiteral("请选择左侧患者"));
    m_imagingPatientNameLabel->setObjectName(QStringLiteral("imagingPatientNameLabel"));

    auto* imagingDivider = new QFrame();
    imagingDivider->setObjectName(QStringLiteral("imagingDivider"));
    imagingDivider->setFrameShape(QFrame::HLine);
    imagingDivider->setFrameShadow(QFrame::Plain);

    m_imagingScrollArea = new QScrollArea();
    m_imagingScrollArea->setObjectName(QStringLiteral("imagingScrollArea"));
    m_imagingScrollArea->setWidgetResizable(true);
    m_imagingScrollArea->setFrameShape(QFrame::NoFrame);
    m_imagingScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imagingScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    m_imagingGridContainer = new QWidget();
    m_imagingGridContainer->setObjectName(QStringLiteral("imagingGridContainer"));
    auto* imagingGridLayout = new QGridLayout(m_imagingGridContainer);
    imagingGridLayout->setContentsMargins(6, 12, 18, 12);
    imagingGridLayout->setHorizontalSpacing(24);
    imagingGridLayout->setVerticalSpacing(28);
    imagingGridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_imagingScrollArea->setWidget(m_imagingGridContainer);

    imagingLayout->addWidget(m_imagingPatientNameLabel, 0, Qt::AlignLeft);
    imagingLayout->addWidget(imagingDivider);
    imagingLayout->addWidget(m_imagingScrollArea, 1);
    imagingPageLayout->addWidget(imagingPanel, 1);

    m_reportPreview = new QTextBrowser();

    m_treatmentTable = new QTableWidget(0, 6);
    m_treatmentTable->setHorizontalHeaderLabels(
        {QStringLiteral("治疗ID"),
         QStringLiteral("方案ID"),
         QStringLiteral("病灶类型"),
         QStringLiteral("总能量(J)"),
         QStringLiteral("总时长(s)"),
         QStringLiteral("状态")});
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_treatmentTable->verticalHeader()->setVisible(false);
    m_treatmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treatmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_sectionStack->addWidget(patientInfoPage);
    m_sectionStack->addWidget(imagingPage);
    m_sectionStack->addWidget(createContentCard(QStringLiteral("治疗报告"), m_reportPreview));
    m_sectionStack->addWidget(createContentCard(QStringLiteral("治疗数据"), m_treatmentTable));
    contentLayout->addWidget(m_sectionStack, 1);

    rootLayout->addLayout(contentLayout, 3);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &DataManagementPage::filterPatients);
    connect(m_patientList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (current == nullptr) {
            return;
        }
        selectPatientById(current->data(Qt::UserRole).toString());
    });
    connect(m_addPatientButton, &QPushButton::clicked, this, &DataManagementPage::onAddPatientClicked);
    connect(m_batchImportButton, &QPushButton::clicked, this, &DataManagementPage::onBatchImportClicked);
    connect(m_editButton, &QPushButton::clicked, this, &DataManagementPage::onEditButtonClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &DataManagementPage::onDeleteButtonClicked);
    connect(m_context, &ApplicationContext::selectedPatientChanged, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::selectedPatientCleared, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::activePlanChanged, this, &DataManagementPage::refreshFromContext);
    connect(m_context, &ApplicationContext::activePlanCleared, this, &DataManagementPage::refreshFromContext);

    populatePatientList();
    showSection(Section::PatientInfo);
    setPatientPanelMode(PatientPanelMode::View);
    if (!m_context->hasSelectedPatient() && m_patientList->count() > 0) {
        m_patientList->setCurrentRow(0);
    } else {
        refreshFromContext();
    }
}

void DataManagementPage::showSection(Section section)
{
    const bool patientInfoSection = section == Section::PatientInfo;
    if (m_sidebarActionsWidget != nullptr) {
        m_sidebarActionsWidget->setVisible(patientInfoSection);
    }

    switch (section) {
    case Section::PatientInfo:
        m_sectionStack->setCurrentIndex(0);
        break;
    case Section::ImagingData:
        m_sectionStack->setCurrentIndex(1);
        break;
    case Section::TreatmentReport:
        m_sectionStack->setCurrentIndex(2);
        break;
    case Section::TreatmentData:
        m_sectionStack->setCurrentIndex(3);
        break;
    }
}
void DataManagementPage::refreshFromContext()
{
    if (m_panelMode == PatientPanelMode::Create) {
        refreshPatientActionState();
        return;
    }

    if (!m_context->hasSelectedPatient()) {
        refreshPatientOverview(nullptr);
        fillImagingGallery(nullptr, {});
        fillTreatmentTable({});
        m_reportPreview->setHtml(QStringLiteral("<h2>治疗报告</h2><p>当前未选择患者。</p>"));
        refreshPatientActionState();
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

    const QVector<ImageSeriesRecord> imageSeries = m_clinicalDataService.listImageSeriesForPatient(patient.id);
    const QVector<TreatmentSessionRecord> treatmentSessions = m_clinicalDataService.listTreatmentSessionsForPatient(patient.id);
    const QVector<TreatmentReportRecord> reports = m_clinicalDataService.listTreatmentReportsForPatient(patient.id);

    fillImagingGallery(&patient, imageSeries);
    fillTreatmentTable(treatmentSessions);

    QString reportHtml =
        QStringLiteral("<h2>治疗报告</h2><p><b>患者ID：</b>%1</p><p><b>患者姓名：</b>%2</p><p><b>患者年龄：</b>%3</p><p><b>诊断结果：</b>%4</p>")
            .arg(wrapValue(patient.id))
            .arg(wrapValue(patient.name))
            .arg(patient.age)
            .arg(wrapValue(patient.diagnosis));

    if (reports.isEmpty()) {
        reportHtml += QStringLiteral("<p>当前没有已归档的治疗报告。</p>");
    } else {
        const TreatmentReportRecord& latestReport = reports.first();
        reportHtml += QStringLiteral("<p><b>报告标题：</b>%1</p><p><b>生成时间：</b>%2</p>")
                          .arg(wrapValue(latestReport.title))
                          .arg(wrapValue(latestReport.generatedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
        reportHtml += latestReport.contentHtml;
    }

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
    refreshPatientActionState();
}

void DataManagementPage::filterPatients(const QString& keyword)
{
    populatePatientList(keyword);
}

void DataManagementPage::onAddPatientClicked()
{
    enterCreateMode();
}

void DataManagementPage::onBatchImportClicked()
{
    importPatientsFromCsv();
}

void DataManagementPage::onEditButtonClicked()
{
    if (m_panelMode == PatientPanelMode::Create) {
        saveCurrentPatient();
        return;
    }

    if (!m_context->hasSelectedPatient()) {
        return;
    }

    if (m_panelMode != PatientPanelMode::Edit) {
        setPatientPanelMode(PatientPanelMode::Edit);
        m_patientNameEdit->setFocus();
        m_patientNameEdit->selectAll();
        return;
    }

    saveCurrentPatient();
}

void DataManagementPage::onDeleteButtonClicked()
{
    if (m_panelMode == PatientPanelMode::Create) {
        closeCreateMode();
        return;
    }

    if (!m_context->hasSelectedPatient()) {
        return;
    }

    archiveCurrentPatient();
}

void DataManagementPage::populatePatientList(const QString& keyword)
{
    m_patientList->clear();
    if (m_clinicalDataService.repository() == nullptr) {
        return;
    }

    const QVector<PatientRecord> patients = m_clinicalDataService.listPatients();
    for (const PatientRecord& patient : patients) {
        if (!matchesPatientKeyword(patient, keyword)) {
            continue;
        }

        auto* item = new QListWidgetItem(toPatientListText(patient), m_patientList);
        item->setData(Qt::UserRole, patient.id);
    }

    if (!m_context->hasSelectedPatient() || m_panelMode == PatientPanelMode::Create) {
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

    if (m_panelMode == PatientPanelMode::Create) {
        closeCreateMode();
    }

    if (m_context->hasSelectedPatient() && m_context->selectedPatient().id == patientId) {
        return;
    }

    if (m_clinicalDataService.repository() == nullptr) {
        return;
    }

    PatientRecord patient;
    if (!m_clinicalDataService.findPatientById(patientId, &patient)) {
        return;
    }

    setPatientPanelMode(PatientPanelMode::View);
    m_context->selectPatient(patient);
}

void DataManagementPage::refreshPatientOverview(const PatientRecord* patient)
{
    if (patient == nullptr) {
        m_patientIdValue->clear();
        m_patientNameEdit->clear();
        m_patientAgeSpin->setValue(0);
        m_patientDiagnosisEdit->clear();
        return;
    }

    m_patientIdValue->setText(patient->id);
    m_patientNameEdit->setText(patient->name);
    m_patientAgeSpin->setValue(patient->age);
    m_patientDiagnosisEdit->setPlainText(patient->diagnosis);
}

void DataManagementPage::setPatientPanelMode(PatientPanelMode mode)
{
    m_panelMode = mode;

    const bool editable = mode != PatientPanelMode::View;
    const bool creating = mode == PatientPanelMode::Create;

    m_searchEdit->setEnabled(!editable);
    m_patientList->setEnabled(!editable);
    m_addPatientButton->setEnabled(!editable);
    m_batchImportButton->setEnabled(!editable);
    m_patientNameEdit->setReadOnly(!editable);
    m_patientAgeSpin->setReadOnly(!editable);
    m_patientAgeSpin->setButtonSymbols(editable ? QAbstractSpinBox::UpDownArrows : QAbstractSpinBox::NoButtons);
    m_patientDiagnosisEdit->setReadOnly(!editable);
    m_panelTitleLabel->setText(QStringLiteral("患者详细档案"));

    if (creating) {
        m_editButton->setText(QStringLiteral("保存信息"));
        m_deleteButton->setText(QStringLiteral("关闭"));
    } else if (mode == PatientPanelMode::Edit) {
        m_editButton->setText(QStringLiteral("保存修改"));
        m_deleteButton->setText(QStringLiteral("删除"));
    } else {
        m_editButton->setText(QStringLiteral("修改信息"));
        m_deleteButton->setText(QStringLiteral("删除"));
    }

    refreshPatientActionState();
}
void DataManagementPage::refreshPatientActionState()
{
    const bool hasPatient = m_context != nullptr && m_context->hasSelectedPatient();
    const bool writable = m_clinicalDataRepository != nullptr && m_clinicalDataRepository->supportsWriteOperations();
    const bool creating = m_panelMode == PatientPanelMode::Create;

    m_addPatientButton->setEnabled(writable && m_panelMode == PatientPanelMode::View);
    m_batchImportButton->setEnabled(writable && m_panelMode == PatientPanelMode::View);
    m_editButton->setEnabled(writable && (creating || hasPatient));
    m_deleteButton->setEnabled(writable && (creating || hasPatient));
}

void DataManagementPage::enterCreateMode()
{
    setPatientPanelMode(PatientPanelMode::Create);
    refreshPatientOverview(nullptr);
    m_patientNameEdit->setFocus();
}

void DataManagementPage::closeCreateMode()
{
    setPatientPanelMode(PatientPanelMode::View);
    if (m_context->hasSelectedPatient()) {
        refreshPatientOverview(&m_context->selectedPatient());
    } else {
        refreshPatientOverview(nullptr);
    }
}

bool DataManagementPage::saveCurrentPatient()
{
    const bool creating = m_panelMode == PatientPanelMode::Create;
    if (!creating && !m_context->hasSelectedPatient()) {
        return false;
    }

    PatientRecord patient = creating ? PatientRecord {} : m_context->selectedPatient();
    patient.name = m_patientNameEdit->text().trimmed();
    patient.age = m_patientAgeSpin->value();
    patient.diagnosis = m_patientDiagnosisEdit->toPlainText().trimmed();

    if (!m_clinicalDataService.savePatient(&patient)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("patient"),
            QStringLiteral("%1患者信息：%2").arg(creating ? QStringLiteral("创建") : QStringLiteral("更新"), patient.id));
    }

    populatePatientList(m_searchEdit->text());
    setPatientPanelMode(PatientPanelMode::View);
    m_context->selectPatient(patient);
    return true;
}

bool DataManagementPage::archiveCurrentPatient()
{
    if (!m_context->hasSelectedPatient()) {
        return false;
    }

    const PatientRecord patient = m_context->selectedPatient();
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确认归档并隐藏患者 %1（%2）吗？").arg(patient.name, patient.id),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return false;
    }

    if (!m_clinicalDataService.archivePatient(patient.id)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(QStringLiteral("system"), QStringLiteral("patient"), QStringLiteral("归档患者：%1").arg(patient.id));
    }

    if (m_context->hasActivePlan() && m_context->activePlan().patientId == patient.id) {
        m_context->clearActivePlan();
    }

    setPatientPanelMode(PatientPanelMode::View);
    populatePatientList(m_searchEdit->text());
    if (m_patientList->count() > 0) {
        m_context->clearSelectedPatient();
        m_patientList->setCurrentRow(0);
    } else {
        m_context->clearSelectedPatient();
        refreshFromContext();
    }
    return true;
}

bool DataManagementPage::importPatientsFromCsv()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择批量导入文件"),
        QString(),
        QStringLiteral("CSV Files (*.csv);;Text Files (*.txt)"));
    if (filePath.isEmpty()) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("无法打开文件：%1").arg(filePath));
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    int importedCount = 0;
    int failedCount = 0;
    QString firstError;
    QHash<QString, int> headerIndexMap;
    bool headerParsed = false;
    int lineNumber = 0;

    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        ++lineNumber;
        if (rawLine.trimmed().isEmpty()) {
            continue;
        }

        const QVector<QString> columns = splitCsvLine(rawLine);
        if (!headerParsed) {
            QHash<QString, int> candidateHeader;
            for (int index = 0; index < columns.size(); ++index) {
                candidateHeader.insert(normalizeHeader(columns[index]), index);
            }
            if (candidateHeader.contains(QStringLiteral("name"))
                || candidateHeader.contains(QStringLiteral("患者姓名"))
                || candidateHeader.contains(QStringLiteral("patientname"))) {
                headerIndexMap = candidateHeader;
                headerParsed = true;
                continue;
            }
            headerParsed = true;
        }

        ImportedPatientRow importedRow;
        QString parseError;
        if (!parseImportedPatientRow(columns, headerIndexMap, &importedRow, &parseError)) {
            ++failedCount;
            if (firstError.isEmpty()) {
                firstError = QStringLiteral("第 %1 行：%2").arg(lineNumber).arg(parseError);
            }
            continue;
        }

        PatientRecord patient;
        patient.id = importedRow.id.trimmed();
        patient.name = importedRow.name.trimmed();
        patient.age = importedRow.age;
        patient.diagnosis = importedRow.diagnosis.trimmed();

        if (!m_clinicalDataService.savePatient(&patient)) {
            ++failedCount;
            if (firstError.isEmpty()) {
                firstError = QStringLiteral("第 %1 行：%2").arg(lineNumber).arg(m_clinicalDataService.lastError());
            }
            continue;
        }

        ++importedCount;
    }

    populatePatientList(m_searchEdit->text());
    if (m_patientList->count() > 0 && !m_context->hasSelectedPatient()) {
        m_patientList->setCurrentRow(0);
    }

    if (m_auditService != nullptr && importedCount > 0) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("patient"),
            QStringLiteral("批量导入患者：成功 %1 条，失败 %2 条").arg(importedCount).arg(failedCount));
    }

    QString message = QStringLiteral("导入完成：成功 %1 条").arg(importedCount);
    if (failedCount > 0) {
        message += QStringLiteral("，失败 %1 条。").arg(failedCount);
        if (!firstError.isEmpty()) {
            message += QStringLiteral("\n\n首个错误：%1").arg(firstError);
        }
    }
    QMessageBox::information(this, QStringLiteral("批量导入"), message);
    return importedCount > 0;
}

QVector<QString> DataManagementPage::splitCsvLine(const QString& line) const
{
    QVector<QString> columns;
    QString current;
    bool inQuotes = false;

    for (int index = 0; index < line.size(); ++index) {
        const QChar ch = line.at(index);
        if (ch == QLatin1Char('"')) {
            if (inQuotes && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                current += QLatin1Char('"');
                ++index;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == QLatin1Char(',') && !inQuotes) {
            columns.push_back(current.trimmed());
            current.clear();
            continue;
        }

        current += ch;
    }

    columns.push_back(current.trimmed());
    return columns;
}
bool DataManagementPage::parseImportedPatientRow(
    const QVector<QString>& columns,
    const QHash<QString, int>& headerIndexMap,
    ImportedPatientRow* row,
    QString* error) const
{
    if (row == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("导入目标为空。");
        }
        return false;
    }

    auto columnValue = [&columns](int index) -> QString {
        return index >= 0 && index < columns.size() ? columns.at(index).trimmed() : QString();
    };

    if (!headerIndexMap.isEmpty()) {
        const int idIndex = headerIndexMap.value(QStringLiteral("id"), headerIndexMap.value(QStringLiteral("患者id"), -1));
        const int nameIndex = headerIndexMap.value(
            QStringLiteral("name"),
            headerIndexMap.value(QStringLiteral("患者姓名"), headerIndexMap.value(QStringLiteral("patientname"), -1)));
        const int ageIndex = headerIndexMap.value(
            QStringLiteral("age"),
            headerIndexMap.value(QStringLiteral("患者年龄"), headerIndexMap.value(QStringLiteral("patientage"), -1)));
        const int diagnosisIndex = headerIndexMap.value(
            QStringLiteral("diagnosis"),
            headerIndexMap.value(QStringLiteral("诊断结果"), headerIndexMap.value(QStringLiteral("diagnosisresult"), -1)));

        row->id = columnValue(idIndex);
        row->name = columnValue(nameIndex);
        row->diagnosis = columnValue(diagnosisIndex);
        row->age = columnValue(ageIndex).toInt();
    } else if (columns.size() == 3) {
        row->name = columnValue(0);
        row->age = columnValue(1).toInt();
        row->diagnosis = columnValue(2);
    } else if (columns.size() >= 4) {
        row->id = columnValue(0);
        row->name = columnValue(1);
        row->age = columnValue(2).toInt();
        row->diagnosis = columnValue(3);
    } else {
        if (error != nullptr) {
            *error = QStringLiteral("列数不足，至少需要姓名、年龄、诊断结果。");
        }
        return false;
    }

    if (row->name.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("患者姓名不能为空。");
        }
        return false;
    }
    if (row->diagnosis.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("诊断结果不能为空。");
        }
        return false;
    }

    return true;
}

void DataManagementPage::fillImagingGallery(const PatientRecord* patient, const QVector<ImageSeriesRecord>& imageSeries)
{
    if (m_imagingPatientNameLabel == nullptr || m_imagingGridContainer == nullptr) {
        return;
    }

    m_imagingPatientNameLabel->setText(patient == nullptr ? QStringLiteral("请选择左侧患者") : patient->name);

    auto* gridLayout = qobject_cast<QGridLayout*>(m_imagingGridContainer->layout());
    if (gridLayout == nullptr) {
        return;
    }

    clearLayout(gridLayout);

    if (patient == nullptr) {
        auto* emptyLabel = new QLabel(QStringLiteral("选择左侧患者后，这里会显示对应影像缩略图。"));
        emptyLabel->setObjectName(QStringLiteral("imagingEmptyStateLabel"));
        gridLayout->addWidget(emptyLabel, 0, 0, 1, 4, Qt::AlignCenter);
        return;
    }

    if (imageSeries.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("当前患者暂无影像检查记录。"));
        emptyLabel->setObjectName(QStringLiteral("imagingEmptyStateLabel"));
        gridLayout->addWidget(emptyLabel, 0, 0, 1, 4, Qt::AlignCenter);
        return;
    }

    constexpr int kColumns = 4;
    for (int index = 0; index < imageSeries.size(); ++index) {
        const int row = index / kColumns;
        const int column = index % kColumns;
        gridLayout->addWidget(createImagingThumbnailCard(imageSeries[index], index), row, column);
    }

    for (int column = 0; column < kColumns; ++column) {
        gridLayout->setColumnStretch(column, 1);
    }
}

QWidget* DataManagementPage::createImagingThumbnailCard(const ImageSeriesRecord& imageSeries, int index) const
{
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("imagingThumbnailCard"));
    card->setFixedWidth(176);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* previewLabel = new QLabel();
    previewLabel->setObjectName(QStringLiteral("imagingThumbnailPreview"));
    previewLabel->setFixedSize(160, 96);
    previewLabel->setAlignment(Qt::AlignCenter);

    QString previewPath;
    const QFileInfo storageInfo(imageSeries.storagePath);
    if (storageInfo.exists()) {
        if (storageInfo.isFile()) {
            previewPath = storageInfo.absoluteFilePath();
        } else if (storageInfo.isDir()) {
            QDirIterator iterator(
                storageInfo.absoluteFilePath(),
                {QStringLiteral("*.png"),
                 QStringLiteral("*.jpg"),
                 QStringLiteral("*.jpeg"),
                 QStringLiteral("*.bmp")},
                QDir::Files,
                QDirIterator::Subdirectories);
            if (iterator.hasNext()) {
                previewPath = iterator.next();
            }
        }
    }

    QPixmap preview;
    if (!previewPath.isEmpty()) {
        QPixmap loaded(previewPath);
        if (!loaded.isNull()) {
            preview = loaded.scaled(previewLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }
    }

    if (preview.isNull()) {
        preview = QPixmap(previewLabel->size());
        preview.fill(QColor(QStringLiteral("#f7f7f5")));
        QPainter painter(&preview);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(QStringLiteral("#ccd2d9")), 1));
        painter.drawRect(preview.rect().adjusted(0, 0, -1, -1));
        painter.drawLine(0, 0, preview.width() - 1, preview.height() - 1);
        painter.drawLine(preview.width() - 1, 0, 0, preview.height() - 1);
    }

    previewLabel->setPixmap(preview);
    previewLabel->setScaledContents(true);

    auto* captionLabel = new QLabel(
        QStringLiteral("%1  %2")
            .arg(imageSeries.type)
            .arg(imageSeries.acquisitionDate.toString(QStringLiteral("yyyy-MM-dd"))));
    captionLabel->setObjectName(QStringLiteral("imagingThumbnailCaption"));
    captionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    card->setToolTip(
        QStringLiteral("影像ID：%1\n类型：%2\n采集日期：%3\n存储路径：%4\n序号：%5")
            .arg(imageSeries.id)
            .arg(imageSeries.type)
            .arg(imageSeries.acquisitionDate.toString(QStringLiteral("yyyy-MM-dd")))
            .arg(imageSeries.storagePath)
            .arg(index + 1));

    layout->addWidget(previewLabel);
    layout->addWidget(captionLabel);
    return card;
}

void DataManagementPage::fillTreatmentTable(const QVector<TreatmentSessionRecord>& treatmentSessions)
{
    m_treatmentTable->clearContents();
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

}  // namespace panthera::modules
