# OpenVR-Emulation-Driver

A fork of [OpenVR-Emulation-Driver by Nmzik](https://github.com/Nmzik/OpenVR-Emulation-Driver) (itself originally based on [Simple-OpenVR-Driver-Tutorial](https://github.com/terminal29/Simple-OpenVR-Driver-Tutorial)) focused on VR emulation when no physical headset is available — for development, testing, and input experimentation.

> **Windows only.** The driver relies on Win32 APIs (`Windows.h`, `XInput`, `GetAsyncKeyState`, `GetModuleHandleEx`) and the SteamVR runtime, which only runs on Windows. Linux/macOS are not supported.

This fork: **https://github.com/omarekik/OpenVR-Emulation-Driver**

Tested with the SteamVR Demo app.

![SteamVR Demo running with emulated driver](images/steamvr-demo.png)

## What this fork adds

- **Configurable input mapping** via [`resources/input_mapping.ini`](driver_files/driver/openvr-emulator/resources/input_mapping.ini) — remap gamepad buttons, thresholds, and speeds without recompiling
- XInput gamepad support for HMD look/move and full controller input
- OpenXR-compatible controller poses and bindings
- A/B/X/Y button mapping for left and right controllers
- Emulated HMD proximity reporting so SteamVR treats the headset as worn
- Visual Studio debugger pre-configured to launch `vrstartup.exe` (set by CMake)
- A standalone [`preview_app`](preview_app/README.md) that mirrors a single SteamVR compositor eye into a desktop window

## Building

**Prerequisites:** Python 3, Visual Studio 2022 with the "Desktop development with C++" workload (includes the Windows SDK and MSVC).

### First-time setup

Run the bootstrap script once after cloning (or whenever `conanfile.py` changes). It creates a Python virtual environment, installs Conan, fetches all dependencies, and configures CMake:

```powershell
git clone --recursive https://github.com/omarekik/OpenVR-Emulation-Driver.git
cd OpenVR-Emulation-Driver
.\scripts\bootstrap.ps1
```

Add `-Release` to also configure the Release preset:

```powershell
.\scripts\bootstrap.ps1 -Release
```

Open `build\OpenVR_Emulation_Driver.sln` in Visual Studio and build.

### Subsequent builds

After the first bootstrap, use Visual Studio normally or:

```powershell
cmake --build build --config Debug
```

No need to re-run the bootstrap unless `conanfile.py` changes.

## Running tests

Unit tests are built as the `driver_tests` target (GTest/GMock). Two ways to run them from Visual Studio:

**Option 1 — RUN_TESTS target** (quick pass/fail)

In Solution Explorer, expand **CMakePredefinedTargets** → right-click **RUN_TESTS** → **Build**. CTest output appears in the Build Output pane.

**Option 2 — Test Explorer** (recommended)

Open **View → Test Explorer**. Visual Studio reads the test list discovered at post-build and shows each `TEST(suite, name)` individually. You can run all tests, re-run failures, or right-click a single test to debug it with a breakpoint.

> The test list is populated by a post-build step that runs `driver_tests.exe --gtest_list_tests`. Build `driver_tests` at least once before opening Test Explorer.

## Code style & static analysis

Both tools are installed into the project's Python virtual environment by the bootstrap script (`pip install clang-format clang-tidy`). No separate LLVM installation is required.

### clang-format

All C++ sources are formatted with **clang-format** using the Microsoft base style (see [`.clang-format`](.clang-format) for the full configuration — only meaningful deviations from the Microsoft defaults are listed there).

Format everything in-place:

```powershell
.\scripts\format_code.ps1
```

CI runs the same script and fails the pipeline if any file would be changed, so format before pushing.

### clang-tidy

Static analysis uses **clang-tidy** with the check set and naming conventions defined in [`.clang-tidy`](.clang-tidy). Naming follows Microsoft C++ conventions (`PascalCase` methods/types, `m_camelCase` members, `camelCase` locals).

**Locally (Visual Studio):** analysis is registered as an on-demand analyser for each target. Run it via:

> **Build → Run Code Analysis on Solution**

Findings appear in the **Code Analysis Results** window and **Error List**. Normal builds are not affected.

**CI (Ninja):** clang-tidy runs automatically on every translation unit during the build via `CXX_CLANG_TIDY`. All warnings are treated as errors (`WarningsAsErrors: "*"`).

The `ENABLE_CLANG_TIDY` CMake option (default `ON` in all presets) controls whether the integration is wired up at configure time.



> **If you build with Visual Studio (or `cmake --build`) running as Administrator**, the post-build step deploys the driver automatically to:
> ```
> C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\openvr-emulator
> ```
> Any previously installed version is backed up as `openvr-emulator.bak` before being replaced. No manual installation is needed.

If you prefer not to run as Administrator, choose one of the manual methods below:

**Option A — copy into SteamVR drivers:**
Copy the built `openvr-emulator` folder into:
```
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\
```

**Option B — register via `openvrpaths.vrpath`:**
Open `C:\Users\<Username>\AppData\Local\openvr\openvrpaths.vrpath` and add the path to the built `openvr-emulator` folder under `"external_drivers"`:

```json
{
    "external_drivers": [
        "C:\\path\\to\\build\\Debug\\openvr-emulator"
    ]
}
```

## Controls

All mappings are configurable in [`resources/input_mapping.ini`](driver_files/driver/openvr-emulator/resources/input_mapping.ini). Defaults are listed below.

### HMD

| Input | Action |
|---|---|
| Left stick X | Look yaw (left / right) |
| Left stick Y | Look pitch (up / down) |
| Left trigger | Move forward (analog) |
| Left trigger + LB | Move backward |
| Mouse (toggle `Space`) | Look yaw / pitch |

### Right Controller

| Input | Action |
|---|---|
| A button / `E` key | A button |
| B button / `R` key | B button |
| Right trigger | Trigger (click ≥ 75%) |
| RB | Grip |
| Right stick X/Y | VR joystick |
| Right stick click | Joystick click |
| Start | System button |
| D-pad ←/→ | Slide controller pose left / right |
| D-pad ↑/↓ | Slide controller pose forward / back |
| Gamepad Y | Raise controller pose |
| Gamepad X | Lower controller pose |

### Left Controller

| Input | Action |
|---|---|
| **Back** | **Swap left/right controller input mapping** (press again to restore) |
| Left stick click | Joystick click |

> Left trigger, LB, left stick, and gamepad X/Y are consumed by HMD movement and right-controller pose adjustment and are not forwarded as left VR controller inputs.
>
> The `Back` button swap is a runtime toggle — no restart needed. Haptic rumble routing also follows the swap (left motor tracks the effective left controller).

### Haptics

OpenVR haptic events are forwarded to XInput rumble (left motor = left controller, right motor = right controller). Routing respects the Back-button swap.

## Input Mapping Configuration

Edit `resources/input_mapping.ini` inside the driver folder and restart SteamVR to apply changes. No recompile needed.

```ini
[hmd]
look_speed        = 1.5   ; radians/sec for left-stick look
move_speed        = 1.0   ; m/s for left-trigger movement
mouse_sensitivity = 0.003 ; radians/pixel

[right_controller]
pose_move_speed         = 0.5  ; m/s for d-pad / X / Y pose adjustment
trigger_click_threshold = 0.75
```

See the file for all available keys and accepted value formats.

## Code Structure

| File | Purpose |
|---|---|
| [`IVRDriver.hpp`](driver_files/src/Driver/IVRDriver.hpp) | Central driver interface — device management, frame updates, OpenVR access |
| [`VRDriver.cpp`](driver_files/src/Driver/VRDriver.cpp) | Driver init, loads `InputConfig`, registers all devices |
| [`InputConfig.hpp/cpp`](driver_files/src/Driver/InputConfig.hpp) | INI-based input mapping config, parsed at startup |
| [`HMDDevice.hpp/cpp`](driver_files/src/Driver/HMDDevice.hpp) | Emulated HMD — look via left stick, move via left trigger, mouse look |
| [`ControllerDevice.hpp/cpp`](driver_files/src/Driver/ControllerDevice.hpp) | Controllers — buttons, triggers, joysticks, haptics, pose adjustment |
| [`TrackerDevice.hpp`](driver_files/src/Driver/TrackerDevice.hpp) | Generic object tracker |
| [`TrackingReferenceDevice.hpp`](driver_files/src/Driver/TrackingReferenceDevice.hpp) | Fixed-position base station / tracking reference |

## Debugging

SteamVR debugging requires attaching to a child process. The recommended setup:

1. Install [Microsoft Child Process Debugging Power Tool](https://marketplace.visualstudio.com/items?itemName=vsdbgplat.MicrosoftChildProcessDebuggingPowerTool)
2. Enable child process debugging, disable all child processes except `vrserver.exe`
3. The CMakeLists already sets the VS debugger command to:
   `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrstartup.exe`

Press **F5** in Visual Studio to launch SteamVR and attach to `vrserver.exe`.

![Child process debugging settings](https://i.imgur.com/yDNvLMm.png)

## Preview App

[`preview_app`](preview_app/README.md) is a small standalone OpenVR utility that mirrors one compositor eye into a desktop window. The default eye is set via `kPreviewEye` in [`SteamVRMirrorPreview.cpp`](preview_app/SteamVRMirrorPreview.cpp).

## License

MIT License — Copyright (c) 2020 Jacob Hilton (Terminal29), portions Copyright (c) 2026 omarekik

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
