# Enables clang-tidy static analysis on a target via CMake's built-in integration.
# Requires clang-tidy to be on PATH.
#
# Usage: enable_clang_tidy(<target>)
#
# Note: Visual Studio generators do not produce compile_commands.json, so this
# function uses the CMAKE_CXX_CLANG_TIDY target property instead of -p.
# clang-tidy will be invoked automatically by MSBuild during the build.
#
# To enable: pass -DENABLE_CLANG_TIDY=ON to cmake, or set it in a preset.
function(enable_clang_tidy target)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy.exe)

    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR
            "enable_clang_tidy: clang-tidy not found. "
            "Install LLVM from https://releases.llvm.org and ensure it is on PATH."
        )
    endif()

    set_target_properties(${target} PROPERTIES
        CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
    )

    message(STATUS "clang-tidy enabled for target '${target}': ${CLANG_TIDY_EXE}")
endfunction()
