#pragma once

#include <chrono>
#include <cmath>

#include <linalg.h>

#include <Driver/IVRDevice.hpp>
#include <Native/DriverFactory.hpp>

namespace ExampleDriver {
    class TrackingReferenceDevice : public IVRDevice {
        public:

            explicit TrackingReferenceDevice(std::string serial);
            ~TrackingReferenceDevice() = default;

            // Inherited via IVRDevice
            std::string GetSerial() override;
            void Update() override;
            vr::TrackedDeviceIndex_t GetDeviceIndex() override;
            DeviceType GetDeviceType() override;

            vr::EVRInitError Activate(uint32_t unObjectId) override;
            void Deactivate() override;
            void EnterStandby() override;
            void* GetComponent(const char* pchComponentNameAndVersion) override;
            void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
            vr::DriverPose_t GetPose() override;

    private:
        vr::TrackedDeviceIndex_t device_index_ = vr::k_unTrackedDeviceIndexInvalid;
        std::string serial_;

        vr::DriverPose_t last_pose_ = IVRDevice::MakeDefaultPose();

        float random_angle_rad_;

    };
};