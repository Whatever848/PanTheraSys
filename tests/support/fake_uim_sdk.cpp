#include <QtCore/qglobal.h>

#include <array>
#include <vector>

// Test-only DLL. No hardware access; the test controls measured positions.
namespace {
constexpr quint32 failure = 0xffffffffU;
struct Motor {
    int position = 0;
    int speed = 0;
    int step = 0;
    int sensor1 = 1;
    int sensor2 = 1;
};
struct Command { quint32 node; int kind; int value; };
struct BasicFeedback {
    unsigned int reserved, enabled, direction, reduced, microstep, current;
    int speed, step;
};
struct SensorFeedback { int sensor1, sensor2, sensor3; float analog; };
std::array<Motor, 3> motors;
std::vector<Command> commands;
bool failClear = false;
Motor& motor(quint32 node) { return motors.at(node - 6); }
}

#define API extern "C" Q_DECL_EXPORT
API void FakeReset() { motors = {}; commands.clear(); failClear = false; }
API void FakeSetFeedback(quint32 node, int position, int sensor1, int sensor2)
{
    auto& value = motor(node);
    value.position = position;
    value.sensor1 = sensor1;
    value.sensor2 = sensor2;
}
API void FakeFailClear(int fail) { failClear = fail != 0; }
API int FakeCommandCount() { return static_cast<int>(commands.size()); }
API int FakeCommandKind(int index) { return commands.at(index).kind; }
API int FakeCommandValue(int index) { return commands.at(index).value; }
API quint32 FakeCommandNode(int index) { return commands.at(index).node; }

API quint32 SearchGateway(quint32, void*, void*, int) { return 0; }
API quint32 OpenGateway(quint32, quint32* nodes, int, quint32* bitRate)
{
    nodes[0] = 6; nodes[1] = 7; nodes[2] = 8;
    *bitRate = 500000;
    return 3;
}
API quint32 CloseGateway(quint32) { return 0; }
API quint32 GetMDL(quint32, quint32, void*) { return failure; }
API quint32 UimENA(quint32, quint32, int, void*) { return failure; }
API quint32 UimOFF(quint32, quint32, int, void*) { return failure; }
API quint32 UimFBK(quint32, quint32 node, BasicFeedback* result)
{
    const auto& value = motor(node);
    *result = {0, 1, 0, 0, 16, 10, value.speed, value.step};
    return 0;
}
API quint32 UimSFBK(quint32, quint32 node, SensorFeedback* result)
{
    const auto& value = motor(node);
    *result = {value.sensor1, value.sensor2, 1, 0.0f};
    return 0;
}
API quint32 GetSTG(quint32, quint32, void*) { return failure; }
API quint32 SetSTG(quint32, quint32, void*, int, void*) { return failure; }
API quint32 GetS12CON(quint32, quint32, void*) { return failure; }
API quint32 SetS12CON(quint32, quint32, void*, int, void*) { return failure; }
API quint32 GetS34CON(quint32, quint32, void*) { return failure; }
API quint32 SetS34CON(quint32, quint32, void*, int, void*) { return failure; }
API quint32 SetCUR(quint32, quint32, int value, int, int* result) { *result = value; return 0; }
API quint32 SetSPD(quint32, quint32 node, int value, int, int* result)
{
    commands.push_back({node, 1, value});
    motor(node).speed = *result = value;
    return 0;
}
API quint32 SetSTP(quint32, quint32 node, int value, int, int* result)
{
    commands.push_back({node, 2, value});
    if (value == 0 && failClear) { return failure; }
    motor(node).step = *result = value;
    return 0;
}
API quint32 SetPOS(quint32, quint32, int, int, int*) { return failure; }
API quint32 SetQEC(quint32, quint32, int, int, int*) { return failure; }
API quint32 SetORG(quint32, quint32, int, int, int*) { return failure; }
API quint32 SetDOUT(quint32, quint32, int, int, int*) { return failure; }
API quint32 GetSPD(quint32, quint32 node, int* result) { *result = motor(node).speed; return 0; }
API quint32 GetSTP(quint32, quint32 node, int* result) { *result = motor(node).step; return 0; }
API quint32 GetPOS(quint32, quint32 node, int* result) { *result = motor(node).position; return 0; }
API quint32 GetQEC(quint32, quint32 node, int* result) { *result = motor(node).position; return 0; }
API quint32 GetDOUT(quint32, quint32, int* result) { *result = 0; return 0; }
