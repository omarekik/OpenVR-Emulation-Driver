cmake_minimum_required(VERSION 3.16)

# Solution
project("OpenVR_Emulation_Driver")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# VS 2022 injects a vcpkg AppLocal post-build step into every C++ project that
# calls pwsh.exe to copy vcpkg DLLs. This project doesn't use vcpkg, so disable
# the integration to suppress the 'pwsh.exe is not recognized' build warning.
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false")

set(SIZEOF_VOIDP ${CMAKE_SIZEOF_VOID_P})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROCESSOR_ARCH "64")
else()
    set(PROCESSOR_ARCH "32")
endif()

if(WIN32)
    set(PLATFORM_NAME "win")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "linux")
elseif(APPLE)
    set(PLATFORM_NAME "osx")
endif()

# OpenVR SDK — from git submodule libraries/openvr
set(OPENVR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/headers")
find_library(
    OPENVR_LIB
    NAMES openvr_api
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/lib/win${PROCESSOR_ARCH}"
    NO_DEFAULT_PATH
    REQUIRED
)

# Driver
set(DRIVER_NAME "openvr-emulator")
set(DRIVER_PROJECT "driver_${DRIVER_NAME}")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src/*.hpp")
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src/*.cpp")
add_library("${DRIVER_PROJECT}" SHARED "${HEADERS}" "${SOURCES}")

# SYSTEM marks OpenVR as a third-party include so MSVC suppresses its warnings.
# Because the include dirs come from a Conan imported target, we extract them
# explicitly and re-add them via SYSTEM so /external:I is applied.
target_include_directories("${DRIVER_PROJECT}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src/")
target_include_directories("${DRIVER_PROJECT}" SYSTEM PRIVATE "${OPENVR_INCLUDE_DIR}")
target_link_libraries("${DRIVER_PROJECT}" PRIVATE "${OPENVR_LIB}" xinput)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src" PREFIX "Header Files" FILES ${HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src" PREFIX "Source Files" FILES ${SOURCES})
target_compile_features("${DRIVER_PROJECT}" PRIVATE cxx_std_20)

# Stricter build: disable GNU extensions, enforce standard, enable warnings
set_target_properties("${DRIVER_PROJECT}" PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options("${DRIVER_PROJECT}" PRIVATE /W4 /permissive- /external:W0)
endif()

# Optional clang-tidy static analysis (pass -DENABLE_CLANG_TIDY=ON to activate)
include(scripts/static_checking.cmake)
enable_clang_tidy("${DRIVER_PROJECT}")

# Set the Visual Studio startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${DRIVER_PROJECT}")

# Include preview app in the solution
add_subdirectory(preview_app)

# Unit tests (GTest/GMock, fetched automatically)
enable_testing()
add_subdirectory(tests)

# Set the Visual Studio debugger command so F5 launches SteamVR directly
set_target_properties("${DRIVER_PROJECT}" PROPERTIES
    VS_DEBUGGER_COMMAND "C:/Program Files (x86)/Steam/steamapps/common/SteamVR/bin/win64/vrstartup.exe"
)

# Copy driver assets to output folder
add_custom_command(
    TARGET ${DRIVER_PROJECT}
    PRE_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
    ${CMAKE_SOURCE_DIR}/driver_files/driver/ 
    $<TARGET_FILE_DIR:${DRIVER_PROJECT}>
)

# Copy dll to output folder
add_custom_command(
    TARGET ${DRIVER_PROJECT} 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    $<TARGET_FILE:${DRIVER_PROJECT}>
    $<TARGET_FILE_DIR:${DRIVER_PROJECT}>/${DRIVER_NAME}/bin/${PLATFORM_NAME}${PROCESSOR_ARCH}/$<TARGET_FILE_NAME:${DRIVER_PROJECT}>
)

# Deploy built driver to SteamVR (backs up existing install, then replaces it)
set(STEAMVR_DRIVERS_DIR "C:/Program Files (x86)/Steam/steamapps/common/SteamVR/drivers")
add_custom_command(
    TARGET ${DRIVER_PROJECT}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DDRIVER_NAME=${DRIVER_NAME}
        -DSRC_DIR=$<TARGET_FILE_DIR:${DRIVER_PROJECT}>
        -DDST_DIR=${STEAMVR_DRIVERS_DIR}
        -P ${CMAKE_SOURCE_DIR}/scripts/deploy_driver.cmake
    COMMENT "Deploying ${DRIVER_NAME} driver to SteamVR"
)
