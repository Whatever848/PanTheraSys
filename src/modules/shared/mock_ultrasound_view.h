#pragma once

#include <QWidget>

#include "core/domain/system_types.h"

namespace panthera::modules {

// MockUltrasoundView 是方案页和治疗页共用的示意性超声视图。
// 它不是诊断级渲染器，只用于演示方案点位和覆盖范围的可视化效果。
class MockUltrasoundView final : public QWidget {
    Q_OBJECT

public:
    explicit MockUltrasoundView(QWidget* parent = nullptr);

    void setPlan(const panthera::core::TherapyPlan& plan);
    void clearPlan();
    void setCompletedPointCount(int completedPointCount);
    void setCaption(const QString& caption);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int totalPointCount() const;
    QPointF mapPointToWidget(const QPointF& logicalPoint) const;

    panthera::core::TherapyPlan m_plan;
    bool m_hasPlan {false};
    int m_completedPointCount {0};
    QString m_caption;
};

}  // panthera::modules 命名空间
