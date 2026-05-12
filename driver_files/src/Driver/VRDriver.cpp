#include "VRDriver.hpp"
#include <Driver/HMDDevice.hpp>
#include <Driver/TrackerDevice.hpp>
#include <Driver/ControllerDevice.hpp>
#include <Driver/TrackingReferenceDevice.hpp>
#include <Driver/InputConfig.hpp>

vr::EVRInitError OpenVREmulatorDriver::VRDriver::Init(vr::IVRDriverContext* pDriverContext)
{
    // Perform driver context initialisation
    if (vr::EVRInitError init_error = vr::InitServerDriverContext(pDriverContext); init_error != vr::EVRInitError::VRInitError_None) {
        return init_error;
    }

    Log("Activating OpenVR Emulator Driver...");

    // Load input mapping from resources/input_mapping.ini (falls back to defaults if absent)
    auto config = InputConfig::LoadFromDriverRoot();
    Log("Input mapping loaded from resources/input_mapping.ini");

    // Add a HMD
    this->AddDevice(std::make_shared<HMDDevice>("OpenVREmulator_HMDDevice", config));

    // Add a couple controllers
    this->AddDevice(std::make_shared<ControllerDevice>("OpenVREmulator_ControllerDevice_Left",  ControllerDevice::Handedness::LEFT,  config));
    this->AddDevice(std::make_shared<ControllerDevice>("OpenVREmulator_ControllerDevice_Right", ControllerDevice::Handedness::RIGHT, config));

    // Add a tracker
    this->AddDevice(std::make_shared<TrackerDevice>("OpenVREmulator_TrackerDevice"));

    // Add a couple tracking references
    this->AddDevice(std::make_shared<TrackingReferenceDevice>("OpenVREmulator_TrackingReference_A"));
    this->AddDevice(std::make_shared<TrackingReferenceDevice>("OpenVREmulator_TrackingReference_B"));

    Log("OpenVR Emulator Driver Loaded Successfully");

	return vr::VRInitError_None;
}

void OpenVREmulatorDriver::VRDriver::Cleanup()
{
}

void OpenVREmulatorDriver::VRDriver::RunFrame()
{
    // Collect events
    vr::VREvent_t event;
    std::vector<vr::VREvent_t> events;
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event)))
    {
        events.push_back(event);
    }
    this->openvr_events_ = events;

    // Update frame timing
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    this->frame_timing_ = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_frame_time_);
    this->last_frame_time_ = now;

    // Update devices
    for (auto& device : this->devices_)
        device->Update();
}

bool OpenVREmulatorDriver::VRDriver::ShouldBlockStandbyMode()
{
    // Emulated devices do not have a physical wake signal, so going into
    // standby makes some games treat the HMD like it disappeared.
    return true;
}

void OpenVREmulatorDriver::VRDriver::EnterStandby()
{
}

void OpenVREmulatorDriver::VRDriver::LeaveStandby()
{
}

std::vector<std::shared_ptr<OpenVREmulatorDriver::IVRDevice>> OpenVREmulatorDriver::VRDriver::GetDevices()
{
    return this->devices_;
}

std::vector<vr::VREvent_t> OpenVREmulatorDriver::VRDriver::GetOpenVREvents()
{
    return this->openvr_events_;
}

std::chrono::milliseconds OpenVREmulatorDriver::VRDriver::GetLastFrameTime()
{
    return this->frame_timing_;
}

bool OpenVREmulatorDriver::VRDriver::AddDevice(std::shared_ptr<IVRDevice> device)
{
    vr::ETrackedDeviceClass openvr_device_class;
    // Remember to update this switch when new device types are added
    switch (device->GetDeviceType()) {
        case DeviceType::CONTROLLER:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_Controller;
            break;
        case DeviceType::HMD:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_HMD;
            break;
        case DeviceType::TRACKER:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_GenericTracker;
            break;
        case DeviceType::TRACKING_REFERENCE:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_TrackingReference;
            break;
        default:
            return false;
    }
    bool result = vr::VRServerDriverHost()->TrackedDeviceAdded(device->GetSerial().c_str(), openvr_device_class, device.get());
    if(result)
        this->devices_.push_back(device);
    return result;
}

OpenVREmulatorDriver::SettingsValue OpenVREmulatorDriver::VRDriver::GetSettingsValue(std::string key)
{
    vr::EVRSettingsError err = vr::EVRSettingsError::VRSettingsError_None;
    int int_value = vr::VRSettings()->GetInt32(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return int_value;
    }
    err = vr::EVRSettingsError::VRSettingsError_None;
    float float_value = vr::VRSettings()->GetFloat(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return float_value;
    }
    err = vr::EVRSettingsError::VRSettingsError_None;
    bool bool_value = vr::VRSettings()->GetBool(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return bool_value;
    }
    std::string str_value;
    str_value.reserve(1024);
    vr::VRSettings()->GetString(settings_key_.c_str(), key.c_str(), str_value.data(), 1024, &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return str_value;
    }
    err = vr::EVRSettingsError::VRSettingsError_None;

    return SettingsValue();
}

void OpenVREmulatorDriver::VRDriver::Log(std::string message)
{
    std::string message_endl = message + "\n";
    vr::VRDriverLog()->Log(message_endl.c_str());
}

vr::IVRDriverInput* OpenVREmulatorDriver::VRDriver::GetInput()
{
    return vr::VRDriverInput();
}

vr::CVRPropertyHelpers* OpenVREmulatorDriver::VRDriver::GetProperties()
{
    return vr::VRProperties();
}

vr::IVRServerDriverHost* OpenVREmulatorDriver::VRDriver::GetDriverHost()
{
    return vr::VRServerDriverHost();
}

