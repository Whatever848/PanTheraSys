#include "limit_test_session.h"

namespace panthera::tools::uim_limit {

void LimitTestSession::start(quint32 targetNodeId, const QDateTime& startedAt)
{
    m_targetNodeId = targetNodeId;
    m_phase = LimitTestPhase::WaitingForTrigger;
    m_triggerEdge = S3Edge::None;
    m_startedAt = startedAt;
    m_triggerTime = QDateTime();
    m_restoreTime = QDateTime();
    m_resultText = QStringLiteral("等待手动触发 S3 限位开关");
}

LimitTestUpdate LimitTestSession::consume(const ParsedCanFrame& frame)
{
    if (!isActive() || !frame.nodeSourceKnown || !frame.isS3Event() || frame.nodeId != m_targetNodeId) {
        return LimitTestUpdate::Ignored;
    }

    if (m_phase == LimitTestPhase::WaitingForTrigger) {
        m_triggerEdge = frame.s3Edge;
        m_triggerTime = frame.raw.receivedAt;
        m_phase = LimitTestPhase::WaitingForRestore;
        m_resultText = QStringLiteral("已检测到状态变化，等待相反边沿恢复");
        return LimitTestUpdate::TriggerDetected;
    }

    if (frame.s3Edge == m_triggerEdge) {
        m_resultText = QStringLiteral("再次收到相同边沿，仍等待相反边沿恢复");
        return LimitTestUpdate::RepeatedSameEdge;
    }

    m_restoreTime = frame.raw.receivedAt;
    m_phase = LimitTestPhase::Passed;
    m_resultText = QStringLiteral("测试通过：已收到触发与恢复两个相反边沿");
    return LimitTestUpdate::Passed;
}

void LimitTestSession::finishWithoutPass(const QString& reason, const QDateTime& finishedAt)
{
    if (!isActive()) {
        return;
    }
    m_restoreTime = finishedAt;
    m_phase = LimitTestPhase::Failed;
    m_resultText = reason;
}

void LimitTestSession::reset()
{
    *this = LimitTestSession {};
}

quint32 LimitTestSession::targetNodeId() const
{
    return m_targetNodeId;
}

LimitTestPhase LimitTestSession::phase() const
{
    return m_phase;
}

S3Edge LimitTestSession::triggerEdge() const
{
    return m_triggerEdge;
}

QDateTime LimitTestSession::startedAt() const
{
    return m_startedAt;
}

QDateTime LimitTestSession::triggerTime() const
{
    return m_triggerTime;
}

QDateTime LimitTestSession::restoreTime() const
{
    return m_restoreTime;
}

QString LimitTestSession::resultText() const
{
    return m_resultText;
}

bool LimitTestSession::isActive() const
{
    return m_phase == LimitTestPhase::WaitingForTrigger
        || m_phase == LimitTestPhase::WaitingForRestore;
}

}  // namespace panthera::tools::uim_limit

