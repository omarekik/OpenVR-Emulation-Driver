#include "ControllerDevice.hpp"
#include <Windows.h>
#include <Xinput.h>

#pragma comment(lib, "xinput.lib")

namespace {
    constexpr DWORD kXInputControllerIndex = 0;
    constexpr float kTriggerClickThreshold = 0.75f;
    constexpr float kAimModeThreshold = 0.2f;
    constexpr float kControllerAimSpeed = 1.5f;
    constexpr float kMaxAimPitch = 3.14159f / 3.0f;
    constexpr float kMinHapticDurationSeconds = 0.02f;

    float NormalizeThumbAxis(SHORT value, SHORT deadzone)
    {
        if (value > deadzone) {
            return static_cast<float>(value - deadzone) / static_cast<float>(32767 - deadzone);
        }

        if (value < -deadzone) {
            return static_cast<float>(value + deadzone) / static_cast<float>(32768 - deadzone);
        }

        return 0.0f;
    }

    float NormalizeTrigger(BYTE value)
    {
        if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
            return 0.0f;
        }

        return static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    }

    float Clamp01(float value)
    {
        return std::fmax(0.0f, std::fmin(value, 1.0f));
    }

    struct SharedXInputRumbleState {
        float left_motor = 0.0f;
        float right_motor = 0.0f;
        std::chrono::steady_clock::time_point left_motor_end_time = {};
        std::chrono::steady_clock::time_point right_motor_end_time = {};
    };

    SharedXInputRumbleState g_xinput_rumble_state;

    void QueueXInputRumble(bool left_motor, float amplitude, float duration_seconds)
    {
        auto end_time = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(std::fmax(duration_seconds, kMinHapticDurationSeconds))
        );

        if (left_motor) {
            g_xinput_rumble_state.left_motor = Clamp01(amplitude);
            g_xinput_rumble_state.left_motor_end_time = end_time;
        }
        else {
            g_xinput_rumble_state.right_motor = Clamp01(amplitude);
            g_xinput_rumble_state.right_motor_end_time = end_time;
        }
    }

    void ApplyXInputRumble(bool has_xinput)
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= g_xinput_rumble_state.left_motor_end_time) {
            g_xinput_rumble_state.left_motor = 0.0f;
        }

        if (now >= g_xinput_rumble_state.right_motor_end_time) {
            g_xinput_rumble_state.right_motor = 0.0f;
        }

        if (!has_xinput) {
            return;
        }

        XINPUT_VIBRATION vibration = {};
        vibration.wLeftMotorSpeed = static_cast<WORD>(g_xinput_rumble_state.left_motor * 65535.0f);
        vibration.wRightMotorSpeed = static_cast<WORD>(g_xinput_rumble_state.right_motor * 65535.0f);
        XInputSetState(kXInputControllerIndex, &vibration);
    }
}

ExampleDriver::ControllerDevice::ControllerDevice(std::string serial, ControllerDevice::Handedness handedness):
    serial_(serial),
    handedness_(handedness),
    joystick_enabled_(handedness == Handedness::LEFT)
{
}

std::string ExampleDriver::ControllerDevice::GetSerial()
{
    return this->serial_;
}

void ExampleDriver::ControllerDevice::Update()
{
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;

    float delta_seconds = GetDriver()->GetLastFrameTime().count() / 1000.0f;

    // Check if this device was asked to be identified
    auto events = GetDriver()->GetOpenVREvents();
    for (auto event : events) {
        // Note here, event.trackedDeviceIndex does not necissarily equal this->device_index_, not sure why, but the component handle will match so we can just use that instead
        //if (event.trackedDeviceIndex == this->device_index_) {
        if (event.eventType == vr::EVREventType::VREvent_Input_HapticVibration) {
            if (event.data.hapticVibration.componentHandle == this->haptic_component_) {
                this->did_vibrate_ = true;
                if (this->handedness_ == Handedness::LEFT) {
                    QueueXInputRumble(true, event.data.hapticVibration.fAmplitude, event.data.hapticVibration.fDurationSeconds);
                }
                else if (this->handedness_ == Handedness::RIGHT) {
                    QueueXInputRumble(false, event.data.hapticVibration.fAmplitude, event.data.hapticVibration.fDurationSeconds);
                }
            }
        }
        //}
    }

    // Check if we need to keep vibrating
    if (this->did_vibrate_) {
        this->vibrate_anim_state_ += (GetDriver()->GetLastFrameTime().count()/1000.f);
        if (this->vibrate_anim_state_ > 1.0f) {
            this->did_vibrate_ = false;
            this->vibrate_anim_state_ = 0.0f;
        }
    }

    // Setup pose for this frame
    auto pose = IVRDevice::MakeDefaultPose();
    XINPUT_STATE xinput_state = {};
    bool has_xinput = XInputGetState(kXInputControllerIndex, &xinput_state) == ERROR_SUCCESS;
    bool is_left_controller = this->handedness_ == Handedness::LEFT;
    bool is_right_controller = this->handedness_ == Handedness::RIGHT;
    bool aim_mode_active = has_xinput && NormalizeTrigger(xinput_state.Gamepad.bLeftTrigger) > kAimModeThreshold;

    // Find a HMD
    auto devices = GetDriver()->GetDevices();
    auto hmd = std::find_if(devices.begin(), devices.end(), [](const std::shared_ptr<IVRDevice>& device_ptr) {return device_ptr->GetDeviceType() == DeviceType::HMD; });
    if (hmd != devices.end()) {
        // Found a HMD
        vr::DriverPose_t hmd_pose = (*hmd)->GetPose();

        // Here we setup some transforms so our controllers are offset from the headset by a small amount so we can see them
        linalg::vec<float, 3> hmd_position{ (float)hmd_pose.vecPosition[0], (float)hmd_pose.vecPosition[1], (float)hmd_pose.vecPosition[2] };
        linalg::vec<float, 4> hmd_rotation{ (float)hmd_pose.qRotation.x, (float)hmd_pose.qRotation.y, (float)hmd_pose.qRotation.z, (float)hmd_pose.qRotation.w };

        // Do shaking animation if haptic vibration was requested
        float controller_y = -0.2f + 0.01f * std::sinf(8 * 3.1415f * vibrate_anim_state_);

        // Left hand controller on the left, right hand controller on the right, any other handedness sticks to the middle
        float controller_x = this->handedness_ == Handedness::LEFT ? -0.2f : (this->handedness_ == Handedness::RIGHT ? 0.2f : 0.f);

        linalg::vec<float, 3> hmd_pose_offset = { controller_x, controller_y, -0.5f };

        hmd_pose_offset = linalg::qrot(hmd_rotation, hmd_pose_offset);

        linalg::vec<float, 3> final_pose = hmd_pose_offset + hmd_position;

        pose.vecPosition[0] = final_pose.x;
        pose.vecPosition[1] = final_pose.y;
        pose.vecPosition[2] = final_pose.z;

        linalg::vec<float, 4> controller_rotation = hmd_rotation;
        if (is_right_controller) {
            if (aim_mode_active && !this->joystick_enabled_) {
                this->aim_yaw_ -= NormalizeThumbAxis(xinput_state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) * kControllerAimSpeed * delta_seconds;
                this->aim_pitch_ += NormalizeThumbAxis(xinput_state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) * kControllerAimSpeed * delta_seconds;
                this->aim_pitch_ = std::fmax(this->aim_pitch_, -kMaxAimPitch);
                this->aim_pitch_ = std::fmin(this->aim_pitch_, kMaxAimPitch);
            }
            else {
                this->aim_yaw_ = 0.0f;
                this->aim_pitch_ = 0.0f;
            }

            linalg::vec<float, 4> aim_y_quat{ 0, std::sinf(this->aim_yaw_ / 2), 0, std::cosf(this->aim_yaw_ / 2) };
            linalg::vec<float, 4> aim_x_quat{ std::sinf(this->aim_pitch_ / 2), 0, 0, std::cosf(this->aim_pitch_ / 2) };
            linalg::vec<float, 4> aim_rotation = linalg::qmul(aim_y_quat, aim_x_quat);
            controller_rotation = linalg::qmul(hmd_rotation, aim_rotation);
        }
        else {
            this->aim_yaw_ = 0.0f;
            this->aim_pitch_ = 0.0f;
        }

        pose.qRotation.w = controller_rotation.w;
        pose.qRotation.x = controller_rotation.x;
        pose.qRotation.y = controller_rotation.y;
        pose.qRotation.z = controller_rotation.z;
    }

    auto update_button_state = [&](vr::VRInputComponentHandle_t click_component, vr::VRInputComponentHandle_t touch_component, bool pressed) {
        GetDriver()->GetInput()->UpdateBooleanComponent(click_component, pressed, 0);
        GetDriver()->GetInput()->UpdateBooleanComponent(touch_component, pressed, 0);
    };

    auto update_scalar_state = [&](vr::VRInputComponentHandle_t component, float value) {
        GetDriver()->GetInput()->UpdateScalarComponent(component, value, 0);
    };

    auto gamepad_button_pressed = [&](WORD button_mask) {
        return has_xinput && (xinput_state.Gamepad.wButtons & button_mask) != 0;
    };

    // Keyboard bindings:
    // Right controller: E = A, R = B
    // Left controller:  Q = X, F = Y
    // XInput bindings:
    // Right controller: A/B, RT trigger, RB grip, right stick, START system
    // Left controller:  X/Y, LT trigger, LB grip, left stick, BACK system
    update_button_state(
        this->a_button_click_component_,
        this->a_button_touch_component_,
        (is_right_controller && GetAsyncKeyState(0x45 /* E */) != 0) || (is_right_controller && gamepad_button_pressed(XINPUT_GAMEPAD_A))
    );
    update_button_state(
        this->b_button_click_component_,
        this->b_button_touch_component_,
        (is_right_controller && GetAsyncKeyState(0x52 /* R */) != 0) || (is_right_controller && gamepad_button_pressed(XINPUT_GAMEPAD_B))
    );
    update_button_state(
        this->x_button_click_component_,
        this->x_button_touch_component_,
        (is_left_controller && GetAsyncKeyState(0x51 /* Q */) != 0) || (is_left_controller && gamepad_button_pressed(XINPUT_GAMEPAD_X))
    );
    update_button_state(
        this->y_button_click_component_,
        this->y_button_touch_component_,
        (is_left_controller && GetAsyncKeyState(0x46 /* F */) != 0) || (is_left_controller && gamepad_button_pressed(XINPUT_GAMEPAD_Y))
    );

    // Toggle joystick control once per combo press.
    // Right controller: A+B toggles right stick as right VR joystick.
    // Left controller: X+Y toggles left stick between HMD movement and left VR joystick.
    bool joystick_toggle_pressed = is_right_controller
        && gamepad_button_pressed(XINPUT_GAMEPAD_A)
        && gamepad_button_pressed(XINPUT_GAMEPAD_B);
    if (is_left_controller) {
        joystick_toggle_pressed = gamepad_button_pressed(XINPUT_GAMEPAD_X)
            && gamepad_button_pressed(XINPUT_GAMEPAD_Y);
    }

    if (joystick_toggle_pressed && !this->joystick_toggle_was_pressed_) {
        if (is_left_controller || is_right_controller) {
            this->joystick_enabled_ = !this->joystick_enabled_;
            GetDriver()->Log(std::string(is_left_controller ? "Left" : "Right") + " controller joystick control "
                + (this->joystick_enabled_ ? "enabled" : "disabled"));
        }
    }
    this->joystick_toggle_was_pressed_ = joystick_toggle_pressed;

    float trigger_value = 0.0f;
    float grip_value = 0.0f;
    float joystick_x = 0.0f;
    float joystick_y = 0.0f;
    bool joystick_click = false;
    bool system_pressed = false;

    // Stick axes are consumed by HMD move/look and LT aim mode unless their joystick mode is enabled.
    if (has_xinput) {
        if (is_left_controller) {
            trigger_value = aim_mode_active ? 0.0f : NormalizeTrigger(xinput_state.Gamepad.bLeftTrigger);
            grip_value = gamepad_button_pressed(XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f;
            joystick_click = gamepad_button_pressed(XINPUT_GAMEPAD_LEFT_THUMB);
            system_pressed = gamepad_button_pressed(XINPUT_GAMEPAD_BACK);

            if (this->joystick_enabled_) {
                joystick_x = NormalizeThumbAxis(xinput_state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                joystick_y = NormalizeThumbAxis(xinput_state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            }
        }
        else if (is_right_controller) {
            trigger_value = NormalizeTrigger(xinput_state.Gamepad.bRightTrigger);
            grip_value = gamepad_button_pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f;
            joystick_click = gamepad_button_pressed(XINPUT_GAMEPAD_RIGHT_THUMB);
            system_pressed = gamepad_button_pressed(XINPUT_GAMEPAD_START);
            
            // Enable joystick control if activated
            if (this->joystick_enabled_) {
                joystick_x = NormalizeThumbAxis(xinput_state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
                joystick_y = NormalizeThumbAxis(xinput_state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            }
        }
    }

    GetDriver()->GetInput()->UpdateBooleanComponent(this->trigger_click_component_, trigger_value >= kTriggerClickThreshold, 0);
    GetDriver()->GetInput()->UpdateBooleanComponent(this->trigger_touch_component_, trigger_value > 0.0f, 0);
    update_scalar_state(this->trigger_value_component_, trigger_value);

    GetDriver()->GetInput()->UpdateBooleanComponent(this->grip_touch_component_, grip_value > 0.0f, 0);
    update_scalar_state(this->grip_value_component_, grip_value);
    update_scalar_state(this->grip_force_component_, grip_value);

    GetDriver()->GetInput()->UpdateBooleanComponent(this->system_click_component_, system_pressed, 0);
    GetDriver()->GetInput()->UpdateBooleanComponent(this->system_touch_component_, system_pressed, 0);

    GetDriver()->GetInput()->UpdateBooleanComponent(this->trackpad_click_component_, false, 0);
    GetDriver()->GetInput()->UpdateBooleanComponent(this->trackpad_touch_component_, false, 0);
    update_scalar_state(this->trackpad_x_component_, 0.0f);
    update_scalar_state(this->trackpad_y_component_, 0.0f);

    GetDriver()->GetInput()->UpdateBooleanComponent(this->joystick_click_component_, joystick_click, 0);
    GetDriver()->GetInput()->UpdateBooleanComponent(this->joystick_touch_component_, joystick_click || (std::abs(joystick_x) > 0.1f || std::abs(joystick_y) > 0.1f), 0);
    update_scalar_state(this->joystick_x_component_, joystick_x);
    update_scalar_state(this->joystick_y_component_, joystick_y);
    ApplyXInputRumble(has_xinput);

    // Post pose
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(this->device_index_, pose, sizeof(vr::DriverPose_t));
    this->last_pose_ = pose;
}

DeviceType ExampleDriver::ControllerDevice::GetDeviceType()
{
    return DeviceType::CONTROLLER;
}

ExampleDriver::ControllerDevice::Handedness ExampleDriver::ControllerDevice::GetHandedness()
{
    return this->handedness_;
}

vr::TrackedDeviceIndex_t ExampleDriver::ControllerDevice::GetDeviceIndex()
{
    return this->device_index_;
}

vr::EVRInitError ExampleDriver::ControllerDevice::Activate(uint32_t unObjectId)
{
    this->device_index_ = unObjectId;

    GetDriver()->Log("Activating controller " + this->serial_);

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

    // Setup inputs and outputs
    GetDriver()->GetInput()->CreateHapticComponent(props, "/output/haptic", &this->haptic_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/a/click", &this->a_button_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/a/touch", &this->a_button_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/b/click", &this->b_button_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/b/touch", &this->b_button_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/x/click", &this->x_button_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/x/touch", &this->x_button_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/y/click", &this->y_button_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/y/touch", &this->y_button_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trigger/click", &this->trigger_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trigger/touch", &this->trigger_touch_component_);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/trigger/value", &this->trigger_value_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedOneSided);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/grip/touch", &this->grip_touch_component_);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/grip/value", &this->grip_value_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedOneSided);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/grip/force", &this->grip_force_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedOneSided);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/system/click", &this->system_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/system/touch", &this->system_touch_component_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trackpad/click", &this->trackpad_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/trackpad/touch", &this->trackpad_touch_component_); 
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/trackpad/x", &this->trackpad_x_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/trackpad/y", &this->trackpad_y_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);
    
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/joystick/click", &this->joystick_click_component_);
    GetDriver()->GetInput()->CreateBooleanComponent(props, "/input/joystick/touch", &this->joystick_touch_component_);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/joystick/x", &this->joystick_x_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);
    GetDriver()->GetInput()->CreateScalarComponent(props, "/input/joystick/y", &this->joystick_y_component_, vr::EVRScalarType::VRScalarType_Absolute, vr::EVRScalarUnits::VRScalarUnits_NormalizedTwoSided);

    // Set some universe ID (Must be 2 or higher)
    GetDriver()->GetProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 2);
    
    // Set up a model "number" (not needed but good to have)
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "example_controller");

    // Use SteamVR's built-in Oculus Touch Plus render models when available locally.
    std::string render_model_name = this->handedness_ == Handedness::LEFT
        ? "oculus_quest_plus_controller_left"
        : "oculus_quest_plus_controller_right";
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, render_model_name.c_str());

    // Give SteamVR a hint at what hand this controller is for
    if (this->handedness_ == Handedness::LEFT) {
        GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
    }
    else if (this->handedness_ == Handedness::RIGHT) {
        GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
    }
    else {
        GetDriver()->GetProperties()->SetInt32Property(props, vr::Prop_ControllerRoleHint_Int32, vr::ETrackedControllerRole::TrackedControllerRole_OptOut);
    }

    // Set controller profile
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_InputProfilePath_String, "{example}/input/example_controller_bindings.json");

    // Change the icon depending on which handedness this controller is using (ANY uses right)
    std::string controller_handedness_str = this->handedness_ == Handedness::LEFT ? "left" : "right";
    std::string controller_ready_file = "{example}/icons/controller_ready_" + controller_handedness_str + ".png";
    std::string controller_not_ready_file = "{example}/icons/controller_not_ready_" + controller_handedness_str + ".png";

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, controller_ready_file.c_str());

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, controller_not_ready_file.c_str());
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, controller_not_ready_file.c_str());

    return vr::EVRInitError::VRInitError_None;
}

void ExampleDriver::ControllerDevice::Deactivate()
{
    this->device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void ExampleDriver::ControllerDevice::EnterStandby()
{
}

void* ExampleDriver::ControllerDevice::GetComponent(const char* pchComponentNameAndVersion)
{
    return nullptr;
}

void ExampleDriver::ControllerDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t ExampleDriver::ControllerDevice::GetPose()
{
    return last_pose_;
}

bool ExampleDriver::ControllerDevice::IsJoystickEnabled() const
{
    return this->joystick_enabled_;
}
