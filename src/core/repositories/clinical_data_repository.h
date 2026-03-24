#pragma once

#include <QString>
#include <QVector>

#include "core/domain/system_types.h"

namespace panthera::core {

// IClinicalDataRepository 瀹氫箟娌荤枟宸ヤ綔绔欏鎮ｈ€呮。妗堛€佸奖鍍忋€佹不鐤楄褰曞拰鎶ュ憡鐨勮鍙栧悎鍚屻€?
// 绗竴闃舵鍙互鐢辩ず渚嬫暟鎹€佹湰鍦版枃浠舵垨 MySQL 瀹炵幇锛屼笂灞傞〉闈笉鐩存帴渚濊禆鍏蜂綋瀛樺偍鏂瑰紡銆?
class IClinicalDataRepository {
public:
    virtual ~IClinicalDataRepository() = default;

    virtual QString repositoryName() const = 0;
    virtual bool supportsWriteOperations() const = 0;
    virtual QString lastError() const = 0;

    virtual QVector<PatientRecord> listPatients() const = 0;
    virtual bool findPatientById(const QString& patientId, PatientRecord* patient) const = 0;
    virtual bool createPatient(const PatientRecord& patient) = 0;
    virtual bool updatePatient(const PatientRecord& patient) = 0;
    virtual bool archivePatient(const QString& patientId, const QDateTime& archivedAt) = 0;

    virtual QVector<ImageSeriesRecord> listImageSeriesForPatient(const QString& patientId) const = 0;
    virtual bool findImageSeriesById(const QString& imageSeriesId, ImageSeriesRecord* imageSeries) const = 0;
    virtual bool createImageSeries(const ImageSeriesRecord& imageSeries) = 0;
    virtual bool updateImageSeries(const ImageSeriesRecord& imageSeries) = 0;
    virtual bool deleteImageSeries(const QString& imageSeriesId) = 0;

    virtual QVector<TreatmentSessionRecord> listTreatmentSessionsForPatient(const QString& patientId) const = 0;
    virtual bool findTreatmentSessionById(const QString& treatmentSessionId, TreatmentSessionRecord* treatmentSession) const = 0;
    virtual bool createTreatmentSession(const TreatmentSessionRecord& treatmentSession) = 0;
    virtual bool updateTreatmentSession(const TreatmentSessionRecord& treatmentSession) = 0;
    virtual bool deleteTreatmentSession(const QString& treatmentSessionId) = 0;

    virtual QVector<TreatmentReportRecord> listTreatmentReportsForPatient(const QString& patientId) const = 0;
    virtual bool findTreatmentReportById(const QString& treatmentReportId, TreatmentReportRecord* treatmentReport) const = 0;
    virtual bool createTreatmentReport(const TreatmentReportRecord& treatmentReport) = 0;
    virtual bool updateTreatmentReport(const TreatmentReportRecord& treatmentReport) = 0;
    virtual bool deleteTreatmentReport(const QString& treatmentReportId) = 0;
};

}  // namespace panthera::core
