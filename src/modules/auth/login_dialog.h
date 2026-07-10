#pragma once

#include <QDialog>

#include "adapters/mysql/mysql_auth_repository.h"

class QLabel;
class QLineEdit;
class QTabWidget;

namespace panthera::modules {

class LoginDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(panthera::adapters::MySqlAuthRepository* authRepository, QWidget* parent = nullptr);

    panthera::core::AuthenticatedOperator authenticatedOperator() const;

private:
    QWidget* createLoginPage();
    QWidget* createRegisterPage();
    QLineEdit* createLineEdit(const QString& placeholder, bool password = false);
    QLabel* createFieldLabel(const QString& text);
    void showMessage(const QString& message, bool error);
    void attemptLogin();
    void attemptRegistration();

    panthera::adapters::MySqlAuthRepository* m_authRepository {nullptr};
    panthera::core::AuthenticatedOperator m_authenticatedOperator;
    QTabWidget* m_tabs {nullptr};
    QLabel* m_messageLabel {nullptr};
    QLineEdit* m_loginUsernameEdit {nullptr};
    QLineEdit* m_loginPasswordEdit {nullptr};
    QLineEdit* m_registerUsernameEdit {nullptr};
    QLineEdit* m_registerPasswordEdit {nullptr};
    QLineEdit* m_registerConfirmPasswordEdit {nullptr};
    QLineEdit* m_doctorNameEdit {nullptr};
    QLineEdit* m_departmentEdit {nullptr};
    QLineEdit* m_titleEdit {nullptr};
    QLineEdit* m_licenseEdit {nullptr};
    QLineEdit* m_phoneEdit {nullptr};
};

}  // namespace panthera::modules
