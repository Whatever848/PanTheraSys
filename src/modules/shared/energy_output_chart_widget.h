#pragma once

#include <QFrame>
#include <QPointF>
#include <QString>
#include <QVector>

namespace panthera::modules {

class EnergyOutputChartWidget final : public QFrame {
public:
    explicit EnergyOutputChartWidget(QWidget* parent = nullptr);

    void setRealtimePowerWatts(double powerWatts);
    void clearPowerCurve(const QString& placeholderText = QString());

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<QPointF> buildCurveSamples(double powerWatts) const;

    QVector<QPointF> m_curveSamples;
    QString m_placeholderText;
    double m_powerWatts {0.0};
};

}  // namespace panthera::modules
