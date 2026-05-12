#pragma once

#include <vector>
#include <memory>

#include <openvr_driver.h>

#include <Driver/IVRDriver.hpp>
#include <Driver/IVRDevice.hpp>

namespace OpenVREmulatorDriver {
    class VRDriver : public IVRDriver {
    public:


        // Inherited via IVRDriver
        std::vector<std::shared_ptr<IVRDevice>> GetDevices() override;
        std::vector<vr::VREvent_t> GetOpenVREvents() override;
        std::chrono::milliseconds GetLastFrameTime() override;
        bool AddDevice(std::shared_ptr<IVRDevice> device) override;
        SettingsValue GetSettingsValue(std::string key) override;
        void Log(std::string message) override;

        vr::IVRDriverInput* GetInput() override;
        vr::CVRPropertyHelpers* GetProperties() override;
        vr::IVRServerDriverHost* GetDriverHost() override;

        // Inherited via IServerTrackedDeviceProvider
        vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
        void Cleanup() override;
        void RunFrame() override;
        bool ShouldBlockStandbyMode() override;
        void EnterStandby() override;
        void LeaveStandby() override;
        ~VRDriver() = default;

    private:
        std::vector<std::shared_ptr<IVRDevice>> devices_;
        std::vector<vr::VREvent_t> openvr_events_;
        std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
        std::chrono::system_clock::time_point last_frame_time_ = std::chrono::system_clock::now();
        std::string settings_key_ = "driver_example";

    };
}; // namespace OpenVREmulatorDriver
