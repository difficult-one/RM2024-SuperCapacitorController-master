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
    if (failures) return 1;
    std::puts("PASS: F334 input power policy tests");
    return 0;
}
