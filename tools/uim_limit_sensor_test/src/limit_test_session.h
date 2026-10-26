#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include "uim_can_protocol.h"

namespace panthera::tools::uim_limit {

enum class LimitTestPhase {
    Idle,
    WaitingForTrigger,
    WaitingForRestore,
    Passed,
    Failed
};

enum class LimitTestUpdate {
    Ignored,
    TriggerDetected,
    RepeatedSameEdge,
    Passed
};

class LimitTestSession final {
public:
    void start(quint32 targetNodeId, const QDateTime& startedAt);
    [[nodiscard]] LimitTestUpdate consume(const ParsedCanFrame& frame);
    void finishWithoutPass(const QString& reason, const QDateTime& finishedAt);
    void reset();

    [[nodiscard]] quint32 targetNodeId() const;
    [[nodiscard]] LimitTestPhase phase() const;
    [[nodiscard]] S3Edge triggerEdge() const;
    [[nodiscard]] QDateTime startedAt() const;
    [[nodiscard]] QDateTime triggerTime() const;
    [[nodiscard]] QDateTime restoreTime() const;
    [[nodiscard]] QString resultText() const;
    [[nodiscard]] bool isActive() const;

private:
    quint32 m_targetNodeId {0};
    LimitTestPhase m_phase {LimitTestPhase::Idle};
    S3Edge m_triggerEdge {S3Edge::None};
    QDateTime m_startedAt;
    QDateTime m_triggerTime;
    QDateTime m_restoreTime;
    QString m_resultText {QStringLiteral("未开始")};
};

}  // namespace panthera::tools::uim_limit

