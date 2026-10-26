#include "modules/auth/login_dialog.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

namespace panthera::modules {

LoginDialog::LoginDialog(panthera::adapters::MySqlAuthRepository* authRepository, QWidget* parent)
    : QDialog(parent)
    , m_authRepository(authRepository)
{
    setObjectName(QStringLiteral("authLoginDialog"));
    setWindowTitle(QStringLiteral("医生登录"));
    setModal(true);
    setMinimumSize(820, 700);
    resize(900, 720);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(28, 28, 28, 28);
    rootLayout->setSpacing(0);

    auto* loginPanel = new QFrame(this);
    loginPanel->setObjectName(QStringLiteral("authLoginPanel"));
    auto* panelLayout = new QVBoxLayout(loginPanel);
    panelLayout->setContentsMargins(42, 32, 42, 26);
    panelLayout->setSpacing(14);

    auto* appTitle = new QLabel(QStringLiteral("低强度超声系统 V1.0"), loginPanel);
    appTitle->setObjectName(QStringLiteral("authTitleLabel"));
    auto* titleDivider = new QFrame(loginPanel);
    titleDivider->setObjectName(QStringLiteral("authTitleDivider"));
    titleDivider->setFrameShape(QFrame::HLine);

    m_tabs = new QTabWidget(loginPanel);
    m_tabs->setObjectName(QStringLiteral("authTabWidget"));
    m_tabs->setMinimumHeight(440);
    m_tabs->addTab(createLoginPage(), QStringLiteral("登录"));
    m_tabs->addTab(createRegisterPage(), QStringLiteral("注册医生"));

    m_messageLabel = new QLabel(loginPanel);
    m_messageLabel->setObjectName(QStringLiteral("authMessageLabel"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setVisible(false);

    auto* securityFooter = new QLabel(QStringLiteral("MySQL 账号库  ·  医生信息绑定  ·  加密口令"), loginPanel);
    securityFooter->setObjectName(QStringLiteral("authFooterLabel"));
    securityFooter->setAlignment(Qt::AlignCenter);

    panelLayout->addWidget(appTitle);
    panelLayout->addWidget(titleDivider);
    panelLayout->addWidget(m_tabs, 1);
    panelLayout->addWidget(m_messageLabel);
    panelLayout->addWidget(securityFooter);
    rootLayout->addWidget(loginPanel, 1);

    if (m_authRepository != nullptr && !m_authRepository->hasAnyActiveUser()) {
        m_tabs->setCurrentIndex(1);
    }
}

panthera::core::AuthenticatedOperator LoginDialog::authenticatedOperator() const
{
    return m_authenticatedOperator;
}

QWidget* LoginDialog::createLoginPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("authLoginPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 26, 14, 12);
    layout->setSpacing(18);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_loginUsernameEdit = createLineEdit(QStringLiteral("医生账号"));
    m_loginPasswordEdit = createLineEdit(QStringLiteral("登录密码"), true);
    form->addRow(createFieldLabel(QStringLiteral("账号")), m_loginUsernameEdit);
    form->addRow(createFieldLabel(QStringLiteral("密码")), m_loginPasswordEdit);
    layout->addLayout(form);

    auto* loginButton = new QPushButton(QStringLiteral("登录系统"), page);
    loginButton->setObjectName(QStringLiteral("authPrimaryButton"));
    loginButton->setMinimumHeight(44);
    layout->addWidget(loginButton);
    layout->addStretch();

    connect(loginButton, &QPushButton::clicked, this, &LoginDialog::attemptLogin);
    connect(m_loginPasswordEdit, &QLineEdit::returnPressed, this, &LoginDialog::attemptLogin);
    connect(m_loginUsernameEdit, &QLineEdit::returnPressed, m_loginPasswordEdit, qOverload<>(&QWidget::setFocus));

    return page;
}

QWidget* LoginDialog::createRegisterPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("authRegisterPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 18, 14, 10);
    layout->setSpacing(10);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(7);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_doctorNameEdit = createLineEdit(QStringLiteral("例如：王医生"));
    m_departmentEdit = createLineEdit(QStringLiteral("例如：肿瘤科"));
    m_titleEdit = createLineEdit(QStringLiteral("例如：主治医师"));
    m_licenseEdit = createLineEdit(QStringLiteral("执业证书编号，可后续补录"));
    m_phoneEdit = createLineEdit(QStringLiteral("联系电话，可选"));
    m_registerUsernameEdit = createLineEdit(QStringLiteral("用于登录的账号"));
    m_registerPasswordEdit = createLineEdit(QStringLiteral("至少 8 位密码"), true);
    m_registerConfirmPasswordEdit = createLineEdit(QStringLiteral("再次输入密码"), true);
    for (QLineEdit* field : {
             m_doctorNameEdit,
             m_departmentEdit,
             m_titleEdit,
             m_licenseEdit,
             m_phoneEdit,
             m_registerUsernameEdit,
             m_registerPasswordEdit,
             m_registerConfirmPasswordEdit}) {
        field->setMinimumHeight(36);
        field->setMaximumHeight(38);
    }

    form->addRow(createFieldLabel(QStringLiteral("医生姓名")), m_doctorNameEdit);
    form->addRow(createFieldLabel(QStringLiteral("科室")), m_departmentEdit);
    form->addRow(createFieldLabel(QStringLiteral("职称")), m_titleEdit);
    form->addRow(createFieldLabel(QStringLiteral("执业证号")), m_licenseEdit);
    form->addRow(createFieldLabel(QStringLiteral("联系电话")), m_phoneEdit);
    form->addRow(createFieldLabel(QStringLiteral("登录账号")), m_registerUsernameEdit);
    form->addRow(createFieldLabel(QStringLiteral("登录密码")), m_registerPasswordEdit);
    form->addRow(createFieldLabel(QStringLiteral("确认密码")), m_registerConfirmPasswordEdit);
    layout->addLayout(form);

    auto* registerButton = new QPushButton(QStringLiteral("注册并登录"), page);
    registerButton->setObjectName(QStringLiteral("authPrimaryButton"));
    registerButton->setMinimumHeight(42);
    layout->addWidget(registerButton);

    connect(registerButton, &QPushButton::clicked, this, &LoginDialog::attemptRegistration);
    connect(m_registerConfirmPasswordEdit, &QLineEdit::returnPressed, this, &LoginDialog::attemptRegistration);

    return page;
}

QLineEdit* LoginDialog::createLineEdit(const QString& placeholder, bool password)
{
    auto* edit = new QLineEdit(this);
    edit->setObjectName(QStringLiteral("authInputEdit"));
    edit->setPlaceholderText(placeholder);
    edit->setMinimumHeight(42);
    if (password) {
        edit->setEchoMode(QLineEdit::Password);
    }
    return edit;
}

QLabel* LoginDialog::createFieldLabel(const QString& text)
{
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("authFieldLabel"));
    return label;
}

void LoginDialog::showMessage(const QString& message, bool error)
{
    if (m_messageLabel == nullptr) {
        return;
    }
    m_messageLabel->setText(message);
    m_messageLabel->setProperty("error", error);
    m_messageLabel->setVisible(!message.trimmed().isEmpty());
    m_messageLabel->style()->unpolish(m_messageLabel);
    m_messageLabel->style()->polish(m_messageLabel);
}

void LoginDialog::attemptLogin()
{
    if (m_authRepository == nullptr) {
        showMessage(QStringLiteral("认证服务不可用。"), true);
        return;
    }

    panthera::core::AuthenticatedOperator account;
    if (!m_authRepository->authenticate(m_loginUsernameEdit->text(), m_loginPasswordEdit->text(), &account)) {
        showMessage(m_authRepository->lastError(), true);
        return;
    }

    m_authenticatedOperator = account;
    accept();
}

void LoginDialog::attemptRegistration()
{
    if (m_authRepository == nullptr) {
        showMessage(QStringLiteral("认证服务不可用。"), true);
        return;
    }

    const QString password = m_registerPasswordEdit->text();
    if (password != m_registerConfirmPasswordEdit->text()) {
        showMessage(QStringLiteral("两次输入的密码不一致。"), true);
        return;
    }

    panthera::adapters::DoctorRegistrationRequest request;
    request.username = m_registerUsernameEdit->text();
    request.password = password;
    request.doctorName = m_doctorNameEdit->text();
    request.department = m_departmentEdit->text();
    request.title = m_titleEdit->text();
    request.licenseNumber = m_licenseEdit->text();
    request.phone = m_phoneEdit->text();

    panthera::core::AuthenticatedOperator account;
    if (!m_authRepository->registerDoctorAccount(request, &account)) {
        showMessage(m_authRepository->lastError(), true);
        return;
    }

    m_authenticatedOperator = account;
    accept();
}

}  // namespace panthera::modules
