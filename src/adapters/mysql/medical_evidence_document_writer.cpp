#include "adapters/mysql/medical_evidence_document_writer.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>

#include <zlib.h>

#include <utility>

namespace panthera::adapters {

namespace {

struct ZipEntry {
    QString name;
    QByteArray data;
    quint32 crc {0};
    quint32 offset {0};
};

void writeUInt16(QIODevice* device, quint16 value)
{
    const char bytes[] {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    device->write(bytes, sizeof(bytes));
}

void writeUInt32(QIODevice* device, quint32 value)
{
    const char bytes[] {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    device->write(bytes, sizeof(bytes));
}

QString xmlEscaped(const QString& value)
{
    QString escaped = value.toHtmlEscaped();
    escaped.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    return escaped;
}

QByteArray createDocumentXml(const QString& title, const QStringList& lines, const QDateTime& generatedAt)
{
    QStringList paragraphs;
    paragraphs << QStringLiteral(
        "<w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr><w:r><w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr><w:t>%1</w:t></w:r></w:p>")
                      .arg(xmlEscaped(title));
    paragraphs << QStringLiteral("<w:p><w:r><w:t>%1</w:t></w:r></w:p>")
                      .arg(xmlEscaped(QStringLiteral("生成时间：%1").arg(generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))));
    for (const QString& line : lines) {
        paragraphs << QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">%1</w:t></w:r></w:p>")
                          .arg(xmlEscaped(line));
    }
    paragraphs << QStringLiteral("<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr>");

    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
               "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
               "<w:body>%1</w:body></w:document>")
        .arg(paragraphs.join(QString()))
        .toUtf8();
}

bool writeZip(QSaveFile* output, QList<ZipEntry>* entries, QString* errorMessage)
{
    for (ZipEntry& entry : *entries) {
        const QByteArray name = entry.name.toUtf8();
        entry.offset = static_cast<quint32>(output->pos());
        entry.crc = static_cast<quint32>(crc32(0L, reinterpret_cast<const Bytef*>(entry.data.constData()), static_cast<uInt>(entry.data.size())));

        writeUInt32(output, 0x04034b50U);
        writeUInt16(output, 20);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt32(output, entry.crc);
        writeUInt32(output, static_cast<quint32>(entry.data.size()));
        writeUInt32(output, static_cast<quint32>(entry.data.size()));
        writeUInt16(output, static_cast<quint16>(name.size()));
        writeUInt16(output, 0);
        output->write(name);
        output->write(entry.data);
    }

    const quint32 centralDirectoryOffset = static_cast<quint32>(output->pos());
    for (const ZipEntry& entry : *entries) {
        const QByteArray name = entry.name.toUtf8();
        writeUInt32(output, 0x02014b50U);
        writeUInt16(output, 20);
        writeUInt16(output, 20);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt32(output, entry.crc);
        writeUInt32(output, static_cast<quint32>(entry.data.size()));
        writeUInt32(output, static_cast<quint32>(entry.data.size()));
        writeUInt16(output, static_cast<quint16>(name.size()));
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt16(output, 0);
        writeUInt32(output, 0);
        writeUInt32(output, entry.offset);
        output->write(name);
    }

    const quint32 centralDirectorySize = static_cast<quint32>(output->pos()) - centralDirectoryOffset;
    writeUInt32(output, 0x06054b50U);
    writeUInt16(output, 0);
    writeUInt16(output, 0);
    writeUInt16(output, static_cast<quint16>(entries->size()));
    writeUInt16(output, static_cast<quint16>(entries->size()));
    writeUInt32(output, centralDirectorySize);
    writeUInt32(output, centralDirectoryOffset);
    writeUInt16(output, 0);

    if (output->error() != QFileDevice::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = output->errorString();
        }
        return false;
    }
    return true;
}

}  // namespace

bool MedicalEvidenceDocumentWriter::writeTreatmentEvidenceDocument(
    const QString& outputPath,
    const QString& title,
    const QStringList& lines,
    const QDateTime& generatedAt,
    QString* errorMessage)
{
    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to create evidence document directory: %1").arg(outputInfo.absolutePath());
        }
        return false;
    }

    QList<ZipEntry> entries {
        {
            QStringLiteral("[Content_Types].xml"),
            QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>"),
            0,
            0
        },
        {
            QStringLiteral("_rels/.rels"),
            QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>"),
            0,
            0
        },
        {QStringLiteral("word/document.xml"), createDocumentXml(title, lines, generatedAt), 0, 0}
    };

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = output.errorString();
        }
        return false;
    }
    if (!writeZip(&output, &entries, errorMessage)) {
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = output.errorString();
        }
        return false;
    }
    return true;
}

}  // namespace panthera::adapters
