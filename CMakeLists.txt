# ================================================================================
# FILE: CMakeLists.txt
# PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
# WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
# LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
#
# PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
#   CMake build configuration compiling all static `/MT` native Win32 C++20
#   executables across `x64` and `ARM64` using only `cl.exe` + Windows 11 SDK:
#     1. `instbuild.exe`       — Headless CLI compiler (.rsip & .rsis), AI & VPAT emitter
#     2. `SetupWizard.exe`     — Customer-facing accessible external UI bootstrapper
#     3. `InstallerStudio.exe` — 7-view native Win32 C++20 Visual IDE (.rsip & .rsis)
#     4. `msiprism.exe`        — Native 64-bit MSI, MSM, MST & MSP SQL table inspector
#     5. `insttests.exe`       — Automated C++20 unit & integration self-tests
#     6. `HelloRaggieSoft.exe` — Sample target payload executable
# ================================================================================

cmake_minimum_required(VERSION 3.25)
project(RaggieSoftInstallerStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if (MSVC)
    add_compile_options(/W4 /EHsc /utf-8)
endif()

add_executable(instbuild
    src/compiler/RswBuildMain.cpp
    src/compiler/RswBuild.rc
)
target_link_libraries(instbuild PRIVATE msi Cabinet Version Rpcrt4 bcrypt ole32)

add_executable(SetupWizard WIN32
    src/bootstrapper/RswSetupMain.cpp
    src/bootstrapper/RswSetupBootstrapper.rc
)
target_link_libraries(SetupWizard PRIVATE msi user32 gdi32 gdiplus advapi32 comctl32 dwmapi uxtheme msimg32 windowscodecs ole32 shlwapi shell32)

add_executable(InstallerStudio WIN32
    src/ide/RswIdeMain.cpp
    src/ide/RswIde.rc
)
target_link_libraries(InstallerStudio PRIVATE msi Cabinet Version Rpcrt4 bcrypt user32 gdi32 gdiplus advapi32 comctl32 comdlg32 dwmapi uxtheme msimg32 windowscodecs ole32 shlwapi shell32)

add_executable(msiprism WIN32
    src/prism/MsiPrismMain.cpp
    src/prism/MsiPrism.rc
)
target_link_libraries(msiprism PRIVATE msi Cabinet Version Rpcrt4 bcrypt user32 gdi32 advapi32 comctl32 comdlg32 dwmapi uxtheme msimg32 windowscodecs ole32 shlwapi shell32)

add_executable(insttests
    src/tests/RswSelfTests.cpp
)
target_link_libraries(insttests PRIVATE msi Cabinet Version Rpcrt4 bcrypt ole32)

add_executable(HelloRaggieSoft WIN32
    examples/hello-raggiesoft/HelloRaggieSoft.cpp
    examples/hello-raggiesoft/HelloRaggieSoft.rc
)
target_link_libraries(HelloRaggieSoft PRIVATE user32 comctl32 shell32)
