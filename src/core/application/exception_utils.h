#pragma once

#include <exception>

#include <QString>

namespace panthera::core {

inline QString describeCurrentExceptionMessage(const QString& fallback = QStringLiteral("未知异常"))
{
    try {
        throw;
    } catch (const QString& message) {
        return message.trimmed().isEmpty() ? fallback : message.trimmed();
    } catch (const std::exception& exception) {
        const QString message = QString::fromLocal8Bit(exception.what()).trimmed();
        return message.isEmpty() ? fallback : message;
    } catch (const char* message) {
        const QString text = QString::fromLocal8Bit(message).trimmed();
        return text.isEmpty() ? fallback : text;
    } catch (...) {
        return fallback;
    }
}

}  // namespace panthera::core
