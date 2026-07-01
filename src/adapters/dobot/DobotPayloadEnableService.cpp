#include "DobotPayloadEnableService.h"

#include <QLocale>
#include <QStringList>

#include <utility>

namespace panthera::adapters::dobot {

DobotPayloadEnableService::DobotPayloadEnableService(DashboardSendFn sender, LogFn logger)
    : m_sender(std::move(sender)), m_logger(std::move(logger)) {}

void DobotPayloadEnableService::setOptions(const DobotPayloadPresetOptions& options) {
    m_options = options;
}

DobotPayloadPresetOptions DobotPayloadEnableService::options() const {
    return m_options;
}

QString DobotPayloadEnableService::quoteDashboardString(const QString& value) {
    QString argument = value.trimmed();
    argument.remove(QLatin1Char('('));
    argument.remove(QLatin1Char(')'));
    argument.remove(QLatin1Char(','));
    return argument;
}

DobotPayloadCommandResult DobotPayloadEnableService::parseDashboardResponse(const QString& response) {
    DobotPayloadCommandResult result;
    result.raw = response.trimmed();

    // DOBOT Dashboard returns: ErrorID,{value},Command(...);
    const int commaIndex = result.raw.indexOf(QLatin1Char(','));
    if (commaIndex <= 0) {
        result.ok = false;
        result.message = QStringLiteral("Cannot parse DOBOT Dashboard response: %1").arg(result.raw);
        return result;
    }

    bool errorIdOk = false;
    result.errorId = result.raw.left(commaIndex).trimmed().toInt(&errorIdOk);
    if (!errorIdOk) {
        result.ok = false;
        result.message = QStringLiteral("Cannot parse DOBOT Dashboard ErrorID: %1").arg(result.raw);
        return result;
    }

    result.ok = (result.errorId == 0);
    if (!result.ok) {
        result.message = QStringLiteral("DOBOT command failed, ErrorID=%1, raw=%2")
                             .arg(result.errorId)
                             .arg(result.raw);
    }
    return result;
}

bool DobotPayloadEnableService::sendAndExpectOk(const QString& command, QString* errorMessage) {
    if (!m_sender) {
        const QString err = QStringLiteral("Dashboard sender is not set.");
        if (errorMessage) *errorMessage = err;
        log(err);
        return false;
    }

    log(QStringLiteral(">>> %1").arg(command));
    const QString raw = m_sender(command);
    log(QStringLiteral("<<< %1").arg(raw));

    const auto parsed = parseDashboardResponse(raw);
    if (!parsed.ok) {
        const QString err = parsed.message.isEmpty()
            ? QStringLiteral("Command failed: %1, response=%2").arg(command, raw)
            : parsed.message;
        if (errorMessage) *errorMessage = err;
        log(err);
        return false;
    }
    return true;
}

QString DobotPayloadEnableService::explicitEnableRobotCommand() const {
    auto f = [](double value) {
        QString text = QLocale::c().toString(value, 'f', 6);
        while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
        return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
    };
    const QStringList arguments {
        f(m_options.loadKg),
        f(m_options.centerX),
        f(m_options.centerY),
        f(m_options.centerZ)
    };
    if (!m_options.enableLoadCheck) {
        return QStringLiteral("EnableRobot(%1)").arg(arguments.join(QLatin1Char(',')));
    }

    QStringList checkedArguments = arguments;
    checkedArguments.push_back(QStringLiteral("1"));
    return QStringLiteral("EnableRobot(%1)").arg(checkedArguments.join(QLatin1Char(',')));
}

bool DobotPayloadEnableService::applyPayloadBeforeEnable(QString* errorMessage) {
    return applyPayloadBeforeEnable(nullptr, errorMessage);
}

bool DobotPayloadEnableService::applyPayloadBeforeEnable(bool* enableRobotAlreadySent, QString* errorMessage) {
    if (enableRobotAlreadySent) {
        *enableRobotAlreadySent = false;
    }

    if (m_options.usePresetName) {
        const QString cmd = QStringLiteral("SetPayload(%1)").arg(quoteDashboardString(m_options.presetName));
        QString err;
        if (sendAndExpectOk(cmd, &err)) {
            return true;
        }

        log(QStringLiteral("SetPayload preset failed, preset=%1, err=%2")
                .arg(m_options.presetName, err));

        if (!m_options.fallbackToExplicitEnableRobot) {
            if (errorMessage) *errorMessage = err;
            return false;
        }

        const QString fallbackCmd = explicitEnableRobotCommand();
        const bool ok = sendAndExpectOk(fallbackCmd, errorMessage);
        if (ok) {
            if (enableRobotAlreadySent) {
                *enableRobotAlreadySent = true;
            }
            log(QStringLiteral("Fallback explicit EnableRobot(load,cx,cy,cz[,isCheck]) succeeded. Caller should not send another EnableRobot()."));
        }
        return ok;
    }

    const bool ok = sendAndExpectOk(explicitEnableRobotCommand(), errorMessage);
    if (ok && enableRobotAlreadySent) {
        *enableRobotAlreadySent = true;
    }
    return ok;
}

bool DobotPayloadEnableService::applyPayloadAndEnable(QString* errorMessage) {
    if (m_options.usePresetName) {
        const QString setPayloadCmd = QStringLiteral("SetPayload(%1)").arg(quoteDashboardString(m_options.presetName));
        if (!sendAndExpectOk(setPayloadCmd, errorMessage)) {
            if (!m_options.fallbackToExplicitEnableRobot) return false;
            return sendAndExpectOk(explicitEnableRobotCommand(), errorMessage);
        }
        return sendAndExpectOk(QStringLiteral("EnableRobot()"), errorMessage);
    }

    return sendAndExpectOk(explicitEnableRobotCommand(), errorMessage);
}

void DobotPayloadEnableService::log(const QString& text) const {
    if (m_logger) m_logger(text);
}

} // namespace panthera::adapters::dobot
