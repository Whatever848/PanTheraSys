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
    setWindowTitle(QStringLiteral("医生登录"));
    setModal(true);
    resize(920, 560);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(28, 28, 28, 28);
    rootLayout->setSpacing(24);

    auto* brandPanel = new QFrame(this);
    brandPanel->setObjectName(QStringLiteral("authBrandPanel"));
    auto* brandLayout = new QVBoxLayout(brandPanel);
    brandLayout->setContentsMargins(30, 34, 30, 34);
    brandLayout->setSpacing(18);

    auto* appTitle = new QLabel(QStringLiteral("低强度超声系统 V1.0"), brandPanel);
    appTitle->setObjectName(QStringLiteral("authTitleLabel"));
    auto* subtitle = new QLabel(QStringLiteral("医生身份认证"), brandPanel);
    subtitle->setObjectName(QStringLiteral("authSubtitleLabel"));
    subtitle->setWordWrap(true);
    auto* description = new QLabel(QStringLiteral("登录后系统会记录当前医生身份，用于方案审批、治疗操作和医疗责任追溯。"), brandPanel);
    description->setObjectName(QStringLiteral("authDescriptionLabel"));
    description->setWordWrap(true);
    brandLayout->addWidget(appTitle);
    brandLayout->addWidget(subtitle);
    brandLayout->addSpacing(24);
    brandLayout->addWidget(description);
    brandLayout->addStretch();

    auto* status = new QLabel(QStringLiteral("MySQL 账号库 | 医生信息绑定 | 加密口令"), brandPanel);
    status->setObjectName(QStringLiteral("authDescriptionLabel"));
    status->setWordWrap(true);
    brandLayout->addWidget(status);

    auto* formPanel = new QFrame(this);
    formPanel->setObjectName(QStringLiteral("authFormPanel"));
    auto* formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(26, 24, 26, 24);
    formLayout->setSpacing(14);

    auto* formTitle = new QLabel(QStringLiteral("进入临床工作站"), formPanel);
    formTitle->setObjectName(QStringLiteral("authSectionTitleLabel"));
    formLayout->addWidget(formTitle);

    m_messageLabel = new QLabel(formPanel);
    m_messageLabel->setObjectName(QStringLiteral("authMessageLabel"));
    m_messageLabel->setWordWrap(true);
    formLayout->addWidget(m_messageLabel);

    m_tabs = new QTabWidget(formPanel);
    m_tabs->setObjectName(QStringLiteral("authTabWidget"));
    m_tabs->addTab(createLoginPage(), QStringLiteral("登录"));
    m_tabs->addTab(createRegisterPage(), QStringLiteral("注册医生"));
    formLayout->addWidget(m_tabs, 1);

    rootLayout->addWidget(brandPanel, 5);
    rootLayout->addWidget(formPanel, 6);

    if (m_authRepository != nullptr && !m_authRepository->hasAnyActiveUser()) {
        m_tabs->setCurrentIndex(1);
        showMessage(QStringLiteral("尚未创建医生账号，请先注册第一位医生。"), false);
    } else {
        showMessage(QStringLiteral("请输入医生账号和密码。"), false);
    }
}

panthera::core::AuthenticatedOperator LoginDialog::authenticatedOperator() const
{
    return m_authenticatedOperator;
}

QWidget* LoginDialog::createLoginPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 18, 0, 0);
    layout->setSpacing(16);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_loginUsernameEdit = createLineEdit(QStringLiteral("医生账号"));
    m_loginPasswordEdit = createLineEdit(QStringLiteral("登录密码"), true);
    form->addRow(createFieldLabel(QStringLiteral("账号")), m_loginUsernameEdit);
    form->addRow(createFieldLabel(QStringLiteral("密码")), m_loginPasswordEdit);
    layout->addLayout(form);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    auto* loginButton = new QPushButton(QStringLiteral("登录系统"), page);
    loginButton->setObjectName(QStringLiteral("panelActionButton"));
    buttonLayout->addWidget(loginButton);
    layout->addLayout(buttonLayout);
    layout->addStretch();

    connect(loginButton, &QPushButton::clicked, this, &LoginDialog::attemptLogin);
    connect(m_loginPasswordEdit, &QLineEdit::returnPressed, this, &LoginDialog::attemptLogin);
    connect(m_loginUsernameEdit, &QLineEdit::returnPressed, m_loginPasswordEdit, qOverload<>(&QWidget::setFocus));

    return page;
}

QWidget* LoginDialog::createRegisterPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 18, 0, 0);
    layout->setSpacing(16);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_doctorNameEdit = createLineEdit(QStringLiteral("例如：王医生"));
    m_departmentEdit = createLineEdit(QStringLiteral("例如：肿瘤科"));
    m_titleEdit = createLineEdit(QStringLiteral("例如：主治医师"));
    m_licenseEdit = createLineEdit(QStringLiteral("执业证书编号，可后续补录"));
    m_phoneEdit = createLineEdit(QStringLiteral("联系电话，可选"));
    m_registerUsernameEdit = createLineEdit(QStringLiteral("用于登录的账号"));
    m_registerPasswordEdit = createLineEdit(QStringLiteral("至少 8 位密码"), true);
    m_registerConfirmPasswordEdit = createLineEdit(QStringLiteral("再次输入密码"), true);

    form->addRow(createFieldLabel(QStringLiteral("医生姓名")), m_doctorNameEdit);
    form->addRow(createFieldLabel(QStringLiteral("科室")), m_departmentEdit);
    form->addRow(createFieldLabel(QStringLiteral("职称")), m_titleEdit);
    form->addRow(createFieldLabel(QStringLiteral("执业证号")), m_licenseEdit);
    form->addRow(createFieldLabel(QStringLiteral("联系电话")), m_phoneEdit);
    form->addRow(createFieldLabel(QStringLiteral("登录账号")), m_registerUsernameEdit);
    form->addRow(createFieldLabel(QStringLiteral("登录密码")), m_registerPasswordEdit);
    form->addRow(createFieldLabel(QStringLiteral("确认密码")), m_registerConfirmPasswordEdit);
    layout->addLayout(form);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    auto* registerButton = new QPushButton(QStringLiteral("注册并登录"), page);
    registerButton->setObjectName(QStringLiteral("panelActionButton"));
    buttonLayout->addWidget(registerButton);
    layout->addLayout(buttonLayout);

    connect(registerButton, &QPushButton::clicked, this, &LoginDialog::attemptRegistration);
    connect(m_registerConfirmPasswordEdit, &QLineEdit::returnPressed, this, &LoginDialog::attemptRegistration);

    return page;
}

QLineEdit* LoginDialog::createLineEdit(const QString& placeholder, bool password)
{
    auto* edit = new QLineEdit(this);
    edit->setPlaceholderText(placeholder);
    edit->setMinimumHeight(38);
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
