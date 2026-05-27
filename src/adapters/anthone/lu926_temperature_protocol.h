#pragma once

#include <QtGlobal>

namespace panthera::adapters::anthone {

struct Lu926TemperatureProtocol final {
    static constexpr int kChannelCount = 6;
    static constexpr int kDefaultAddress = 1;
    static constexpr int kDefaultBaudRate = 9600;
    static constexpr double kSamplePeriodSeconds = 0.5;

    static constexpr int kLocalAddressRegister = 0x0010;
    static constexpr int kBaudRateRegister = 0x0011;
    static constexpr int kDataModeRegister = 0x0012;
    static constexpr int kSetpointBaseRegister = 0x0100;
    static constexpr int kOutputPercentBaseRegister = 0x0108;
    static constexpr int kProcessValueBaseRegister = 0x0110;
    static constexpr int kAlarmStateBaseRegister = 0x0116;
    static constexpr int kColdJunctionRegister = 0x0128;
    static constexpr int kFaultCodeRegister = 0x0129;

    static constexpr int kMinimumTemperatureRegister = -1999;
    static constexpr int kMaximumTemperatureRegister = 9999;
    static constexpr quint16 kMaximumOutputPercentRegister = 25600;

    static bool isValidChannelIndex(int channelIndex);
    static bool isSupportedBaudRate(int baudRate);
    static int setpointRegister(int channelIndex);
    static int processValueRegister(int channelIndex);
    static int outputPercentRegister(int channelIndex);
    static int alarmStateRegister(int channelIndex);
    static double decodeRegisterTemperature(qint16 rawValue);
    static qint16 encodeRegisterTemperature(double celsius);
    static double decodeOutputPercent(quint16 rawValue);
    static quint16 encodeOutputPercent(double percent);
};

}  // namespace panthera::adapters::anthone
