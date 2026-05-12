#include "TrackingReferenceDevice.hpp"
#include "InputMath.hpp"
#include <Windows.h>
#include <numbers>

using namespace DirectX;

namespace OpenVREmulatorDriver {

TrackingReferenceDevice::TrackingReferenceDevice(std::string serial):
    serial_(serial)
{

    // Get some random angle to place this tracking reference at in the scene
    this->random_angle_rad_ = fmod(rand() / 10000.f, 2 * std::numbers::pi_v<float>);
}

std::string TrackingReferenceDevice::GetSerial()
{
    return this->serial_;
}

void TrackingReferenceDevice::Update()
{
    if (this->device_index_ == vr::k_unTrackedDeviceIndexInvalid)
        return;


    // Setup pose for this frame
    auto pose = IVRDevice::MakeDefaultPose();

    XMFLOAT3 device_position_f{ 0.f, 1.f, 1.f };

    XMFLOAT4 y_quat_f{ 0, std::sinf(this->random_angle_rad_ / 2), 0, std::cosf(this->random_angle_rad_ / 2) }; // Point inwards (z- is forward)

    XMFLOAT4 x_look_down_f{ std::sinf((-std::numbers::pi_v<float>/4) / 2), 0, 0, std::cosf((-std::numbers::pi_v<float> / 4) / 2) }; // Tilt downwards to look at the centre

    XMFLOAT4 device_rotation_f;
    XMStoreFloat4(&device_rotation_f, XMQuaternionMultiply(XMLoadFloat4(&y_quat_f), XMLoadFloat4(&x_look_down_f)));

    XMFLOAT3 rotated_pos;
    XMStoreFloat3(&rotated_pos, XMVector3Rotate(XMLoadFloat3(&device_position_f), XMLoadFloat4(&y_quat_f)));

    pose.vecPosition[0] = rotated_pos.x;
    pose.vecPosition[1] = rotated_pos.y;
    pose.vecPosition[2] = rotated_pos.z;

    pose.qRotation.w = device_rotation_f.w;
    pose.qRotation.x = device_rotation_f.x;
    pose.qRotation.y = device_rotation_f.y;
    pose.qRotation.z = device_rotation_f.z;

    // Post pose
    GetDriver()->GetDriverHost()->TrackedDevicePoseUpdated(this->device_index_, pose, sizeof(vr::DriverPose_t));
    this->last_pose_ = pose;
}

DeviceType TrackingReferenceDevice::GetDeviceType()
{
    return DeviceType::TRACKING_REFERENCE;
}

vr::TrackedDeviceIndex_t TrackingReferenceDevice::GetDeviceIndex()
{
    return this->device_index_;
}

vr::EVRInitError TrackingReferenceDevice::Activate(uint32_t unObjectId)
{
    this->device_index_ = unObjectId;

    GetDriver()->Log("Activating tracking reference " + this->serial_);

    // Get the properties handle
    auto props = GetDriver()->GetProperties()->TrackedDeviceToPropertyContainer(this->device_index_);

    // Set some universe ID (Must be 2 or higher)
    GetDriver()->GetProperties()->SetUint64Property(props, vr::Prop_CurrentUniverseId_Uint64, 2);
    
    // Set up a model "number" (not needed but good to have)
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_ModelNumber_String, "openvr-emulator_trackingreference");

    // Set up a render model path
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_RenderModelName_String, "locator");

    // Set the icons
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReady_String, "{openvr-emulator}/icons/trackingreference_ready.png");

    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceOff_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearching_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceNotReady_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceStandby_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");
    GetDriver()->GetProperties()->SetStringProperty(props, vr::Prop_NamedIconPathDeviceAlertLow_String, "{openvr-emulator}/icons/trackingreference_not_ready.png");

    return vr::EVRInitError::VRInitError_None;
}

void TrackingReferenceDevice::Deactivate()
{
    this->device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void TrackingReferenceDevice::EnterStandby()
{
}

void* TrackingReferenceDevice::GetComponent(const char* /*pchComponentNameAndVersion*/)
{
    return nullptr;
}

void TrackingReferenceDevice::DebugRequest(const char* /*pchRequest*/, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t TrackingReferenceDevice::GetPose()
{
    return last_pose_;
}

} // namespace OpenVREmulatorDriver

