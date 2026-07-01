#pragma once

#include <QString>
#include <functional>

namespace panthera::adapters::dobot {

struct DobotPayloadPresetOptions {
    // Preset name created in DobotStudio Pro: Settings -> Load Parameter.
    QString presetName = QStringLiteral("TEST");

    // Preset mode: call SetPayload(TEST) before the existing EnableRobot() flow.
    // Explicit mode: call EnableRobot(load,cx,cy,cz[,isCheck]) and skip the later EnableRobot().
    bool usePresetName = false;

    // Optional fallback: if SetPayload(TEST) fails, call EnableRobot(load,cx,cy,cz[,isCheck]).
    bool fallbackToExplicitEnableRobot = false;
    double loadKg = 7.5;
    double centerX = 0.0;
    double centerY = 0.0;
    double centerZ = 0.0;
    bool enableLoadCheck = false;

    // The final EnableRobot() command should be sent by the existing enable button flow.
    // Kept for compatibility with earlier integration packages.
    bool sendEnableRobotInsideService = false;
};

struct DobotPayloadCommandResult {
    bool ok = false;
    int errorId = -999999;
    QString raw;
    QString message;
};

class DobotPayloadEnableService {
public:
    using DashboardSendFn = std::function<QString(const QString& command)>;
    using LogFn = std::function<void(const QString& text)>;

    explicit DobotPayloadEnableService(DashboardSendFn sender, LogFn logger = nullptr);

    void setOptions(const DobotPayloadPresetOptions& options);
    DobotPayloadPresetOptions options() const;

    // Call this immediately before the original enable command.
    // It will try SetPayload(TEST), or EnableRobot(load,cx,cy,cz[,isCheck]) when usePresetName is false.
    bool applyPayloadBeforeEnable(QString* errorMessage = nullptr);
    bool applyPayloadBeforeEnable(bool* enableRobotAlreadySent, QString* errorMessage = nullptr);

    // Optional all-in-one flow. Use only when you want this class to perform enable too.
    bool applyPayloadAndEnable(QString* errorMessage = nullptr);

    static DobotPayloadCommandResult parseDashboardResponse(const QString& response);
    static QString quoteDashboardString(const QString& value);

private:
    bool sendAndExpectOk(const QString& command, QString* errorMessage);
    QString explicitEnableRobotCommand() const;
    void log(const QString& text) const;

private:
    DashboardSendFn m_sender;
    LogFn m_logger;
    DobotPayloadPresetOptions m_options;
};

} // namespace panthera::adapters::dobot
