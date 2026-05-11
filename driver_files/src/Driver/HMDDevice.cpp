#include "HMDDevice.hpp"
#include "ControllerDevice.hpp"
#include <Windows.h>
#include <Xinput.h>
#include <numbers>

namespace {
    constexpr DWORD kXInputControllerIndex = 0;
    constexpr float kSecondsFromVsyncToPhotons = 0.011f;

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

}

ExampleDriver::HMDDevice::HMDDevice(std::string serial, InputConfig config)
    : serial_(serial), config_(std::move(config))
{
}

std::string ExampleDriver::HMDDevice::GetSerial()
{
    return this->serial_;
}

void ExampleDriver::HMDDevice::Update()
{
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;
    
    // Setup pose for this frame
    auto pose = IVRDevice::MakeDefaultPose();

    float delta_seconds = GetDriver()->GetLastFrameTime().count() / 1000.0f;
    const auto& hmd_cfg = config_.hmd;

    bool space_down = (GetAsyncKeyState(hmd_cfg.key_mouse_toggle) & 0x8000) != 0;
    if (space_down && !this->space_was_down_) {
        this->mouse_emulation_enabled_ = !this->mouse_emulation_enabled_;
        GetDriver()->Log(std::string("Mouse emulation ") + (this->mouse_emulation_enabled_ ? "enabled" : "disabled"));
    }
    this->space_was_down_ = space_down;

    // Get orientation
    POINT current_mouse_pos;
    if (GetCursorPos(&current_mouse_pos)) {
        if (this->mouse_emulation_enabled_ && this->mouse_pos_valid_) {
            this->rot_y_ -= (current_mouse_pos.x - this->last_mouse_x_) * hmd_cfg.mouse_sensitivity;
            this->rot_x_ -= (current_mouse_pos.y - this->last_mouse_y_) * hmd_cfg.mouse_sensitivity;
        }
        this->last_mouse_x_ = current_mouse_pos.x;
        this->last_mouse_y_ = current_mouse_pos.y;
        this->mouse_pos_valid_ = true;
    }

    XINPUT_STATE xinput_state = {};
    bool has_xinput = XInputGetState(kXInputControllerIndex, &xinput_state) == ERROR_SUCCESS;

    // Left stick → HMD look (yaw / pitch)
    if (has_xinput) {
        this->rot_y_ -= NormalizeThumbAxis(xinput_state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) * hmd_cfg.look_speed * delta_seconds;
        this->rot_x_ += NormalizeThumbAxis(xinput_state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) * hmd_cfg.look_speed * delta_seconds;
    }

    this->rot_x_ = std::fmax(this->rot_x_, -std::numbers::pi_v<float> / 2);
    this->rot_x_ = std::fmin(this->rot_x_,  std::numbers::pi_v<float> / 2);

    linalg::vec<float, 4> y_quat{ 0, std::sinf(this->rot_y_ / 2), 0, std::cosf(this->rot_y_ / 2) };
    linalg::vec<float, 4> x_quat{ std::sinf(this->rot_x_ / 2), 0, 0, std::cosf(this->rot_x_ / 2) };
    linalg::vec<float, 4> pose_rot = linalg::qmul(y_quat, x_quat);

    pose.qRotation.w = (float) pose_rot.w;
    pose.qRotation.x = (float) pose_rot.x;
    pose.qRotation.y = (float) pose_rot.y;
    pose.qRotation.z = (float) pose_rot.z;

    // Left trigger alone → move forward; left trigger + LB → move backward
    if (has_xinput) {
        float lt_value = NormalizeTrigger(xinput_state.Gamepad.bLeftTrigger);
        if (lt_value > 0.0f) {
            bool lb_pressed = (xinput_state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            float sign = lb_pressed ? 1.0f : -1.0f;
            linalg::vec<float, 3> move_dir{0, 0, sign * lt_value * hmd_cfg.move_speed * delta_seconds};
            move_dir = linalg::qrot(pose_rot, move_dir);
            this->pos_x_ += move_dir.x;
            this->pos_y_ += move_dir.y;
            this->pos_z_ += move_dir.z;
        }
    }

    pose.vecPosition[0] = (float) this->pos_x_;
    pose.vecPosition[1] = (float) this->pos_y_;
    pose.vecPosition[2] = (float) this->pos_z_;

    // Report the emulated HMD as being worn so SteamVR doesn't sleep it.
    if (this->proximity_component_ != 0) {
        GetDriver()->GetInput()->UpdateBooleanComponent(this->proximity_component_, true, 0);
    }

    // Post pose
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(this->device_index_, pose, sizeof(vr::DriverPose_t));
    this->last_pose_ = pose;
}

DeviceType ExampleDriver::HMDDevice::GetDeviceType()
{
    return DeviceType::HMD;
}

vr::TrackedDeviceIndex_t ExampleDriver::HMDDevice::GetDeviceIndex()
{
    return this->device_index_;
}

vr::EVRInitError ExampleDriver::HMDDevice::Activate(uint32_t unObjectId)
{
    this->device_index_ = unObjectId;
    POINT current_mouse_pos;
    this->mouse_pos_valid_ = GetCursorPos(&current_mouse_pos) == TRUE;
    if (this->mouse_pos_valid_) {
        this->last_mouse_x_ = current_mouse_pos.x;
        this->last_mouse_y_ = current_mouse_pos.y;
    }

    GetDriver()->Log("Activating HMD " + this->serial_);

    // Load settings values
    // Could probably make this cleaner with making a wrapper class
    try {
        int window_x = std::get<int>(GetDriver()->GetSettingsValue("window_x"));
        if (window_x > 0)
            this->window_x_ = window_x;
    }
    catch (const std::bad_variant_access&) {}; // Wrong type or doesnt exist

    try {
        int window_y = std::get<int>(GetDriver()->GetSettingsValue("window_y"));
        if (window_y > 0)
            this->window_y_ = window_y;
    }
    catch (const std::bad_variant_access&) {}; // Wrong type or doesnt exist

    try {
        int window_width = std::get<int>(GetDriver()->GetSettingsValue("window_width"));
        if (window_width > 0)
            this->window_width_ = window_width;
    }
    catch (const std::bad_variant_access&) {}; // Wrong type or doesnt exist

    try {
        int window_height = std::get<int>(GetDriver()->GetSettingsValue("window_height"));
        if (window_height > 0)
            this->window_height_ = window_height;
    }
    catch (const std::bad_variant_access&) {}; // Wrong type or doesnt exist

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

    GetDriver()->GetInput()->CreateBooleanComponent(props, "/proximity", &this->proximity_component_);

    // Set some universe ID (Must be 2 or higher)
    GetDriver()->GetProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 2);

    // Set the IPD to be whatever steam has configured
    GetDriver()->GetProperties()->SetFloatProperty(props, vr::Prop_UserIpdMeters_Float, vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float));
    GetDriver()->GetProperties()->SetFloatProperty(props, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.0f);

    // Set the display FPS
    GetDriver()->GetProperties()->SetFloatProperty(props, vr::Prop_DisplayFrequency_Float, 90.f);
    GetDriver()->GetProperties()->SetFloatProperty(props, vr::Prop_SecondsFromVsyncToPhotons_Float, kSecondsFromVsyncToPhotons);
    
    // Set up a model "number" (not needed but good to have)
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "EXAMPLE_HMD_DEVICE");

    // Set up icon paths
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, "{example}/icons/hmd_ready.png");

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, "{example}/icons/hmd_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, "{example}/icons/hmd_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{example}/icons/hmd_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{example}/icons/hmd_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, "{example}/icons/hmd_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, "{example}/icons/hmd_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, "{example}/icons/hmd_not_ready.png");

    GetDriver()->GetProperties()->SetBoolProperty(props, vr::Prop_HasDisplayComponent_Bool, true);
    GetDriver()->GetProperties()->SetBoolProperty(props, vr::Prop_DeviceCanPowerOff_Bool, false);
    GetDriver()->GetProperties()->SetBoolProperty(props, vr::Prop_IsOnDesktop_Bool, false);
    GetDriver()->GetProperties()->SetBoolProperty(props, vr::Prop_DisplayDebugMode_Bool, true);

    return vr::EVRInitError::VRInitError_None;
}

void ExampleDriver::HMDDevice::Deactivate()
{
    this->device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void ExampleDriver::HMDDevice::EnterStandby()
{
}

void* ExampleDriver::HMDDevice::GetComponent(const char* pchComponentNameAndVersion)
{
    if (!_stricmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version)) {
        return static_cast<vr::IVRDisplayComponent*>(this);
    }
    return nullptr;
}

void ExampleDriver::HMDDevice::DebugRequest(const char* /*pchRequest*/, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t ExampleDriver::HMDDevice::GetPose()
{
    return this->last_pose_;
}

void ExampleDriver::HMDDevice::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnX = this->window_x_;
    *pnY = this->window_y_;
    *pnWidth = this->window_width_;
    *pnHeight = this->window_height_;
}

bool ExampleDriver::HMDDevice::IsDisplayOnDesktop()
{
    return true;
}

bool ExampleDriver::HMDDevice::IsDisplayRealDisplay()
{
    return false;
}

void ExampleDriver::HMDDevice::GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnWidth = this->window_width_ / 2;
    *pnHeight = this->window_height_;
}

void ExampleDriver::HMDDevice::GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    const uint32_t eye_width = this->window_width_ / 2;

    *pnY = 0;
    *pnWidth = eye_width;
    *pnHeight = this->window_height_;

    if (eEye == vr::EVREye::Eye_Left) {
        *pnX = 0;
    }
    else {
        *pnX = eye_width;
    }
}

void ExampleDriver::HMDDevice::GetProjectionRaw(vr::EVREye /*eEye*/, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom)
{
    const float eye_aspect = static_cast<float>(this->window_width_ / 2) / static_cast<float>(this->window_height_);

    *pfLeft = -eye_aspect;
    *pfRight = eye_aspect;
    *pfTop = -1;
    *pfBottom = 1;
}

vr::DistortionCoordinates_t ExampleDriver::HMDDevice::ComputeDistortion(vr::EVREye /*eEye*/, float fU, float fV)
{
    vr::DistortionCoordinates_t coordinates;
    coordinates.rfBlue[0] = fU;
    coordinates.rfBlue[1] = fV;
    coordinates.rfGreen[0] = fU;
    coordinates.rfGreen[1] = fV;
    coordinates.rfRed[0] = fU;
    coordinates.rfRed[1] = fV;
    return coordinates;
}
