#include "modules/shared/dobot_alarm_dialog.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPoint>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

namespace panthera::modules {

DobotAlarmDialog::DobotAlarmDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("dobotAlarmDialog"));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto* overlayLayout = new QVBoxLayout(this);
    overlayLayout->setContentsMargins(24, 24, 24, 24);
    overlayLayout->addStretch(1);

    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("dobotAlarmCard"));
    m_card->setProperty("collision", true);
    m_card->setMinimumWidth(520);
    m_card->setMaximumWidth(640);
    m_card->setStyleSheet(QStringLiteral(
        "QFrame#dobotAlarmCard { background: #0b1a2b; border: 2px solid #e9ad2f; border-radius: 14px; }"
        "QFrame#dobotAlarmCard[collision=\"false\"] { border-color: #df4338; }"
        "QLabel { color: #eaf4ff; background: transparent; }"
        "QLabel#dobotAlarmTitle { font-size: 23px; font-weight: 800; color: #f2f7ff; }"
        "QLabel#dobotAlarmPrompt { color: #f6e3ad; background: #192d3e; border: 1px solid #b98625; border-radius: 9px; padding: 16px; font-size: 16px; font-weight: 700; }"
        "QLabel#dobotAlarmPrompt[collision=\"false\"] { color: #ffd1cc; background: #2d1d28; border-color: #b23a35; }"
        "QLabel#dobotAlarmResult { color: #bfe8ff; background: #102b46; border: 1px solid #21547b; border-radius: 8px; padding: 10px 12px; }"
        "QLabel#dobotAlarmResult[resetError=\"true\"] { color: #ffb5ad; background: rgba(116, 18, 14, 0.30); border-color: #df4338; }"
        "QPushButton#dobotAlarmResetButton { min-width: 128px; color: #ffffff; background: #0085ff; border: 1px solid #00b7ff; border-radius: 8px; padding: 10px 28px; font-size: 16px; font-weight: 700; }"
        "QPushButton#dobotAlarmResetButton:hover { background: #1594ff; }"
        "QPushButton#dobotAlarmResetButton:pressed { background: #0671d4; }"
        "QPushButton#dobotAlarmResetButton:disabled { color: #7c97b7; background: #17304c; border-color: #29415d; }"));

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(30, 28, 30, 26);
    cardLayout->setSpacing(18);

    auto* titleRow = new QHBoxLayout();
    m_iconLabel = new QLabel(QStringLiteral("!"), m_card);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(42, 42);
    m_iconLabel->setStyleSheet(QStringLiteral(
        "background: #f3a712; color: #ffffff; border-radius: 21px; font-size: 25px; font-weight: 800;"));
    m_titleLabel = new QLabel(QStringLiteral("碰撞检测"), m_card);
    m_titleLabel->setObjectName(QStringLiteral("dobotAlarmTitle"));
    titleRow->addWidget(m_iconLabel);
    titleRow->addSpacing(10);
    titleRow->addWidget(m_titleLabel, 1);
    cardLayout->addLayout(titleRow);

    m_promptLabel = new QLabel(
        QStringLiteral("检测到机械臂碰撞保护。\n请排除碰撞原因并确认现场安全，然后点击“复位”。"),
        m_card);
    m_promptLabel->setObjectName(QStringLiteral("dobotAlarmPrompt"));
    m_promptLabel->setProperty("collision", true);
    m_promptLabel->setWordWrap(true);
    m_promptLabel->setMinimumHeight(92);
    cardLayout->addWidget(m_promptLabel);

    m_resultLabel = new QLabel(m_card);
    m_resultLabel->setObjectName(QStringLiteral("dobotAlarmResult"));
    m_resultLabel->setWordWrap(true);
    m_resultLabel->hide();
    cardLayout->addWidget(m_resultLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    m_resetButton = new QPushButton(QStringLiteral("复位"), m_card);
    m_resetButton->setObjectName(QStringLiteral("dobotAlarmResetButton"));
    buttonRow->addWidget(m_resetButton);
    cardLayout->addLayout(buttonRow);

    overlayLayout->addWidget(m_card, 0, Qt::AlignHCenter);
    overlayLayout->addStretch(1);

    setStyleSheet(QStringLiteral("QDialog#dobotAlarmDialog { background: rgba(2, 5, 5, 205); }"));

    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        setResetInProgress(true, QStringLiteral("正在读取 RobotMode()/GetErrorID() 并执行人工复位确认……"));
        emit resetRequested();
    });
    updateResetButton();
}

void DobotAlarmDialog::setAlarmEvent(const panthera::core::AlarmEvent& event)
{
    m_event = event;
    const bool collision = event.collision;
    m_card->setProperty("collision", collision);
    m_promptLabel->setProperty("collision", collision);
    m_card->style()->unpolish(m_card);
    m_card->style()->polish(m_card);
    m_promptLabel->style()->unpolish(m_promptLabel);
    m_promptLabel->style()->polish(m_promptLabel);
    m_iconLabel->setStyleSheet(collision
            ? QStringLiteral("background: #f3a712; color: #ffffff; border-radius: 21px; font-size: 25px; font-weight: 800;")
            : QStringLiteral("background: #df4338; color: #ffffff; border-radius: 21px; font-size: 25px; font-weight: 800;"));
    m_titleLabel->setText(collision ? QStringLiteral("碰撞检测") : QStringLiteral("机械臂报警"));
    if (!event.communicationHealthy) {
        m_promptLabel->setText(QStringLiteral("DOBOT 通信异常，无法确认机械臂当前状态。\n请恢复通信后再点击“复位”。"));
    } else if (collision) {
        m_promptLabel->setText(QStringLiteral("检测到机械臂碰撞保护。\n请排除碰撞原因并确认现场安全，然后点击“复位”。"));
    } else {
        m_promptLabel->setText(QStringLiteral("检测到机械臂错误报警。\n请排除报警原因并确认现场安全，然后点击“复位”。"));
    }

    if (!event.details.trimmed().isEmpty()) {
        const bool resetError = event.details.contains(QStringLiteral("失败"))
            || event.details.contains(QStringLiteral("拒绝"))
            || event.details.contains(QStringLiteral("断开"))
            || event.details.contains(QStringLiteral("未完成"))
            || event.details.contains(QStringLiteral("异常"));
        m_resultLabel->setProperty("resetError", resetError);
        m_resultLabel->style()->unpolish(m_resultLabel);
        m_resultLabel->style()->polish(m_resultLabel);
        m_resultLabel->setText(event.details);
        m_resultLabel->show();
    } else {
        m_resultLabel->hide();
    }
    updateResetButton();
}

void DobotAlarmDialog::showOverParent()
{
    if (QWidget* owner = parentWidget()) {
        const QPoint topLeft = owner->mapToGlobal(QPoint(0, 0));
        setGeometry(QRect(topLeft, owner->size()));
    }
    show();
    raise();
    activateWindow();
}

void DobotAlarmDialog::resolveAndClose(const QString& message)
{
    m_allowClose = true;
    if (!message.trimmed().isEmpty()) {
        m_resultLabel->setProperty("resetError", false);
        m_resultLabel->setText(message);
    }
    accept();
}

void DobotAlarmDialog::setResetInProgress(bool inProgress, const QString& message)
{
    m_resetInProgress = inProgress;
    if (!message.trimmed().isEmpty()) {
        const bool resetError = message.contains(QStringLiteral("失败"))
            || message.contains(QStringLiteral("拒绝"))
            || message.contains(QStringLiteral("断开"))
            || message.contains(QStringLiteral("未完成"))
            || message.contains(QStringLiteral("异常"));
        m_resultLabel->setProperty("resetError", resetError);
        m_resultLabel->style()->unpolish(m_resultLabel);
        m_resultLabel->style()->polish(m_resultLabel);
        m_resultLabel->setText(message);
        m_resultLabel->show();
    }
    updateResetButton();
}

void DobotAlarmDialog::closeEvent(QCloseEvent* event)
{
    if (m_allowClose) {
        QDialog::closeEvent(event);
    } else {
        event->ignore();
    }
}

void DobotAlarmDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && !m_allowClose) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void DobotAlarmDialog::reject()
{
    if (m_allowClose) {
        QDialog::reject();
    }
}

void DobotAlarmDialog::updateResetButton()
{
    const bool canReset = m_event.active
        && m_event.communicationHealthy
        && !m_resetInProgress;
    m_resetButton->setEnabled(canReset);
    m_resetButton->setText(m_resetInProgress ? QStringLiteral("复位中……") : QStringLiteral("复位"));
}

}  // namespace panthera::modules

