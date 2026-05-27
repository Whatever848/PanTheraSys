#include "adapters/anthone/lu926_temperature_protocol.h"

#include <algorithm>
#include <cmath>

namespace panthera::adapters::anthone {

bool Lu926TemperatureProtocol::isValidChannelIndex(int channelIndex)
{
    return channelIndex >= 1 && channelIndex <= kChannelCount;
}

bool Lu926TemperatureProtocol::isSupportedBaudRate(int baudRate)
{
    switch (baudRate) {
    case 1200:
    case 2400:
    case 4800:
    case 9600:
    case 19200:
    case 38400:
        return true;
    default:
        return false;
    }
}

int Lu926TemperatureProtocol::setpointRegister(int channelIndex)
{
    return isValidChannelIndex(channelIndex) ? kSetpointBaseRegister + channelIndex - 1 : -1;
}

int Lu926TemperatureProtocol::processValueRegister(int channelIndex)
{
    return isValidChannelIndex(channelIndex) ? kProcessValueBaseRegister + channelIndex - 1 : -1;
}

int Lu926TemperatureProtocol::outputPercentRegister(int channelIndex)
{
    return isValidChannelIndex(channelIndex) ? kOutputPercentBaseRegister + channelIndex - 1 : -1;
}

int Lu926TemperatureProtocol::alarmStateRegister(int channelIndex)
{
    return isValidChannelIndex(channelIndex) ? kAlarmStateBaseRegister + channelIndex - 1 : -1;
}

double Lu926TemperatureProtocol::decodeRegisterTemperature(qint16 rawValue)
{
    return static_cast<double>(rawValue) / 10.0;
}

qint16 Lu926TemperatureProtocol::encodeRegisterTemperature(double celsius)
{
    const int rawValue = static_cast<int>(std::lround(celsius * 10.0));
    return static_cast<qint16>(std::clamp(rawValue, kMinimumTemperatureRegister, kMaximumTemperatureRegister));
}

double Lu926TemperatureProtocol::decodeOutputPercent(quint16 rawValue)
{
    return std::clamp(static_cast<double>(rawValue) / 256.0, 0.0, 100.0);
}

quint16 Lu926TemperatureProtocol::encodeOutputPercent(double percent)
{
    const int rawValue = static_cast<int>(std::lround(std::clamp(percent, 0.0, 100.0) * 256.0));
    return static_cast<quint16>(std::clamp(rawValue, 0, static_cast<int>(kMaximumOutputPercentRegister)));
}

}  // namespace panthera::adapters::anthone
