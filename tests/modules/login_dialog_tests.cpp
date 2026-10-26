#include <QtTest/QtTest>

#include <algorithm>

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>

#include "modules/auth/login_dialog.h"

using namespace panthera::modules;

class LoginDialogTests final : public QObject {
    Q_OBJECT

private slots:
    void loginPageUsesSinglePanelWithoutPermanentPrompt();
    void registerPageKeepsFieldsSeparated();
};

void LoginDialogTests::loginPageUsesSinglePanelWithoutPermanentPrompt()
{
    LoginDialog dialog(nullptr);
    dialog.show();
    QCoreApplication::processEvents();

    const auto loginPanels = dialog.findChildren<QFrame*>(QStringLiteral("authLoginPanel"));
    QCOMPARE(loginPanels.size(), 1);
    QVERIFY(dialog.findChild<QFrame*>(QStringLiteral("authBrandPanel")) == nullptr);
    QVERIFY(dialog.findChild<QFrame*>(QStringLiteral("authFormPanel")) == nullptr);

    auto* title = dialog.findChild<QLabel*>(QStringLiteral("authTitleLabel"));
    QVERIFY(title != nullptr);
    QCOMPARE(title->text(), QStringLiteral("\u4f4e\u5f3a\u5ea6\u8d85\u58f0\u7cfb\u7edf V1.0"));

    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("authTabWidget"));
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 2);

    auto* message = dialog.findChild<QLabel*>(QStringLiteral("authMessageLabel"));
    QVERIFY(message != nullptr);
    QVERIFY(!message->isVisible());

    const auto inputs = dialog.findChildren<QLineEdit*>(QStringLiteral("authInputEdit"));
    QVERIFY(inputs.size() >= 2);
    QVERIFY(!dialog.findChildren<QPushButton*>(QStringLiteral("authPrimaryButton")).isEmpty());
}

void LoginDialogTests::registerPageKeepsFieldsSeparated()
{
    LoginDialog dialog(nullptr);
    dialog.show();
    QCoreApplication::processEvents();

    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("authTabWidget"));
    QVERIFY(tabs != nullptr);
    tabs->setCurrentIndex(1);
    QCoreApplication::processEvents();

    QVERIFY(dialog.minimumHeight() >= 700);
    QVERIFY(tabs->minimumHeight() >= 440);

    auto registerFields = tabs->widget(1)->findChildren<QLineEdit*>(QStringLiteral("authInputEdit"));
    QCOMPARE(registerFields.size(), 8);
    std::sort(registerFields.begin(), registerFields.end(), [](const QLineEdit* left, const QLineEdit* right) {
        return left->geometry().top() < right->geometry().top();
    });

    for (int index = 0; index < registerFields.size(); ++index) {
        const QLineEdit* field = registerFields.at(index);
        QVERIFY(field->isVisible());
        QVERIFY(field->height() >= 36);
        if (index > 0) {
            QVERIFY(registerFields.at(index - 1)->geometry().bottom() < field->geometry().top());
        }
    }
}

QTEST_MAIN(LoginDialogTests)

#include "login_dialog_tests.moc"
