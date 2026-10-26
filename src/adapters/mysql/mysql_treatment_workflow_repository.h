#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/domain/system_types.h"

namespace panthera::adapters {

struct TreatmentImageAnnotationSnapshot {
    QString capturedImageId;
    QString annotationId;
    QString patientId;
    QString planId;
    QString treatmentSessionId;
    QString userId;
    int layerIndex {0};
    int sliceIndex {0};
    QString capturePath;
    QString annotatedImagePath;
    QString annotationPayload;
    double targetAreaMm2 {0.0};
    double targetVolumeMm3 {0.0};
    QDateTime capturedAt;
    QString savedReason;
};

struct TreatmentTerminationEvidence {
    QString treatmentSessionId;
    QString patientId;
    QString planId;
    QString userId;
    QString doctorId;
    QDateTime startedAt;
    QDateTime endedAt;
    QString terminationType;
    QString finalStatus;
    bool continueTreatmentSelected {false};
    QString reason;
    QString summaryPayload;
    QString documentPath;
    QString documentChecksum;
};

struct ClinicalOperationSnapshot {
    QDateTime occurredAt;
    QString actor;
    QString category;
    QString details;
};

class MySqlTreatmentWorkflowRepository final {
public:
    explicit MySqlTreatmentWorkflowRepository(QString connectionName = QStringLiteral("PanTheraClinicalData"));

    bool saveImageAnnotation(const TreatmentImageAnnotationSnapshot& snapshot, QString* errorMessage = nullptr) const;
    QVector<TreatmentImageAnnotationSnapshot> loadPlanAnnotations(
        const QString& patientId,
        const QString& planId,
        QString* errorMessage = nullptr) const;
    bool recordContinuationImports(
        const QString& previousSessionId,
        const QStringList& annotationIds,
        const QString& userId,
        QString* errorMessage = nullptr) const;
    bool attachPlanAnnotationsToTreatmentSession(
        const QString& planId,
        const QString& treatmentSessionId,
        QString* errorMessage = nullptr) const;
    bool recordTreatmentPointRecords(
        const QString& treatmentSessionId,
        const QVector<int>& completedPointCountsBySegment,
        const QVector<double>& deliveredEnergyJBySegment,
        double totalDose,
        const QDateTime& startedAt,
        const QDateTime& endedAt,
        QString* errorMessage = nullptr) const;
    bool recordTreatmentDeviceSnapshot(
        const panthera::core::DeviceSnapshot& snapshot,
        const QString& treatmentSessionId,
        const QString& userId,
        QString* errorMessage = nullptr) const;
    bool recordClinicalOperationLogs(
        const QVector<ClinicalOperationSnapshot>& operations,
        const QString& patientId,
        const QString& treatmentSessionId,
        const QString& userId,
        const QString& doctorId,
        QString* errorMessage = nullptr) const;
    bool recordTerminationEvidence(const TreatmentTerminationEvidence& evidence, QString* errorMessage = nullptr) const;
    bool registerRecoveredEvidenceDocument(
        const QString& treatmentSessionId,
        const QString& patientId,
        const QString& documentPath,
        const QDateTime& generatedAt,
        QString* errorMessage = nullptr) const;
    bool findLatestEvidenceDocumentPath(const QString& treatmentSessionId, QString* documentPath, QString* errorMessage = nullptr) const;

private:
    QString m_connectionName;
};

}  // namespace panthera::adapters
