#include "modules/data/data_management_page.h"

#include <algorithm>

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
#include <QToolButton>
#include <QVBoxLayout>

namespace panthera::modules {

using namespace panthera::core;

namespace {

QString toPatientListText(const PatientRecord& patient)
{
    return QStringLiteral("%1 (%2岁)\nID:%3 | %4")
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

QString previewPathForStorage(const QString& storagePath)
{
    if (storagePath.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo storageInfo(storagePath);
    if (!storageInfo.exists()) {
        return {};
    }
    if (storageInfo.isFile()) {
        return storageInfo.absoluteFilePath();
    }
    if (!storageInfo.isDir()) {
        return {};
    }

    QDirIterator iterator(
        storageInfo.absoluteFilePath(),
        {QStringLiteral("*.png"),
         QStringLiteral("*.jpg"),
         QStringLiteral("*.jpeg"),
         QStringLiteral("*.bmp")},
        QDir::Files,
        QDirIterator::Subdirectories);
    return iterator.hasNext() ? iterator.next() : QString();
}

QString reportListText(const TreatmentReportRecord& report)
{
    const QString title = report.title.trimmed().isEmpty() ? QStringLiteral("治疗报告") : report.title.trimmed();
    return QStringLiteral("%1\n%2")
        .arg(title)
        .arg(report.generatedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
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

    auto* imagingHeaderLayout = new QHBoxLayout();
    imagingHeaderLayout->setSpacing(12);

    m_imagingPatientNameLabel = new QLabel(QStringLiteral("请选择左侧患者"));
    m_imagingPatientNameLabel->setObjectName(QStringLiteral("imagingPatientNameLabel"));

    m_addImageSeriesButton = new QPushButton(QStringLiteral("新增影像"));
    m_addImageSeriesButton->setObjectName(QStringLiteral("panelActionButton"));

    m_saveImageSeriesButton = new QPushButton(QStringLiteral("保存影像"));
    m_saveImageSeriesButton->setObjectName(QStringLiteral("panelActionButton"));

    m_deleteImageSeriesButton = new QPushButton(QStringLiteral("删除影像"));
    m_deleteImageSeriesButton->setObjectName(QStringLiteral("panelDangerButton"));

    imagingHeaderLayout->addWidget(m_imagingPatientNameLabel);
    imagingHeaderLayout->addStretch();
    imagingHeaderLayout->addWidget(m_addImageSeriesButton);
    imagingHeaderLayout->addWidget(m_saveImageSeriesButton);
    imagingHeaderLayout->addWidget(m_deleteImageSeriesButton);

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
    m_imagingScrollArea->setMinimumHeight(360);

    m_imagingGridContainer = new QWidget();
    m_imagingGridContainer->setObjectName(QStringLiteral("imagingGridContainer"));
    auto* imagingGridLayout = new QGridLayout(m_imagingGridContainer);
    imagingGridLayout->setContentsMargins(6, 12, 18, 12);
    imagingGridLayout->setHorizontalSpacing(24);
    imagingGridLayout->setVerticalSpacing(28);
    imagingGridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_imagingScrollArea->setWidget(m_imagingGridContainer);

    auto* imagingEditorFrame = new QFrame();
    imagingEditorFrame->setObjectName(QStringLiteral("dataEditorPanel"));
    auto* imagingEditorLayout = new QHBoxLayout(imagingEditorFrame);
    imagingEditorLayout->setContentsMargins(18, 18, 18, 18);
    imagingEditorLayout->setSpacing(18);

    auto* imagingFormLayout = new QFormLayout();
    imagingFormLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    imagingFormLayout->setHorizontalSpacing(20);
    imagingFormLayout->setVerticalSpacing(14);

    m_imageSeriesIdValue = new QLineEdit();
    m_imageSeriesIdValue->setObjectName(QStringLiteral("patientReadOnlyField"));
    m_imageSeriesIdValue->setReadOnly(true);

    m_imageSeriesTypeEdit = new QLineEdit();
    m_imageSeriesStoragePathEdit = new QLineEdit();
    m_imageSeriesDateEdit = new QDateEdit();
    m_imageSeriesDateEdit->setCalendarPopup(true);
    m_imageSeriesDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_imageSeriesNotesEdit = new QPlainTextEdit();
    m_imageSeriesNotesEdit->setMinimumHeight(110);

    imagingFormLayout->addRow(QStringLiteral("影像ID"), m_imageSeriesIdValue);
    imagingFormLayout->addRow(QStringLiteral("影像类型"), m_imageSeriesTypeEdit);
    imagingFormLayout->addRow(QStringLiteral("采集日期"), m_imageSeriesDateEdit);
    imagingFormLayout->addRow(QStringLiteral("存储路径"), m_imageSeriesStoragePathEdit);
    imagingFormLayout->addRow(QStringLiteral("备注"), m_imageSeriesNotesEdit);

    auto* imagingFormWidget = new QWidget();
    imagingFormWidget->setLayout(imagingFormLayout);

    m_imageSeriesPreviewLabel = new QLabel();
    m_imageSeriesPreviewLabel->setObjectName(QStringLiteral("imageSeriesPreviewLabel"));
    m_imageSeriesPreviewLabel->setAlignment(Qt::AlignCenter);
    m_imageSeriesPreviewLabel->setMinimumSize(320, 220);

    imagingEditorLayout->addWidget(imagingFormWidget, 1);
    imagingEditorLayout->addWidget(m_imageSeriesPreviewLabel, 0, Qt::AlignTop);

    imagingLayout->addLayout(imagingHeaderLayout);
    imagingLayout->addWidget(imagingDivider);
    imagingLayout->addWidget(m_imagingScrollArea, 1);
    imagingLayout->addWidget(imagingEditorFrame);
    imagingPageLayout->addWidget(imagingPanel, 1);

    auto* reportPage = new QWidget();
    auto* reportPageLayout = new QVBoxLayout(reportPage);
    reportPageLayout->setContentsMargins(0, 0, 0, 0);
    reportPageLayout->setSpacing(0);

    auto* reportPanel = new QFrame();
    reportPanel->setObjectName(QStringLiteral("patientDetailPanel"));
    auto* reportLayout = new QVBoxLayout(reportPanel);
    reportLayout->setContentsMargins(24, 22, 24, 22);
    reportLayout->setSpacing(18);

    auto* reportHeaderLayout = new QHBoxLayout();
    reportHeaderLayout->setSpacing(12);

    auto* reportTitleLabel = new QLabel(QStringLiteral("治疗报告维护"));
    reportTitleLabel->setObjectName(QStringLiteral("patientPanelTitle"));

    m_addReportButton = new QPushButton(QStringLiteral("新增报告"));
    m_addReportButton->setObjectName(QStringLiteral("panelActionButton"));

    m_saveReportButton = new QPushButton(QStringLiteral("保存报告"));
    m_saveReportButton->setObjectName(QStringLiteral("panelActionButton"));

    m_deleteReportButton = new QPushButton(QStringLiteral("删除报告"));
    m_deleteReportButton->setObjectName(QStringLiteral("panelDangerButton"));

    reportHeaderLayout->addWidget(reportTitleLabel);
    reportHeaderLayout->addStretch();
    reportHeaderLayout->addWidget(m_addReportButton);
    reportHeaderLayout->addWidget(m_saveReportButton);
    reportHeaderLayout->addWidget(m_deleteReportButton);

    auto* reportTopLayout = new QHBoxLayout();
    reportTopLayout->setSpacing(18);

    m_reportListWidget = new QListWidget();
    m_reportListWidget->setObjectName(QStringLiteral("dataRecordList"));
    m_reportListWidget->setMinimumWidth(280);
    m_reportListWidget->setMaximumWidth(320);

    m_reportPreview = new QTextBrowser();
    m_reportPreview->setObjectName(QStringLiteral("reportPreviewBrowser"));
    m_reportPreview->setMinimumHeight(240);

    reportTopLayout->addWidget(m_reportListWidget);
    reportTopLayout->addWidget(m_reportPreview, 1);

    auto* reportFormLayout = new QFormLayout();
    reportFormLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    reportFormLayout->setHorizontalSpacing(20);
    reportFormLayout->setVerticalSpacing(14);

    m_reportIdValue = new QLineEdit();
    m_reportIdValue->setObjectName(QStringLiteral("patientReadOnlyField"));
    m_reportIdValue->setReadOnly(true);

    m_reportSessionCombo = new QComboBox();
    m_reportTitleEdit = new QLineEdit();

    m_reportGeneratedAtEdit = new QDateTimeEdit();
    m_reportGeneratedAtEdit->setCalendarPopup(true);
    m_reportGeneratedAtEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    m_reportNotesEdit = new QPlainTextEdit();
    m_reportNotesEdit->setMinimumHeight(80);

    m_reportContentEdit = new QTextEdit();
    m_reportContentEdit->setMinimumHeight(220);
    m_reportContentEdit->setAcceptRichText(true);

    reportFormLayout->addRow(QStringLiteral("报告ID"), m_reportIdValue);
    reportFormLayout->addRow(QStringLiteral("关联治疗"), m_reportSessionCombo);
    reportFormLayout->addRow(QStringLiteral("报告标题"), m_reportTitleEdit);
    reportFormLayout->addRow(QStringLiteral("生成时间"), m_reportGeneratedAtEdit);
    reportFormLayout->addRow(QStringLiteral("备注"), m_reportNotesEdit);
    reportFormLayout->addRow(QStringLiteral("报告内容"), m_reportContentEdit);

    reportLayout->addLayout(reportHeaderLayout);
    reportLayout->addLayout(reportTopLayout, 1);
    reportLayout->addLayout(reportFormLayout);
    reportPageLayout->addWidget(reportPanel, 1);

    auto* treatmentPage = new QWidget();
    auto* treatmentPageLayout = new QVBoxLayout(treatmentPage);
    treatmentPageLayout->setContentsMargins(0, 0, 0, 0);
    treatmentPageLayout->setSpacing(0);

    auto* treatmentPanel = new QFrame();
    treatmentPanel->setObjectName(QStringLiteral("patientDetailPanel"));
    auto* treatmentLayout = new QVBoxLayout(treatmentPanel);
    treatmentLayout->setContentsMargins(24, 22, 24, 22);
    treatmentLayout->setSpacing(18);

    auto* treatmentHeaderLayout = new QHBoxLayout();
    treatmentHeaderLayout->setSpacing(12);

    auto* treatmentTitleLabel = new QLabel(QStringLiteral("治疗数据维护"));
    treatmentTitleLabel->setObjectName(QStringLiteral("patientPanelTitle"));

    m_addTreatmentButton = new QPushButton(QStringLiteral("新增治疗"));
    m_addTreatmentButton->setObjectName(QStringLiteral("panelActionButton"));

    m_saveTreatmentButton = new QPushButton(QStringLiteral("保存治疗"));
    m_saveTreatmentButton->setObjectName(QStringLiteral("panelActionButton"));

    m_deleteTreatmentButton = new QPushButton(QStringLiteral("删除治疗"));
    m_deleteTreatmentButton->setObjectName(QStringLiteral("panelDangerButton"));

    treatmentHeaderLayout->addWidget(treatmentTitleLabel);
    treatmentHeaderLayout->addStretch();
    treatmentHeaderLayout->addWidget(m_addTreatmentButton);
    treatmentHeaderLayout->addWidget(m_saveTreatmentButton);
    treatmentHeaderLayout->addWidget(m_deleteTreatmentButton);

    m_treatmentTable = new QTableWidget(0, 7);
    m_treatmentTable->setHorizontalHeaderLabels(
        {QStringLiteral("治疗ID"),
         QStringLiteral("方案ID"),
         QStringLiteral("病灶类型"),
         QStringLiteral("治疗日期"),
         QStringLiteral("总能量(J)"),
         QStringLiteral("总时长(s)"),
         QStringLiteral("状态")});
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_treatmentTable->verticalHeader()->setVisible(false);
    m_treatmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treatmentTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treatmentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treatmentTable->setMinimumHeight(260);

    auto* treatmentFormLayout = new QFormLayout();
    treatmentFormLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    treatmentFormLayout->setHorizontalSpacing(20);
    treatmentFormLayout->setVerticalSpacing(14);

    m_treatmentIdValue = new QLineEdit();
    m_treatmentIdValue->setObjectName(QStringLiteral("patientReadOnlyField"));
    m_treatmentIdValue->setReadOnly(true);

    m_treatmentPlanIdEdit = new QLineEdit();
    m_treatmentLesionTypeEdit = new QLineEdit();

    m_treatmentDateEdit = new QDateTimeEdit();
    m_treatmentDateEdit->setCalendarPopup(true);
    m_treatmentDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    m_treatmentStatusEdit = new QLineEdit();

    m_treatmentEnergySpin = new QDoubleSpinBox();
    m_treatmentEnergySpin->setRange(0.0, 999999.0);
    m_treatmentEnergySpin->setDecimals(1);
    m_treatmentEnergySpin->setSuffix(QStringLiteral(" J"));

    m_treatmentDurationSpin = new QDoubleSpinBox();
    m_treatmentDurationSpin->setRange(0.0, 999999.0);
    m_treatmentDurationSpin->setDecimals(1);
    m_treatmentDurationSpin->setSuffix(QStringLiteral(" s"));

    m_treatmentDoseSpin = new QDoubleSpinBox();
    m_treatmentDoseSpin->setRange(0.0, 999999.0);
    m_treatmentDoseSpin->setDecimals(2);

    m_treatmentPathSummaryEdit = new QPlainTextEdit();
    m_treatmentPathSummaryEdit->setMinimumHeight(120);

    treatmentFormLayout->addRow(QStringLiteral("治疗ID"), m_treatmentIdValue);
    treatmentFormLayout->addRow(QStringLiteral("方案ID"), m_treatmentPlanIdEdit);
    treatmentFormLayout->addRow(QStringLiteral("病灶类型"), m_treatmentLesionTypeEdit);
    treatmentFormLayout->addRow(QStringLiteral("治疗日期"), m_treatmentDateEdit);
    treatmentFormLayout->addRow(QStringLiteral("治疗状态"), m_treatmentStatusEdit);
    treatmentFormLayout->addRow(QStringLiteral("总能量"), m_treatmentEnergySpin);
    treatmentFormLayout->addRow(QStringLiteral("总时长"), m_treatmentDurationSpin);
    treatmentFormLayout->addRow(QStringLiteral("剂量"), m_treatmentDoseSpin);
    treatmentFormLayout->addRow(QStringLiteral("路径摘要"), m_treatmentPathSummaryEdit);

    treatmentLayout->addLayout(treatmentHeaderLayout);
    treatmentLayout->addWidget(m_treatmentTable);
    treatmentLayout->addLayout(treatmentFormLayout);
    treatmentPageLayout->addWidget(treatmentPanel, 1);

    m_sectionStack->addWidget(patientInfoPage);
    m_sectionStack->addWidget(imagingPage);
    m_sectionStack->addWidget(reportPage);
    m_sectionStack->addWidget(treatmentPage);
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

    connect(m_addImageSeriesButton, &QPushButton::clicked, this, &DataManagementPage::onAddImageSeriesClicked);
    connect(m_saveImageSeriesButton, &QPushButton::clicked, this, &DataManagementPage::onSaveImageSeriesClicked);
    connect(m_deleteImageSeriesButton, &QPushButton::clicked, this, &DataManagementPage::onDeleteImageSeriesClicked);
    connect(m_imageSeriesStoragePathEdit, &QLineEdit::textChanged, this, &DataManagementPage::refreshImagePreview);

    connect(m_addTreatmentButton, &QPushButton::clicked, this, &DataManagementPage::onAddTreatmentSessionClicked);
    connect(m_saveTreatmentButton, &QPushButton::clicked, this, &DataManagementPage::onSaveTreatmentSessionClicked);
    connect(m_deleteTreatmentButton, &QPushButton::clicked, this, &DataManagementPage::onDeleteTreatmentSessionClicked);
    connect(m_treatmentTable, &QTableWidget::itemSelectionChanged, this, &DataManagementPage::onTreatmentSelectionChanged);

    connect(m_addReportButton, &QPushButton::clicked, this, &DataManagementPage::onAddTreatmentReportClicked);
    connect(m_saveReportButton, &QPushButton::clicked, this, &DataManagementPage::onSaveTreatmentReportClicked);
    connect(m_deleteReportButton, &QPushButton::clicked, this, &DataManagementPage::onDeleteTreatmentReportClicked);
    connect(m_reportListWidget, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        setSelectedTreatmentReport(current == nullptr ? QString() : current->data(Qt::UserRole).toString());
    });
    connect(m_reportContentEdit, &QTextEdit::textChanged, this, &DataManagementPage::syncReportPreview);
    connect(m_reportTitleEdit, &QLineEdit::textChanged, this, &DataManagementPage::syncReportPreview);
    connect(m_reportGeneratedAtEdit, &QDateTimeEdit::dateTimeChanged, this, &DataManagementPage::syncReportPreview);
    connect(m_reportSessionCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        syncReportPreview();
        refreshDataActionState();
    });

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
        refreshDataActionState();
        return;
    }

    if (!m_context->hasSelectedPatient()) {
        refreshPatientOverview(nullptr);
        m_currentImageSeries.clear();
        m_currentTreatmentSessions.clear();
        m_currentTreatmentReports.clear();
        m_selectedImageSeriesId.clear();
        m_selectedTreatmentSessionId.clear();
        m_selectedTreatmentReportId.clear();
        fillImagingGallery(nullptr, {});
        populateImageSeriesEditor(nullptr);
        fillTreatmentTable({});
        populateTreatmentSessionEditor(nullptr);
        fillReportList({});
        populateTreatmentReportEditor(nullptr);
        clearReportPreview(QStringLiteral("当前未选择患者。"));
        refreshPatientActionState();
        refreshDataActionState();
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

    m_currentImageSeries = m_clinicalDataService.listImageSeriesForPatient(patient.id);
    m_currentTreatmentSessions = m_clinicalDataService.listTreatmentSessionsForPatient(patient.id);
    m_currentTreatmentReports = m_clinicalDataService.listTreatmentReportsForPatient(patient.id);

    if (findSelectedImageSeries() == nullptr) {
        m_selectedImageSeriesId = m_currentImageSeries.isEmpty() ? QString() : m_currentImageSeries.first().id;
    }
    if (findSelectedTreatmentSession() == nullptr) {
        m_selectedTreatmentSessionId = m_currentTreatmentSessions.isEmpty() ? QString() : m_currentTreatmentSessions.first().id;
    }
    if (findSelectedTreatmentReport() == nullptr) {
        m_selectedTreatmentReportId = m_currentTreatmentReports.isEmpty() ? QString() : m_currentTreatmentReports.first().id;
    }

    fillImagingGallery(&patient, m_currentImageSeries);
    populateImageSeriesEditor(findSelectedImageSeries());

    fillTreatmentTable(m_currentTreatmentSessions);
    populateTreatmentSessionEditor(findSelectedTreatmentSession());

    fillReportList(m_currentTreatmentReports);
    populateTreatmentReportEditor(findSelectedTreatmentReport());

    refreshPatientActionState();
    refreshDataActionState();
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

void DataManagementPage::onAddImageSeriesClicked()
{
    m_selectedImageSeriesId.clear();
    populateImageSeriesEditor(nullptr);
    refreshDataActionState();
}

void DataManagementPage::onSaveImageSeriesClicked()
{
    saveCurrentImageSeries();
}

void DataManagementPage::onDeleteImageSeriesClicked()
{
    deleteCurrentImageSeries();
}

void DataManagementPage::onAddTreatmentSessionClicked()
{
    m_selectedTreatmentSessionId.clear();
    populateTreatmentSessionEditor(nullptr);
    refreshDataActionState();
}

void DataManagementPage::onSaveTreatmentSessionClicked()
{
    saveCurrentTreatmentSession();
}

void DataManagementPage::onDeleteTreatmentSessionClicked()
{
    deleteCurrentTreatmentSession();
}

void DataManagementPage::onTreatmentSelectionChanged()
{
    if (m_treatmentTable == nullptr || m_treatmentTable->currentRow() < 0) {
        setSelectedTreatmentSession(QString());
        return;
    }

    QTableWidgetItem* idItem = m_treatmentTable->item(m_treatmentTable->currentRow(), 0);
    setSelectedTreatmentSession(idItem == nullptr ? QString() : idItem->data(Qt::UserRole).toString());
}

void DataManagementPage::onAddTreatmentReportClicked()
{
    m_selectedTreatmentReportId.clear();
    populateTreatmentReportEditor(nullptr);
    refreshDataActionState();
}

void DataManagementPage::onSaveTreatmentReportClicked()
{
    saveCurrentTreatmentReport();
}

void DataManagementPage::onDeleteTreatmentReportClicked()
{
    deleteCurrentTreatmentReport();
}

void DataManagementPage::onReportSelectionChanged()
{
    setSelectedTreatmentReport(
        m_reportListWidget != nullptr && m_reportListWidget->currentItem() != nullptr
            ? m_reportListWidget->currentItem()->data(Qt::UserRole).toString()
            : QString());
}

void DataManagementPage::syncReportPreview()
{
    if (m_reportPreview == nullptr) {
        return;
    }

    const QString contentText = m_reportContentEdit != nullptr ? m_reportContentEdit->toPlainText().trimmed() : QString();
    if (contentText.isEmpty()) {
        clearReportPreview(QStringLiteral("当前报告尚未填写内容。"));
        return;
    }

    m_reportPreview->setHtml(m_reportContentEdit->toHtml());
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
    const bool writable = hasWritableRepository();
    const bool creating = m_panelMode == PatientPanelMode::Create;

    m_addPatientButton->setEnabled(writable && m_panelMode == PatientPanelMode::View);
    m_batchImportButton->setEnabled(writable && m_panelMode == PatientPanelMode::View);
    m_editButton->setEnabled(writable && (creating || hasPatient));
    m_deleteButton->setEnabled(writable && (creating || hasPatient));
}

void DataManagementPage::refreshDataActionState()
{
    const bool hasPatient = m_context != nullptr && m_context->hasSelectedPatient();
    const bool writable = hasWritableRepository();

    const QList<QWidget*> imageEditors {
        m_imageSeriesTypeEdit,
        m_imageSeriesStoragePathEdit,
        m_imageSeriesDateEdit,
        m_imageSeriesNotesEdit
    };
    for (QWidget* widget : imageEditors) {
        if (widget != nullptr) {
            widget->setEnabled(writable && hasPatient);
        }
    }

    const QList<QWidget*> treatmentEditors {
        m_treatmentPlanIdEdit,
        m_treatmentLesionTypeEdit,
        m_treatmentDateEdit,
        m_treatmentStatusEdit,
        m_treatmentEnergySpin,
        m_treatmentDurationSpin,
        m_treatmentDoseSpin,
        m_treatmentPathSummaryEdit
    };
    for (QWidget* widget : treatmentEditors) {
        if (widget != nullptr) {
            widget->setEnabled(writable && hasPatient);
        }
    }

    const QList<QWidget*> reportEditors {
        m_reportSessionCombo,
        m_reportTitleEdit,
        m_reportGeneratedAtEdit,
        m_reportNotesEdit,
        m_reportContentEdit
    };
    for (QWidget* widget : reportEditors) {
        if (widget != nullptr) {
            widget->setEnabled(writable && hasPatient);
        }
    }

    if (m_imagingScrollArea != nullptr) {
        m_imagingScrollArea->setEnabled(hasPatient);
    }
    if (m_treatmentTable != nullptr) {
        m_treatmentTable->setEnabled(hasPatient);
    }
    if (m_reportListWidget != nullptr) {
        m_reportListWidget->setEnabled(hasPatient);
    }

    if (m_addImageSeriesButton != nullptr) {
        m_addImageSeriesButton->setEnabled(writable && hasPatient);
    }
    if (m_saveImageSeriesButton != nullptr) {
        m_saveImageSeriesButton->setEnabled(writable && hasPatient);
    }
    if (m_deleteImageSeriesButton != nullptr) {
        m_deleteImageSeriesButton->setEnabled(writable && hasPatient && !m_selectedImageSeriesId.isEmpty());
    }

    if (m_addTreatmentButton != nullptr) {
        m_addTreatmentButton->setEnabled(writable && hasPatient);
    }
    if (m_saveTreatmentButton != nullptr) {
        m_saveTreatmentButton->setEnabled(writable && hasPatient);
    }
    if (m_deleteTreatmentButton != nullptr) {
        m_deleteTreatmentButton->setEnabled(writable && hasPatient && !m_selectedTreatmentSessionId.isEmpty());
    }

    const bool canSaveReport = writable && hasPatient && m_reportSessionCombo != nullptr && m_reportSessionCombo->count() > 0;
    if (m_addReportButton != nullptr) {
        m_addReportButton->setEnabled(writable && hasPatient);
    }
    if (m_saveReportButton != nullptr) {
        m_saveReportButton->setEnabled(canSaveReport);
    }
    if (m_deleteReportButton != nullptr) {
        m_deleteReportButton->setEnabled(writable && hasPatient && !m_selectedTreatmentReportId.isEmpty());
    }
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
        auto* emptyLabel = new QLabel(QStringLiteral("当前患者暂无影像检查记录，可在下方表单新增。"));
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

QWidget* DataManagementPage::createImagingThumbnailCard(const ImageSeriesRecord& imageSeries, int index)
{
    auto* button = new QToolButton();
    button->setObjectName(QStringLiteral("imagingThumbnailButton"));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setCheckable(true);
    button->setChecked(imageSeries.id == m_selectedImageSeriesId);
    button->setIcon(QIcon(loadPreviewPixmap(imageSeries.storagePath, QSize(160, 96))));
    button->setIconSize(QSize(160, 96));
    button->setFixedWidth(176);
    button->setText(
        QStringLiteral("%1\n%2")
            .arg(imageSeries.type)
            .arg(imageSeries.acquisitionDate.toString(QStringLiteral("yyyy-MM-dd"))));
    button->setToolTip(
        QStringLiteral("影像ID：%1\n类型：%2\n采集日期：%3\n存储路径：%4\n序号：%5")
            .arg(imageSeries.id)
            .arg(imageSeries.type)
            .arg(imageSeries.acquisitionDate.toString(QStringLiteral("yyyy-MM-dd")))
            .arg(imageSeries.storagePath)
            .arg(index + 1));
    connect(button, &QToolButton::clicked, this, [this, imageSeriesId = imageSeries.id]() {
        setSelectedImageSeries(imageSeriesId);
    });
    return button;
}

void DataManagementPage::populateImageSeriesEditor(const ImageSeriesRecord* imageSeries)
{
    if (imageSeries == nullptr) {
        m_imageSeriesIdValue->clear();
        m_imageSeriesTypeEdit->clear();
        m_imageSeriesStoragePathEdit->clear();
        m_imageSeriesDateEdit->setDate(QDate::currentDate());
        m_imageSeriesNotesEdit->clear();
        refreshImagePreview(QString());
        m_imageSeriesPreviewLabel->setToolTip(
            m_context != nullptr && m_context->hasSelectedPatient()
                ? QStringLiteral("请从上方缩略图选择影像，或新增一条影像记录。")
                : QStringLiteral("当前未选择患者。"));
        return;
    }

    m_imageSeriesIdValue->setText(imageSeries->id);
    m_imageSeriesTypeEdit->setText(imageSeries->type);
    m_imageSeriesStoragePathEdit->setText(imageSeries->storagePath);
    m_imageSeriesDateEdit->setDate(imageSeries->acquisitionDate.isValid() ? imageSeries->acquisitionDate : QDate::currentDate());
    m_imageSeriesNotesEdit->setPlainText(imageSeries->notes);
    refreshImagePreview(imageSeries->storagePath);
}

const ImageSeriesRecord* DataManagementPage::findSelectedImageSeries() const
{
    const auto it = std::find_if(m_currentImageSeries.cbegin(), m_currentImageSeries.cend(), [this](const ImageSeriesRecord& imageSeries) {
        return imageSeries.id == m_selectedImageSeriesId;
    });
    return it == m_currentImageSeries.cend() ? nullptr : &(*it);
}

void DataManagementPage::setSelectedImageSeries(const QString& imageSeriesId)
{
    m_selectedImageSeriesId = imageSeriesId;
    fillImagingGallery(m_context != nullptr && m_context->hasSelectedPatient() ? &m_context->selectedPatient() : nullptr, m_currentImageSeries);
    populateImageSeriesEditor(findSelectedImageSeries());
    refreshDataActionState();
}

bool DataManagementPage::saveCurrentImageSeries()
{
    if (m_context == nullptr || !m_context->hasSelectedPatient()) {
        return false;
    }

    ImageSeriesRecord imageSeries = findSelectedImageSeries() != nullptr ? *findSelectedImageSeries() : ImageSeriesRecord {};
    const bool creating = imageSeries.id.trimmed().isEmpty();
    imageSeries.patientId = m_context->selectedPatient().id;
    imageSeries.type = m_imageSeriesTypeEdit->text().trimmed();
    imageSeries.storagePath = m_imageSeriesStoragePathEdit->text().trimmed();
    imageSeries.acquisitionDate = m_imageSeriesDateEdit->date();
    imageSeries.notes = m_imageSeriesNotesEdit->toPlainText().trimmed();

    if (!m_clinicalDataService.saveImageSeries(&imageSeries)) {
        QMessageBox::warning(this, QStringLiteral("保存影像失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("image_series"),
            QStringLiteral("%1影像记录：%2").arg(creating ? QStringLiteral("创建") : QStringLiteral("更新"), imageSeries.id));
    }

    m_selectedImageSeriesId = imageSeries.id;
    refreshFromContext();
    return true;
}

bool DataManagementPage::deleteCurrentImageSeries()
{
    if (m_selectedImageSeriesId.isEmpty()) {
        return false;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确认删除当前影像记录 %1 吗？").arg(m_selectedImageSeriesId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return false;
    }

    if (!m_clinicalDataService.deleteImageSeries(m_selectedImageSeriesId)) {
        QMessageBox::warning(this, QStringLiteral("删除影像失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("image_series"),
            QStringLiteral("删除影像记录：%1").arg(m_selectedImageSeriesId));
    }

    m_selectedImageSeriesId.clear();
    refreshFromContext();
    return true;
}

void DataManagementPage::fillTreatmentTable(const QVector<TreatmentSessionRecord>& treatmentSessions)
{
    const QSignalBlocker blocker(m_treatmentTable);
    m_treatmentTable->clearContents();
    m_treatmentTable->setRowCount(treatmentSessions.size());

    int selectedRow = -1;
    for (int row = 0; row < treatmentSessions.size(); ++row) {
        const TreatmentSessionRecord& session = treatmentSessions[row];

        auto* idItem = new QTableWidgetItem(session.id);
        idItem->setData(Qt::UserRole, session.id);
        m_treatmentTable->setItem(row, 0, idItem);
        m_treatmentTable->setItem(row, 1, new QTableWidgetItem(session.planId));
        m_treatmentTable->setItem(row, 2, new QTableWidgetItem(session.lesionType));
        m_treatmentTable->setItem(row, 3, new QTableWidgetItem(session.treatmentDate.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
        m_treatmentTable->setItem(row, 4, new QTableWidgetItem(QString::number(session.totalEnergyJ, 'f', 0)));
        m_treatmentTable->setItem(row, 5, new QTableWidgetItem(QString::number(session.totalDurationSeconds, 'f', 1)));
        m_treatmentTable->setItem(row, 6, new QTableWidgetItem(session.status));

        if (session.id == m_selectedTreatmentSessionId) {
            selectedRow = row;
        }
    }

    if (selectedRow >= 0) {
        m_treatmentTable->selectRow(selectedRow);
    } else {
        m_treatmentTable->clearSelection();
    }
}

void DataManagementPage::populateTreatmentSessionEditor(const TreatmentSessionRecord* treatmentSession)
{
    if (treatmentSession == nullptr) {
        QString defaultPlanId;
        if (m_context != nullptr
            && m_context->hasSelectedPatient()
            && m_context->hasActivePlan()
            && m_context->activePlan().patientId == m_context->selectedPatient().id) {
            defaultPlanId = m_context->activePlan().id;
        }

        m_treatmentIdValue->clear();
        m_treatmentPlanIdEdit->setText(defaultPlanId);
        m_treatmentLesionTypeEdit->clear();
        m_treatmentDateEdit->setDateTime(QDateTime::currentDateTime());
        m_treatmentStatusEdit->clear();
        m_treatmentEnergySpin->setValue(0.0);
        m_treatmentDurationSpin->setValue(0.0);
        m_treatmentDoseSpin->setValue(0.0);
        m_treatmentPathSummaryEdit->clear();
        return;
    }

    m_treatmentIdValue->setText(treatmentSession->id);
    m_treatmentPlanIdEdit->setText(treatmentSession->planId);
    m_treatmentLesionTypeEdit->setText(treatmentSession->lesionType);
    m_treatmentDateEdit->setDateTime(treatmentSession->treatmentDate.isValid() ? treatmentSession->treatmentDate : QDateTime::currentDateTime());
    m_treatmentStatusEdit->setText(treatmentSession->status);
    m_treatmentEnergySpin->setValue(treatmentSession->totalEnergyJ);
    m_treatmentDurationSpin->setValue(treatmentSession->totalDurationSeconds);
    m_treatmentDoseSpin->setValue(treatmentSession->dose);
    m_treatmentPathSummaryEdit->setPlainText(treatmentSession->pathSummary);
}

const TreatmentSessionRecord* DataManagementPage::findSelectedTreatmentSession() const
{
    const auto it = std::find_if(
        m_currentTreatmentSessions.cbegin(),
        m_currentTreatmentSessions.cend(),
        [this](const TreatmentSessionRecord& session) { return session.id == m_selectedTreatmentSessionId; });
    return it == m_currentTreatmentSessions.cend() ? nullptr : &(*it);
}

void DataManagementPage::setSelectedTreatmentSession(const QString& treatmentSessionId)
{
    m_selectedTreatmentSessionId = treatmentSessionId;
    fillTreatmentTable(m_currentTreatmentSessions);
    populateTreatmentSessionEditor(findSelectedTreatmentSession());

    if (m_selectedTreatmentReportId.isEmpty()) {
        refreshReportSessionOptions(m_selectedTreatmentSessionId);
    }
    refreshDataActionState();
}

bool DataManagementPage::saveCurrentTreatmentSession()
{
    if (m_context == nullptr || !m_context->hasSelectedPatient()) {
        return false;
    }

    TreatmentSessionRecord treatmentSession =
        findSelectedTreatmentSession() != nullptr ? *findSelectedTreatmentSession() : TreatmentSessionRecord {};
    const bool creating = treatmentSession.id.trimmed().isEmpty();
    treatmentSession.patientId = m_context->selectedPatient().id;
    treatmentSession.planId = m_treatmentPlanIdEdit->text().trimmed();
    treatmentSession.lesionType = m_treatmentLesionTypeEdit->text().trimmed();
    treatmentSession.treatmentDate = m_treatmentDateEdit->dateTime();
    treatmentSession.startedAt = treatmentSession.treatmentDate;
    treatmentSession.totalEnergyJ = m_treatmentEnergySpin->value();
    treatmentSession.totalDurationSeconds = m_treatmentDurationSpin->value();
    treatmentSession.endedAt = treatmentSession.totalDurationSeconds > 0.0
        ? treatmentSession.startedAt.addSecs(static_cast<int>(treatmentSession.totalDurationSeconds))
        : QDateTime {};
    treatmentSession.dose = m_treatmentDoseSpin->value();
    treatmentSession.status = m_treatmentStatusEdit->text().trimmed();
    treatmentSession.pathSummary = m_treatmentPathSummaryEdit->toPlainText().trimmed();

    if (!m_clinicalDataService.saveTreatmentSession(&treatmentSession)) {
        QMessageBox::warning(this, QStringLiteral("保存治疗失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("treatment_session"),
            QStringLiteral("%1治疗记录：%2").arg(creating ? QStringLiteral("创建") : QStringLiteral("更新"), treatmentSession.id));
    }

    m_selectedTreatmentSessionId = treatmentSession.id;
    refreshFromContext();
    return true;
}

bool DataManagementPage::deleteCurrentTreatmentSession()
{
    if (m_selectedTreatmentSessionId.isEmpty()) {
        return false;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确认删除当前治疗记录 %1 及其关联报告吗？").arg(m_selectedTreatmentSessionId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return false;
    }

    if (!m_clinicalDataService.deleteTreatmentSession(m_selectedTreatmentSessionId)) {
        QMessageBox::warning(this, QStringLiteral("删除治疗失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("treatment_session"),
            QStringLiteral("删除治疗记录：%1").arg(m_selectedTreatmentSessionId));
    }

    m_selectedTreatmentSessionId.clear();
    m_selectedTreatmentReportId.clear();
    refreshFromContext();
    return true;
}

void DataManagementPage::fillReportList(const QVector<TreatmentReportRecord>& treatmentReports)
{
    const QSignalBlocker blocker(m_reportListWidget);
    m_reportListWidget->clear();

    int selectedRow = -1;
    for (int index = 0; index < treatmentReports.size(); ++index) {
        const TreatmentReportRecord& report = treatmentReports[index];
        auto* item = new QListWidgetItem(reportListText(report), m_reportListWidget);
        item->setData(Qt::UserRole, report.id);
        if (report.id == m_selectedTreatmentReportId) {
            selectedRow = index;
        }
    }

    if (selectedRow >= 0) {
        m_reportListWidget->setCurrentRow(selectedRow);
    } else {
        m_reportListWidget->clearSelection();
    }
}

void DataManagementPage::populateTreatmentReportEditor(const TreatmentReportRecord* treatmentReport)
{
    if (treatmentReport == nullptr) {
        m_reportIdValue->clear();
        m_reportTitleEdit->clear();
        m_reportGeneratedAtEdit->setDateTime(QDateTime::currentDateTime());
        m_reportNotesEdit->clear();
        refreshReportSessionOptions(m_selectedTreatmentSessionId);
        m_reportContentEdit->clear();
        clearReportPreview(
            m_context != nullptr && m_context->hasSelectedPatient()
                ? QStringLiteral("请选择左侧报告，或新增一份治疗报告。")
                : QStringLiteral("当前未选择患者。"));
        return;
    }

    m_reportIdValue->setText(treatmentReport->id);
    refreshReportSessionOptions(treatmentReport->treatmentSessionId);
    m_reportTitleEdit->setText(treatmentReport->title);
    m_reportGeneratedAtEdit->setDateTime(
        treatmentReport->generatedAt.isValid() ? treatmentReport->generatedAt : QDateTime::currentDateTime());
    m_reportNotesEdit->setPlainText(treatmentReport->notes);
    m_reportContentEdit->setHtml(treatmentReport->contentHtml);
    syncReportPreview();
}

const TreatmentReportRecord* DataManagementPage::findSelectedTreatmentReport() const
{
    const auto it = std::find_if(
        m_currentTreatmentReports.cbegin(),
        m_currentTreatmentReports.cend(),
        [this](const TreatmentReportRecord& report) { return report.id == m_selectedTreatmentReportId; });
    return it == m_currentTreatmentReports.cend() ? nullptr : &(*it);
}

void DataManagementPage::setSelectedTreatmentReport(const QString& treatmentReportId)
{
    m_selectedTreatmentReportId = treatmentReportId;
    fillReportList(m_currentTreatmentReports);
    populateTreatmentReportEditor(findSelectedTreatmentReport());
    refreshDataActionState();
}

void DataManagementPage::refreshReportSessionOptions(const QString& preferredTreatmentSessionId)
{
    QString targetSessionId = preferredTreatmentSessionId;
    if (targetSessionId.isEmpty() && m_reportSessionCombo != nullptr) {
        targetSessionId = m_reportSessionCombo->currentData().toString();
    }
    if (targetSessionId.isEmpty()) {
        targetSessionId = m_selectedTreatmentSessionId;
    }

    const QSignalBlocker blocker(m_reportSessionCombo);
    m_reportSessionCombo->clear();

    for (const TreatmentSessionRecord& treatmentSession : m_currentTreatmentSessions) {
        const QString label = QStringLiteral("%1 | %2 | %3")
                                  .arg(treatmentSession.id)
                                  .arg(treatmentSession.planId)
                                  .arg(treatmentSession.status);
        m_reportSessionCombo->addItem(label, treatmentSession.id);
    }

    if (m_reportSessionCombo->count() == 0) {
        return;
    }

    int targetIndex = m_reportSessionCombo->findData(targetSessionId);
    if (targetIndex < 0) {
        targetIndex = 0;
    }
    m_reportSessionCombo->setCurrentIndex(targetIndex);
}

bool DataManagementPage::saveCurrentTreatmentReport()
{
    if (m_context == nullptr || !m_context->hasSelectedPatient()) {
        return false;
    }

    TreatmentReportRecord treatmentReport =
        findSelectedTreatmentReport() != nullptr ? *findSelectedTreatmentReport() : TreatmentReportRecord {};
    const bool creating = treatmentReport.id.trimmed().isEmpty();
    treatmentReport.patientId = m_context->selectedPatient().id;
    treatmentReport.treatmentSessionId = m_reportSessionCombo->currentData().toString();
    treatmentReport.generatedAt = m_reportGeneratedAtEdit->dateTime();
    treatmentReport.title = m_reportTitleEdit->text().trimmed();
    treatmentReport.notes = m_reportNotesEdit->toPlainText().trimmed();
    treatmentReport.contentHtml = m_reportContentEdit->toPlainText().trimmed().isEmpty() ? QString() : m_reportContentEdit->toHtml();

    if (!m_clinicalDataService.saveTreatmentReport(&treatmentReport)) {
        QMessageBox::warning(this, QStringLiteral("保存报告失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("treatment_report"),
            QStringLiteral("%1治疗报告：%2").arg(creating ? QStringLiteral("创建") : QStringLiteral("更新"), treatmentReport.id));
    }

    m_selectedTreatmentReportId = treatmentReport.id;
    refreshFromContext();
    return true;
}

bool DataManagementPage::deleteCurrentTreatmentReport()
{
    if (m_selectedTreatmentReportId.isEmpty()) {
        return false;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("确认删除"),
        QStringLiteral("确认删除当前治疗报告 %1 吗？").arg(m_selectedTreatmentReportId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return false;
    }

    if (!m_clinicalDataService.deleteTreatmentReport(m_selectedTreatmentReportId)) {
        QMessageBox::warning(this, QStringLiteral("删除报告失败"), m_clinicalDataService.lastError());
        return false;
    }

    if (m_auditService != nullptr) {
        m_auditService->appendEntry(
            QStringLiteral("system"),
            QStringLiteral("treatment_report"),
            QStringLiteral("删除治疗报告：%1").arg(m_selectedTreatmentReportId));
    }

    m_selectedTreatmentReportId.clear();
    refreshFromContext();
    return true;
}

void DataManagementPage::refreshImagePreview(const QString& storagePath)
{
    if (m_imageSeriesPreviewLabel == nullptr) {
        return;
    }

    m_imageSeriesPreviewLabel->setPixmap(loadPreviewPixmap(storagePath, m_imageSeriesPreviewLabel->size()));
    m_imageSeriesPreviewLabel->setToolTip(storagePath.trimmed().isEmpty() ? QStringLiteral("暂无影像预览") : storagePath);
}

QPixmap DataManagementPage::loadPreviewPixmap(const QString& storagePath, const QSize& targetSize) const
{
    QPixmap preview;
    const QString previewPath = previewPathForStorage(storagePath);
    if (!previewPath.isEmpty()) {
        QPixmap loaded(previewPath);
        if (!loaded.isNull()) {
            preview = loaded.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }
    }

    if (!preview.isNull()) {
        return preview;
    }

    preview = QPixmap(targetSize);
    preview.fill(QColor(QStringLiteral("#f7f7f5")));
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#ccd2d9")), 1));
    painter.drawRect(preview.rect().adjusted(0, 0, -1, -1));
    painter.drawLine(0, 0, preview.width() - 1, preview.height() - 1);
    painter.drawLine(preview.width() - 1, 0, 0, preview.height() - 1);
    painter.setPen(QColor(QStringLiteral("#6d7f92")));
    painter.drawText(preview.rect(), Qt::AlignCenter, QStringLiteral("暂无预览"));
    return preview;
}

void DataManagementPage::clearReportPreview(const QString& message)
{
    if (m_reportPreview == nullptr) {
        return;
    }

    m_reportPreview->setHtml(
        QStringLiteral("<h2>治疗报告预览</h2><p>%1</p>").arg(wrapValue(message)));
}

bool DataManagementPage::hasWritableRepository() const
{
    return m_clinicalDataRepository != nullptr && m_clinicalDataRepository->supportsWriteOperations();
}

}  // namespace panthera::modules
