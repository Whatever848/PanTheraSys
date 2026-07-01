#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

namespace panthera::modules {

class UsbCameraFrameSource final : public QObject {
    Q_OBJECT

public:
    explicit UsbCameraFrameSource(QObject* parent = nullptr);
    ~UsbCameraFrameSource() override;

    bool start(const QString& preferredDescription = QStringLiteral("USB3 PLUS Video"));
    void stop();
    bool isActive() const;
    QString activeCameraDescription() const;

signals:
    void frameAvailable(const QImage& image);
    void statusChanged(const QString& status);
    void errorOccurred(const QString& error);

private:
    struct DirectShowState;

    bool initializeDirectShow(const QString& preferredDescription, QString* errorMessage);
    void releaseDirectShow();
    void pollFrame();

    DirectShowState* m_state {nullptr};
    QTimer m_frameTimer;
    QString m_preferredDescription {QStringLiteral("USB3 PLUS Video")};
    QString m_activeCameraDescription;
    bool m_announcedFirstFrame {false};
};

}  // namespace panthera::modules
