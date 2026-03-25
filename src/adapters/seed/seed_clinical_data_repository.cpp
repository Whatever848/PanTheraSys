#include "adapters/seed/seed_clinical_data_repository.h"

#include <algorithm>

namespace panthera::adapters {

using namespace panthera::core;

namespace {

QDateTime at(int year, int month, int day, int hour, int minute)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute));
}

template <typename T>
bool containsId(const QVector<T>& items, const QString& id)
{
    return std::any_of(items.cbegin(), items.cend(), [&id](const T& item) {
        return item.id == id;
    });
}

}  // namespace

SeedClinicalDataRepository::SeedClinicalDataRepository()
    : m_seedBundles {buildFirstPatientBundle(), buildSecondPatientBundle()}
{
}

QString SeedClinicalDataRepository::repositoryName() const
{
    return QStringLiteral("seed");
}

bool SeedClinicalDataRepository::supportsWriteOperations() const
{
    return true;
}

QString SeedClinicalDataRepository::lastError() const
{
    return m_lastError;
}

QVector<PatientRecord> SeedClinicalDataRepository::listPatients() const
{
    QVector<PatientRecord> patients;
    patients.reserve(m_seedBundles.size());
    for (const SeedPatientBundle& bundle : m_seedBundles) {
        if (bundle.patient.deletedAt.isValid()) {
            continue;
        }
        patients.push_back(bundle.patient);
    }
    setLastError(QString());
    return patients;
}

bool SeedClinicalDataRepository::findPatientById(const QString& patientId, PatientRecord* patient) const
{
    const SeedPatientBundle* bundle = findBundleByPatientId(patientId);
    if (bundle == nullptr || bundle->patient.deletedAt.isValid()) {
        setLastError(QStringLiteral("Patient not found: %1").arg(patientId));
        return false;
    }

    if (patient != nullptr) {
        *patient = bundle->patient;
    }

    setLastError(QString());
    return true;
}

bool SeedClinicalDataRepository::createPatient(const PatientRecord& patient)
{
    if (patient.id.isEmpty()) {
        setLastError(QStringLiteral("Patient id is required."));
        return false;
    }
    if (findBundleByPatientId(patient.id) != nullptr) {
        setLastError(QStringLiteral("Patient already exists: %1").arg(patient.id));
        return false;
    }

    SeedPatientBundle bundle;
    bundle.patient = patient;
    m_seedBundles.push_back(bundle);
    setLastError(QString());
    return true;
}

bool SeedClinicalDataRepository::updatePatient(const PatientRecord& patient)
{
    SeedPatientBundle* bundle = findBundleByPatientId(patient.id);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found: %1").arg(patient.id));
        return false;
    }

    bundle->patient = patient;
    setLastError(QString());
    return true;
}

bool SeedClinicalDataRepository::archivePatient(const QString& patientId, const QDateTime& archivedAt)
{
    SeedPatientBundle* bundle = findBundleByPatientId(patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found: %1").arg(patientId));
        return false;
    }

    bundle->patient.deletedAt = archivedAt;
    bundle->patient.updatedAt = archivedAt;
    setLastError(QString());
    return true;
}

QVector<ImageSeriesRecord> SeedClinicalDataRepository::listImageSeriesForPatient(const QString& patientId) const
{
    const SeedPatientBundle* bundle = findBundleByPatientId(patientId);
    if (bundle == nullptr || bundle->patient.deletedAt.isValid()) {
        setLastError(QString());
        return {};
    }
    setLastError(QString());
    return bundle->imageSeries;
}

bool SeedClinicalDataRepository::findImageSeriesById(const QString& imageSeriesId, ImageSeriesRecord* imageSeries) const
{
    for (const SeedPatientBundle& bundle : m_seedBundles) {
        if (bundle.patient.deletedAt.isValid()) {
            continue;
        }
        const auto it = std::find_if(bundle.imageSeries.cbegin(), bundle.imageSeries.cend(), [&imageSeriesId](const ImageSeriesRecord& record) {
            return record.id == imageSeriesId;
        });
        if (it == bundle.imageSeries.cend()) {
            continue;
        }

        if (imageSeries != nullptr) {
            *imageSeries = *it;
        }
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Image series not found: %1").arg(imageSeriesId));
    return false;
}

bool SeedClinicalDataRepository::createImageSeries(const ImageSeriesRecord& imageSeries)
{
    SeedPatientBundle* bundle = findBundleByPatientId(imageSeries.patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found for image series: %1").arg(imageSeries.patientId));
        return false;
    }
    if (containsId(bundle->imageSeries, imageSeries.id)) {
        setLastError(QStringLiteral("Image series already exists: %1").arg(imageSeries.id));
        return false;
    }

    bundle->imageSeries.push_back(imageSeries);
    setLastError(QString());
    return true;
}

bool SeedClinicalDataRepository::updateImageSeries(const ImageSeriesRecord& imageSeries)
{
    SeedPatientBundle* bundle = findBundleByPatientId(imageSeries.patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found for image series: %1").arg(imageSeries.patientId));
        return false;
    }

    for (ImageSeriesRecord& current : bundle->imageSeries) {
        if (current.id != imageSeries.id) {
            continue;
        }

        current = imageSeries;
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Image series not found: %1").arg(imageSeries.id));
    return false;
}

bool SeedClinicalDataRepository::deleteImageSeries(const QString& imageSeriesId)
{
    for (SeedPatientBundle& bundle : m_seedBundles) {
        auto it = std::find_if(bundle.imageSeries.begin(), bundle.imageSeries.end(), [&imageSeriesId](const ImageSeriesRecord& record) {
            return record.id == imageSeriesId;
        });
        if (it == bundle.imageSeries.end()) {
            continue;
        }

        bundle.imageSeries.erase(it);
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Image series not found: %1").arg(imageSeriesId));
    return false;
}

QVector<TreatmentSessionRecord> SeedClinicalDataRepository::listTreatmentSessionsForPatient(const QString& patientId) const
{
    const SeedPatientBundle* bundle = findBundleByPatientId(patientId);
    if (bundle == nullptr || bundle->patient.deletedAt.isValid()) {
        setLastError(QString());
        return {};
    }
    setLastError(QString());
    return bundle->treatmentSessions;
}

bool SeedClinicalDataRepository::findTreatmentSessionById(const QString& treatmentSessionId, TreatmentSessionRecord* treatmentSession) const
{
    for (const SeedPatientBundle& bundle : m_seedBundles) {
        if (bundle.patient.deletedAt.isValid()) {
            continue;
        }
        const auto it = std::find_if(
            bundle.treatmentSessions.cbegin(),
            bundle.treatmentSessions.cend(),
            [&treatmentSessionId](const TreatmentSessionRecord& record) {
                return record.id == treatmentSessionId;
            });
        if (it == bundle.treatmentSessions.cend()) {
            continue;
        }

        if (treatmentSession != nullptr) {
            *treatmentSession = *it;
        }
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Treatment session not found: %1").arg(treatmentSessionId));
    return false;
}

bool SeedClinicalDataRepository::createTreatmentSession(const TreatmentSessionRecord& treatmentSession)
{
    SeedPatientBundle* bundle = findBundleByPatientId(treatmentSession.patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found for treatment session: %1").arg(treatmentSession.patientId));
        return false;
    }
    if (containsId(bundle->treatmentSessions, treatmentSession.id)) {
        setLastError(QStringLiteral("Treatment session already exists: %1").arg(treatmentSession.id));
        return false;
    }

    bundle->treatmentSessions.push_back(treatmentSession);
    setLastError(QString());
    return true;
}

bool SeedClinicalDataRepository::updateTreatmentSession(const TreatmentSessionRecord& treatmentSession)
{
    SeedPatientBundle* bundle = findBundleByPatientId(treatmentSession.patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found for treatment session: %1").arg(treatmentSession.patientId));
        return false;
    }

    for (TreatmentSessionRecord& current : bundle->treatmentSessions) {
        if (current.id != treatmentSession.id) {
            continue;
        }

        current = treatmentSession;
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Treatment session not found: %1").arg(treatmentSession.id));
    return false;
}

bool SeedClinicalDataRepository::deleteTreatmentSession(const QString& treatmentSessionId)
{
    for (SeedPatientBundle& bundle : m_seedBundles) {
        auto it = std::find_if(bundle.treatmentSessions.begin(), bundle.treatmentSessions.end(), [&treatmentSessionId](const TreatmentSessionRecord& record) {
            return record.id == treatmentSessionId;
        });
        if (it == bundle.treatmentSessions.end()) {
            continue;
        }

        bundle.treatmentSessions.erase(it);
        bundle.treatmentReports.erase(
            std::remove_if(
                bundle.treatmentReports.begin(),
                bundle.treatmentReports.end(),
                [&treatmentSessionId](const TreatmentReportRecord& record) {
                    return record.treatmentSessionId == treatmentSessionId;
                }),
            bundle.treatmentReports.end());
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Treatment session not found: %1").arg(treatmentSessionId));
    return false;
}

QVector<TreatmentReportRecord> SeedClinicalDataRepository::listTreatmentReportsForPatient(const QString& patientId) const
{
    const SeedPatientBundle* bundle = findBundleByPatientId(patientId);
    if (bundle == nullptr || bundle->patient.deletedAt.isValid()) {
        setLastError(QString());
        return {};
    }
    setLastError(QString());
    return bundle->treatmentReports;
}

bool SeedClinicalDataRepository::findTreatmentReportById(const QString& treatmentReportId, TreatmentReportRecord* treatmentReport) const
{
    for (const SeedPatientBundle& bundle : m_seedBundles) {
        if (bundle.patient.deletedAt.isValid()) {
            continue;
        }
        const auto it = std::find_if(
            bundle.treatmentReports.cbegin(),
            bundle.treatmentReports.cend(),
            [&treatmentReportId](const TreatmentReportRecord& record) {
                return record.id == treatmentReportId;
            });
        if (it == bundle.treatmentReports.cend()) {
            continue;
        }

        if (treatmentReport != nullptr) {
            *treatmentReport = *it;
        }
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Treatment report not found: %1").arg(treatmentReportId));
    return false;
}

bool SeedClinicalDataRepository::createTreatmentReport(const TreatmentReportRecord& treatmentReport)
{
    SeedPatientBundle* bundle = findBundleByPatientId(treatmentReport.patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found for treatment report: %1").arg(treatmentReport.patientId));
        return false;
    }
    if (containsId(bundle->treatmentReports, treatmentReport.id)) {
        setLastError(QStringLiteral("Treatment report already exists: %1").arg(treatmentReport.id));
        return false;
    }

    bundle->treatmentReports.push_back(treatmentReport);
    setLastError(QString());
    return true;
}

bool SeedClinicalDataRepository::updateTreatmentReport(const TreatmentReportRecord& treatmentReport)
{
    SeedPatientBundle* bundle = findBundleByPatientId(treatmentReport.patientId);
    if (bundle == nullptr) {
        setLastError(QStringLiteral("Patient not found for treatment report: %1").arg(treatmentReport.patientId));
        return false;
    }

    for (TreatmentReportRecord& current : bundle->treatmentReports) {
        if (current.id != treatmentReport.id) {
            continue;
        }

        current = treatmentReport;
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Treatment report not found: %1").arg(treatmentReport.id));
    return false;
}

bool SeedClinicalDataRepository::deleteTreatmentReport(const QString& treatmentReportId)
{
    for (SeedPatientBundle& bundle : m_seedBundles) {
        auto it = std::find_if(bundle.treatmentReports.begin(), bundle.treatmentReports.end(), [&treatmentReportId](const TreatmentReportRecord& record) {
            return record.id == treatmentReportId;
        });
        if (it == bundle.treatmentReports.end()) {
            continue;
        }

        bundle.treatmentReports.erase(it);
        setLastError(QString());
        return true;
    }

    setLastError(QStringLiteral("Treatment report not found: %1").arg(treatmentReportId));
    return false;
}

SeedClinicalDataRepository::SeedPatientBundle* SeedClinicalDataRepository::findBundleByPatientId(const QString& patientId)
{
    auto it = std::find_if(m_seedBundles.begin(), m_seedBundles.end(), [&patientId](const SeedPatientBundle& bundle) {
        return bundle.patient.id == patientId;
    });
    return it == m_seedBundles.end() ? nullptr : &(*it);
}

const SeedClinicalDataRepository::SeedPatientBundle* SeedClinicalDataRepository::findBundleByPatientId(const QString& patientId) const
{
    const auto it = std::find_if(m_seedBundles.cbegin(), m_seedBundles.cend(), [&patientId](const SeedPatientBundle& bundle) {
        return bundle.patient.id == patientId;
    });
    return it == m_seedBundles.cend() ? nullptr : &(*it);
}

void SeedClinicalDataRepository::setLastError(const QString& error) const
{
    m_lastError = error;
}

SeedClinicalDataRepository::SeedPatientBundle SeedClinicalDataRepository::buildFirstPatientBundle() const
{
    SeedPatientBundle bundle;
    bundle.patient = PatientRecord {
        QStringLiteral("P2026001"),
        QStringLiteral("张三"),
        24,
        QStringLiteral("女"),
        QStringLiteral("右乳浸润性导管癌 II 期"),
        QStringLiteral("13800000001"),
        at(2026, 1, 1, 8, 30),
        at(2026, 1, 2, 10, 5),
        {}
    };

    bundle.imageSeries = {
        ImageSeriesRecord {
            QStringLiteral("IMG-2026001-US-01"),
            bundle.patient.id,
            QStringLiteral("超声图像"),
            QStringLiteral("runtime/images/P2026001/series-01"),
            QDate(2026, 1, 2),
            QStringLiteral("治疗前病灶定位采集"),
            at(2026, 1, 2, 8, 55)
        },
        ImageSeriesRecord {
            QStringLiteral("IMG-2026001-3D-01"),
            bundle.patient.id,
            QStringLiteral("三维重建"),
            QStringLiteral("runtime/models/P2026001/reconstruction-01"),
            QDate(2026, 1, 2),
            QStringLiteral("病灶轮廓与治疗路径评估"),
            at(2026, 1, 2, 9, 0)
        }
    };

    bundle.treatmentSessions = {
        TreatmentSessionRecord {
            QStringLiteral("TX-800"),
            bundle.patient.id,
            QStringLiteral("PLAN-20260102091500"),
            QStringLiteral("乳腺癌"),
            QStringLiteral("point[15], dose[380]"),
            at(2026, 1, 2, 9, 15),
            at(2026, 1, 2, 9, 15),
            at(2026, 1, 2, 9, 20),
            12500.0,
            300.0,
            380.0,
            QStringLiteral("已完成"),
            at(2026, 1, 2, 9, 14)
        }
    };

    bundle.treatmentReports = {
        TreatmentReportRecord {
            QStringLiteral("REP-2026001-01"),
            bundle.patient.id,
            bundle.treatmentSessions.first().id,
            at(2026, 1, 2, 10, 5),
            QStringLiteral("治疗报告"),
            QStringLiteral(
                "<h2>治疗报告</h2>"
                "<p><b>患者ID：</b>P2026001</p>"
                "<p><b>患者姓名：</b>张三</p>"
                "<p><b>年龄：</b>24</p>"
                "<p><b>诊断结果：</b>右乳浸润性导管癌 II 期，伴右侧腋窝淋巴结可疑转移。</p>"
                "<p><b>治疗摘要：</b>采用点治疗与分段执行联合方案，累计能量 12500 J，总时长 300 s。</p>"
                "<p><b>结论：</b>已完成本次模拟治疗流程，建议结合影像随访复核治疗效果。</p>"),
            QStringLiteral("首次模拟治疗报告")
        }
    };

    return bundle;
}

SeedClinicalDataRepository::SeedPatientBundle SeedClinicalDataRepository::buildSecondPatientBundle() const
{
    SeedPatientBundle bundle;
    bundle.patient = PatientRecord {
        QStringLiteral("P2026002"),
        QStringLiteral("李四"),
        57,
        QStringLiteral("女"),
        QStringLiteral("左乳浸润性导管癌 I 期"),
        QStringLiteral("13800000002"),
        at(2026, 1, 3, 13, 20),
        at(2026, 1, 4, 9, 10),
        {}
    };

    bundle.imageSeries = {
        ImageSeriesRecord {
            QStringLiteral("IMG-2026002-US-01"),
            bundle.patient.id,
            QStringLiteral("超声图像"),
            QStringLiteral("runtime/images/P2026002/series-01"),
            QDate(2026, 1, 4),
            QStringLiteral("术前影像分层采集"),
            at(2026, 1, 4, 8, 15)
        }
    };

    bundle.treatmentSessions = {
        TreatmentSessionRecord {
            QStringLiteral("TX-801"),
            bundle.patient.id,
            QStringLiteral("PLAN-20260104083000"),
            QStringLiteral("乳腺癌"),
            QStringLiteral("line[8], dose[260]"),
            at(2026, 1, 4, 8, 30),
            at(2026, 1, 4, 8, 30),
            at(2026, 1, 4, 8, 34),
            7800.0,
            240.0,
            260.0,
            QStringLiteral("待复核"),
            at(2026, 1, 4, 8, 28)
        }
    };

    bundle.treatmentReports = {
        TreatmentReportRecord {
            QStringLiteral("REP-2026002-01"),
            bundle.patient.id,
            bundle.treatmentSessions.first().id,
            at(2026, 1, 4, 9, 10),
            QStringLiteral("治疗报告"),
            QStringLiteral(
                "<h2>治疗报告</h2>"
                "<p><b>患者ID：</b>P2026002</p>"
                "<p><b>患者姓名：</b>李四</p>"
                "<p><b>年龄：</b>57</p>"
                "<p><b>诊断结果：</b>左乳浸润性导管癌 I 期。</p>"
                "<p><b>治疗摘要：</b>已完成首轮线治疗模拟，累计能量 7800 J，总时长 240 s。</p>"
                "<p><b>结论：</b>建议在治疗评估模块完成三维重建和疗效复核后归档。</p>"),
            QStringLiteral("线治疗阶段性报告")
        }
    };

    return bundle;
}

}  // namespace panthera::adapters
