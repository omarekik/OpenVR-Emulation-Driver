cmake_minimum_required(VERSION 3.16)

# Solution
project("Simple_SteamVR_Driver_Tutorial")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Deps
set(OPENVR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/headers")

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

find_library(OPENVR_LIB openvr_api HINTS "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/lib/${PLATFORM_NAME}${PROCESSOR_ARCH}/" NO_DEFAULT_PATH )

# Example Driver
set(DRIVER_NAME "example")
set(EXAMPLE_PROJECT "driver_${DRIVER_NAME}")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src/*.hpp")
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src/*.cpp")
add_library("${EXAMPLE_PROJECT}" SHARED "${HEADERS}" "${SOURCES}")

# SYSTEM marks OpenVR as a third-party include so MSVC suppresses its warnings
target_include_directories("${EXAMPLE_PROJECT}" SYSTEM PRIVATE "${OPENVR_INCLUDE_DIR}")
target_include_directories("${EXAMPLE_PROJECT}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libraries/linalg")
target_include_directories("${EXAMPLE_PROJECT}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src/")
target_link_libraries("${EXAMPLE_PROJECT}" PRIVATE "${OPENVR_LIB}" xinput)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src" PREFIX "Header Files" FILES ${HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/driver_files/src" PREFIX "Source Files" FILES ${SOURCES})
target_compile_features("${EXAMPLE_PROJECT}" PRIVATE cxx_std_20)

# Stricter build: disable GNU extensions, enforce standard, enable warnings
set_target_properties("${EXAMPLE_PROJECT}" PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options("${EXAMPLE_PROJECT}" PRIVATE /W4 /permissive- /external:W0)
endif()

# Set the Visual Studio startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${EXAMPLE_PROJECT}")

# Include preview app in the solution
add_subdirectory(preview_app)

# Set the Visual Studio debugger command so F5 launches SteamVR directly
set_target_properties("${EXAMPLE_PROJECT}" PROPERTIES
    VS_DEBUGGER_COMMAND "C:/Program Files (x86)/Steam/steamapps/common/SteamVR/bin/win64/vrstartup.exe"
)

# Copy driver assets to output folder
add_custom_command(
    TARGET ${EXAMPLE_PROJECT}
    PRE_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
    ${CMAKE_SOURCE_DIR}/driver_files/driver/ 
    $<TARGET_FILE_DIR:${EXAMPLE_PROJECT}>
)

# Copy dll to output folder
add_custom_command(
    TARGET ${EXAMPLE_PROJECT} 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    $<TARGET_FILE:${EXAMPLE_PROJECT}>
    $<TARGET_FILE_DIR:${EXAMPLE_PROJECT}>/${DRIVER_NAME}/bin/${PLATFORM_NAME}${PROCESSOR_ARCH}/$<TARGET_FILE_NAME:${EXAMPLE_PROJECT}>
)

# Deploy built driver to SteamVR: rename old folder then replace with new build
set(STEAMVR_DRIVERS_DIR "C:/Program Files (x86)/Steam/steamapps/common/SteamVR/drivers")
add_custom_command(
    TARGET ${EXAMPLE_PROJECT}
    POST_BUILD
    # Remove any leftover backup before renaming
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${STEAMVR_DRIVERS_DIR}/${DRIVER_NAME}.bak"
    # Rename existing driver folder to example.bak (overwrite any previous backup)
    COMMAND ${CMAKE_COMMAND} -E rename
        "${STEAMVR_DRIVERS_DIR}/${DRIVER_NAME}"
        "${STEAMVR_DRIVERS_DIR}/${DRIVER_NAME}.bak"
    # Copy the freshly built driver into place
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        $<TARGET_FILE_DIR:${EXAMPLE_PROJECT}>/${DRIVER_NAME}
        "${STEAMVR_DRIVERS_DIR}/${DRIVER_NAME}"
    COMMENT "Deploying ${DRIVER_NAME} driver to SteamVR"
)