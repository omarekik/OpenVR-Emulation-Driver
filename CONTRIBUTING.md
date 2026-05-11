# Contributing to OpenVR-Emulation-Driver

Thank you for your interest in contributing! This guide covers everything you need to get from an idea to a merged pull request.

## Table of Contents

- [Getting Started](#getting-started)
- [Workflow](#workflow)
- [Commit Messages](#commit-messages)
- [Code Style](#code-style)
- [CMake Guidelines](#cmake-guidelines)
- [Testing](#testing)
- [Reporting Bugs](#reporting-bugs)

---

## Getting Started

1. **Fork** the repository on GitHub and clone your fork:
   ```sh
   git clone --recursive https://github.com/<your-username>/OpenVR-Emulation-Driver.git
   cd OpenVR-Emulation-Driver
   ```
2. Configure and open the Visual Studio solution:
   ```sh
   cmake -B build
   .\build\Simple_SteamVR_Driver_Tutorial.sln
   ```
3. Build the `driver_example` project. The output driver is automatically deployed to SteamVR's drivers folder (requires running Visual Studio as Administrator).

---

## Workflow

1. Create a branch from `main` using the appropriate prefix:
   ```sh
   git checkout -b feat/my-feature    # new feature
   git checkout -b fix/crash-on-init  # bug fix
   git checkout -b chore/cmake-update # build/tooling
   git checkout -b docs/controls      # documentation
   ```
2. Make your changes.
3. Re-run `cmake -B build` if you modified `CMakeLists.txt`.
4. Build and confirm **zero warnings**.
5. Test in SteamVR (see [Testing](#testing)).
6. Commit (see [Commit Messages](#commit-messages)).
7. Push and open a pull request against `main`.

---

## Commit Messages

Use [Conventional Commits](https://www.conventionalcommits.org/) format:

```
<type>(<optional scope>): <short summary>

<optional body>

<optional footer>
```

| Type | When to use |
|---|---|
| `feat` | A new feature visible to users |
| `fix` | A bug fix |
| `chore` | Build, tooling, or dependency changes |
| `docs` | Documentation only |
| `refactor` | Code restructuring with no behavior change |
| `perf` | Performance improvement |
| `test` | Adding or updating tests |

**Examples:**
```
feat(controller): add Back button runtime swap of left/right mapping
fix(hmd): clamp pitch rotation to ±90 degrees
chore(cmake): add CMakePresets.json for VS 2022
docs(readme): add versioning and contributing sections
```

Breaking changes must include `BREAKING CHANGE:` in the footer:
```
feat!: rename input_mapping.ini keys for consistency

BREAKING CHANGE: btn_a is now gamepad_a; update your .ini files.
```

---

## Code Style

This project targets **C++20** and must compile clean under MSVC `/W4 /permissive-`.

### General

- **No raw `new`/`delete`** — use `std::make_shared` / `std::make_unique`.
- **No `using namespace std`** in headers.
- Prefer `const` references for function parameters that are not mutated.
- Use `auto` where the type is already obvious from the right-hand side.

### Classes

- Use `override` on overriding methods; **do not** add `virtual` redundantly.
- Add `explicit` to single-argument constructors to prevent implicit conversions.
- Add `[[nodiscard]]` to functions whose return value must not be silently discarded.
- Member variables use `snake_case_` with a trailing underscore.

### Parameters

- Comment out unused parameter names rather than deleting them:
  ```cpp
  void Foo(int /*unused*/) {}
  ```
- Never suppress warnings by casting to `void` inside the body — prefer the comment approach.

### Narrowing Conversions

- Use `static_cast<TargetType>(value)` whenever a narrowing conversion is intentional (e.g. `int` → `WORD`).

---

## CMake Guidelines

- Use **`PRIVATE`** visibility for `target_include_directories` and `target_link_libraries` unless downstream consumers genuinely need the dependency.
- Mark third-party includes as **`SYSTEM`** to suppress their warnings:
  ```cmake
  target_include_directories(my_target SYSTEM PRIVATE path/to/third-party)
  ```
- Express linker dependencies in CMake (`target_link_libraries`), not in source files via `#pragma comment(lib, ...)`.
- Use `target_compile_features(... PRIVATE cxx_std_20)` instead of `set_property(... CXX_STANDARD 20)`.

---

## Testing

There is currently no automated test suite. Manual testing steps:

1. Build the driver (zero warnings required).
2. Launch SteamVR via F5 (with `driver_example` as the startup project, or run `vrstartup.exe` directly).
3. Verify in the SteamVR Dashboard that the HMD and both controllers appear.
4. Exercise the affected inputs:
   - **HMD**: left stick look, left trigger move, mouse look toggle.
   - **Right controller**: A/B buttons, trigger, grip, right stick joystick, d-pad pose adjustment.
   - **Left controller**: left stick click, Back button swap (and swap back).
5. Check the SteamVR log (`vrserver.txt`) for any driver errors.

---

## Reporting Bugs

There is no formal issue template. When filing a bug, please include:

- A clear description of the problem and steps to reproduce it.
- Expected vs. actual behaviour.
- Your SteamVR and Windows version.
- The relevant section of `vrserver.txt` (found in `C:\Program Files (x86)\Steam\logs\`).
- Your `input_mapping.ini` if the issue is input-related.

Pull requests with a fix are always welcome alongside a bug report.
