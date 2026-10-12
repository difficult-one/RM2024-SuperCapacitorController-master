#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace InputPowerPolicy
{
constexpr uint8_t commandTag = 0xA5;
constexpr uint8_t feedbackVersion = 1;
constexpr uint32_t commandTimeoutMs = 100;

inline bool feedbackDue(uint32_t now, uint32_t last)
{
    return static_cast<uint32_t>(now - last) >= 5;
}

inline bool commandFresh(bool received, uint32_t now, uint32_t last)
{
    return received && static_cast<uint32_t>(now - last) <= commandTimeoutMs;
}

inline bool nextOutputEnable(bool current, bool lastRequested, bool requested,
                             bool sourceWasFresh, bool hardwareFault)
{
    if (!requested)
        return false;
    if (!lastRequested)
        return true; // An explicit disable -> enable transition requests retry.
    if (!sourceWasFresh)
        return !hardwareFault; // Recover communication loss only when healthy.
    return current; // Repeated enable frames preserve a hardware fault latch.
}

struct Command
{
    bool enabled = false;
    bool restart = false;
    bool extended = false;
    float referee_limit_w = 0;
    float buffer_energy_j = 0;
    float target_input_w = 0;
};

inline uint16_t readU16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0] | static_cast<uint16_t>(data[1]) << 8);
}

inline bool finite(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000U) != 0x7F800000U;
}

inline float boundTarget(float referee, float energy, float requested)
{
    if (!finite(referee) || !finite(energy) || !finite(requested) || referee <= 0)
        return 0;
    const float extra = std::fmin(60.0f, std::fmax(0.0f, (energy - 15.0f) / 0.5f));
    const float ceiling = energy <= 5.0f ? std::fmax(0.0f, referee - 20.0f) : referee + extra;
    return std::fmax(0.0f, std::fmin(requested, ceiling));
}

inline bool decode(const uint8_t *data, uint8_t length, Command &result)
{
    if (data == nullptr || length != 8 || (data[0] & 0xFCU) != 0 ||
        (data[7] != 0 && data[7] != commandTag))
        return false;
    Command decoded;
    decoded.enabled = (data[0] & 1U) != 0;
    decoded.restart = (data[0] & 2U) != 0;
    decoded.referee_limit_w = readU16(data + 1);
    decoded.buffer_energy_j = readU16(data + 3);
    if (decoded.enabled && (decoded.referee_limit_w <= 0 || decoded.referee_limit_w > 250))
        return false;
    decoded.extended = decoded.enabled && data[7] == commandTag;
    decoded.target_input_w = decoded.extended ? boundTarget(decoded.referee_limit_w,
        decoded.buffer_energy_j, readU16(data + 5)) : decoded.referee_limit_w;
    result = decoded;
    return true;
}

inline bool encodeFeedback(float measured, float target, bool active, uint8_t *data)
{
    if (data == nullptr || !finite(measured) || !finite(target))
        return false;
    std::memcpy(data, &measured, sizeof(measured));
    const uint16_t value = static_cast<uint16_t>(std::fmin(65535.0f, std::fmax(0.0f, target)));
    data[4] = static_cast<uint8_t>(value);
    data[5] = static_cast<uint8_t>(value >> 8);
    data[6] = feedbackVersion;
    data[7] = active ? 1 : 0;
    return true;
}
} // namespace InputPowerPolicy
