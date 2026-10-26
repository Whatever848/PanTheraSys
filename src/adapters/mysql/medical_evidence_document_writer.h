#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace panthera::adapters {

class MedicalEvidenceDocumentWriter final {
public:
    static bool writeTreatmentEvidenceDocument(
        const QString& outputPath,
        const QString& title,
        const QStringList& lines,
        const QDateTime& generatedAt,
        QString* errorMessage = nullptr);
};

}  // namespace panthera::adapters
