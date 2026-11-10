#pragma once

#include <QDialog>

#include "core/domain/system_types.h"

class QFrame;
class QLabel;
class QPushButton;

namespace panthera::modules {

class DobotAlarmDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DobotAlarmDialog(QWidget* parent = nullptr);

    void setAlarmEvent(const panthera::core::AlarmEvent& event);
    void showOverParent();
    void resolveAndClose(const QString& message);
    void setResetInProgress(bool inProgress, const QString& message = {});

signals:
    void resetRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void reject() override;

private:
    void updateResetButton();

    panthera::core::AlarmEvent m_event;
    QFrame* m_card {nullptr};
    QLabel* m_iconLabel {nullptr};
    QLabel* m_titleLabel {nullptr};
    QLabel* m_promptLabel {nullptr};
    QLabel* m_resultLabel {nullptr};
    QPushButton* m_resetButton {nullptr};
    bool m_resetInProgress {false};
    bool m_allowClose {false};
};

}  // namespace panthera::modules

