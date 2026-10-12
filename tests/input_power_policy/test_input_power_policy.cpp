#include "InputPowerPolicy.hpp"
#include <cmath>
#include <cstdio>

static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main()
{
    const uint8_t command[8] = {1, 80, 0, 60, 0, 110, 0, 0xA5};
    InputPowerPolicy::Command decoded{};
    CHECK(InputPowerPolicy::decode(command, 8, decoded));
    CHECK(decoded.enabled && decoded.extended);
    CHECK(decoded.referee_limit_w == 80);
    CHECK(decoded.target_input_w == 110);
    CHECK(InputPowerPolicy::boundTarget(80, 10, 140) <= 80);
    CHECK(InputPowerPolicy::boundTarget(80, 60, 110) == 110);
    CHECK(InputPowerPolicy::boundTarget(80, 60, 1000) <= 140);
    CHECK(InputPowerPolicy::boundTarget(80, 0, 140) <= 60);
    uint8_t feedback[8]{};
    CHECK(InputPowerPolicy::encodeFeedback(110, 110, true, feedback));
    CHECK(feedback[2] == 0xDC && feedback[3] == 0x42);
    CHECK(feedback[4] == 110 && feedback[6] == 1 && feedback[7] == 1);
    CHECK(!InputPowerPolicy::encodeFeedback(NAN, 80, true, feedback));
    CHECK(!InputPowerPolicy::decode(command, 7, decoded));
    const uint8_t zero[8]{};
    CHECK(InputPowerPolicy::decode(zero, 8, decoded));
    CHECK(!decoded.enabled && !decoded.extended);
    CHECK(!InputPowerPolicy::feedbackDue(104, 100));
    CHECK(InputPowerPolicy::feedbackDue(105, 100));
    CHECK(InputPowerPolicy::feedbackDue(2, UINT32_MAX - 3));
    CHECK(!InputPowerPolicy::commandFresh(false, 1000, 0));
    CHECK(InputPowerPolicy::commandFresh(true, 100, 0));
    CHECK(!InputPowerPolicy::commandFresh(true, 101, 0));
    CHECK(!InputPowerPolicy::nextOutputEnable(false, true, true, true, true));
    CHECK(!InputPowerPolicy::nextOutputEnable(false, true, true, true, false));
    CHECK(InputPowerPolicy::nextOutputEnable(false, false, true, true, true));
    CHECK(InputPowerPolicy::nextOutputEnable(false, true, true, false, false));
    CHECK(!InputPowerPolicy::nextOutputEnable(false, true, true, false, true));
    CHECK(!InputPowerPolicy::nextOutputEnable(true, true, false, true, false));
    CHECK(!InputPowerPolicy::nextOutputEnable(false, false, true, false, false, false));
    CHECK(!InputPowerPolicy::nextOutputEnable(true, true, true, true, false, false));
    CHECK(InputPowerPolicy::nextOutputEnable(false, false, true, false, false, true));
    CHECK(InputPowerPolicy::legacyTransitionTarget(80, 140) == 80);
    CHECK(InputPowerPolicy::legacyTransitionTarget(80, 60) == 60);
    CHECK(InputPowerPolicy::legacyTransitionTarget(0, 140) == 0);
    const auto low_voltage = InputPowerPolicy::capacitorPowerLimits(24, 4.5f, 0.8f, 22.5f);
    CHECK(std::fabs(low_voltage.charge_w - 18) < 0.001f);
    CHECK(low_voltage.discharge_w <= 0.3f);
    const auto normal_voltage = InputPowerPolicy::capacitorPowerLimits(24, 12, 0.8f, 22.5f);
    CHECK(std::fabs(normal_voltage.charge_w - 270) < 0.001f);
    CHECK(normal_voltage.discharge_w <= 172.81f);
    const auto no_voltage = InputPowerPolicy::capacitorPowerLimits(0, 0, 0, 22.5f);
    CHECK(no_voltage.charge_w == 0 && no_voltage.discharge_w == 0);
    const auto invalid_voltage = InputPowerPolicy::capacitorPowerLimits(24, NAN, 0.8f, 22.5f);
    CHECK(invalid_voltage.charge_w == 0 && invalid_voltage.discharge_w == 0);
    if (failures) return 1;
    std::puts("PASS: F334 input power policy tests");
    return 0;
}
