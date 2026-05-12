#pragma once

#include <Windows.h>
#include <Xinput.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace OpenVREmulatorDriver {

// ---------------------------------------------------------------------------
// XInput normalisation helpers
// ---------------------------------------------------------------------------

// Normalize a thumb-stick axis value to [-1, 1], applying a dead-zone.
// Returns 0 when |value| <= deadzone.
inline float NormalizeThumbAxis(SHORT value, SHORT deadzone) noexcept
{
    if (value > deadzone)
        return static_cast<float>(value - deadzone) / static_cast<float>(32767 - deadzone);
    if (value < -deadzone)
        return static_cast<float>(value + deadzone) / static_cast<float>(32768 - deadzone);
    return 0.0f;
}

// Normalize a trigger byte value to [0, 1], applying the XInput trigger threshold.
// Returns 0 when value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD.
inline float NormalizeTrigger(BYTE value) noexcept
{
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
        return 0.0f;
    return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
         / static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}

// Clamp a float value to [0, 1].
inline float Clamp01(float value) noexcept
{
    return std::fmax(0.0f, std::fmin(value, 1.0f));
}

} // namespace OpenVREmulatorDriver
