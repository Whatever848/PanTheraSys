#pragma once

#include <type_traits>
#include <utility>

#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QWidget>

#include "core/application/exception_utils.h"
#include "core/services/audit_service.h"

namespace panthera::modules {

inline void reportOperationFailure(
    QWidget* parent,
    panthera::core::AuditService* auditService,
    const QString& category,
    const QString& action,
    const QString& detail)
{
    const QString message = detail.trimmed().isEmpty() ? QStringLiteral("未知错误") : detail.trimmed();
    if (auditService != nullptr) {
        auditService->appendEntry(
            QStringLiteral("system"),
            category,
            QStringLiteral("%1 异常：%2").arg(action, message));
    }

    QMessageBox::critical(
        parent,
        action,
        QStringLiteral("%1失败，请检查当前操作或数据状态。\n\n%2").arg(action, message));
}

template <typename Callable>
auto executeGuarded(
    QWidget* parent,
    panthera::core::AuditService* auditService,
    const QString& category,
    const QString& action,
    Callable&& callable) -> std::invoke_result_t<Callable>
{
    using Result = std::invoke_result_t<Callable>;

    try {
        if constexpr (std::is_void_v<Result>) {
            std::forward<Callable>(callable)();
        } else {
            return std::forward<Callable>(callable)();
        }
    } catch (...) {
        reportOperationFailure(parent, auditService, category, action, panthera::core::describeCurrentExceptionMessage());
        if constexpr (!std::is_void_v<Result>) {
            return Result {};
        }
    }
}

template <typename Work, typename Success, typename Error>
void runAsyncTask(QObject* contextObject, Work&& work, Success&& onSuccess, Error&& onError)
{
    using Result = std::invoke_result_t<Work>;

    QPointer<QObject> guard(contextObject);
    auto* runnable = QRunnable::create(
        [guard,
         work = std::forward<Work>(work),
         onSuccess = std::forward<Success>(onSuccess),
         onError = std::forward<Error>(onError)]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    work();
                    if (guard.isNull()) {
                        return;
                    }

                    QMetaObject::invokeMethod(
                        guard.data(),
                        [guard, onSuccess = std::move(onSuccess)]() mutable {
                            if (guard.isNull()) {
                                return;
                            }
                            onSuccess();
                        },
                        Qt::QueuedConnection);
                } else {
                    Result result = work();
                    if (guard.isNull()) {
                        return;
                    }

                    QMetaObject::invokeMethod(
                        guard.data(),
                        [guard, onSuccess = std::move(onSuccess), result = std::move(result)]() mutable {
                            if (guard.isNull()) {
                                return;
                            }
                            onSuccess(std::move(result));
                        },
                        Qt::QueuedConnection);
                }
            } catch (...) {
                const QString detail = panthera::core::describeCurrentExceptionMessage();
                if (guard.isNull()) {
                    return;
                }

                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, onError = std::move(onError), detail]() mutable {
                        if (guard.isNull()) {
                            return;
                        }
                        onError(detail);
                    },
                    Qt::QueuedConnection);
            }
        });
    QThreadPool::globalInstance()->start(runnable);
}

}  // namespace panthera::modules
