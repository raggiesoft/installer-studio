// ================================================================================
// FILE: src/bootstrapper/RswCompactAndSuiteUi.hpp
// PROJECT: RaggieSoft Installer Studio — SetupWizard.exe ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this header add to `SetupWizard.exe` in `v0.6.0`?
//     1. **Ultra-Compact Single-Dialog UI (`WizardLayout = simple`)**:
//        - Inspired by classic minimal single-window extractors/installers, this
//          mode presents ONE clean, fast `480 x 224` Per-Monitor V2 DPI-scaled
//          dialog (`Destination folder` / `Extract to` + `[...]` browse button +
//          live progress bar + `[&Install]` / `[&Extract]` + `[Close]`).
//        - Maintains 100% WCAG 2.2 AA & Revised Section 508 compliance: automatic
//          Windows 11 Dark Mode, Light Mode, & Contrast Themes (`SPI_GETHIGHCONTRAST`),
//          `&` keyboard mnemonics, `Tab` traversal, UAC shield (`BCM_SETSHIELD`),
//          and `ITaskbarList3` taskbar progress!
//        - Powers BOTH `PackageKind = sfx` self-extracting archives AND
//          **Simple Setup Projects** running on `Engine = native`!
//     2. **Unified Suite Setup & Maintenance Hub (`PackageKind = suite` / `.rsis`)**:
//        - Single-UAC multi-package chainer with **Dynamic `ARPSYSTEMCOMPONENT`
//          Adoption** (`RS_SUITE_MANAGED=1` / `SystemComponent=1`) so installing a
//          Suite adopts already-installed standalone sibling `.msi` or `native`
//          tools under ONE clean *Installed apps* Suite Maintenance entry—and
//          cleanly detaches/restores standalone visibility (`RS_SUITE_MANAGED=0`)
//          whenever requested!
// ================================================================================

#pragma once

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswNativeAndSuiteEngine.hpp"
#include "../common/RswVersion.hpp"
#include "../common/RswWizardStyles.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <msi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace rsw::compact_suite {

/// Loads a zero-copy `(ptr, size)` view of an embedded `RT_RCDATA` resource inside the running EXE.
inline bool GetRcDataViewLocal(int resourceId, const std::uint8_t*& outPtr, std::size_t& outSize) noexcept {
    outPtr  = nullptr;
    outSize = 0;
    HMODULE hSelf = ::GetModuleHandleW(nullptr);
    HRSRC hFound  = ::FindResourceW(hSelf, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hFound) return false;
    const DWORD sz = ::SizeofResource(hSelf, hFound);
    HGLOBAL hGlob  = ::LoadResource(hSelf, hFound);
    if (!hGlob || sz == 0) return false;
    outPtr  = static_cast<const std::uint8_t*>(::LockResource(hGlob));
    outSize = static_cast<std::size_t>(sz);
    return outPtr != nullptr;
}

/// Computes the default destination path for `NativeOrSfxManifest` (`%TEMP%`, `<ExeDir>\<Stem>`,
/// `[HostApp]` content target for `addon`, `%ProgramW6432%\<Subdir>`, or `%LOCALAPPDATA%\<Subdir>`).
inline std::filesystem::path ComputeDefaultNativeOrSfxPath(const NativeOrSfxManifest& m) {
    if (m.mode == "addon") {
        return ResolveHostApplicationForAddOn(m).resolvedContentDir;
    }

    std::error_code ec;
    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath(selfBuf);

    if (m.mode == "sfx") {
        if (m.extractToTemp) {
            return (std::filesystem::temp_directory_path(ec) /
                    (L"rsw-sfx-" + selfPath.stem().wstring() + L"-" + std::to_wstring(::GetCurrentProcessId()))).lexically_normal();
        }
        if (!m.defaultSubdir.empty()) {
            std::filesystem::path sub(Utf8ToWide(m.defaultSubdir));
            if (sub.is_absolute()) return sub.lexically_normal();
            return (selfPath.parent_path() / sub).lexically_normal();
        }
        return (selfPath.parent_path() / selfPath.stem()).lexically_normal();
    }

    wchar_t envBuf[MAX_PATH * 2]{};
    const wchar_t* envVar = m.perMachine ? L"ProgramW6432" : L"LOCALAPPDATA";
    DWORD n = ::GetEnvironmentVariableW(envVar, envBuf, static_cast<DWORD>(std::size(envBuf)));
    if (n == 0) n = ::GetEnvironmentVariableW(L"ProgramFiles", envBuf, static_cast<DWORD>(std::size(envBuf)));
    std::filesystem::path base = (n > 0) ? std::filesystem::path(envBuf) : std::filesystem::path(L"C:\\Program Files");
    const std::string sub = m.defaultSubdir.empty() ? (m.manufacturer + "\\" + m.productName) : m.defaultSubdir;
    return (base / Utf8ToWide(sub)).lexically_normal();
}

/// Runs optional `[SFX] PostExtractOpen` (document viewer via `ShellExecuteExW`) and/or
/// `PostExtractRun` / `MainExecutable` after extraction or installation completes!
inline void ExecutePostExtractOrFinishActions(
    const NativeOrSfxManifest& m,
    const std::filesystem::path& targetDir) noexcept
{
    DWORD dummyCode = 0;
    std::string dummyErr;
    (void)ExecuteSfxLaunchWaitAndCleanup(m, targetDir, nullptr, nullptr, dummyCode, dummyErr);
}

/// Executes an elevated `Engine = native` worker job (`--elevated-native-worker <job.ini>`) spawned
/// from an un-elevated `asInvoker` Simple Setup window when `Scope = machine`!
inline int RunElevatedNativeWorkerJob(const std::filesystem::path& jobFilePath) {
    std::vector<std::uint8_t> raw;
    std::string err;
    if (!ReadBinaryFileBytes(jobFilePath, raw, err)) return ERROR_BAD_ARGUMENTS;
    auto kv = native_detail::ParseFlatIniMap(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));

    const std::filesystem::path statePath(jobFilePath.wstring() + L".state");
    const std::filesystem::path cancelPath(jobFilePath.wstring() + L".cancel");
    auto writeState = [&](int permille, const std::wstring& status, const std::string& errorMsg) {
        std::ofstream st(statePath, std::ios::binary | std::ios::trunc);
        if (st) {
            st << "Permille=" << permille << "\n"
               << "Status="   << native_detail::EscapeSingleLine(WideToUtf8(status)) << "\n"
               << "Error="    << native_detail::EscapeSingleLine(errorMsg) << "\n";
        }
    };

    const std::uint8_t* manPtr = nullptr;
    std::size_t manSz = 0;
    const std::uint8_t* pakPtr = nullptr;
    std::size_t pakSz = 0;
    if (!GetRcDataViewLocal(kResIdNativeManifest, manPtr, manSz) ||
        !GetRcDataViewLocal(kResIdSfxOrNativeArchive, pakPtr, pakSz)) {
        writeState(0, L"Missing embedded native payload.", "Missing embedded native payload.");
        return ERROR_INSTALL_PACKAGE_OPEN_FAILED;
    }

    const NativeOrSfxManifest manifest = ParseNativeOrSfxManifestIni(
        std::string_view(reinterpret_cast<const char*>(manPtr), manSz));
    std::vector<FeaturePackRuntimeEntry> packs;
    const std::uint8_t* fpPtr = nullptr;
    std::size_t fpSz = 0;
    if (GetRcDataViewLocal(kResIdFeaturePackManifest, fpPtr, fpSz) && fpSz > 0) {
        packs = ParseFeaturePackManifestIni(std::string_view(reinterpret_cast<const char*>(fpPtr), fpSz));
    }

    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath(selfBuf);

    NativeExecutionOptions opts{};
    opts.targetDir              = std::filesystem::path(Utf8ToWide(kv["TargetDir"]));
    if (manifest.mode == "addon") {
        opts.detectedHostDir = ResolveHostApplicationForAddOn(manifest).hostInstallDir;
    }
    opts.createDesktopShortcuts = !EqualsIgnoreCase(kv["DesktopShortcuts"], "false");
    opts.registerFileAssocs     = !EqualsIgnoreCase(kv["RegisterAssocs"], "false");
    opts.suiteManagedFlag       = kv["SuiteManaged"].empty() ? -1 : std::atoi(kv["SuiteManaged"].c_str());
    opts.runningSetupExePath    = selfPath;
    opts.externalPackSearchDir  = selfPath.parent_path();

    std::atomic<bool> workerCancel{false};
    std::string runErr;
    const bool ok = ExecuteNativeOrSfxTransaction(
        manifest, pakPtr, pakSz, packs, {}, opts, &workerCancel,
        [&](int perm, const std::wstring& txt) {
            std::error_code cEc;
            if (std::filesystem::exists(cancelPath, cEc)) {
                workerCancel.store(true);
            }
            writeState(perm, txt, "");
        },
        runErr);
    writeState(ok ? 1000 : 0, ok ? L"Completed." : (workerCancel.load() ? L"Canceled and rolled back." : L"Failed."), runErr);
    return ok ? 0 : (workerCancel.load() ? ERROR_INSTALL_USEREXIT : ERROR_INSTALL_FAILURE);
}

// ================================================================================
// ULTRA-COMPACT SINGLE-DIALOG WINDOW (`WizardLayout = simple` — SFX & Native!)
// ================================================================================
class SimpleCompactDialogApp {
public:
    static constexpr UINT WM_SMP_PROGRESS = WM_APP + 60;
    static constexpr UINT WM_SMP_DONE     = WM_APP + 61;
    static constexpr int  IDC_SMP_DIR_LBL = 1501;
    static constexpr int  IDC_SMP_DIR_EDT = 1502;
    static constexpr int  IDC_SMP_BROWSE  = 1503;
    static constexpr int  IDC_SMP_STATUS  = 1504;
    static constexpr int  IDC_SMP_PROG    = 1505;
    static constexpr int  IDC_SMP_LAUNCH  = 1506;
    static constexpr int  IDC_SMP_PRIMARY = 1507;
    static constexpr int  IDC_SMP_CLOSE   = 1508;

    NativeOrSfxManifest                  manifest{};
    HostAppResolution                    hostRes{};
    const std::uint8_t*                  pakPtr = nullptr;
    std::size_t                          pakSize = 0;
    std::vector<FeaturePackRuntimeEntry> featurePacks;
    int                                  suiteManagedFlag = -1;
    std::filesystem::path                exportShotDir;

    ui::RswTheme          theme{};
    HWND                  hWnd        = nullptr;
    HWND                  hDirLbl     = nullptr;
    HWND                  hDirEdit    = nullptr;
    HWND                  hBtnBrowse  = nullptr;
    HWND                  hStatusLbl  = nullptr;
    HWND                  hProgBar    = nullptr;
    HWND                  hChkLaunch  = nullptr;
    HWND                  hBtnPrimary = nullptr;
    HWND                  hBtnClose   = nullptr;
    HICON                 hIconBig    = nullptr;
    HICON                 hIconSmall  = nullptr;
    HFONT                 hFontTitle  = nullptr;
    HFONT                 hFontBold   = nullptr;
    HFONT                 hFontBody   = nullptr;

    static constexpr UINT_PTR kAutoStartTimerId = 77;
    std::filesystem::path targetDir;
    DWORD                 childExitCode = 0;
    std::atomic<bool>     running{false};
    std::atomic<bool>     finished{false};
    std::atomic<bool>     cancelFlag{false};
    std::mutex            mtx;
    std::wstring          statusText;
    std::string           errorTextUtf8;
    std::thread           worker;

    ~SimpleCompactDialogApp() {
        cancelFlag.store(true);
        if (worker.joinable()) worker.join();
        for (HFONT* f : {&hFontTitle, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    bool IsTempSoftwareInstallerMode() const noexcept {
        return manifest.mode == "sfx" &&
               manifest.extractToTemp &&
               manifest.cleanupTempAfterRun &&
               !manifest.postExtractRun.empty();
    }

    bool NeedsAdminElevation() const noexcept {
        if (manifest.mode == "sfx") return false;
        if (manifest.mode == "addon" && !manifest.perMachine &&
            !EqualsIgnoreCase(manifest.contentTargetRoot, "HostInstallDir") &&
            !EqualsIgnoreCase(manifest.contentTargetRoot, "CommonAppData")) {
            return false;
        }
        if (!manifest.perMachine && !(manifest.mode == "addon" && EqualsIgnoreCase(manifest.contentTargetRoot, "CommonAppData"))) {
            return false;
        }
        return !QueryProcessPrivilegeState().HasEffectiveAdminRights();
    }

    bool IsBlockedByMissingRequiredHost() const noexcept {
        return manifest.mode == "addon" && manifest.requireHostInstalled && !hostRes.found;
    }

    bool HasPostAction() const noexcept {
        if (IsTempSoftwareInstallerMode()) {
            // Software Installation SFX runs `PostExtractRun`, waits (incl. `_ISDEL`), and cleans `%TEMP%` inline!
            return false;
        }
        if (manifest.mode == "sfx") {
            return !manifest.postExtractRun.empty() || !manifest.postExtractOpen.empty();
        }
        if (manifest.mode == "addon") {
            return manifest.offerLaunchOnFinish &&
                   (!manifest.hostLaunchUri.empty() || !manifest.mainExecutable.empty() || !hostRes.hostExePath.empty());
        }
        return manifest.offerLaunchOnFinish && !manifest.mainExecutable.empty();
    }

    void RebuildFonts(UINT dpi) {
        for (HFONT* f : {&hFontTitle, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        const int ts = theme.textScalePercent;
        hFontTitle = ui::CreateScaledFont(L"Segoe UI", 12, FW_BOLD,     dpi, ts);
        hFontBold  = ui::CreateScaledFont(L"Segoe UI",  9, FW_SEMIBOLD, dpi, ts);
        hFontBody  = ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);
        for (HWND h : {hDirLbl, hBtnPrimary}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hDirEdit, hBtnBrowse, hStatusLbl, hChkLaunch, hBtnClose}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }
    }

    void LayoutControls() {
        if (!hWnd) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        RebuildFonts(dpi);
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = rc.right - rc.left;
        const int H = rc.bottom - rc.top;
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };

        ::MoveWindow(hDirLbl,     S(18), S(62),  W - S(36),  S(18), TRUE);
        ::MoveWindow(hDirEdit,    S(18), S(82),  W - S(114), S(28), TRUE);
        ::MoveWindow(hBtnBrowse,  W - S(90), S(81), S(72),   S(30), TRUE);
        ::MoveWindow(hStatusLbl,  S(18), S(120), W - S(36),  S(18), TRUE);
        ::MoveWindow(hProgBar,    S(18), S(142), W - S(36),  S(20), TRUE);

        const int footY = H - S(44);
        if (hChkLaunch) {
            ::MoveWindow(hChkLaunch, S(18), footY + S(3), W - S(238), S(24), TRUE);
            ::ShowWindow(hChkLaunch, HasPostAction() ? SW_SHOW : SW_HIDE);
        }
        ::MoveWindow(hBtnPrimary, W - S(210), footY, S(104), S(32), TRUE);
        ::MoveWindow(hBtnClose,   W - S(98),  footY, S(80),  S(32), TRUE);
        ::SendMessageW(hBtnPrimary, BCM_SETSHIELD, 0, (!finished.load() && NeedsAdminElevation()) ? TRUE : FALSE);
    }

    void StartExtractionOrInstall() {
        if (running.load()) return;
        wchar_t dirBuf[MAX_PATH * 4]{};
        ::GetWindowTextW(hDirEdit, dirBuf, static_cast<int>(std::size(dirBuf)));
        if (dirBuf[0] != L'\0') targetDir = dirBuf;

        if (NeedsAdminElevation() && !QueryProcessPrivilegeState().CanPromptForUacElevation()) {
            const std::wstring title = Utf8ToWide(manifest.productName) + L" Setup";
            ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), title.c_str(),
                         L"Administrator Privileges Required",
                         L"Installing into Program Files requires Administrator rights, and UAC elevation is disabled on this PC.",
                         TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
            return;
        }

        running.store(true);
        cancelFlag.store(false);
        ::EnableWindow(hDirEdit, FALSE);
        ::EnableWindow(hBtnBrowse, FALSE);
        ::EnableWindow(hBtnPrimary, FALSE);
        ::SetWindowTextW(hBtnClose, L"&Cancel");

        if (worker.joinable()) worker.join();
        worker = std::thread([this]() {
            wchar_t selfBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
            const std::filesystem::path selfPath(selfBuf);

            if (NeedsAdminElevation()) {
                // Spawn elevated self with `--elevated-native-worker <job.ini>` under ONE UAC prompt!
                std::error_code ec;
                const auto jobPath = std::filesystem::temp_directory_path(ec) /
                                     (L"rsw-natjob-" + std::to_wstring(::GetCurrentProcessId()) + L".ini");
                const auto statePath  = std::filesystem::path(jobPath.wstring() + L".state");
                const auto cancelPath = std::filesystem::path(jobPath.wstring() + L".cancel");
                std::filesystem::remove(statePath, ec);
                std::filesystem::remove(cancelPath, ec);
                {
                    std::ofstream jf(jobPath, std::ios::binary | std::ios::trunc);
                    jf << "TargetDir=" << native_detail::EscapeSingleLine(WideToUtf8(targetDir.wstring())) << "\n"
                       << "DesktopShortcuts=true\nRegisterAssocs=true\n"
                       << "SuiteManaged=" << suiteManagedFlag << "\n";
                }
                const std::wstring params = L"--elevated-native-worker \"" + jobPath.wstring() + L"\"";
                SHELLEXECUTEINFOW sei{sizeof(sei)};
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.hwnd         = hWnd;
                sei.lpVerb       = L"runas";
                const std::wstring wSelf = selfPath.wstring();
                sei.lpFile       = wSelf.c_str();
                sei.lpParameters = params.c_str();
                sei.nShow        = SW_HIDE;
                if (!::ShellExecuteExW(&sei) || !sei.hProcess) {
                    std::filesystem::remove(jobPath, ec);
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        errorTextUtf8 = "Elevation was canceled.";
                    }
                    ::PostMessageW(hWnd, WM_SMP_DONE, 0, 0);
                    return;
                }
                while (::WaitForSingleObject(sei.hProcess, 80) == WAIT_TIMEOUT) {
                    if (cancelFlag.load() && !std::filesystem::exists(cancelPath, ec)) {
                        std::ofstream cf(cancelPath, std::ios::binary | std::ios::trunc);
                        cf << "1";
                    }
                    std::vector<std::uint8_t> stBytes;
                    std::string dummy;
                    if (ReadBinaryFileBytes(statePath, stBytes, dummy) && !stBytes.empty()) {
                        auto skv = native_detail::ParseFlatIniMap(
                            std::string_view(reinterpret_cast<const char*>(stBytes.data()), stBytes.size()));
                        const int perm = std::atoi(skv["Permille"].c_str());
                        {
                            std::lock_guard<std::mutex> lk(mtx);
                            statusText = Utf8ToWide(skv["Status"]);
                            if (!skv["Error"].empty()) errorTextUtf8 = skv["Error"];
                        }
                        ::PostMessageW(hWnd, WM_SMP_PROGRESS, static_cast<WPARAM>(perm), 0);
                    }
                }
                DWORD exitCode = 1;
                ::GetExitCodeProcess(sei.hProcess, &exitCode);
                ::CloseHandle(sei.hProcess);
                {
                    std::vector<std::uint8_t> stBytes;
                    std::string dummy;
                    if (ReadBinaryFileBytes(statePath, stBytes, dummy) && !stBytes.empty()) {
                        auto skv = native_detail::ParseFlatIniMap(
                            std::string_view(reinterpret_cast<const char*>(stBytes.data()), stBytes.size()));
                        std::lock_guard<std::mutex> lk(mtx);
                        if (!skv["Error"].empty()) errorTextUtf8 = skv["Error"];
                    }
                }
                std::filesystem::remove(jobPath, ec);
                std::filesystem::remove(statePath, ec);
                std::filesystem::remove(cancelPath, ec);
                ::PostMessageW(hWnd, WM_SMP_DONE, (exitCode == 0) ? 1 : 0, 0);
                return;
            }

            NativeExecutionOptions opts{};
            opts.targetDir              = targetDir;
            opts.detectedHostDir        = hostRes.hostInstallDir;
            opts.createDesktopShortcuts = true;
            opts.registerFileAssocs     = true;
            opts.suiteManagedFlag       = suiteManagedFlag;
            opts.runningSetupExePath    = selfPath;
            opts.externalPackSearchDir  = selfPath.parent_path();

            std::string err;
            bool ok = ExecuteNativeOrSfxTransaction(
                manifest, pakPtr, pakSize, featurePacks, {}, opts, &cancelFlag,
                [this](int perm, const std::wstring& st) {
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        statusText = st;
                    }
                    ::PostMessageW(hWnd, WM_SMP_PROGRESS, static_cast<WPARAM>(perm), 0);
                },
                err);

            if (ok && manifest.mode == "addon" && !manifest.hostRefreshArgs.empty() && !hostRes.hostExePath.empty()) {
                std::wstring runningName;
                const std::vector<std::wstring> hostPats{hostRes.hostExePath.filename().wstring()};
                if (sfx_wait_detail::FindActiveCompanionOrTempProcess({}, hostPats, false, runningName)) {
                    ::ShellExecuteW(nullptr, L"open", hostRes.hostExePath.wstring().c_str(),
                                    Utf8ToWide(manifest.hostRefreshArgs).c_str(),
                                    hostRes.hostInstallDir.wstring().c_str(), SW_SHOWNOACTIVATE);
                }
            }

            if (ok && IsTempSoftwareInstallerMode() && !cancelFlag.load()) {
                ok = ExecuteSfxLaunchWaitAndCleanup(
                    manifest,
                    targetDir,
                    &cancelFlag,
                    [this](const std::wstring& waitSt) {
                        {
                            std::lock_guard<std::mutex> lk(mtx);
                            statusText = waitSt;
                        }
                        ::PostMessageW(hWnd, WM_SMP_PROGRESS, 950, 0);
                    },
                    childExitCode,
                    err);
            }

            {
                std::lock_guard<std::mutex> lk(mtx);
                errorTextUtf8 = err;
            }
            ::PostMessageW(hWnd, WM_SMP_DONE, ok ? 1 : 0, 0);
        });
    }

    void PaintWindow(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC dc         = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp    = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(dc, bmp);
        ::FillRect(dc, &rc, theme.hbrWindow);
        ::SetBkMode(dc, TRANSPARENT);

        const int hdrH = S(52);
        RECT hdrRc{0, 0, W, hdrH};
        if (theme.isHighContrast) {
            ::FillRect(dc, &hdrRc, theme.hbrFooter);
        } else {
            ui::FillHorizontalGradient(dc, hdrRc, theme.bannerNavyLeft, theme.bannerNavyRight);
        }
        RECT accRc{0, hdrH - S(3), W, hdrH};
        HBRUSH hAcc = ::CreateSolidBrush(theme.accentPrimary);
        ::FillRect(dc, &accRc, hAcc);
        ::DeleteObject(hAcc);

        if (hIconBig) {
            ::DrawIconEx(dc, S(16), S(10), hIconBig, S(30), S(30), 0, nullptr, DI_NORMAL);
        }

        HGDIOBJ oldF = ::SelectObject(dc, hFontTitle);
        ::SetTextColor(dc, theme.isHighContrast ? theme.textPrimary : theme.bannerTitleText);
        const std::wstring head = Utf8ToWide(manifest.productName + " v" + manifest.version);
        RECT tRc{S(54), S(6), W - S(16), S(28)};
        ::DrawTextW(dc, head.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(dc, hFontBody);
        ::SetTextColor(dc, theme.isHighContrast ? theme.textSecondary : RGB(203, 213, 225));
        const std::wstring sub = !manifest.tagline.empty()
            ? Utf8ToWide(manifest.tagline)
            : ((manifest.mode == "sfx")
                   ? L"Self-Extracting Archive (LZMS)"
                   : ((manifest.mode == "addon")
                          ? (L"Add-On Pack for " + Utf8ToWide(manifest.hostName))
                          : Utf8ToWide(manifest.manufacturer + " Quick Setup")));
        RECT sRc{S(54), S(28), W - S(16), S(48)};
        ::DrawTextW(dc, sub.c_str(), -1, &sRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(dc, oldF);
        ::BitBlt(hdcOut, 0, 0, W, H, dc, 0, 0, SRCCOPY);
        ::SelectObject(dc, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(dc);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<SimpleCompactDialogApp*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SimpleCompactDialogApp*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
            ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE: {
                HINSTANCE hInst = ::GetModuleHandleW(nullptr);
                const bool isSfx    = (self->manifest.mode == "sfx");
                const bool isAddon  = (self->manifest.mode == "addon");
                const bool tempInst = self->IsTempSoftwareInstallerMode();
                const std::wstring dirCaptionW = tempInst
                    ? L"Temporary staging folder (cleaned up automatically after setup):"
                    : (isSfx
                           ? L"E&xtract to folder:"
                           : (isAddon
                                  ? (self->hostRes.found
                                         ? (L"Verified Host: " + Utf8ToWide(self->manifest.hostName) + L" \u2014 Content folder:")
                                         : (L"Host Not Installed: Please install " + Utf8ToWide(self->manifest.hostName) + L" first:"))
                                  : L"&Destination folder:"));
                self->hDirLbl = ::CreateWindowExW(
                    0, L"STATIC", dirCaptionW.c_str(), WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_DIR_LBL)), hInst, nullptr);
                self->hDirEdit = ::CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", self->targetDir.wstring().c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (self->manifest.allowChangeFolder ? 0 : ES_READONLY),
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_DIR_EDT)), hInst, nullptr);
                self->hBtnBrowse = ::CreateWindowExW(
                    0, L"BUTTON", L"B&rowse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_BROWSE)), hInst, nullptr);
                ::EnableWindow(self->hBtnBrowse, self->manifest.allowChangeFolder ? TRUE : FALSE);

                const std::wstring initStatusW = isSfx
                    ? L"Click Extract to unpack files."
                    : (self->IsBlockedByMissingRequiredHost()
                           ? (L"Required application '" + Utf8ToWide(self->manifest.hostName) + L"' was not found on this PC.")
                           : (isAddon
                                  ? (L"Ready to install content into " + Utf8ToWide(self->manifest.hostName) + L".")
                                  : L"Click Install to begin setup."));
                self->hStatusLbl = ::CreateWindowExW(
                    0, L"STATIC", initStatusW.c_str(), WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_STATUS)), hInst, nullptr);
                self->hProgBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Setup Progress", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_PROG)), hInst, nullptr);
                ::SendMessageW(self->hProgBar, PBM_SETRANGE32, 0, 1000);

                const std::wstring launchLbl = isSfx
                    ? (!self->manifest.postExtractRun.empty() ? L"&Run program after extraction" : L"&Open document after extraction")
                    : ((isAddon && self->manifest.finishLaunchText == "&Launch application when finished")
                           ? (L"&Open " + Utf8ToWide(self->manifest.hostName) + L" when finished")
                           : Utf8ToWide(self->manifest.finishLaunchText));
                self->hChkLaunch = ::CreateWindowExW(
                    0, L"BUTTON", launchLbl.c_str(), WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_LAUNCH)), hInst, nullptr);
                ::SendMessageW(self->hChkLaunch, BM_SETCHECK, self->HasPostAction() ? BST_CHECKED : BST_UNCHECKED, 0);

                const bool missingHost = self->IsBlockedByMissingRequiredHost();
                const std::wstring cta = (missingHost && !self->manifest.hostDownloadUrl.empty())
                    ? L"&Get Host App..."
                    : Utf8ToWide(self->manifest.installBtnText);
                self->hBtnPrimary = ::CreateWindowExW(
                    0, L"BUTTON", cta.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_PRIMARY)), hInst, nullptr);
                if (missingHost && self->manifest.hostDownloadUrl.empty()) {
                    ::EnableWindow(self->hBtnPrimary, FALSE);
                }
                self->hBtnClose = ::CreateWindowExW(
                    0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_CLOSE)), hInst, nullptr);

                ui::AttachAccessibleCheckboxSubclass(self->hChkLaunch, &self->theme, false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnBrowse,  &self->theme, false, false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnPrimary, &self->theme, true,  false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnClose,   &self->theme, false, false);
                self->LayoutControls();
                if (self->manifest.autoStartExtract && self->exportShotDir.empty()) {
                    ::SetTimer(hWnd, kAutoStartTimerId, 60, nullptr);
                }
                return 0;
            }
            case WM_TIMER: {
                if (wParam == kAutoStartTimerId) {
                    ::KillTimer(hWnd, kAutoStartTimerId);
                    self->StartExtractionOrInstall();
                    return 0;
                }
                break;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT: {
                HDC hdc     = reinterpret_cast<HDC>(wParam);
                HWND hChild = reinterpret_cast<HWND>(lParam);
                // Note: Read-only Win32 `EDIT` controls (`ES_READONLY`, used on `addon` installers!)
                // send `WM_CTLCOLORSTATIC` instead of `WM_CTLCOLOREDIT` — paint them on `cardBg`!
                const bool useCard = (msg == WM_CTLCOLOREDIT) || (hChild != nullptr && hChild == self->hDirEdit);
                ::SetTextColor(hdc, self->theme.textPrimary);
                ::SetBkColor(hdc, useCard ? self->theme.cardBg : self->theme.windowBg);
                return reinterpret_cast<LRESULT>(useCard ? self->theme.hbrCard : self->theme.hbrWindow);
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintWindow(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                               suggested->right - suggested->left, suggested->bottom - suggested->top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutControls();
                ::InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    const COLORREF pCol = ui::ParseHexColor(self->manifest.accentPrimaryHex, RGB(37, 99, 235));
                    const COLORREF sCol = ui::ParseHexColor(self->manifest.accentSecondaryHex, RGB(251, 191, 36));
                    self->theme.Refresh(pCol, sCol);
                    ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    self->LayoutControls();
                    ::InvalidateRect(hWnd, nullptr, TRUE);
                    return 0;
                }
                break;
            }
            case WM_SMP_PROGRESS: {
                ::SendMessageW(self->hProgBar, PBM_SETPOS, wParam, 0);
                std::wstring st;
                {
                    std::lock_guard<std::mutex> lk(self->mtx);
                    st = self->statusText;
                }
                if (!st.empty()) ::SetWindowTextW(self->hStatusLbl, st.c_str());
                return 0;
            }
            case WM_SMP_DONE: {
                self->running.store(false);
                if (wParam == 1) {
                    self->finished.store(true);
                    if (self->IsTempSoftwareInstallerMode()) {
                        ::SendMessageW(self->hProgBar, PBM_SETPOS, 1000, 0);
                        ::DestroyWindow(hWnd);
                        return 0;
                    }
                    ::SendMessageW(self->hProgBar, PBM_SETPOS, 1000, 0);
                    ::SetWindowTextW(self->hStatusLbl,
                                     (self->manifest.mode == "sfx") ? L"Extraction completed successfully." : L"Installation completed successfully.");
                    ::SetWindowTextW(self->hBtnPrimary, L"&Finish");
                    ::SendMessageW(self->hBtnPrimary, BCM_SETSHIELD, 0, FALSE);
                    ::EnableWindow(self->hBtnPrimary, TRUE);
                    ::SetWindowTextW(self->hBtnClose, L"Close");
                    ::SetFocus(self->hBtnPrimary);
                } else {
                    std::string errCopy;
                    {
                        std::lock_guard<std::mutex> lk(self->mtx);
                        errCopy = self->errorTextUtf8;
                    }
                    ::SendMessageW(self->hProgBar, PBM_SETPOS, 0, 0);
                    const std::wstring doneStatus = self->cancelFlag.load()
                        ? L"Setup canceled — all partial changes were rolled back cleanly."
                        : Utf8ToWide(errCopy.empty() ? "Operation canceled." : errCopy);
                    ::SetWindowTextW(self->hStatusLbl, doneStatus.c_str());
                    ::EnableWindow(self->hDirEdit, self->manifest.allowChangeFolder ? TRUE : FALSE);
                    ::EnableWindow(self->hBtnBrowse, self->manifest.allowChangeFolder ? TRUE : FALSE);
                    ::EnableWindow(self->hBtnPrimary, TRUE);
                    ::EnableWindow(self->hBtnClose, TRUE);
                    ::SetWindowTextW(self->hBtnClose, L"Close");
                }
                return 0;
            }
            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id == IDC_SMP_BROWSE && !self->running.load()) {
                    IFileOpenDialog* pDlg = nullptr;
                    if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
                        DWORD opts = 0;
                        pDlg->GetOptions(&opts);
                        pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                        if (SUCCEEDED(pDlg->Show(hWnd))) {
                            IShellItem* pItem = nullptr;
                            if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                                PWSTR psz = nullptr;
                                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                                    self->targetDir = psz;
                                    ::SetWindowTextW(self->hDirEdit, psz);
                                    ::CoTaskMemFree(psz);
                                }
                                pItem->Release();
                            }
                        }
                        pDlg->Release();
                    }
                    return 0;
                }
                if (id == IDC_SMP_PRIMARY || id == IDOK) {
                    if (self->finished.load()) {
                        if (self->HasPostAction() && ::SendMessageW(self->hChkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            if (self->manifest.mode == "addon" && !self->manifest.hostLaunchUri.empty()) {
                                ::ShellExecuteW(hWnd, L"open", Utf8ToWide(self->manifest.hostLaunchUri).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                            } else if (self->manifest.mode == "addon" && !self->hostRes.hostExePath.empty() && self->manifest.mainExecutable.empty()) {
                                ::ShellExecuteW(hWnd, L"open", self->hostRes.hostExePath.wstring().c_str(), nullptr,
                                                self->hostRes.hostInstallDir.wstring().c_str(), SW_SHOWNORMAL);
                            } else {
                                ExecutePostExtractOrFinishActions(self->manifest, self->targetDir);
                            }
                        }
                        ::DestroyWindow(hWnd);
                    } else if (self->IsBlockedByMissingRequiredHost()) {
                        if (!self->manifest.hostDownloadUrl.empty()) {
                            ::ShellExecuteW(hWnd, L"open", Utf8ToWide(self->manifest.hostDownloadUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                    } else {
                        self->StartExtractionOrInstall();
                    }
                    return 0;
                }
                if (id == IDC_SMP_CLOSE || id == IDCANCEL) {
                    if (self->running.load()) {
                        if (!self->cancelFlag.load() &&
                            ui::ConfirmSetupCancelDialog(hWnd, Utf8ToWide(self->manifest.productName), true)) {
                            self->cancelFlag.store(true);
                            ::EnableWindow(self->hBtnClose, FALSE);
                            ::SetWindowTextW(self->hStatusLbl, L"Canceling setup and rolling back all changes...");
                        }
                    } else if (self->finished.load()) {
                        if (self->HasPostAction() &&
                            ::SendMessageW(self->hChkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            ExecutePostExtractOrFinishActions(self->manifest, self->targetDir);
                        }
                        ::DestroyWindow(hWnd);
                    } else if (ui::ConfirmSetupCancelDialog(hWnd, Utf8ToWide(self->manifest.productName), false)) {
                        ::DestroyWindow(hWnd);
                    }
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                if (self->running.load()) {
                    if (!self->cancelFlag.load() &&
                        ui::ConfirmSetupCancelDialog(hWnd, Utf8ToWide(self->manifest.productName), true)) {
                        self->cancelFlag.store(true);
                        ::EnableWindow(self->hBtnClose, FALSE);
                        ::SetWindowTextW(self->hStatusLbl, L"Canceling setup and rolling back all changes...");
                    }
                } else if (self->finished.load() ||
                           ui::ConfirmSetupCancelDialog(hWnd, Utf8ToWide(self->manifest.productName), false)) {
                    ::DestroyWindow(hWnd);
                }
                return 0;
            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

/// Main runtime entrypoint when `kResIdNativeManifest` (`#221`) is present (`PackageKind = sfx` or `Engine = native`)!
inline int RunNativeOrSfxEntrypoint(
    HINSTANCE hInstance,
    int nCmdShow,
    const std::vector<std::wstring>& args,
    std::string_view nativeManifestText)
{
    NativeOrSfxManifest manifest = ParseNativeOrSfxManifestIni(nativeManifestText);
    const std::uint8_t* pakPtr = nullptr;
    std::size_t pakSz = 0;
    if (!GetRcDataViewLocal(kResIdSfxOrNativeArchive, pakPtr, pakSz)) {
        return ERROR_INSTALL_PACKAGE_OPEN_FAILED;
    }

    std::vector<FeaturePackRuntimeEntry> featurePacks;
    const std::uint8_t* fpPtr = nullptr;
    std::size_t fpSz = 0;
    if (GetRcDataViewLocal(kResIdFeaturePackManifest, fpPtr, fpSz) && fpSz > 0) {
        featurePacks = ParseFeaturePackManifestIni(std::string_view(reinterpret_cast<const char*>(fpPtr), fpSz));
    }

    bool silentMode     = false;
    bool runPostActions = false;
    bool explicitCliDir = false;
    int  suiteManaged   = -1;
    HostAppResolution hostRes{};
    if (manifest.mode == "addon") {
        hostRes = ResolveHostApplicationForAddOn(manifest);
    }
    std::filesystem::path targetDir = (manifest.mode == "addon")
        ? hostRes.resolvedContentDir
        : ComputeDefaultNativeOrSfxPath(manifest);
    std::filesystem::path suiteForwardedDir;
    std::filesystem::path exportShotDir;
    std::filesystem::path progressStateFile;
    {
        wchar_t envProgBuf[MAX_PATH * 4]{};
        const DWORD nProg = ::GetEnvironmentVariableW(L"RSW_CHILD_PROGRESS_FILE", envProgBuf, static_cast<DWORD>(std::size(envProgBuf)));
        if (nProg > 0 && nProg < std::size(envProgBuf)) {
            progressStateFile = envProgBuf;
        }
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") {
            silentMode = true;
        } else if ((a == L"--extract-to" || a == L"--install-dir") && i + 1 < args.size()) {
            targetDir      = args[++i];
            explicitCliDir = true;
            silentMode     = true;
        } else if (a == L"--progress-state-file" && i + 1 < args.size()) {
            progressStateFile = args[++i];
        } else if (a.rfind(L"-o", 0) == 0 && a.size() > 2) {
            targetDir      = a.substr(2);
            explicitCliDir = true;
            silentMode     = true;
        } else if (a.rfind(L"INSTALLDIR=", 0) == 0) {
            targetDir      = a.substr(11);
            explicitCliDir = true;
        } else if (a.rfind(L"SUITE_INSTALLDIR=", 0) == 0) {
            suiteForwardedDir = a.substr(17);
        } else if (a == L"RS_SUITE_MANAGED=1") {
            suiteManaged = 1;
        } else if (a == L"RS_SUITE_MANAGED=0") {
            suiteManaged = 0;
        } else if (a == L"--run-post-extract") {
            runPostActions = true;
        } else if (a == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[++i];
            ui::ForcedThemeOverride() = 1;
        }
    }

    if (!explicitCliDir && !suiteForwardedDir.empty() && manifest.mode != "sfx" && manifest.mode != "addon") {
        targetDir = (suiteForwardedDir / Utf8ToWide(SlugifyName(manifest.productName))).lexically_normal();
    }

    if (!MeetsWindowsBuildFloor(manifest.minWindowsBuild)) {
        return 1603;
    }

    const auto priv = QueryProcessPrivilegeState();
    if (exportShotDir.empty() &&
        (manifest.requestedExecLevel == "requireAdministrator" || (silentMode && manifest.mode == "native" && manifest.perMachine)) &&
        !priv.HasEffectiveAdminRights()) {
        if (!silentMode) {
            const std::wstring title = Utf8ToWide(manifest.productName) + L" Setup";
            ::TaskDialog(nullptr, hInstance, title.c_str(), L"Administrator Privileges Required",
                         L"Administrator privileges are required to run this package.", TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
        }
        return ERROR_ACCESS_DENIED;
    }

    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath(selfBuf);

    if (silentMode) {
        if (manifest.mode == "addon" && manifest.requireHostInstalled && !hostRes.found) {
            return ERROR_UNKNOWN_PRODUCT; // `1605` — Required host application not installed; never dump orphaned content!
        }

        NativeExecutionOptions opts{};
        opts.targetDir              = targetDir;
        opts.detectedHostDir        = hostRes.hostInstallDir;
        opts.createDesktopShortcuts = true;
        opts.registerFileAssocs     = true;
        opts.suiteManagedFlag       = suiteManaged;
        opts.runningSetupExePath    = selfPath;
        opts.externalPackSearchDir  = selfPath.parent_path();

        std::function<void(int, const std::wstring&)> silentProgCb;
        if (!progressStateFile.empty()) {
            silentProgCb = [&progressStateFile](int perm, const std::wstring& st) {
                std::ofstream pf(progressStateFile, std::ios::binary | std::ios::trunc);
                if (pf) {
                    pf << "Permille=" << std::clamp(perm, 0, 1000) << "\n"
                       << "Status="   << native_detail::EscapeSingleLine(WideToUtf8(st)) << "\n";
                }
            };
        }

        std::string err;
        if (!ExecuteNativeOrSfxTransaction(manifest, pakPtr, pakSz, featurePacks, {}, opts, nullptr, silentProgCb, err)) {
            return ERROR_INSTALL_FAILURE;
        }
        if (manifest.mode == "addon" && !manifest.hostRefreshArgs.empty() && !hostRes.hostExePath.empty()) {
            std::wstring runningName;
            const std::vector<std::wstring> hostPats{hostRes.hostExePath.filename().wstring()};
            if (sfx_wait_detail::FindActiveCompanionOrTempProcess({}, hostPats, false, runningName)) {
                ::ShellExecuteW(nullptr, L"open", hostRes.hostExePath.wstring().c_str(),
                                Utf8ToWide(manifest.hostRefreshArgs).c_str(),
                                hostRes.hostInstallDir.wstring().c_str(), SW_SHOWNOACTIVATE);
            }
        }
        const bool isTempInstallerWrapper =
            (manifest.mode == "sfx" && manifest.extractToTemp && manifest.cleanupTempAfterRun && !manifest.postExtractRun.empty());
        if (runPostActions || isTempInstallerWrapper) {
            DWORD childCode = 0;
            if (!ExecuteSfxLaunchWaitAndCleanup(manifest, targetDir, nullptr, nullptr, childCode, err)) {
                return ERROR_INSTALL_FAILURE;
            }
            return static_cast<int>(childCode);
        }
        return 0;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int exitRc = 0;
    {
        ui::ScopedGdiPlusSession gpSession;
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
        ::InitCommonControlsEx(&icc);

        SimpleCompactDialogApp app{};
        app.manifest         = manifest;
        app.hostRes          = hostRes;
        app.pakPtr           = pakPtr;
        app.pakSize          = pakSz;
        app.featurePacks     = featurePacks;
        app.suiteManagedFlag = suiteManaged;
        app.targetDir        = !exportShotDir.empty()
            ? ((manifest.mode == "sfx")
                   ? std::filesystem::path(L"C:\\RaggieSoft\\Samples\\ZipUtility")
                   : std::filesystem::path(L"C:\\Program Files\\RaggieSoft\\Sample App"))
            : targetDir;
        app.exportShotDir    = exportShotDir;

        const COLORREF pCol = ui::ParseHexColor(manifest.accentPrimaryHex, RGB(37, 99, 235));
        const COLORREF sCol = ui::ParseHexColor(manifest.accentSecondaryHex, RGB(251, 191, 36));
        app.theme.Refresh(pCol, sCol);
        app.hIconBig   = static_cast<HICON>(::LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        app.hIconSmall = static_cast<HICON>(::LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        if (!app.hIconBig) app.hIconBig = ::LoadIconW(nullptr, IDI_APPLICATION);

        constexpr const wchar_t* kCls = L"RaggieSoftSimpleCompactSetupWindow";
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &SimpleCompactDialogApp::WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon         = app.hIconBig;
        wc.hIconSm       = app.hIconSmall ? app.hIconSmall : app.hIconBig;
        wc.lpszClassName = kCls;
        ::RegisterClassExW(&wc);

        const UINT dpi = ::GetDpiForSystem();
        RECT wRc{0, 0,
                 ui::ScalePx(480, dpi, app.theme.textScalePercent),
                 ui::ScalePx(224, dpi, app.theme.textScalePercent)};
        constexpr DWORD kStyle   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        const DWORD kExStyle     = exportShotDir.empty() ? WS_EX_APPWINDOW : 0u;
        ::AdjustWindowRectExForDpi(&wRc, kStyle, FALSE, kExStyle, dpi);
        const int wW = wRc.right - wRc.left;
        const int wH = wRc.bottom - wRc.top;
        const int wX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - wW) / 2);
        const int wY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - wH) / 2);

        const std::wstring wTitle = Utf8ToWide(manifest.productName) +
                                    ((manifest.mode == "sfx")
                                         ? L" \u2014 Self-Extractor"
                                         : ((manifest.mode == "addon") ? L" \u2014 Add-On Setup" : L" Setup"));
        HWND hWnd = ::CreateWindowExW(kExStyle, kCls, wTitle.c_str(), kStyle, wX, wY, wW, wH, nullptr, nullptr, hInstance, &app);
        if (!hWnd) {
            exitRc = 1;
        } else {
            ::ShowWindow(hWnd, nCmdShow);
            ::UpdateWindow(hWnd);
            if (!exportShotDir.empty()) {
                ::SendMessageW(app.hProgBar, PBM_SETRANGE32, 0, 1000);
                ::SendMessageW(app.hProgBar, PBM_SETPOS, 421, 0);
                ::SendMessageW(app.hProgBar, PBM_SETPOS, 420, 0);
                ::SetWindowTextW(
                    app.hStatusLbl,
                    (manifest.mode == "sfx")
                        ? L"Unpacking LZMS archive (42%) \u2014 Docs\\Guide.txt"
                        : L"Installing native payload (42%)...");
                ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const bool okShot = ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-simple-compact.png");
                ::DestroyWindow(hWnd);
                exitRc = okShot ? 0 : 2;
            } else {
                MSG msg{};
                while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    if (!::IsDialogMessageW(hWnd, &msg)) {
                        ::TranslateMessage(&msg);
                        ::DispatchMessageW(&msg);
                    }
                }
                if (app.IsTempSoftwareInstallerMode() && app.finished.load()) {
                    exitRc = static_cast<int>(app.childExitCode);
                }
            }
        }
    }
    ::CoUninitialize();
    return exitRc;
}

// ================================================================================
// UNIFIED SUITE CHAINER EXECUTION, MONOTONIC DUAL-BAR PROGRESS, & TASKBAR (`#230`)
// ================================================================================
enum class SuiteOperationKind {
    InstallOrModify      = 0,
    DetachKeepStandalone = 1,
    FullUninstall        = 2,
    RepairInstalled      = 3
};

namespace suite_detail {

/// Sets a Win32 `ComCtl32 v6` `PROGRESS_CLASSW` bar (`0..1000` permille) to its exact
/// instantaneous pixel fill without `ComCtl32`'s 500ms laggy forward animation timer
/// (so fast NVMe installs and screenshots always match the numeric `%` readout 1:1!).
inline void SetProgressBarPermilleExact(HWND hBar, int permille) noexcept {
    if (!hBar) return;
    const int p = std::clamp(permille, 0, 1000);
    if (p < 1000) {
        ::SendMessageW(hBar, PBM_SETPOS, static_cast<WPARAM>(p + 1), 0);
        ::SendMessageW(hBar, PBM_SETPOS, static_cast<WPARAM>(p), 0);
    } else {
        ::SendMessageW(hBar, PBM_SETRANGE32, 0, 1001);
        ::SendMessageW(hBar, PBM_SETPOS, 1001, 0);
        ::SendMessageW(hBar, PBM_SETPOS, 1000, 0);
        ::SendMessageW(hBar, PBM_SETRANGE32, 0, 1000);
        ::SendMessageW(hBar, PBM_SETPOS, 1000, 0);
    }
}

/// Formats byte counts cleanly (`"512 KB"`, `"14.8 MB"`, `"1.24 GB"`) for Suite checklist rows.
inline std::wstring FormatSuiteBytesShort(std::uint64_t bytes) {
    wchar_t buf[64]{};
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        std::swprintf(buf, std::size(buf), L"%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        std::swprintf(buf, std::size(buf), L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::swprintf(buf, std::size(buf), L"%llu KB", static_cast<unsigned long long>(std::max<std::uint64_t>(1ull, bytes / 1024ull)));
    }
    return buf;
}

/// Self-contained Windows 7+ / Windows 11 `ITaskbarList3` taskbar progress helper for Suite Setup.
/// Architectural Rule: The Windows Taskbar button ALWAYS displays `overallPermille` (`0..1000`)
/// monotonically across the entire Suite—NEVER resetting per component!
struct SuiteTaskbarProgress {
    ITaskbarList3* pList         = nullptr;
    UINT           wmCreated     = 0;
    TBPFLAG        lastState     = TBPF_NOPROGRESS;
    ULONGLONG      lastCompleted = 0;

    void AttachWindow(HWND hWnd) noexcept {
        if (wmCreated == 0) wmCreated = ::RegisterWindowMessageW(L"TaskbarButtonCreated");
        if (hWnd && wmCreated != 0) {
            CHANGEFILTERSTRUCT cfs{sizeof(cfs), 0};
            ::ChangeWindowMessageFilterEx(hWnd, wmCreated, MSGFLT_ALLOW, &cfs);
        }
    }

    bool EnsureInit() noexcept {
        if (pList) return true;
        ITaskbarList3* raw = nullptr;
        if (SUCCEEDED(::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))) && raw) {
            if (SUCCEEDED(raw->HrInit())) {
                pList = raw;
                return true;
            }
            raw->Release();
        }
        return false;
    }

    void Release() noexcept {
        if (pList) {
            pList->Release();
            pList = nullptr;
        }
    }

    ~SuiteTaskbarProgress() { Release(); }

    void SetOverallPermille(HWND hWnd, int overallPermille, bool paused) noexcept {
        lastCompleted = static_cast<ULONGLONG>(std::clamp(overallPermille, 0, 1000));
        lastState     = paused ? TBPF_PAUSED : TBPF_NORMAL;
        if (hWnd && EnsureInit()) {
            pList->SetProgressValue(hWnd, paused ? std::max<ULONGLONG>(40, lastCompleted) : lastCompleted, 1000);
            pList->SetProgressState(hWnd, lastState);
        }
    }

    void SetError(HWND hWnd) noexcept {
        lastState = TBPF_ERROR;
        if (hWnd && EnsureInit()) {
            pList->SetProgressValue(hWnd, std::max<ULONGLONG>(100, lastCompleted), 1000);
            pList->SetProgressState(hWnd, TBPF_ERROR);
        }
    }

    void Clear(HWND hWnd) noexcept {
        lastState     = TBPF_NOPROGRESS;
        lastCompleted = 0;
        if (hWnd && EnsureInit()) pList->SetProgressState(hWnd, TBPF_NOPROGRESS);
    }

    bool HandleMessage(HWND hWnd, UINT msg) noexcept {
        if (wmCreated != 0 && msg == wmCreated) {
            Release();
            if (lastState != TBPF_NOPROGRESS && hWnd && EnsureInit()) {
                pList->SetProgressValue(hWnd, lastCompleted, 1000);
                pList->SetProgressState(hWnd, lastState);
            }
            return true;
        }
        return false;
    }
};

/// Live `MsiSetExternalUIRecord` state streaming `0..1000` component progress for child `.msi` installs!
struct SuiteChildMsiTracker {
    long long                           totalTicks = 1000;
    long long                           curTicks   = 0;
    int                                 stepTicks  = 1;
    bool                                forward    = true;
    int                                 highWater  = 0;
    std::wstring                        actionDesc;
    std::string                         errorUtf8;
    const std::atomic<bool>*            cancelFlag = nullptr;
    std::function<void(int, const std::wstring&)> emitUpdate;

    static INT CALLBACK OnRecord(LPVOID pvContext, UINT iMessageType, MSIHANDLE hRecord) {
        auto* st = static_cast<SuiteChildMsiTracker*>(pvContext);
        if (!st) return 0;
        if (st->cancelFlag && st->cancelFlag->load()) return IDCANCEL;

        const UINT mt = iMessageType & 0xFF000000u;
        if (mt == INSTALLMESSAGE_ACTIONSTART && hRecord != 0) {
            wchar_t descBuf[512]{};
            DWORD cch = static_cast<DWORD>(std::size(descBuf));
            if (::MsiRecordGetFieldCount(hRecord) >= 2 &&
                ::MsiRecordGetStringW(hRecord, 2, descBuf, &cch) == ERROR_SUCCESS && descBuf[0] != L'\0') {
                st->actionDesc = descBuf;
                if (st->emitUpdate) st->emitUpdate(st->highWater, st->actionDesc);
            }
            return IDOK;
        }
        if ((mt == INSTALLMESSAGE_ERROR || mt == INSTALLMESSAGE_FATALEXIT) && hRecord != 0) {
            wchar_t errBuf[1024]{};
            DWORD cch = static_cast<DWORD>(std::size(errBuf));
            ::MsiFormatRecordW(0, hRecord, errBuf, &cch);
            if (errBuf[0] != L'\0') st->errorUtf8 = WideToUtf8(errBuf);
            return IDOK;
        }
        if (mt == INSTALLMESSAGE_PROGRESS && hRecord != 0 && ::MsiRecordGetFieldCount(hRecord) > 0) {
            const int subType = ::MsiRecordGetInteger(hRecord, 1);
            const int f2      = ::MsiRecordGetInteger(hRecord, 2);
            const int f3      = ::MsiRecordGetInteger(hRecord, 3);
            if (subType == 0) {
                st->totalTicks = std::max<long long>(1, f2);
                st->forward    = (f3 == 0);
                st->curTicks   = st->forward ? 0 : st->totalTicks;
            } else if (subType == 1) {
                if (f3 != 0) st->stepTicks = std::max<int>(1, f2);
            } else if (subType == 2) {
                const long long delta = (f2 != MSI_NULL_INTEGER && f2 > 0) ? f2 : st->stepTicks;
                if (st->forward) st->curTicks += delta;
                else st->curTicks = std::max<long long>(0, st->curTicks - delta);
            } else if (subType == 3) {
                st->totalTicks += std::max<int>(0, f2);
            }
            const int rawPerm = static_cast<int>(
                std::clamp<long long>((st->curTicks * 1000LL) / std::max<long long>(1, st->totalTicks), 0, 995));
            st->highWater = std::max(st->highWater, rawPerm);
            if (st->emitUpdate) st->emitUpdate(st->highWater, st->actionDesc);
            return IDOK;
        }
        return 0;
    }
};

} // namespace suite_detail

/// Executes a Unified Suite Chainer operation (`InstallOrModify`, `DetachKeepStandalone`, or `FullUninstall`),
/// streaming **Dual-Bar Monotonic Suite Progress** (`overallPermille` byte-weighted & strictly non-decreasing +
/// `componentPermille` `0..1000` per active package), dynamically adopting existing standalone siblings via
/// `RS_SUITE_MANAGED=1` / `SystemComponent=1`, and registering ONE clean Suite Maintenance entry!
inline int ExecuteSuiteChainerOperationDetailed(
    const SuiteChainerManifest& suite,
    SuiteOperationKind opKind,
    const std::set<std::string>& selectedChildIdsLower,
    const std::filesystem::path& cliInstallDirOverride,
    const std::atomic<bool>* cancelFlag,
    const std::function<void(const SuiteProgressSnapshot&)>& onSuiteProgress,
    std::string& outError)
{
    std::error_code ec;
    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath(selfBuf);

    const auto existingSuiteReg = QuerySuiteInstalledRegistration(suite);

    wchar_t baseEnv[MAX_PATH * 2]{};
    const wchar_t* envName = suite.perMachine ? L"ProgramW6432" : L"LOCALAPPDATA";
    DWORD nEnv = ::GetEnvironmentVariableW(envName, baseEnv, static_cast<DWORD>(std::size(baseEnv)));
    if (nEnv == 0) nEnv = ::GetEnvironmentVariableW(L"ProgramFiles", baseEnv, static_cast<DWORD>(std::size(baseEnv)));
    const std::filesystem::path suiteInstallDir = !cliInstallDirOverride.empty()
        ? cliInstallDirOverride.lexically_normal()
        : (!existingSuiteReg.installLocation.empty()
               ? existingSuiteReg.installLocation.lexically_normal()
               : (((nEnv > 0 ? std::filesystem::path(baseEnv) : std::filesystem::path(L"C:\\Program Files")) /
                   Utf8ToWide(suite.defaultSubdir)).lexically_normal()));

    const std::string suiteKeyLeaf = ComputeSuiteUninstallKeyLeaf(suite);
    const std::wstring suiteUninstSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + Utf8ToWide(suiteKeyLeaf);
    const HKEY hSuiteHive = suite.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const std::string suiteHiveCode = suite.perMachine ? "HKLM" : "HKCU";
    const auto virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();

    const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                        (L"rsw-suite-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir, ec);

    // Optional unit-test hook (`RSW_TEST_SUITE_CANCEL_AFTER_STEP=N`) to deterministically verify
    // mid-Suite cancellation & full LIFO session rollback in `insttests.exe`!
    int testCancelAfterStep = 0;
    {
        wchar_t tcBuf[64]{};
        if (::GetEnvironmentVariableW(L"RSW_TEST_SUITE_CANCEL_AFTER_STEP", tcBuf, static_cast<DWORD>(std::size(tcBuf))) > 0) {
            testCancelAfterStep = std::atoi(WideToUtf8(tcBuf).c_str());
        }
    }

    // Build deterministic active execution plan BEFORE starting so byte weights & step counts are exact!
    struct PlannedSuiteStep {
        std::size_t childIdx          = 0;
        bool        wantInstall       = true; // `false` = remove deselected installed child during Modify
        bool        alreadyInstalled  = false;
        std::wstring msiProductCode;
        std::wstring nativeQuietUninstall;
    };
    std::vector<PlannedSuiteStep> plan;
    std::vector<std::uint64_t>    stepWeights;

    for (std::size_t i = 0; i < suite.children.size(); ++i) {
        const auto& ch = suite.children[i];
        std::wstring msiProdCode, nativeQuiet;
        const bool alreadyInst = IsSuiteChildCurrentlyInstalled(ch, msiProdCode, nativeQuiet);

        if (opKind == SuiteOperationKind::DetachKeepStandalone || opKind == SuiteOperationKind::FullUninstall) {
            if (!alreadyInst) continue;
            plan.push_back({i, false, true, msiProdCode, nativeQuiet});
            stepWeights.push_back(std::max<std::uint64_t>(256ull * 1024ull, ch.uncompressedBytes));
        } else if (opKind == SuiteOperationKind::RepairInstalled) {
            if (!alreadyInst && !ch.required) continue;
            plan.push_back({i, true, alreadyInst, msiProdCode, nativeQuiet});
            stepWeights.push_back(std::max<std::uint64_t>(256ull * 1024ull, ch.uncompressedBytes));
        } else {
            const bool wantInstalled = ch.required ||
                                       selectedChildIdsLower.empty() ||
                                       (selectedChildIdsLower.count(AsciiLower(ch.id)) > 0);
            if (!wantInstalled && !alreadyInst) continue;
            plan.push_back({i, wantInstalled, alreadyInst, msiProdCode, nativeQuiet});
            stepWeights.push_back(std::max<std::uint64_t>(256ull * 1024ull, ch.uncompressedBytes));
        }
    }

    int highWaterOverallPermille = 0;
    auto emitProgress = [&](std::size_t stepIdx, int compPerm, const std::wstring& compName, const std::wstring& detail, bool paused = false) {
        if (!onSuiteProgress) return;
        SuiteProgressSnapshot snap{};
        snap.stepIndex         = stepIdx;
        snap.stepCount         = std::max<std::size_t>(1, plan.size());
        snap.componentPermille = std::clamp(compPerm, 0, 1000);
        snap.overallPermille   = ComputeSuiteWeightedOverallPermille(stepWeights, stepIdx, snap.componentPermille, highWaterOverallPermille);
        snap.queuePaused       = paused;
        snap.componentName     = compName;
        snap.detailStatus      = detail;
        onSuiteProgress(snap);
    };

    auto extractSuiteChildBinary = [&](std::size_t stepIdx, const SuiteChildSpec& ch, std::filesystem::path& outPath) -> bool {
        const std::wstring wName = Utf8ToWide(ch.name);
        auto verifyChildSha = [&](const std::filesystem::path& p) -> bool {
            if (ch.sha256Hex.empty()) return true;
            std::string actualSha;
            if (!ComputeFileSha256Hex(p, actualSha) || !EqualsIgnoreCase(actualSha, ch.sha256Hex)) {
                outError = "SHA-256 verification failed for suite child package '" + ch.name + "'.";
                return false;
            }
            return true;
        };

        if (ch.delivery == "side-by-side") {
            const std::wstring wFn = Utf8ToWide(ch.fileName);
            std::vector<std::filesystem::path> sideCandidates{
                selfPath.parent_path() / L"packages" / wFn,
                selfPath.parent_path() / wFn
            };
            if (!existingSuiteReg.installSource.empty()) {
                sideCandidates.push_back(existingSuiteReg.installSource / L"packages" / wFn);
                sideCandidates.push_back(existingSuiteReg.installSource / wFn);
            }
            outPath.clear();
            for (const auto& cand : sideCandidates) {
                if (std::filesystem::is_regular_file(cand, ec)) {
                    outPath = cand;
                    break;
                }
            }
            if (outPath.empty()) {
                outError = "Missing side-by-side suite package 'packages\\" + ch.fileName + "' for '" + ch.name +
                           "'. Please run setup from the original Suite folder containing 'packages\\'.";
                return false;
            }
            return verifyChildSha(outPath);
        }

        if (ch.delivery == "web") {
            outPath = tmpDir / Utf8ToWide(ch.fileName.empty() ? (ch.id + "-setup.exe") : ch.fileName);
            if (ch.downloadUrl.rfind("file://", 0) == 0 || ch.downloadUrl.rfind("FILE://", 0) == 0) {
                std::string localPart = ch.downloadUrl.substr(7);
                if (!localPart.empty() && localPart.front() == '/' && localPart.size() > 2 && localPart[2] == ':') {
                    localPart.erase(localPart.begin());
                }
                for (char& c : localPart) if (c == '/') c = '\\';
                const std::filesystem::path localSrc(Utf8ToWide(localPart));
                emitProgress(stepIdx, 180, wName, L"Fetching workload package: " + Utf8ToWide(ch.fileName));
                if (!std::filesystem::copy_file(localSrc, outPath, std::filesystem::copy_options::overwrite_existing, ec)) {
                    outError = "Failed copying file:// workload '" + ch.downloadUrl + "' for '" + ch.name + "'.";
                    return false;
                }
            } else {
                emitProgress(stepIdx, 50, wName, L"Downloading workload: " + Utf8ToWide(ch.fileName) + L"...");
                if (!DownloadFileViaWinHttp(
                        ch.downloadUrl,
                        outPath,
                        cancelFlag,
                        [&](std::uint64_t gotBytes, std::uint64_t totalBytes) {
                            const int dlPerm = (totalBytes > 0)
                                ? static_cast<int>(std::clamp<std::uint64_t>((gotBytes * 420ull) / totalBytes, 20ull, 420ull))
                                : 220;
                            emitProgress(stepIdx, dlPerm, wName,
                                         L"Downloading " + Utf8ToWide(ch.fileName) + L" (" + suite_detail::FormatSuiteBytesShort(gotBytes) + L")...");
                        },
                        outError)) {
                    return false;
                }
            }
            if (!verifyChildSha(outPath)) {
                std::filesystem::remove(outPath, ec);
                return false;
            }
            emitProgress(stepIdx, 440, wName, L"Verified SHA-256 signature; starting installer...");
            return true;
        }

        const std::uint8_t* ptr = nullptr;
        std::size_t sz = 0;
        if (!GetRcDataViewLocal(ch.embeddedResId, ptr, sz)) {
            outError = "Missing embedded resource (#" + std::to_string(ch.embeddedResId) + ") for suite package '" + ch.name + "'.";
            return false;
        }
        std::vector<std::uint8_t> rawBytes;
        if (!DecompressLzmsOrCopyRaw(ptr, sz, rawBytes, outError)) return false;

        outPath = tmpDir / Utf8ToWide(ch.fileName);
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            outError = "Could not stage suite child binary: " + ch.fileName;
            return false;
        }
        if (!rawBytes.empty()) {
            out.write(reinterpret_cast<const char*>(rawBytes.data()), static_cast<std::streamsize>(rawBytes.size()));
        }
        out.close();
        return verifyChildSha(outPath);
    };

    auto runMsiWithLiveTelemetry = [&](std::size_t stepIdx,
                                       const std::wstring& compName,
                                       const std::function<UINT()>& invokeMsi,
                                       std::string& ioMsiErr) -> UINT {
        while (IsSystemMsiExecuteMutexBusy()) {
            if (cancelFlag && cancelFlag->load()) return ERROR_INSTALL_USEREXIT;
            const std::wstring holder = ScopedActiveMsiBeacon::ReadActiveProductName();
            const std::wstring waitTxt = !holder.empty()
                ? (L"Waiting for " + holder + L" Setup to finish...")
                : L"Waiting for another Windows installation to finish...";
            emitProgress(stepIdx, 0, compName, waitTxt, true);
            ::Sleep(200);
        }

        suite_detail::SuiteChildMsiTracker tracker{};
        tracker.cancelFlag = cancelFlag;
        tracker.emitUpdate = [&](int cPerm, const std::wstring& actTxt) {
            emitProgress(stepIdx, cPerm, compName, actTxt.empty() ? L"Configuring Windows Installer package..." : actTxt, false);
        };

        constexpr DWORD kFilter =
            INSTALLLOGMODE_FATALEXIT | INSTALLLOGMODE_ERROR |
            INSTALLLOGMODE_ACTIONSTART | INSTALLLOGMODE_PROGRESS;
        INSTALLUI_HANDLER_RECORD prevHandler = nullptr;
        ::MsiSetExternalUIRecord(&suite_detail::SuiteChildMsiTracker::OnRecord, kFilter, &tracker, &prevHandler);
        UINT rc = ERROR_INSTALL_FAILURE;
        {
            ScopedActiveMsiBeacon beacon(compName);
            rc = invokeMsi();
        }
        ::MsiSetExternalUIRecord(prevHandler, 0, nullptr, nullptr);
        if (!tracker.errorUtf8.empty()) ioMsiErr = tracker.errorUtf8;
        return rc;
    };

    auto runNativeProcessWithLiveTelemetry = [&](std::size_t stepIdx,
                                                 const std::wstring& compName,
                                                 std::wstring cmdLine,
                                                 const std::wstring& initialVerb,
                                                 DWORD& outExitCode) -> bool {
        outExitCode = 1;
        const auto progFile        = tmpDir / (L"child-prog-"   + std::to_wstring(stepIdx) + L".ini");
        const auto childCancelFile = tmpDir / (L"child-cancel-" + std::to_wstring(stepIdx) + L".flag");
        std::filesystem::remove(progFile, ec);
        std::filesystem::remove(childCancelFile, ec);
        ::SetEnvironmentVariableW(L"RSW_CHILD_PROGRESS_FILE", progFile.wstring().c_str());
        ::SetEnvironmentVariableW(L"RSW_CHILD_CANCEL_FILE",   childCancelFile.wstring().c_str());

        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        const BOOL launched = ::CreateProcessW(
            nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, tmpDir.wstring().c_str(), &si, &pi);
        ::SetEnvironmentVariableW(L"RSW_CHILD_PROGRESS_FILE", nullptr);
        ::SetEnvironmentVariableW(L"RSW_CHILD_CANCEL_FILE",   nullptr);
        if (!launched) return false;

        int bestCompPerm = 50;
        bool signaledChildCancel = false;
        ULONGLONG cancelTickStart = 0;
        emitProgress(stepIdx, bestCompPerm, compName, initialVerb, false);
        while (::WaitForSingleObject(pi.hProcess, 40) == WAIT_TIMEOUT) {
            if (cancelFlag && cancelFlag->load()) {
                if (!signaledChildCancel) {
                    signaledChildCancel = true;
                    cancelTickStart     = ::GetTickCount64();
                    std::ofstream cf(childCancelFile, std::ios::binary | std::ios::trunc);
                    cf << "1";
                    emitProgress(stepIdx, bestCompPerm, compName, L"Rolling back " + compName + L"...", true);
                } else if (::GetTickCount64() - cancelTickStart > 15000ull) {
                    // Fallback safety valve only if an external child `.exe` does not exit after 15s!
                    ::TerminateProcess(pi.hProcess, ERROR_INSTALL_USEREXIT);
                    break;
                }
            }
            std::vector<std::uint8_t> rawP;
            std::string dummy;
            if (ReadBinaryFileBytes(progFile, rawP, dummy) && !rawP.empty()) {
                auto pkv = native_detail::ParseFlatIniMap(
                    std::string_view(reinterpret_cast<const char*>(rawP.data()), rawP.size()));
                const int pVal = std::clamp(std::atoi(pkv["Permille"].c_str()), 0, 995);
                bestCompPerm = std::max(bestCompPerm, pVal);
                const std::wstring stW = pkv["Status"].empty() ? initialVerb : Utf8ToWide(pkv["Status"]);
                emitProgress(stepIdx, bestCompPerm, compName, stW, signaledChildCancel);
            }
        }
        ::GetExitCodeProcess(pi.hProcess, &outExitCode);
        if (signaledChildCancel && outExitCode != 0) {
            outExitCode = ERROR_INSTALL_USEREXIT;
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        std::filesystem::remove(progFile, ec);
        std::filesystem::remove(childCancelFile, ec);
        return true;
    };

    ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);

    if (onSuiteProgress) {
        SuiteProgressSnapshot initSnap{};
        initSnap.overallPermille   = 15;
        initSnap.componentPermille = 0;
        initSnap.stepIndex         = 0;
        initSnap.stepCount         = std::max<std::size_t>(1, plan.size());
        initSnap.componentName     = Utf8ToWide(suite.suiteName);
        initSnap.detailStatus      = L"Preparing Unified Suite transaction...";
        highWaterOverallPermille   = 15;
        onSuiteProgress(initSnap);
    }

    if (opKind == SuiteOperationKind::DetachKeepStandalone) {
        for (std::size_t sIdx = 0; sIdx < plan.size(); ++sIdx) {
            if (cancelFlag && cancelFlag->load()) {
                std::filesystem::remove_all(tmpDir, ec);
                outError = "Suite operation canceled by user.";
                return ERROR_INSTALL_USEREXIT;
            }
            const auto& step = plan[sIdx];
            const auto& ch   = suite.children[step.childIdx];
            const std::wstring wName = Utf8ToWide(ch.name);
            emitProgress(sIdx, 0, wName, L"Restoring standalone Installed Apps visibility...");

            if (!step.msiProductCode.empty()) {
                std::filesystem::path stagedMsi;
                if (extractSuiteChildBinary(sIdx, ch, stagedMsi)) {
                    std::string dummyErr;
                    runMsiWithLiveTelemetry(sIdx, wName, [&]() {
                        return ::MsiInstallProductW(stagedMsi.wstring().c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus RS_SUITE_MANAGED=0");
                    }, dummyErr);
                }
                SetInstalledAppsSystemComponentFlag(step.msiProductCode, false);
            }
            if (!ch.upgradeCode.empty()) {
                SetInstalledAppsSystemComponentFlag(Utf8ToWide(ch.upgradeCode), false);
            }
            emitProgress(sIdx, 1000, wName, L"Restored standalone visibility.");
        }
        if (!virtRegRoot.empty()) native_detail::VirtualRegDeleteTree(virtRegRoot, suiteHiveCode, suiteUninstSubKey);
        else ::RegDeleteTreeW(hSuiteHive, suiteUninstSubKey.c_str());
        if (_wcsicmp(selfPath.wstring().c_str(), (suiteInstallDir / L"SuiteSetup.exe").wstring().c_str()) != 0) {
            std::filesystem::remove(suiteInstallDir / L"SuiteSetup.exe", ec);
            std::filesystem::remove(suiteInstallDir, ec);
        }
        std::filesystem::remove_all(tmpDir, ec);
        if (onSuiteProgress) {
            onSuiteProgress({1000, 1000, plan.size(), std::max<std::size_t>(1, plan.size()), false,
                             Utf8ToWide(suite.suiteName), L"Suite detached; member applications kept standalone."});
        }
        return 0;
    }

    if (opKind == SuiteOperationKind::FullUninstall) {
        for (std::size_t sIdx = 0; sIdx < plan.size(); ++sIdx) {
            if (cancelFlag && cancelFlag->load()) {
                std::filesystem::remove_all(tmpDir, ec);
                outError = "Suite uninstallation canceled by user.";
                return ERROR_INSTALL_USEREXIT;
            }
            const auto& step = plan[sIdx];
            const auto& ch   = suite.children[step.childIdx];
            const std::wstring wName = Utf8ToWide(ch.name);
            emitProgress(sIdx, 0, wName, L"Uninstalling " + wName + L"...");

            if (!step.msiProductCode.empty()) {
                std::string msiErr;
                runMsiWithLiveTelemetry(sIdx, wName, [&]() {
                    return ::MsiConfigureProductExW(step.msiProductCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
                }, msiErr);
            } else if (!step.nativeQuietUninstall.empty()) {
                DWORD code = 0;
                runNativeProcessWithLiveTelemetry(sIdx, wName, step.nativeQuietUninstall, L"Removing application files...", code);
            }
            emitProgress(sIdx, 1000, wName, L"Removed " + wName + L".");
        }
        if (!virtRegRoot.empty()) native_detail::VirtualRegDeleteTree(virtRegRoot, suiteHiveCode, suiteUninstSubKey);
        else ::RegDeleteTreeW(hSuiteHive, suiteUninstSubKey.c_str());
        if (_wcsicmp(selfPath.wstring().c_str(), (suiteInstallDir / L"SuiteSetup.exe").wstring().c_str()) != 0) {
            std::filesystem::remove(suiteInstallDir / L"SuiteSetup.exe", ec);
            std::filesystem::remove(suiteInstallDir, ec);
        }
        std::filesystem::remove_all(tmpDir, ec);
        if (onSuiteProgress) {
            onSuiteProgress({1000, 1000, plan.size(), std::max<std::size_t>(1, plan.size()), false,
                             Utf8ToWide(suite.suiteName), L"Suite and member applications uninstalled."});
        }
        return 0;
    }

    // `SuiteOperationKind::InstallOrModify` & `SuiteOperationKind::RepairInstalled`:
    const bool isRepairOp = (opKind == SuiteOperationKind::RepairInstalled);
    struct NewlyInstalledSuiteChild {
        std::size_t  childIdx = 0;
        std::wstring name;
        std::string  engine;
        std::wstring msiProductCode;
    };
    std::vector<NewlyInstalledSuiteChild> rollbackStack;            // Savepoint-aware stack for unexpected child errors
    std::vector<NewlyInstalledSuiteChild> allSessionNewlyInstalled; // Full session stack for explicit user Cancel!
    std::vector<std::wstring>             adoptedStandaloneKeys;    // Restores `SystemComponent=0` if session is canceled!
    const bool enableSuiteLifoRollback = EqualsIgnoreCase(suite.rollbackPolicy, "rollback_new");

    auto performSuiteLifoRollback = [&](std::size_t failedStepIdx, bool unconditionalUserCancel) {
        if (!unconditionalUserCancel && !enableSuiteLifoRollback) return;
        auto& targetStack = unconditionalUserCancel ? allSessionNewlyInstalled : rollbackStack;
        for (auto it = targetStack.rbegin(); it != targetStack.rend(); ++it) {
            emitProgress(failedStepIdx, 0, it->name, L"Rolling back newly installed package: " + it->name + L"...", true);
            if (it->engine == "msi" && !it->msiProductCode.empty()) {
                ::MsiConfigureProductExW(it->msiProductCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
            } else {
                std::wstring dummyMsi, quietUninst;
                if (IsSuiteChildCurrentlyInstalled(suite.children[it->childIdx], dummyMsi, quietUninst) && !quietUninst.empty()) {
                    STARTUPINFOW si{sizeof(si)};
                    PROCESS_INFORMATION pi{};
                    std::wstring mutCmd = quietUninst;
                    if (::CreateProcessW(nullptr, mutCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                        ::WaitForSingleObject(pi.hProcess, 120000);
                        ::CloseHandle(pi.hThread);
                        ::CloseHandle(pi.hProcess);
                    }
                }
            }
        }
        if (unconditionalUserCancel) {
            for (const auto& leaf : adoptedStandaloneKeys) {
                SetInstalledAppsSystemComponentFlag(leaf, false);
            }
            adoptedStandaloneKeys.clear();
        }
        rollbackStack.clear();
        allSessionNewlyInstalled.clear();
    };

    bool anyReboot = false;
    for (std::size_t sIdx = 0; sIdx < plan.size(); ++sIdx) {
        if ((cancelFlag && cancelFlag->load()) ||
            (testCancelAfterStep > 0 && static_cast<int>(sIdx) >= testCancelAfterStep)) {
            performSuiteLifoRollback(sIdx, true);
            std::filesystem::remove_all(tmpDir, ec);
            outError = "Suite installation canceled by user.";
            return ERROR_INSTALL_USEREXIT;
        }
        const auto& step = plan[sIdx];
        const auto& ch   = suite.children[step.childIdx];
        const std::wstring wName = Utf8ToWide(ch.name);

        if (!step.wantInstall) {
            emitProgress(sIdx, 0, wName, L"Removing deselected component...");
            if (!step.msiProductCode.empty()) {
                std::string dummyErr;
                runMsiWithLiveTelemetry(sIdx, wName, [&]() {
                    return ::MsiConfigureProductExW(step.msiProductCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
                }, dummyErr);
            } else if (!step.nativeQuietUninstall.empty()) {
                DWORD code = 0;
                runNativeProcessWithLiveTelemetry(sIdx, wName, step.nativeQuietUninstall, L"Removing deselected component...", code);
            }
            emitProgress(sIdx, 1000, wName, L"Component removed.");
            continue;
        }

        // In `RepairInstalled` mode, if an `.msi` child is already installed and `side-by-side` media
        // was moved/unmounted, repair directly from Windows Installer's cached database without failing!
        std::filesystem::path stagedChild;
        const bool haveStagedChild = extractSuiteChildBinary(sIdx, ch, stagedChild);
        if (!haveStagedChild) {
            if (isRepairOp && step.alreadyInstalled) {
                outError.clear();
                if (ch.engine == "msi" && !step.msiProductCode.empty()) {
                    emitProgress(sIdx, 200, wName, L"Repairing installed package: " + wName + L"...");
                    std::string msiErr;
                    runMsiWithLiveTelemetry(sIdx, wName, [&]() {
                        return ::MsiReinstallProductW(
                            step.msiProductCode.c_str(),
                            REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_FILEVERIFY |
                            REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA | REINSTALLMODE_SHORTCUT);
                    }, msiErr);
                    SetInstalledAppsSystemComponentFlag(step.msiProductCode, true);
                } else if (!ch.upgradeCode.empty()) {
                    SetInstalledAppsSystemComponentFlag(Utf8ToWide(ch.upgradeCode), true);
                }
                emitProgress(sIdx, 1000, wName, L"Verified " + wName + L".");
                continue;
            }
            performSuiteLifoRollback(sIdx, false);
            std::filesystem::remove_all(tmpDir, ec);
            return ERROR_INSTALL_PACKAGE_OPEN_FAILED;
        }

        const std::wstring verb = isRepairOp
            ? (L"Repairing & verifying " + wName + L"...")
            : (step.alreadyInstalled
                   ? (L"Adopting standalone installation into Suite: " + wName)
                   : (L"Installing " + wName + L"..."));
        emitProgress(sIdx, (ch.delivery == "web") ? 450 : 35, wName, verb);

        const std::wstring preCheckLeaf = !step.msiProductCode.empty() ? step.msiProductCode : Utf8ToWide(ch.upgradeCode);
        const bool wasStandaloneVisibleBefore = step.alreadyInstalled && !preCheckLeaf.empty() &&
                                                !IsInstalledAppHiddenAsSuiteComponent(preCheckLeaf);

        std::wstring installedMsiProdCode;
        if (ch.engine == "msi") {
            std::wstring props = (isRepairOp || (step.alreadyInstalled && ch.adoptStandalone))
                ? L"REINSTALL=ALL REINSTALLMODE=vomus RS_SUITE_MANAGED=1"
                : L"RS_SUITE_MANAGED=1";
            if (ch.forwardInstallDir) {
                props += L" SUITE_INSTALLDIR=\"" + suiteInstallDir.wstring() + L"\"";
            }
            if (!ch.installArgs.empty()) {
                props += L" " + Utf8ToWide(ch.installArgs);
            }
            std::string msiErr;
            const UINT rc = runMsiWithLiveTelemetry(sIdx, wName, [&]() {
                return ::MsiInstallProductW(stagedChild.wstring().c_str(), props.c_str());
            }, msiErr);
            if (rc == ERROR_INSTALL_USEREXIT || (cancelFlag && cancelFlag->load())) {
                performSuiteLifoRollback(sIdx, true);
                outError = "Installation canceled by user.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_USEREXIT;
            } else if (rc == ERROR_SUCCESS_REBOOT_REQUIRED) {
                anyReboot = true;
            } else if (rc != ERROR_SUCCESS && rc != ERROR_INSTALL_SERVICE_FAILURE) {
                performSuiteLifoRollback(sIdx, false);
                outError = "MSI installation failed on '" + ch.name + "' (code " + std::to_string(rc) + ")" +
                           (msiErr.empty() ? "." : (": " + msiErr));
                std::filesystem::remove_all(tmpDir, ec);
                return static_cast<int>(rc);
            }
            wchar_t newProdBuf[39]{};
            if (!ch.upgradeCode.empty() &&
                ::MsiEnumRelatedProductsW(Utf8ToWide(ch.upgradeCode).c_str(), 0, 0, newProdBuf) == ERROR_SUCCESS) {
                installedMsiProdCode = newProdBuf;
                SetInstalledAppsSystemComponentFlag(newProdBuf, true);
                if (wasStandaloneVisibleBefore) adoptedStandaloneKeys.push_back(newProdBuf);
            }
        } else {
            std::wstring cmd = L"\"" + stagedChild.wstring() + L"\" /s RS_SUITE_MANAGED=1";
            if (ch.forwardInstallDir) {
                cmd += L" SUITE_INSTALLDIR=\"" + suiteInstallDir.wstring() + L"\"";
            }
            if (!ch.installArgs.empty()) cmd += L" " + Utf8ToWide(ch.installArgs);
            DWORD code = 1;
            if (!runNativeProcessWithLiveTelemetry(sIdx, wName, cmd, verb, code)) {
                performSuiteLifoRollback(sIdx, false);
                outError = "Failed launching child setup '" + ch.name + "'.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_FAILURE;
            }
            if (code == ERROR_INSTALL_USEREXIT || (cancelFlag && cancelFlag->load())) {
                performSuiteLifoRollback(sIdx, true);
                outError = "Suite installation canceled by user.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_USEREXIT;
            }
            if (code == 3010 || code == 1641) anyReboot = true;
            else if (code != 0) {
                performSuiteLifoRollback(sIdx, false);
                outError = "Child setup '" + ch.name + "' exited with code " + std::to_string(code) + ".";
                std::filesystem::remove_all(tmpDir, ec);
                return static_cast<int>(code);
            }
            if (!ch.upgradeCode.empty()) {
                SetInstalledAppsSystemComponentFlag(Utf8ToWide(ch.upgradeCode), true);
                if (wasStandaloneVisibleBefore) adoptedStandaloneKeys.push_back(Utf8ToWide(ch.upgradeCode));
            }
        }

        if (!step.alreadyInstalled) {
            NewlyInstalledSuiteChild rec{step.childIdx, wName, ch.engine, installedMsiProdCode};
            rollbackStack.push_back(rec);
            allSessionNewlyInstalled.push_back(std::move(rec));
        }
        if (ch.rollbackBoundary) {
            rollbackStack.clear(); // Commit savepoint for unexpected errors (explicit user Cancel still unwinds `allSessionNewlyInstalled`!)
        }

        // Complete this component's bar at `100%` (`1000` permille) before advancing to the next component!
        emitProgress(sIdx, 1000, wName, L"Completed " + wName + L".");
    }

    if (cancelFlag && cancelFlag->load()) {
        performSuiteLifoRollback(plan.size(), true);
        std::filesystem::remove_all(tmpDir, ec);
        outError = "Suite installation canceled by user.";
        return ERROR_INSTALL_USEREXIT;
    }

    // Copy Suite Hub binary to `<suiteInstallDir>\SuiteSetup.exe` & register ONE Suite Maintenance entry!
    if (onSuiteProgress) {
        highWaterOverallPermille = std::max(highWaterOverallPermille, 975);
        onSuiteProgress({highWaterOverallPermille, 1000, plan.size(), std::max<std::size_t>(1, plan.size()), false,
                         Utf8ToWide(suite.suiteName), L"Registering unified Suite Maintenance hub..."});
    }

    std::filesystem::create_directories(suiteInstallDir, ec);
    const auto installedSuiteExe = suiteInstallDir / L"SuiteSetup.exe";
    const bool runningFromInstalledHub = (_wcsicmp(selfPath.wstring().c_str(), installedSuiteExe.wstring().c_str()) == 0);
    if (!runningFromInstalledHub) {
        std::filesystem::copy_file(selfPath, installedSuiteExe, std::filesystem::copy_options::overwrite_existing, ec);
    }

    // Persist `InstallSource` so `Settings -> Apps -> Modify` can find `packages\` on side-by-side Suite media,
    // AND wire `ModifyPath` (`--suite-manage`) separately from `UninstallString` (`--suite-uninstall`) so clicking
    // **Uninstall** in Windows `Settings -> Apps` opens directly into the Suite Uninstall confirmation page!
    const std::filesystem::path effInstallSource = !runningFromInstalledHub
        ? selfPath.parent_path()
        : existingSuiteReg.installSource;
    const std::wstring manageCmd    = L"\"" + installedSuiteExe.wstring() + L"\" --suite-manage";
    const std::wstring uninstallCmd = L"\"" + installedSuiteExe.wstring() + L"\" --suite-uninstall";
    const std::wstring quietCmd     = L"\"" + installedSuiteExe.wstring() + L"\" --suite-uninstall /s";
    if (!virtRegRoot.empty()) {
        bool dummy = false;
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"DisplayName",          suite.suiteName, dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"DisplayVersion",       suite.version, dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"Publisher",            suite.manufacturer, dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"InstallLocation",      WideToUtf8(suiteInstallDir.wstring()), dummy);
        if (!effInstallSource.empty()) {
            native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"InstallSource",    WideToUtf8(effInstallSource.wstring()), dummy);
        }
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"ModifyPath",           WideToUtf8(manageCmd), dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"UninstallString",      WideToUtf8(uninstallCmd), dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"QuietUninstallString", WideToUtf8(quietCmd), dummy);
    } else {
        HKEY hk = nullptr;
        if (::RegCreateKeyExW(hSuiteHive, suiteUninstSubKey.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            auto writeSz = [&](const wchar_t* name, const std::wstring& val) {
                ::RegSetValueExW(hk, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                                 static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
            };
            auto writeDw = [&](const wchar_t* name, DWORD v) {
                ::RegSetValueExW(hk, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
            };
            writeSz(L"DisplayName",          Utf8ToWide(suite.suiteName));
            writeSz(L"DisplayVersion",       Utf8ToWide(suite.version));
            writeSz(L"Publisher",            Utf8ToWide(suite.manufacturer));
            writeSz(L"InstallLocation",      suiteInstallDir.wstring());
            if (!effInstallSource.empty()) {
                writeSz(L"InstallSource",    effInstallSource.wstring());
            }
            writeSz(L"DisplayIcon",          installedSuiteExe.wstring());
            writeSz(L"ModifyPath",           manageCmd);
            writeSz(L"UninstallString",      uninstallCmd);
            writeSz(L"QuietUninstallString", quietCmd);
            if (!suite.aboutUrl.empty()) {
                writeSz(L"URLInfoAbout",     Utf8ToWide(suite.aboutUrl));
            }
            writeDw(L"NoModify", 0);
            writeDw(L"NoRepair", 0);
            ::RegCloseKey(hk);
        }
    }

    std::filesystem::remove_all(tmpDir, ec);
    if (onSuiteProgress) {
        onSuiteProgress({1000, 1000, plan.size(), std::max<std::size_t>(1, plan.size()), false,
                         Utf8ToWide(suite.suiteName), L"Suite configuration complete."});
    }
    return anyReboot ? ERROR_SUCCESS_REBOOT_REQUIRED : 0;
}

/// Backwards-compatible wrapper accepting a simple `(overallPermille, status)` callback.
inline int ExecuteSuiteChainerOperation(
    const SuiteChainerManifest& suite,
    SuiteOperationKind opKind,
    const std::set<std::string>& selectedChildIdsLower,
    const std::filesystem::path& cliInstallDirOverride,
    const std::function<void(int permille, const std::wstring& status)>& onProgress,
    std::string& outError)
{
    return ExecuteSuiteChainerOperationDetailed(
        suite, opKind, selectedChildIdsLower, cliInstallDirOverride, nullptr,
        [&](const SuiteProgressSnapshot& snap) {
            if (onProgress) {
                onProgress(snap.overallPermille, snap.componentName + L" — " + snap.detailStatus);
            }
        },
        outError);
}

/// Runs an elevated Unified Suite worker job (`--elevated-suite-worker <job.ini>`) spawned under
/// ONE UAC prompt when an `asInvoker` Suite window installs per-machine (`Scope = machine`) packages,
/// streaming live Dual-Bar `SuiteProgressSnapshot` state back to the un-elevated desktop window!
inline int RunElevatedSuiteWorkerJob(const std::filesystem::path& jobFilePath) {
    std::vector<std::uint8_t> raw;
    std::string err;
    if (!ReadBinaryFileBytes(jobFilePath, raw, err)) return ERROR_BAD_ARGUMENTS;
    auto kv = native_detail::ParseFlatIniMap(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));

    const std::filesystem::path statePath(jobFilePath.wstring() + L".state");
    const std::filesystem::path cancelPath(jobFilePath.wstring() + L".cancel");
    auto writeState = [&](const SuiteProgressSnapshot& snap, const std::string& errUtf8) {
        std::ofstream st(statePath, std::ios::binary | std::ios::trunc);
        if (st) {
            st << "OverallPermille="   << snap.overallPermille << "\n"
               << "ComponentPermille=" << snap.componentPermille << "\n"
               << "StepIndex="         << snap.stepIndex << "\n"
               << "StepCount="         << snap.stepCount << "\n"
               << "QueuePaused="       << (snap.queuePaused ? "1" : "0") << "\n"
               << "ComponentName="     << native_detail::EscapeSingleLine(WideToUtf8(snap.componentName)) << "\n"
               << "DetailStatus="      << native_detail::EscapeSingleLine(WideToUtf8(snap.detailStatus)) << "\n"
               << "Error="             << native_detail::EscapeSingleLine(errUtf8) << "\n";
        }
    };

    const std::uint8_t* suitePtr = nullptr;
    std::size_t suiteSz = 0;
    if (!GetRcDataViewLocal(kResIdSuiteManifest, suitePtr, suiteSz) || suiteSz == 0) {
        SuiteProgressSnapshot f{};
        writeState(f, "Missing embedded Suite manifest (#230).");
        return ERROR_INSTALL_PACKAGE_OPEN_FAILED;
    }

    const SuiteChainerManifest suite = ParseSuiteChainerManifestIni(
        std::string_view(reinterpret_cast<const char*>(suitePtr), suiteSz));
    const auto op = static_cast<SuiteOperationKind>(std::clamp(std::atoi(kv["OpKind"].c_str()), 0, 3));
    const std::filesystem::path installOverride = kv["InstallDir"].empty()
        ? std::filesystem::path{}
        : std::filesystem::path(Utf8ToWide(kv["InstallDir"]));

    std::set<std::string> selectedIds;
    std::istringstream css(kv["SelectedCsv"]);
    std::string item;
    while (std::getline(css, item, ',')) {
        item = AsciiLower(TrimAscii(item));
        if (!item.empty()) selectedIds.insert(item);
    }

    std::atomic<bool> cancelFlag{false};
    std::string runErr;
    const int rc = ExecuteSuiteChainerOperationDetailed(
        suite, op, selectedIds, installOverride, &cancelFlag,
        [&](const SuiteProgressSnapshot& snap) {
            std::error_code cEc;
            if (std::filesystem::exists(cancelPath, cEc)) cancelFlag.store(true);
            writeState(snap, "");
        },
        runErr);
    SuiteProgressSnapshot doneSnap{};
    doneSnap.overallPermille   = (rc == 0 || rc == ERROR_SUCCESS_REBOOT_REQUIRED) ? 1000 : 0;
    doneSnap.componentPermille = doneSnap.overallPermille;
    doneSnap.componentName     = Utf8ToWide(suite.suiteName);
    doneSnap.detailStatus      = (rc == 0 || rc == ERROR_SUCCESS_REBOOT_REQUIRED) ? L"Completed." : L"Failed.";
    writeState(doneSnap, runErr);
    return rc;
}

// ================================================================================
// INTERACTIVE MULTI-STEP UNIFIED SUITE SETUP & MAINTENANCE WIZARD (`SuiteSetupWizardApp`)
//   - Fresh Install Flow:
//       `Welcome` -> `License` (optional) -> `SetupType` (Express/Typical, Compact,
//       Complete, Custom) -> `CustomSuite` (enabled `SysListView32` checklist when
//       Custom is chosen) -> `VerifyReady` -> `Installing` (Billboards + Monotonic
//       Overall & Component Dual Progress Bars, ZERO disabled ListView!) -> `Finished`
//   - Re-Launch & `Settings -> Apps` Maintenance Flow:
//       * Re-running `<suite>-setup.exe` or clicking **Modify** (`--suite-manage`)
//         opens `Maintenance` (`&Modify` / `&Repair` / `&Remove` accessible cards).
//       * Clicking **Uninstall** in `Settings -> Apps` (`--suite-uninstall`) opens
//         directly on `VerifyReady` (`FullUninstall`, with `< &Back` to `Maintenance`).
//   - Universal Cancel Confirmation (`"Are you sure? Yes/No"`):
//       * Pre-install: confirms exiting without making changes.
//       * Mid-install: pauses taskbar (`TBPF_PAUSED`) and confirms full LIFO rollback
//         of every package newly installed during this Suite session!
// ================================================================================
enum class SuiteWizardPage : int {
    Welcome     = 0,
    Maintenance = 1,
    License     = 2,
    SetupType   = 3,
    CustomSuite = 4,
    VerifyReady = 5,
    Installing  = 6,
    Finished    = 7
};

namespace suite_detail {

/// Schedules deferred post-exit removal of `<suiteInstallDir>\SuiteSetup.exe` and `<suiteInstallDir>`
/// ONLY when the outermost process exiting is `SuiteSetup.exe` itself after `FullUninstall` or `DetachKeepStandalone`!
inline void ScheduleDeferredSuiteHubSelfCleanup(const std::filesystem::path& suiteInstallDir) noexcept {
    if (suiteInstallDir.empty()) return;
    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath    = std::filesystem::path(selfBuf).lexically_normal();
    const std::filesystem::path hubExeInDir = (suiteInstallDir / L"SuiteSetup.exe").lexically_normal();
    if (_wcsicmp(selfPath.wstring().c_str(), hubExeInDir.wstring().c_str()) != 0) {
        return;
    }
    std::wstring cmd = L"cmd.exe /c ping -n 2 127.0.0.1 >nul & del /f /q \"" + selfPath.wstring() +
                       L"\" >nul 2>&1 & rmdir \"" + suiteInstallDir.lexically_normal().wstring() + L"\" >nul 2>&1";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, L"C:\\", &si, &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
}

/// Converts minimal RTF or plain-text license bytes into clean UTF-16 readable text as a universal fallback.
inline std::wstring StripBasicRtfToPlainWide(std::string_view rawBytes) {
    if (rawBytes.size() < 5 || rawBytes.substr(0, 5) != "{\\rtf") {
        return Utf8ToWide(rawBytes);
    }
    std::string out;
    out.reserve(rawBytes.size());
    int braceDepth = 0;
    int skipGroupDepth = -1;
    for (std::size_t i = 0; i < rawBytes.size(); ++i) {
        const char ch = rawBytes[i];
        if (ch == '{') {
            ++braceDepth;
            if (skipGroupDepth < 0 && i + 2 < rawBytes.size() && rawBytes[i + 1] == '\\') {
                const std::string_view rest = rawBytes.substr(i + 1, 16);
                if (rest.rfind("\\fonttbl", 0) == 0 || rest.rfind("\\colortbl", 0) == 0 ||
                    rest.rfind("\\stylesheet", 0) == 0 || rest.rfind("\\*", 0) == 0) {
                    skipGroupDepth = braceDepth;
                }
            }
            continue;
        }
        if (ch == '}') {
            if (braceDepth == skipGroupDepth) skipGroupDepth = -1;
            braceDepth = std::max(0, braceDepth - 1);
            continue;
        }
        if (skipGroupDepth >= 0) continue;
        if (ch == '\r' || ch == '\n') continue;
        if (ch == '\\') {
            if (i + 1 < rawBytes.size() && (rawBytes[i + 1] == '\\' || rawBytes[i + 1] == '{' || rawBytes[i + 1] == '}')) {
                out.push_back(rawBytes[++i]);
                continue;
            }
            if (i + 3 < rawBytes.size() && rawBytes[i + 1] == '\'') {
                auto hexNibble = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                    return -1;
                };
                const int hi = hexNibble(rawBytes[i + 2]);
                const int lo = hexNibble(rawBytes[i + 3]);
                if (hi >= 0 && lo >= 0) {
                    const unsigned char code = static_cast<unsigned char>((hi << 4) | lo);
                    if (code >= 0x20 && code < 0x7F) out.push_back(static_cast<char>(code));
                    i += 3;
                    continue;
                }
            }
            std::size_t j = i + 1;
            while (j < rawBytes.size() && ((rawBytes[j] >= 'a' && rawBytes[j] <= 'z') || (rawBytes[j] >= 'A' && rawBytes[j] <= 'Z'))) ++j;
            const std::string_view word = rawBytes.substr(i + 1, j - (i + 1));
            while (j < rawBytes.size() && (rawBytes[j] == '-' || (rawBytes[j] >= '0' && rawBytes[j] <= '9'))) ++j;
            if (j < rawBytes.size() && rawBytes[j] == ' ') ++j;
            if (word == "par" || word == "line") out += "\r\n";
            else if (word == "tab") out += "    ";
            i = j - 1;
            continue;
        }
        out.push_back(ch);
    }
    return Utf8ToWide(out);
}

} // namespace suite_detail

class SuiteSetupWizardApp {
public:
    static constexpr UINT WM_SUITE_PROGRESS    = WM_APP + 70;
    static constexpr UINT WM_SUITE_DONE        = WM_APP + 71;
    static constexpr UINT WM_SUITE_REQUEST_UAC = WM_APP + 72;
    static constexpr UINT_PTR kBillboardTimer  = 88;
    static constexpr int  kUacDeclinedSentinel = 99091;

    // Accessible Win32 Control IDs (`IsDialogMessageW` keyboard traversal + `&` mnemonics!)
    static constexpr int IDC_SWZ_LIC_EDIT    = 1601;
    static constexpr int IDC_SWZ_LIC_ACCEPT  = 1602;
    static constexpr int IDC_SWZ_TYPE_CARD_0 = 1610;
    static constexpr int IDC_SWZ_TYPE_CARD_1 = 1611;
    static constexpr int IDC_SWZ_TYPE_CARD_2 = 1612;
    static constexpr int IDC_SWZ_TYPE_CARD_3 = 1613;
    static constexpr int IDC_SWZ_MNT_MODIFY  = 1620;
    static constexpr int IDC_SWZ_MNT_REPAIR  = 1621;
    static constexpr int IDC_SWZ_MNT_REMOVE  = 1622;
    static constexpr int IDC_SWZ_LIST_LBL    = 1630;
    static constexpr int IDC_SWZ_LIST        = 1631;
    static constexpr int IDC_SWZ_SEL_ALL     = 1632;
    static constexpr int IDC_SWZ_SEL_RESET   = 1633;
    static constexpr int IDC_SWZ_DIR_LBL     = 1634;
    static constexpr int IDC_SWZ_DIR_EDIT    = 1635;
    static constexpr int IDC_SWZ_DIR_BROWSE  = 1636;
    static constexpr int IDC_SWZ_DETACH_CHK  = 1640;
    static constexpr int IDC_SWZ_OVR_LBL     = 1650;
    static constexpr int IDC_SWZ_OVR_PCT     = 1651;
    static constexpr int IDC_SWZ_OVR_BAR     = 1652;
    static constexpr int IDC_SWZ_CMP_LBL     = 1653;
    static constexpr int IDC_SWZ_CMP_PCT     = 1654;
    static constexpr int IDC_SWZ_CMP_BAR     = 1655;
    static constexpr int IDC_SWZ_FIN_LAUNCH  = 1660;
    static constexpr int IDC_SWZ_BACK        = 1670;
    static constexpr int IDC_SWZ_PRIMARY     = 1671;
    static constexpr int IDC_SWZ_CANCEL      = 1672;

    struct SuiteUacLaunchCtx {
        std::wstring jobPath;
        HANDLE       hProcess       = nullptr;
        bool         canceledByUser = false;
        DWORD        win32Error     = 0;
    };

    SuiteChainerManifest                 suite{};
    SuiteInstalledRegistrationInfo       installedReg{};
    WizardStyleKind                      wizardStyle = WizardStyleKind::StandardGlass;
    std::vector<SetupTypeOptionSpec>     setupTypeCards;
    std::filesystem::path                suiteInstallDir;
    std::filesystem::path                exportShotDir;
    bool                                 openedForMaintenance = false;
    bool                                 openedDirectUninstall = false;

    SuiteWizardPage                      currentPage       = SuiteWizardPage::Welcome;
    SuiteWizardPage                      pageBeforeInstall = SuiteWizardPage::VerifyReady;
    SuiteOperationKind                   activeOp          = SuiteOperationKind::InstallOrModify;
    int                                  selectedTypeIdx   = 0;
    std::size_t                          billboardIdx      = 0;

    ui::RswTheme                         theme{};
    suite_detail::SuiteTaskbarProgress   taskbar{};
    HMODULE                              hRichEditDll = nullptr;
    HWND                                 hBackdropWnd = nullptr;
    HWND                                 hWnd         = nullptr;

    HWND                                 hLicEdit     = nullptr;
    HWND                                 hLicAccept   = nullptr;
    HWND                                 hTypeCards[4]{};
    HWND                                 hMntModify   = nullptr;
    HWND                                 hMntRepair   = nullptr;
    HWND                                 hMntRemove   = nullptr;
    HWND                                 hListLbl     = nullptr;
    HWND                                 hList        = nullptr;
    HWND                                 hBtnSelAll   = nullptr;
    HWND                                 hBtnReset    = nullptr;
    HWND                                 hDirLbl      = nullptr;
    HWND                                 hDirEdit     = nullptr;
    HWND                                 hBtnBrowse   = nullptr;
    HWND                                 hChkDetach   = nullptr;
    HWND                                 hOvrLbl      = nullptr;
    HWND                                 hOvrPct      = nullptr;
    HWND                                 hOvrBar      = nullptr;
    HWND                                 hCmpLbl      = nullptr;
    HWND                                 hCmpPct      = nullptr;
    HWND                                 hCmpBar      = nullptr;
    HWND                                 hChkLaunch   = nullptr;
    HWND                                 hBtnBack     = nullptr;
    HWND                                 hBtnPrimary  = nullptr;
    HWND                                 hBtnCancel   = nullptr;

    HICON                                hIconBig     = nullptr;
    HICON                                hIconSmall   = nullptr;
    HBITMAP                              hArtworkBmp  = nullptr;
    int                                  artworkW     = 0;
    int                                  artworkH     = 0;
    HFONT                                hFontTitle   = nullptr;
    HFONT                                hFontSection = nullptr;
    HFONT                                hFontBold    = nullptr;
    HFONT                                hFontBody    = nullptr;
    HFONT                                hFontSmall   = nullptr;

    std::vector<bool>                    childAlreadyInstalled;
    std::vector<bool>                    childChecked;
    bool                                 populatingList      = false;
    bool                                 anyChildInstalled   = false;
    int                                  lastAnnouncedDecile = -1;

    std::atomic<bool>                    running{false};
    std::atomic<bool>                    finished{false};
    std::atomic<bool>                    cancelFlag{false};
    int                                  finalExitCode = 0;
    std::mutex                           mtx;
    SuiteProgressSnapshot                latestSnap{};
    std::string                          errorUtf8;
    std::thread                          worker;

    ~SuiteSetupWizardApp() {
        cancelFlag.store(true);
        if (worker.joinable()) worker.join();
        for (HFONT* f : {&hFontTitle, &hFontSection, &hFontBold, &hFontBody, &hFontSmall}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        if (hArtworkBmp) {
            ::DeleteObject(hArtworkBmp);
            hArtworkBmp = nullptr;
        }
        if (hRichEditDll) {
            ::FreeLibrary(hRichEditDll);
            hRichEditDll = nullptr;
        }
    }

    bool IsHeadlessExport() const noexcept { return !exportShotDir.empty(); }

    bool NeedsAdminElevation() const noexcept {
        return suite.perMachine && !QueryProcessPrivilegeState().HasEffectiveAdminRights();
    }

    bool HasPostFinishLaunch() const noexcept {
        return suite.offerLaunchOnFinish && !suite.mainExecutable.empty() &&
               activeOp == SuiteOperationKind::InstallOrModify &&
               (finalExitCode == 0 || finalExitCode == ERROR_SUCCESS_REBOOT_REQUIRED);
    }

    bool HasLicenseStep() const noexcept {
        if (!suite.hasLicenseRtf) return false;
        const std::uint8_t* ptr = nullptr;
        std::size_t sz = 0;
        return GetRcDataViewLocal(kResIdLicenseRtf, ptr, sz) && sz > 0;
    }

    /// Evaluates whether child `idx` belongs to a given Setup Type tier (`minimal`, `typical`, `complete`).
    bool DoesChildMatchTier(std::size_t idx, std::string_view tier) const noexcept {
        if (idx >= suite.children.size()) return false;
        const auto& ch = suite.children[idx];
        if (ch.required) return true;
        const std::string t = AsciiLower(TrimAscii(tier));
        const std::string inc = AsciiLower(TrimAscii(ch.inclusion));
        if (t == "minimal" || t == "compact") {
            return inc == "minimal" || inc == "compact";
        }
        if (t == "complete") {
            return true;
        }
        // Default `"typical"` / `"express"`: includes `"minimal"` + `"typical"`!
        return inc == "minimal" || inc == "compact" || inc == "typical" || inc == "express";
    }

    void ComputeTierMetrics(std::string_view tier, std::size_t& outCount, std::uint64_t& outBytes) const noexcept {
        outCount = 0;
        outBytes = 0;
        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            if (DoesChildMatchTier(i, tier)) {
                ++outCount;
                outBytes += suite.children[i].uncompressedBytes;
            }
        }
    }

    void ApplyTierToChecklist(std::string_view tier) {
        const std::string t = AsciiLower(TrimAscii(tier));
        if (t == "custom") return;
        for (std::size_t i = 0; i < suite.children.size() && i < childChecked.size(); ++i) {
            childChecked[i] = DoesChildMatchTier(i, t);
        }
        SyncListViewCheckmarksFromState();
        UpdateSelectionSummaryLabel();
    }

    void BuildSetupTypeCardSpecs() {
        std::vector<SetupTypeOptionSpec> customSpecs;
        for (const auto& st : suite.setupTypes) {
            SetupTypeOptionSpec o{};
            o.id            = st.id;
            o.titleMnemonic = Utf8ToWide(st.title);
            o.description   = Utf8ToWide(st.description);
            o.tier          = st.tier;
            o.glyph         = ParseSetupGlyphName(st.glyph, ui::GlassGlyph::Bolt);
            o.recommended   = st.recommended;
            customSpecs.push_back(std::move(o));
        }
        const std::wstring expD = suite.expressDesc.empty()
            ? L"Installs the recommended applications and content packs using automatic folder defaults."
            : Utf8ToWide(suite.expressDesc);
        const std::wstring cmpD = suite.compactDesc.empty()
            ? L"Installs only the essential core applications to conserve disk space."
            : Utf8ToWide(suite.compactDesc);
        const std::wstring cusD = suite.customDesc.empty()
            ? L"Choose individual applications and content packs from the Suite checklist before installing."
            : Utf8ToWide(suite.customDesc);

        setupTypeCards = BuildEffectiveSetupTypeOptions(suite.setupTypePreset, customSpecs, expD, cmpD, cusD);

        // Append live Suite package count & byte size onto each non-Custom card description!
        selectedTypeIdx = 0;
        for (std::size_t i = 0; i < setupTypeCards.size(); ++i) {
            if (setupTypeCards[i].recommended) selectedTypeIdx = static_cast<int>(i);
            if (!EqualsIgnoreCase(setupTypeCards[i].tier, "custom")) {
                std::size_t tCnt = 0;
                std::uint64_t tBytes = 0;
                ComputeTierMetrics(setupTypeCards[i].tier, tCnt, tBytes);
                setupTypeCards[i].description += L" (" + std::to_wstring(tCnt) + L" of " +
                    std::to_wstring(suite.children.size()) + L" items, " +
                    suite_detail::FormatSuiteBytesShort(tBytes) + L")";
            } else {
                std::size_t allCnt = suite.children.size();
                std::uint64_t allBytes = 0;
                for (const auto& c : suite.children) allBytes += c.uncompressedBytes;
                setupTypeCards[i].description += L" (Up to " + std::to_wstring(allCnt) + L" items, " +
                    suite_detail::FormatSuiteBytesShort(allBytes) + L")";
            }
        }
    }

    void RefreshInstalledChildrenSnapshot(bool resetChecksForMode) {
        childAlreadyInstalled.assign(suite.children.size(), false);
        if (childChecked.size() != suite.children.size()) {
            childChecked.assign(suite.children.size(), true);
        }
        anyChildInstalled = false;
        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            std::wstring msiProd, natQuiet;
            const bool inst = IsSuiteChildCurrentlyInstalled(suite.children[i], msiProd, natQuiet);
            childAlreadyInstalled[i] = inst;
            if (inst) anyChildInstalled = true;
            if (resetChecksForMode) {
                if (openedForMaintenance) {
                    childChecked[i] = suite.children[i].required || inst;
                } else if (selectedTypeIdx >= 0 && static_cast<std::size_t>(selectedTypeIdx) < setupTypeCards.size() &&
                           !EqualsIgnoreCase(setupTypeCards[static_cast<std::size_t>(selectedTypeIdx)].tier, "custom")) {
                    childChecked[i] = DoesChildMatchTier(i, setupTypeCards[static_cast<std::size_t>(selectedTypeIdx)].tier);
                } else {
                    childChecked[i] = suite.children[i].required || suite.children[i].selectedByDefault || inst;
                }
            }
        }
    }

    void SyncListViewCheckmarksFromState() {
        if (!hList) return;
        populatingList = true;
        const int itemCount = ListView_GetItemCount(hList);
        for (int i = 0; i < itemCount && static_cast<std::size_t>(i) < childChecked.size(); ++i) {
            ListView_SetCheckState(hList, i, childChecked[static_cast<std::size_t>(i)] ? TRUE : FALSE);
        }
        populatingList = false;
    }

    void PopulateSuiteListView() {
        if (!hList) return;
        populatingList = true;
        ListView_DeleteAllItems(hList);
        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            const auto& ch = suite.children[i];
            const bool inst = (i < childAlreadyInstalled.size()) ? childAlreadyInstalled[i] : false;

            std::wstring colName = Utf8ToWide(ch.name);
            if (!ch.version.empty()) colName += L" (v" + Utf8ToWide(ch.version) + L")";
            if (ch.required) colName += L" [Required]";

            LVITEMW lvi{};
            lvi.mask    = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem   = static_cast<int>(i);
            lvi.pszText = colName.data();
            lvi.lParam  = static_cast<LPARAM>(i);
            const int row = ListView_InsertItem(hList, &lvi);

            std::wstring colCat = ch.category.empty() ? L"Suite" : Utf8ToWide(ch.category);
            ListView_SetItemText(hList, row, 1, colCat.data());

            std::wstring colSz = suite_detail::FormatSuiteBytesShort(ch.uncompressedBytes);
            ListView_SetItemText(hList, row, 2, colSz.data());

            std::wstring colSt = L"Available";
            if (inst) {
                std::wstring msiProd, natQuiet;
                IsSuiteChildCurrentlyInstalled(ch, msiProd, natQuiet);
                const std::wstring keyLeaf = !msiProd.empty() ? msiProd : Utf8ToWide(ch.upgradeCode);
                colSt = IsInstalledAppHiddenAsSuiteComponent(keyLeaf)
                    ? L"Installed"
                    : L"Installed Standalone";
            }
            ListView_SetItemText(hList, row, 3, colSt.data());
            ListView_SetCheckState(hList, row, childChecked[i] ? TRUE : FALSE);
        }
        populatingList = false;
        UpdateSelectionSummaryLabel();
    }

    void CountSelectedItems(std::size_t& outSelCount, std::uint64_t& outSelBytes, std::size_t& outInstalledCount) const noexcept {
        outSelCount       = 0;
        outSelBytes       = 0;
        outInstalledCount = 0;
        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            if (i < childAlreadyInstalled.size() && childAlreadyInstalled[i]) ++outInstalledCount;
            if ((i < childChecked.size() && childChecked[i]) || suite.children[i].required) {
                ++outSelCount;
                outSelBytes += suite.children[i].uncompressedBytes;
            }
        }
    }

    void UpdateSelectionSummaryLabel() {
        if (!hListLbl) return;
        std::size_t selCount = 0, instCount = 0;
        std::uint64_t selBytes = 0;
        CountSelectedItems(selCount, selBytes, instCount);
        const std::wstring txt =
            L"&Components to Keep / Install — Selected " + std::to_wstring(selCount) + L" of " +
            std::to_wstring(suite.children.size()) + L" (" + suite_detail::FormatSuiteBytesShort(selBytes) + L"):";
        ::SetWindowTextW(hListLbl, txt.c_str());
        if (currentPage == SuiteWizardPage::CustomSuite && hBtnPrimary) {
            ::EnableWindow(hBtnPrimary, selCount > 0 ? TRUE : FALSE);
        }
    }

    void RebuildFonts(UINT dpi) {
        for (HFONT* f : {&hFontTitle, &hFontSection, &hFontBold, &hFontBody, &hFontSmall}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        const int ts = theme.textScalePercent;
        hFontTitle   = ui::CreateScaledFont(L"Segoe UI", 15, FW_BOLD,     dpi, ts);
        hFontSection = ui::CreateScaledFont(L"Segoe UI", 12, FW_BOLD,     dpi, ts);
        hFontBold    = ui::CreateScaledFont(L"Segoe UI",  9, FW_SEMIBOLD, dpi, ts);
        hFontBody    = ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);
        hFontSmall   = ui::CreateScaledFont(L"Segoe UI",  8, FW_NORMAL,   dpi, ts);

        for (HWND h : {hListLbl, hDirLbl, hOvrLbl, hOvrPct, hCmpPct, hBtnPrimary}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hLicEdit, hLicAccept, hList, hBtnSelAll, hBtnReset, hDirEdit, hBtnBrowse,
                       hChkDetach, hCmpLbl, hChkLaunch, hBtnBack, hBtnCancel}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }
    }

    void ApplyControlThemes() {
        if (hList) {
            ListView_SetBkColor(hList, theme.cardBg);
            ListView_SetTextBkColor(hList, theme.cardBg);
            ListView_SetTextColor(hList, theme.textPrimary);
            ui::AttachDarkAwareListViewSubclass(hList, &theme);
        }
        if (hLicEdit) {
            ui::ApplyDarkScrollbarTheme(hLicEdit, theme);
            ::SendMessageW(hLicEdit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.cardBg));
            CHARFORMAT2W cf{};
            cf.cbSize      = sizeof(cf);
            cf.dwMask      = CFM_COLOR;
            cf.crTextColor = theme.textPrimary;
            ::SendMessageW(hLicEdit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
        }
        for (std::size_t i = 0; i < setupTypeCards.size() && i < 4; ++i) {
            if (hTypeCards[i]) {
                ui::AttachAccessibleOptionCardSubclass(
                    hTypeCards[i], &theme,
                    setupTypeCards[i].glyph, setupTypeCards[i].rimRgb, setupTypeCards[i].accentRgb,
                    setupTypeCards[i].description);
            }
        }
        if (hMntModify) {
            ui::AttachAccessibleOptionCardSubclass(
                hMntModify, &theme, ui::GlassGlyph::Sliders, ui::brand::kSkyCyan, ui::brand::kWarmGold,
                L"Add new applications or content packs to this Suite, or remove individual items you no longer need.");
        }
        if (hMntRepair) {
            ui::AttachAccessibleOptionCardSubclass(
                hMntRepair, &theme, ui::GlassGlyph::Reinstall, ui::brand::kEmeraldOk, ui::brand::kSkyCyan,
                L"Verify and restore installed Suite applications, shortcuts, and Windows Installed Apps registration.");
        }
        if (hMntRemove) {
            ui::AttachAccessibleOptionCardSubclass(
                hMntRemove, &theme, ui::GlassGlyph::Uninstall, ui::brand::kCoralRose, ui::brand::kWarmGold,
                L"Uninstall the entire Suite and its managed applications cleanly from this computer.");
        }
    }

    int ContentLeftLogicalPx() const noexcept {
        if (wizardStyle == WizardStyleKind::FluentMica) return 186;
        if (UsesClassic3dDialogSheet(wizardStyle) &&
            (currentPage == SuiteWizardPage::Welcome || currentPage == SuiteWizardPage::Finished)) {
            return 182;
        }
        return 22;
    }

    int HeaderHeightLogicalPx() const noexcept {
        if (wizardStyle == WizardStyleKind::FluentMica) return 58;
        if (UsesClassic3dDialogSheet(wizardStyle) &&
            (currentPage == SuiteWizardPage::Welcome || currentPage == SuiteWizardPage::Finished)) {
            return 14;
        }
        return 64;
    }

    void LayoutAndSyncVisibility() {
        if (!hWnd) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        RebuildFonts(dpi);
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int contentX = S(ContentLeftLogicalPx());
        const int contentW = std::max(S(260), W - contentX - S(22));
        const int bodyTop  = S(HeaderHeightLogicalPx()) + S(12);
        const int footerH  = S(52);
        const int footerY  = H - footerH;
        const int bodyBot  = footerY - S(12);

        // Hide all step-specific child controls first, then show ONLY the active wizard page's controls!
        for (HWND h : {hLicEdit, hLicAccept, hTypeCards[0], hTypeCards[1], hTypeCards[2], hTypeCards[3],
                       hMntModify, hMntRepair, hMntRemove, hListLbl, hList, hBtnSelAll, hBtnReset,
                       hDirLbl, hDirEdit, hBtnBrowse, hChkDetach, hOvrLbl, hOvrPct, hOvrBar,
                       hCmpLbl, hCmpPct, hCmpBar, hChkLaunch}) {
            if (h) ::ShowWindow(h, SW_HIDE);
        }

        switch (currentPage) {
            case SuiteWizardPage::Welcome:
                break; // Painted cleanly in `PaintWindow` with Hero Card + Overview Card!

            case SuiteWizardPage::Maintenance: {
                const int cardH = S(78);
                const int gapY  = S(10);
                int y = bodyTop + S(44);
                for (HWND hCard : {hMntModify, hMntRepair, hMntRemove}) {
                    if (hCard) {
                        ::MoveWindow(hCard, contentX, y, contentW, cardH, TRUE);
                        ::ShowWindow(hCard, SW_SHOW);
                        y += cardH + gapY;
                    }
                }
                break;
            }

            case SuiteWizardPage::License: {
                const int chkH  = S(26);
                const int editY = bodyTop + S(34);
                const int editH = std::max(S(120), bodyBot - editY - chkH - S(10));
                ::MoveWindow(hLicEdit,   contentX, editY, contentW, editH, TRUE);
                ::MoveWindow(hLicAccept, contentX, editY + editH + S(8), contentW, chkH, TRUE);
                ::ShowWindow(hLicEdit,   SW_SHOW);
                ::ShowWindow(hLicAccept, SW_SHOW);
                break;
            }

            case SuiteWizardPage::SetupType: {
                const std::size_t nCards = std::min<std::size_t>(4, setupTypeCards.size());
                const int startY = bodyTop + S(32);
                const int availH = std::max(S(200), bodyBot - startY);
                const int gapY   = (nCards >= 4) ? S(8) : S(10);
                const int cardH  = std::clamp((availH - gapY * static_cast<int>(nCards - 1)) / static_cast<int>(std::max<std::size_t>(1, nCards)),
                                              S(62), S(86));
                for (std::size_t i = 0; i < nCards; ++i) {
                    const int cy = startY + static_cast<int>(i) * (cardH + gapY);
                    ::MoveWindow(hTypeCards[i], contentX, cy, contentW, cardH, TRUE);
                    ::ShowWindow(hTypeCards[i], SW_SHOW);
                }
                break;
            }

            case SuiteWizardPage::CustomSuite: {
                ::MoveWindow(hListLbl,   contentX, bodyTop, contentW - S(204), S(24), TRUE);
                ::MoveWindow(hBtnSelAll, contentX + contentW - S(196), bodyTop - S(2), S(94), S(28), TRUE);
                ::MoveWindow(hBtnReset,  contentX + contentW - S(96),  bodyTop - S(2), S(96), S(28), TRUE);
                ::ShowWindow(hListLbl,   SW_SHOW);
                ::ShowWindow(hBtnSelAll, SW_SHOW);
                ::ShowWindow(hBtnReset,  SW_SHOW);

                const int folderRowH = suite.allowChangeFolder ? S(54) : 0;
                const int listTop    = bodyTop + S(30);
                const int listH      = std::max(S(140), bodyBot - listTop - folderRowH);
                ::MoveWindow(hList, contentX, listTop, contentW, listH, TRUE);
                ::ShowWindow(hList, SW_SHOW);

                // Fit all 4 columns inside `contentW - scrollbarWidth` so no horizontal scrollbar clips rows!
                const int vScrollW = ::GetSystemMetrics(SM_CXVSCROLL) + S(6);
                const int innerW   = std::max(S(240), contentW - vScrollW);
                const int colCatW  = S(86);
                const int colSzW   = S(78);
                const int colStW   = S(132);
                const int colAppW  = std::max(S(180), innerW - colCatW - colSzW - colStW);
                ListView_SetColumnWidth(hList, 0, colAppW);
                ListView_SetColumnWidth(hList, 1, colCatW);
                ListView_SetColumnWidth(hList, 2, colSzW);
                ListView_SetColumnWidth(hList, 3, colStW);

                if (suite.allowChangeFolder) {
                    const int dirY = listTop + listH + S(8);
                    ::MoveWindow(hDirLbl,    contentX, dirY, contentW, S(18), TRUE);
                    ::MoveWindow(hDirEdit,   contentX, dirY + S(20), contentW - S(92), S(28), TRUE);
                    ::MoveWindow(hBtnBrowse, contentX + contentW - S(84), dirY + S(19), S(84), S(30), TRUE);
                    ::ShowWindow(hDirLbl,    SW_SHOW);
                    ::ShowWindow(hDirEdit,   SW_SHOW);
                    ::ShowWindow(hBtnBrowse, SW_SHOW);
                }
                break;
            }

            case SuiteWizardPage::VerifyReady: {
                if (activeOp == SuiteOperationKind::FullUninstall || activeOp == SuiteOperationKind::DetachKeepStandalone) {
                    ::MoveWindow(hChkDetach, contentX + S(14), bodyBot - S(34), contentW - S(28), S(26), TRUE);
                    ::ShowWindow(hChkDetach, SW_SHOW);
                }
                break;
            }

            case SuiteWizardPage::Installing: {
                const bool hasBillboards = !suite.billboards.empty();
                const int progTop = hasBillboards ? (bodyBot - S(128)) : (bodyTop + S(96));
                ::MoveWindow(hOvrLbl, contentX + S(16), progTop,         contentW - S(206), S(20), TRUE);
                ::MoveWindow(hOvrPct, contentX + contentW - S(186), progTop, S(170), S(20), TRUE);
                ::MoveWindow(hOvrBar, contentX + S(16), progTop + S(22), contentW - S(32), S(22), TRUE);
                ::ShowWindow(hOvrLbl, SW_SHOW);
                ::ShowWindow(hOvrPct, SW_SHOW);
                ::ShowWindow(hOvrBar, SW_SHOW);

                if (suite.showComponentProgressBar) {
                    const int cmpTop = progTop + S(58);
                    ::MoveWindow(hCmpLbl, contentX + S(16), cmpTop,         contentW - S(112), S(20), TRUE);
                    ::MoveWindow(hCmpPct, contentX + contentW - S(92), cmpTop, S(76), S(20), TRUE);
                    ::MoveWindow(hCmpBar, contentX + S(16), cmpTop + S(22), contentW - S(32), S(18), TRUE);
                    ::ShowWindow(hCmpLbl, SW_SHOW);
                    ::ShowWindow(hCmpPct, SW_SHOW);
                    ::ShowWindow(hCmpBar, SW_SHOW);
                }
                break;
            }

            case SuiteWizardPage::Finished: {
                if (HasPostFinishLaunch()) {
                    ::MoveWindow(hChkLaunch, contentX + S(16), bodyBot - S(34), contentW - S(32), S(26), TRUE);
                    ::ShowWindow(hChkLaunch, SW_SHOW);
                }
                break;
            }
        }

        // Footer Navigation Buttons (`< &Back`, Primary Action, `&Cancel`)
        const int btnY   = footerY + (footerH - S(32)) / 2;
        const bool isAero = (wizardStyle == WizardStyleKind::AeroWizard);
        ui::SetPushButtonAeroCircleBackMode(hBtnBack, isAero);
        if (isAero) {
            ::MoveWindow(hBtnBack, S(14), S(15), S(32), S(32), TRUE);
        } else {
            ::MoveWindow(hBtnBack, W - S(338), btnY, S(92), S(32), TRUE);
        }
        ::MoveWindow(hBtnPrimary, W - S(236), btnY, S(124), S(32), TRUE);
        ::MoveWindow(hBtnCancel,  W - S(102), btnY, S(84),  S(32), TRUE);

        SyncNavigationButtonStates();
    }

    void SyncNavigationButtonStates() {
        if (!hBtnBack || !hBtnPrimary || !hBtnCancel) return;

        bool canBack      = false;
        bool showBack     = true;
        bool enablePrim   = true;
        bool showShield   = false;
        bool showCancel   = true;
        bool enableCancel = true;
        std::wstring primText = L"&Next >";
        std::wstring cancText = L"&Cancel";

        switch (currentPage) {
            case SuiteWizardPage::Welcome:
                canBack  = false;
                primText = L"&Next >";
                break;

            case SuiteWizardPage::Maintenance:
                canBack  = false;
                primText = L"&Next >";
                break;

            case SuiteWizardPage::License:
                canBack    = true;
                enablePrim = (::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) == BST_CHECKED);
                primText   = L"&Next >";
                break;

            case SuiteWizardPage::SetupType:
                canBack  = true;
                primText = L"&Next >";
                break;

            case SuiteWizardPage::CustomSuite: {
                canBack = true;
                std::size_t selCount = 0, instCount = 0;
                std::uint64_t selBytes = 0;
                CountSelectedItems(selCount, selBytes, instCount);
                enablePrim = (selCount > 0);
                primText   = L"&Next >";
                break;
            }

            case SuiteWizardPage::VerifyReady:
                canBack    = true;
                showShield = NeedsAdminElevation();
                if (activeOp == SuiteOperationKind::FullUninstall)           primText = L"&Uninstall";
                else if (activeOp == SuiteOperationKind::DetachKeepStandalone) primText = L"&Detach Suite";
                else if (activeOp == SuiteOperationKind::RepairInstalled)    primText = L"&Repair";
                else if (openedForMaintenance)                               primText = L"&Apply Changes";
                else                                                         primText = Utf8ToWide(suite.installBtnText);
                break;

            case SuiteWizardPage::Installing:
                canBack      = false;
                enablePrim   = false;
                primText     = (activeOp == SuiteOperationKind::FullUninstall) ? L"&Uninstalling" : L"&Installing";
                enableCancel = !cancelFlag.load();
                break;

            case SuiteWizardPage::Finished:
                showBack   = false;
                showCancel = false;
                primText   = L"&Finish";
                break;
        }

        ::ShowWindow(hBtnBack, showBack ? SW_SHOW : SW_HIDE);
        ::EnableWindow(hBtnBack, canBack ? TRUE : FALSE);
        ::SetWindowTextW(hBtnPrimary, primText.c_str());
        ::EnableWindow(hBtnPrimary, enablePrim ? TRUE : FALSE);
        ::SendMessageW(hBtnPrimary, BCM_SETSHIELD, 0, (showShield && enablePrim) ? TRUE : FALSE);
        ::ShowWindow(hBtnCancel, showCancel ? SW_SHOW : SW_HIDE);
        ::EnableWindow(hBtnCancel, enableCancel ? TRUE : FALSE);
        ::SetWindowTextW(hBtnCancel, cancText.c_str());
    }

    void NavigateToPage(SuiteWizardPage next) {
        if (hDirEdit && ::IsWindowVisible(hDirEdit)) {
            wchar_t dirBuf[MAX_PATH * 4]{};
            ::GetWindowTextW(hDirEdit, dirBuf, static_cast<int>(std::size(dirBuf)));
            if (dirBuf[0] != L'\0') suiteInstallDir = dirBuf;
        }
        currentPage = next;
        if (next != SuiteWizardPage::Installing) {
            ::KillTimer(hWnd, kBillboardTimer);
        }
        LayoutAndSyncVisibility();
        ::InvalidateRect(hWnd, nullptr, TRUE);
        if (hBackdropWnd) ::InvalidateRect(hBackdropWnd, nullptr, FALSE);
    }

    void OnBackClicked() {
        if (running.load()) return;
        switch (currentPage) {
            case SuiteWizardPage::License:
                NavigateToPage(SuiteWizardPage::Welcome);
                break;
            case SuiteWizardPage::SetupType:
                NavigateToPage(HasLicenseStep() ? SuiteWizardPage::License : SuiteWizardPage::Welcome);
                break;
            case SuiteWizardPage::CustomSuite:
                NavigateToPage(openedForMaintenance ? SuiteWizardPage::Maintenance : SuiteWizardPage::SetupType);
                break;
            case SuiteWizardPage::VerifyReady:
                if (openedForMaintenance) {
                    if (activeOp == SuiteOperationKind::InstallOrModify) {
                        NavigateToPage(SuiteWizardPage::CustomSuite);
                    } else {
                        // Even when launched via `Settings -> Apps -> Uninstall` (`--suite-uninstall`),
                        // clicking `< &Back` lets the user return to the Maintenance hub (`Modify`/`Repair`/`Remove`)!
                        NavigateToPage(SuiteWizardPage::Maintenance);
                    }
                } else {
                    const bool isCustom = (selectedTypeIdx >= 0 &&
                        static_cast<std::size_t>(selectedTypeIdx) < setupTypeCards.size() &&
                        EqualsIgnoreCase(setupTypeCards[static_cast<std::size_t>(selectedTypeIdx)].tier, "custom"));
                    NavigateToPage(isCustom ? SuiteWizardPage::CustomSuite : SuiteWizardPage::SetupType);
                }
                break;
            default:
                break;
        }
    }

    void OnPrimaryClicked() {
        if (running.load()) return;
        switch (currentPage) {
            case SuiteWizardPage::Welcome:
                NavigateToPage(HasLicenseStep() ? SuiteWizardPage::License : SuiteWizardPage::SetupType);
                break;

            case SuiteWizardPage::Maintenance: {
                if (::SendMessageW(hMntRepair, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    activeOp = SuiteOperationKind::RepairInstalled;
                    NavigateToPage(SuiteWizardPage::VerifyReady);
                } else if (::SendMessageW(hMntRemove, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    const bool detach = (hChkDetach && ::SendMessageW(hChkDetach, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    activeOp = detach ? SuiteOperationKind::DetachKeepStandalone : SuiteOperationKind::FullUninstall;
                    NavigateToPage(SuiteWizardPage::VerifyReady);
                } else {
                    activeOp = SuiteOperationKind::InstallOrModify;
                    UpdateSelectionSummaryLabel();
                    NavigateToPage(SuiteWizardPage::CustomSuite);
                }
                break;
            }

            case SuiteWizardPage::License:
                if (::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    NavigateToPage(SuiteWizardPage::SetupType);
                }
                break;

            case SuiteWizardPage::SetupType: {
                activeOp = SuiteOperationKind::InstallOrModify;
                if (selectedTypeIdx >= 0 && static_cast<std::size_t>(selectedTypeIdx) < setupTypeCards.size()) {
                    const auto& card = setupTypeCards[static_cast<std::size_t>(selectedTypeIdx)];
                    if (EqualsIgnoreCase(card.tier, "custom")) {
                        UpdateSelectionSummaryLabel();
                        NavigateToPage(SuiteWizardPage::CustomSuite);
                        return;
                    }
                    ApplyTierToChecklist(card.tier);
                }
                NavigateToPage(SuiteWizardPage::VerifyReady);
                break;
            }

            case SuiteWizardPage::CustomSuite:
                activeOp = SuiteOperationKind::InstallOrModify;
                NavigateToPage(SuiteWizardPage::VerifyReady);
                break;

            case SuiteWizardPage::VerifyReady:
                StartSuiteExecution();
                break;

            case SuiteWizardPage::Finished:
                if (HasPostFinishLaunch() && hChkLaunch &&
                    ::SendMessageW(hChkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    const auto exePath = (suiteInstallDir / Utf8ToWide(suite.mainExecutable)).lexically_normal();
                    ::ShellExecuteW(hWnd, L"open", exePath.wstring().c_str(), nullptr,
                                    exePath.parent_path().wstring().c_str(), SW_SHOWNORMAL);
                }
                ::DestroyWindow(hWnd);
                break;

            default:
                break;
        }
    }

    void PromptAndHandleCancel() {
        if (IsHeadlessExport() || currentPage == SuiteWizardPage::Finished) {
            ::DestroyWindow(hWnd);
            return;
        }
        const bool isMaintOrUninst = openedForMaintenance ||
                                     activeOp == SuiteOperationKind::FullUninstall ||
                                     activeOp == SuiteOperationKind::DetachKeepStandalone ||
                                     activeOp == SuiteOperationKind::RepairInstalled;
        if (running.load()) {
            if (cancelFlag.load()) return; // Rollback already underway!
            taskbar.SetOverallPermille(hWnd, latestSnap.overallPermille, true /* TBPF_PAUSED */);
            if (!ui::ConfirmSetupCancelDialog(hWnd, Utf8ToWide(suite.suiteName), true, isMaintOrUninst)) {
                taskbar.SetOverallPermille(hWnd, latestSnap.overallPermille, latestSnap.queuePaused);
                return;
            }
            cancelFlag.store(true);
            ::EnableWindow(hBtnCancel, FALSE);
            ::SetWindowTextW(hCmpLbl, L"Canceling Suite setup and rolling back all session changes...");
            return;
        }

        if (ui::ConfirmSetupCancelDialog(hWnd, Utf8ToWide(suite.suiteName), false, isMaintOrUninst)) {
            finalExitCode = ERROR_INSTALL_USEREXIT;
            ::DestroyWindow(hWnd);
        }
    }

    void ApplyProgressSnapshotToUi(const SuiteProgressSnapshot& snap) {
        const int ovrPerm = std::clamp(snap.overallPermille, 0, 1000);
        const int cmpPerm = std::clamp(snap.componentPermille, 0, 1000);
        const int ovrPct  = ovrPerm / 10;
        const int cmpPct  = cmpPerm / 10;

        suite_detail::SetProgressBarPermilleExact(hOvrBar, ovrPerm);
        ::SendMessageW(hOvrBar, PBM_SETSTATE, snap.queuePaused ? PBST_PAUSED : PBST_NORMAL, 0);

        std::wstring ovrRight = std::to_wstring(ovrPct) + L"%";
        if (snap.stepCount > 0 && ovrPerm < 1000) {
            const std::size_t humanIdx = std::min(snap.stepIndex + 1, snap.stepCount);
            ovrRight += L"  (Package " + std::to_wstring(humanIdx) + L" of " + std::to_wstring(snap.stepCount) + L")";
        }
        ::SetWindowTextW(hOvrPct, ovrRight.c_str());

        if (suite.showComponentProgressBar) {
            suite_detail::SetProgressBarPermilleExact(hCmpBar, cmpPerm);
            std::wstring cmpLeft = L"Current Component: ";
            if (!snap.componentName.empty()) {
                cmpLeft += snap.componentName;
                if (!snap.detailStatus.empty()) cmpLeft += L" — " + snap.detailStatus;
            } else {
                cmpLeft += snap.detailStatus;
            }
            ::SetWindowTextW(hCmpLbl, cmpLeft.c_str());
            const std::wstring cmpRight = std::to_wstring(cmpPct) + L"%";
            ::SetWindowTextW(hCmpPct, cmpRight.c_str());
        } else if (!snap.detailStatus.empty()) {
            const std::wstring combined = L"Overall Suite Progress — " + snap.componentName + L": " + snap.detailStatus;
            ::SetWindowTextW(hOvrLbl, combined.c_str());
        }

        if (!IsHeadlessExport()) {
            taskbar.SetOverallPermille(hWnd, ovrPerm, snap.queuePaused);
        }

        const int decile = ovrPct / 10;
        if (decile != lastAnnouncedDecile) {
            lastAnnouncedDecile = decile;
            ::NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hOvrBar, OBJID_CLIENT, CHILDID_SELF);
        }
    }

    void StartSuiteExecution() {
        if (running.load()) return;
        if (NeedsAdminElevation() && !QueryProcessPrivilegeState().CanPromptForUacElevation()) {
            const std::wstring wTitle = Utf8ToWide(suite.suiteName) + L" Setup";
            ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), wTitle.c_str(),
                         L"Administrator Privileges Required",
                         L"Configuring this Suite requires Administrator rights, and UAC elevation is disabled on this PC.",
                         TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
            return;
        }

        std::set<std::string> selectedIds;
        std::ostringstream csv;
        for (std::size_t i = 0; i < suite.children.size() && i < childChecked.size(); ++i) {
            if (childChecked[i] || suite.children[i].required) {
                const std::string low = AsciiLower(suite.children[i].id);
                selectedIds.insert(low);
                if (csv.tellp() > 0) csv << ",";
                csv << low;
            }
        }
        const SuiteOperationKind op   = activeOp;
        const std::string selectedCsv = csv.str();
        const std::filesystem::path targetHubDir = suiteInstallDir;

        pageBeforeInstall   = currentPage;
        running.store(true);
        finished.store(false);
        cancelFlag.store(false);
        lastAnnouncedDecile = -1;
        billboardIdx        = 0;
        {
            std::lock_guard<std::mutex> lk(mtx);
            latestSnap = {};
            errorUtf8.clear();
        }
        suite_detail::SetProgressBarPermilleExact(hOvrBar, 0);
        suite_detail::SetProgressBarPermilleExact(hCmpBar, 0);
        ::SetWindowTextW(hOvrPct, L"0%");
        ::SetWindowTextW(hCmpPct, L"0%");
        ::SetWindowTextW(hCmpLbl, L"Preparing Suite transaction...");

        NavigateToPage(SuiteWizardPage::Installing);
        if (!suite.billboards.empty() && !IsHeadlessExport()) {
            ::SetTimer(hWnd, kBillboardTimer, 3500, nullptr);
        }

        if (worker.joinable()) worker.join();
        worker = std::thread([this, op, selectedIds, selectedCsv, targetHubDir]() {
            if (NeedsAdminElevation()) {
                std::error_code ec;
                const auto jobPath = std::filesystem::temp_directory_path(ec) /
                                     (L"rsw-suitejob-" + std::to_wstring(::GetCurrentProcessId()) + L".ini");
                const auto statePath  = std::filesystem::path(jobPath.wstring() + L".state");
                const auto cancelPath = std::filesystem::path(jobPath.wstring() + L".cancel");
                std::filesystem::remove(statePath, ec);
                std::filesystem::remove(cancelPath, ec);
                {
                    std::ofstream jf(jobPath, std::ios::binary | std::ios::trunc);
                    jf << "OpKind="      << static_cast<int>(op) << "\n"
                       << "SelectedCsv=" << native_detail::EscapeSingleLine(selectedCsv) << "\n"
                       << "InstallDir="  << native_detail::EscapeSingleLine(WideToUtf8(targetHubDir.wstring())) << "\n";
                }

                // Request UAC elevation synchronously on the UI thread so Windows 11 centers `consent.exe`
                // cleanly over the wizard window (and returns to `VerifyReady` if the user clicks "No" on UAC!).
                SuiteUacLaunchCtx uacCtx{jobPath.wstring(), nullptr, false, 0};
                ::SendMessageW(hWnd, WM_SUITE_REQUEST_UAC, reinterpret_cast<WPARAM>(&uacCtx), 0);
                if (uacCtx.canceledByUser) {
                    std::filesystem::remove(jobPath, ec);
                    ::PostMessageW(hWnd, WM_SUITE_DONE, static_cast<WPARAM>(kUacDeclinedSentinel), 0);
                    return;
                }
                if (!uacCtx.hProcess) {
                    std::filesystem::remove(jobPath, ec);
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        errorUtf8 = "Could not start elevated Suite helper (Win32 error " + std::to_string(uacCtx.win32Error) + ").";
                    }
                    finalExitCode = ERROR_INSTALL_FAILURE;
                    ::PostMessageW(hWnd, WM_SUITE_DONE, 0, 0);
                    return;
                }

                while (::WaitForSingleObject(uacCtx.hProcess, 45) == WAIT_TIMEOUT) {
                    if (cancelFlag.load() && !std::filesystem::exists(cancelPath, ec)) {
                        std::ofstream cf(cancelPath, std::ios::binary | std::ios::trunc);
                        cf << "1";
                    }
                    std::vector<std::uint8_t> stBytes;
                    std::string dummy;
                    if (ReadBinaryFileBytes(statePath, stBytes, dummy) && !stBytes.empty()) {
                        auto skv = native_detail::ParseFlatIniMap(
                            std::string_view(reinterpret_cast<const char*>(stBytes.data()), stBytes.size()));
                        SuiteProgressSnapshot snap{};
                        snap.overallPermille   = std::atoi(skv["OverallPermille"].c_str());
                        snap.componentPermille = std::atoi(skv["ComponentPermille"].c_str());
                        snap.stepIndex         = static_cast<std::size_t>(std::max(0, std::atoi(skv["StepIndex"].c_str())));
                        snap.stepCount         = static_cast<std::size_t>(std::max(1, std::atoi(skv["StepCount"].c_str())));
                        snap.queuePaused       = (skv["QueuePaused"] == "1");
                        snap.componentName     = Utf8ToWide(skv["ComponentName"]);
                        snap.detailStatus      = Utf8ToWide(skv["DetailStatus"]);
                        {
                            std::lock_guard<std::mutex> lk(mtx);
                            if (snap.overallPermille >= latestSnap.overallPermille) {
                                latestSnap = snap;
                            } else {
                                snap.overallPermille = latestSnap.overallPermille;
                                latestSnap = snap;
                            }
                            if (!skv["Error"].empty()) errorUtf8 = skv["Error"];
                        }
                        ::PostMessageW(hWnd, WM_SUITE_PROGRESS, 0, 0);
                    }
                }
                DWORD code = 1;
                ::GetExitCodeProcess(uacCtx.hProcess, &code);
                ::CloseHandle(uacCtx.hProcess);
                {
                    std::vector<std::uint8_t> stBytes;
                    std::string dummy;
                    if (ReadBinaryFileBytes(statePath, stBytes, dummy) && !stBytes.empty()) {
                        auto skv = native_detail::ParseFlatIniMap(
                            std::string_view(reinterpret_cast<const char*>(stBytes.data()), stBytes.size()));
                        std::lock_guard<std::mutex> lk(mtx);
                        if (!skv["Error"].empty()) errorUtf8 = skv["Error"];
                    }
                }
                std::filesystem::remove(jobPath, ec);
                std::filesystem::remove(statePath, ec);
                std::filesystem::remove(cancelPath, ec);
                finalExitCode = static_cast<int>(code);
                ::PostMessageW(hWnd, WM_SUITE_DONE, (code == 0 || code == ERROR_SUCCESS_REBOOT_REQUIRED) ? 1 : 0, 0);
                return;
            }

            std::string err;
            const int rc = ExecuteSuiteChainerOperationDetailed(
                suite, op, selectedIds, targetHubDir, &cancelFlag,
                [this](const SuiteProgressSnapshot& snap) {
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        latestSnap = snap;
                    }
                    ::PostMessageW(hWnd, WM_SUITE_PROGRESS, 0, 0);
                },
                err);
            {
                std::lock_guard<std::mutex> lk(mtx);
                errorUtf8 = err;
            }
            finalExitCode = rc;
            ::PostMessageW(hWnd, WM_SUITE_DONE, (rc == 0 || rc == ERROR_SUCCESS_REBOOT_REQUIRED) ? 1 : 0, 0);
        });
    }

    void DrawStepSurfaceCard(HDC hdc, Gdiplus::Graphics& g, const RECT& cardRc, int radPx) const noexcept {
        if (UsesClassic3dDialogSheet(wizardStyle) && !theme.isHighContrast) {
            ::FillRect(hdc, &cardRc, theme.hbrWindow);
            ui::DrawClassic3dBevelFrame(hdc, cardRc, true, theme);
            return;
        }
        Gdiplus::SolidBrush fillBr(ui::ToGpColor(theme.cardBg));
        Gdiplus::Pen rimPen(ui::ToGpColor(theme.borderSubtle), 1.0f);
        ui::FillAndStrokeRoundRect(
            g, static_cast<float>(cardRc.left), static_cast<float>(cardRc.top),
            static_cast<float>(cardRc.right - cardRc.left),
            static_cast<float>(cardRc.bottom - cardRc.top),
            static_cast<float>(radPx), &fillBr, &rimPen);
    }

    void PaintFluentStepRail(HDC dc, Gdiplus::Graphics& g, int H, int footerH, UINT dpi) const {
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };
        const int railW = S(168);
        RECT railRc{0, S(58), railW, H - footerH};
        const COLORREF railBg = theme.isHighContrast
            ? theme.windowBg
            : (theme.isDarkMode ? RGB(10, 16, 32) : RGB(236, 242, 250));
        HBRUSH hBr = ::CreateSolidBrush(railBg);
        ::FillRect(dc, &railRc, hBr);
        ::DeleteObject(hBr);

        HPEN divPen = ::CreatePen(PS_SOLID, 1, theme.borderSubtle);
        HGDIOBJ oldP = ::SelectObject(dc, divPen);
        ::MoveToEx(dc, railW - 1, railRc.top, nullptr);
        ::LineTo(dc, railW - 1, railRc.bottom);
        ::SelectObject(dc, oldP);
        ::DeleteObject(divPen);

        struct StepItem { const wchar_t* label; int ord; };
        std::vector<StepItem> steps;
        if (openedForMaintenance) {
            steps = {{L"Maintenance", 0}, {L"Components", 1}, {L"Confirm", 2}, {L"Progress", 3}, {L"Complete", 4}};
        } else {
            steps.push_back({L"Welcome", 0});
            if (HasLicenseStep()) steps.push_back({L"License", 1});
            steps.push_back({L"Setup Type", 2});
            steps.push_back({L"Customize", 3});
            steps.push_back({L"Ready", 4});
            steps.push_back({L"Installing", 5});
            steps.push_back({L"Complete", 6});
        }

        int activeOrd = 0;
        switch (currentPage) {
            case SuiteWizardPage::Welcome:     activeOrd = 0; break;
            case SuiteWizardPage::Maintenance: activeOrd = 0; break;
            case SuiteWizardPage::License:     activeOrd = 1; break;
            case SuiteWizardPage::SetupType:   activeOrd = 2; break;
            case SuiteWizardPage::CustomSuite: activeOrd = openedForMaintenance ? 1 : 3; break;
            case SuiteWizardPage::VerifyReady: activeOrd = openedForMaintenance ? 2 : 4; break;
            case SuiteWizardPage::Installing:  activeOrd = openedForMaintenance ? 3 : 5; break;
            case SuiteWizardPage::Finished:    activeOrd = openedForMaintenance ? 4 : 6; break;
        }

        HGDIOBJ oldF = ::SelectObject(dc, hFontBold);
        int y = railRc.top + S(18);
        for (const auto& st : steps) {
            const bool isCur  = (st.ord == activeOrd);
            const bool isDone = (st.ord < activeOrd);
            const float dotR  = static_cast<float>(S(10));
            const float dx    = static_cast<float>(S(18));
            const float dy    = static_cast<float>(y + S(3));
            if (isCur || isDone) {
                Gdiplus::SolidBrush dBr(ui::ToGpColor(isCur ? theme.accentPrimary : ui::brand::kEmeraldOk));
                g.FillEllipse(&dBr, dx, dy, dotR, dotR);
            } else {
                Gdiplus::Pen dPen(ui::ToGpColor(theme.textSecondary), 1.5f);
                g.DrawEllipse(&dPen, dx, dy, dotR, dotR);
            }
            ::SelectObject(dc, isCur ? hFontBold : hFontBody);
            ::SetTextColor(dc, isCur ? theme.textPrimary : theme.textSecondary);
            RECT tRc{S(36), y, railW - S(8), y + S(20)};
            ::DrawTextW(dc, st.label, -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += S(34);
        }
        ::SelectObject(dc, oldF);
    }

    void PaintWindow(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC dc         = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp    = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(dc, bmp);
        ::FillRect(dc, &rc, theme.hbrWindow);
        ::SetBkMode(dc, TRANSPARENT);

        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        const int footerH   = S(52);
        const int footerY   = H - footerH;
        const bool isWin95  = UsesClassic3dDialogSheet(wizardStyle);
        const int contentX  = S(ContentLeftLogicalPx());
        const int contentW  = std::max(S(260), W - contentX - S(22));
        const int hdrH      = S(HeaderHeightLogicalPx());
        const int bodyTop   = hdrH + S(12);
        const int bodyBot   = footerY - S(12);

        // 1. Top Header Banner or Classic 1995 Left Sidebar Rail
        if (isWin95 && (currentPage == SuiteWizardPage::Welcome || currentPage == SuiteWizardPage::Finished)) {
            RECT railRc{S(12), S(12), S(170), footerY - S(10)};
            ui::DrawClassicLeftSidebarRail(
                dc, g, railRc, dpi, theme,
                wizardStyle == WizardStyleKind::FullScreenTeal,
                hIconBig, hArtworkBmp, artworkW, artworkH, Utf8ToWide(suite.manufacturer));
        } else {
            RECT hdrRc{0, 0, W, hdrH};
            if (theme.isHighContrast || isWin95) {
                ::FillRect(dc, &hdrRc, isWin95 ? theme.hbrCard : theme.hbrFooter);
                if (isWin95) ui::DrawClassicEtchedHorizontalRule(dc, 0, W, hdrH - 2, theme);
            } else {
                ui::FillHorizontalGradient(dc, hdrRc, theme.bannerNavyLeft, theme.bannerNavyRight);
                RECT accRc{0, hdrH - S(3), W, hdrH};
                HBRUSH hAcc = ::CreateSolidBrush(theme.accentPrimary);
                ::FillRect(dc, &accRc, hAcc);
                ::DeleteObject(hAcc);
            }

            const int titleLeft = (wizardStyle == WizardStyleKind::AeroWizard) ? S(56) : S(58);
            if (hIconBig && wizardStyle != WizardStyleKind::AeroWizard) {
                ::DrawIconEx(dc, S(16), (hdrH - S(32)) / 2, hIconBig, S(32), S(32), 0, nullptr, DI_NORMAL);
            } else if (hIconBig) {
                ::DrawIconEx(dc, W - S(44), (hdrH - S(28)) / 2, hIconBig, S(28), S(28), 0, nullptr, DI_NORMAL);
            }

            std::wstring stepHead = Utf8ToWide(suite.suiteName + " v" + suite.version);
            std::wstring stepSub  = !suite.tagline.empty() ? Utf8ToWide(suite.tagline) : Utf8ToWide(suite.manufacturer + " Unified Suite Setup");
            switch (currentPage) {
                case SuiteWizardPage::Maintenance:
                    stepHead = L"Modify, Repair, or Remove Suite";
                    stepSub  = L"Currently installed: " + Utf8ToWide(suite.suiteName) +
                               (installedReg.displayVersion.empty() ? L"" : (L" (v" + Utf8ToWide(installedReg.displayVersion) + L")"));
                    break;
                case SuiteWizardPage::License:
                    stepHead = L"License Agreement";
                    stepSub  = L"Please review the terms below before installing " + Utf8ToWide(suite.suiteName) + L".";
                    break;
                case SuiteWizardPage::SetupType:
                    stepHead = L"Choose Setup Type";
                    stepSub  = suite.bannerTitle.empty()
                        ? (L"Select the installation option that best fits how you use " + Utf8ToWide(suite.suiteName) + L".")
                        : Utf8ToWide(suite.bannerTitle);
                    break;
                case SuiteWizardPage::CustomSuite:
                    stepHead = openedForMaintenance ? L"Modify Installed Suite Components" : L"Custom Suite Selection";
                    stepSub  = L"Check the applications and add-ons you want installed on this computer.";
                    break;
                case SuiteWizardPage::VerifyReady:
                    if (activeOp == SuiteOperationKind::FullUninstall || activeOp == SuiteOperationKind::DetachKeepStandalone) {
                        stepHead = L"Ready to Remove " + Utf8ToWide(suite.suiteName);
                        stepSub  = L"Review the removal summary below, then click Uninstall to proceed.";
                    } else if (activeOp == SuiteOperationKind::RepairInstalled) {
                        stepHead = L"Ready to Repair " + Utf8ToWide(suite.suiteName);
                        stepSub  = L"Setup will verify and restore installed Suite components and shortcuts.";
                    } else {
                        stepHead = openedForMaintenance ? L"Ready to Update Suite" : L"Ready to Install Suite";
                        stepSub  = L"Review your selections below. Setup is ready to configure your computer.";
                    }
                    break;
                case SuiteWizardPage::Installing:
                    stepHead = (activeOp == SuiteOperationKind::FullUninstall)
                        ? (L"Removing " + Utf8ToWide(suite.suiteName) + L"...")
                        : ((activeOp == SuiteOperationKind::RepairInstalled)
                               ? (L"Repairing " + Utf8ToWide(suite.suiteName) + L"...")
                               : (L"Configuring " + Utf8ToWide(suite.suiteName) + L"..."));
                    stepSub  = L"Please wait while Suite components are configured. Click Cancel at any time to roll back.";
                    break;
                case SuiteWizardPage::Finished:
                    stepHead = (finalExitCode == ERROR_INSTALL_USEREXIT)
                        ? L"Setup Canceled — Changes Rolled Back"
                        : L"Suite Setup Complete";
                    break;
                default:
                    break;
            }

            HGDIOBJ oldHF = ::SelectObject(dc, hFontSection);
            ::SetTextColor(dc, (theme.isHighContrast || isWin95) ? theme.textPrimary : theme.bannerTitleText);
            RECT tRc{titleLeft, S(8), W - S(52), S(32)};
            ::DrawTextW(dc, stepHead.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            ::SelectObject(dc, hFontBody);
            ::SetTextColor(dc, (theme.isHighContrast || isWin95) ? theme.textSecondary : RGB(203, 213, 225));
            RECT sRc{titleLeft, S(34), W - S(52), hdrH - S(6)};
            ::DrawTextW(dc, stepSub.c_str(), -1, &sRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            ::SelectObject(dc, oldHF);
        }

        if (wizardStyle == WizardStyleKind::FluentMica) {
            PaintFluentStepRail(dc, g, H, footerH, dpi);
        }

        // 2. Page-Specific Content Painting
        HGDIOBJ oldF = ::SelectObject(dc, hFontBody);
        switch (currentPage) {
            case SuiteWizardPage::Welcome: {
                int cardTop = bodyTop;
                if (!isWin95) {
                    RECT heroRc{contentX, bodyTop, contentX + contentW, bodyTop + S(68)};
                    ui::DrawHeroBrandCard(
                        dc, g, heroRc, dpi, theme,
                        Utf8ToWide(suite.manufacturer), Utf8ToWide(suite.tagline), Utf8ToWide(suite.websiteLabel),
                        L"UNIFIED SUITE", hIconBig);
                    cardTop = bodyTop + S(80);
                }

                RECT mainCard{contentX, cardTop, contentX + contentW, bodyBot};
                DrawStepSurfaceCard(dc, g, mainCard, S(10));

                const std::wstring head = suite.welcomeHeadline.empty()
                    ? (L"Welcome to " + Utf8ToWide(suite.suiteName) + L" Setup")
                    : Utf8ToWide(suite.welcomeHeadline);
                ::SelectObject(dc, hFontTitle);
                ::SetTextColor(dc, theme.textPrimary);
                RECT hRc{mainCard.left + S(18), mainCard.top + S(16), mainCard.right - S(18), mainCard.top + S(68)};
                ::DrawTextW(dc, head.c_str(), -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                std::uint64_t totalBytes = 0;
                for (const auto& c : suite.children) totalBytes += c.uncompressedBytes;
                std::wstring intro = suite.welcomeIntro.empty()
                    ? (L"This wizard guides you through installing " + Utf8ToWide(suite.suiteName) + L" (v" + Utf8ToWide(suite.version) + L") on your computer.")
                    : Utf8ToWide(suite.welcomeIntro);
                intro += L"\r\n\r\nIncluded in this Suite: " + std::to_wstring(suite.children.size()) +
                         L" verified packages (up to " + suite_detail::FormatSuiteBytesShort(totalBytes) + L")."
                         L"\r\n\r\n• Single-prompt installation with automatic standalone adoption."
                         L"\r\n• Full rollback protection if you cancel setup at any point."
                         L"\r\n\r\nClick Next to choose Express, Compact, or Custom installation options.";

                ::SelectObject(dc, hFontBody);
                ::SetTextColor(dc, theme.textSecondary);
                RECT bRc{mainCard.left + S(18), mainCard.top + S(72), mainCard.right - S(18), mainCard.bottom - S(16)};
                ::DrawTextW(dc, intro.c_str(), -1, &bRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case SuiteWizardPage::Maintenance: {
                ::SelectObject(dc, hFontBold);
                ::SetTextColor(dc, theme.textPrimary);
                std::wstring prompt = L"Select how you would like to manage " + Utf8ToWide(suite.suiteName);
                if (!installedReg.displayVersion.empty() && installedReg.displayVersion != suite.version) {
                    prompt += L" (installed v" + Utf8ToWide(installedReg.displayVersion) + L" \u2192 setup v" + Utf8ToWide(suite.version) + L")";
                }
                prompt += L":";
                RECT pRc{contentX, bodyTop, contentX + contentW, bodyTop + S(36)};
                ::DrawTextW(dc, prompt.c_str(), -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case SuiteWizardPage::License: {
                ::SelectObject(dc, hFontBold);
                ::SetTextColor(dc, theme.textPrimary);
                RECT lRc{contentX, bodyTop, contentX + contentW, bodyTop + S(28)};
                ::DrawTextW(dc, L"Please read the following license agreement carefully:", -1, &lRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                break;
            }

            case SuiteWizardPage::SetupType: {
                ::SelectObject(dc, hFontBold);
                ::SetTextColor(dc, theme.textPrimary);
                RECT sRc{contentX, bodyTop, contentX + contentW, bodyTop + S(26)};
                ::DrawTextW(dc, L"Choose the setup type that best suits your needs:", -1, &sRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                break;
            }

            case SuiteWizardPage::VerifyReady: {
                RECT sumCard{contentX, bodyTop, contentX + contentW, bodyBot};
                DrawStepSurfaceCard(dc, g, sumCard, S(10));

                std::size_t selCount = 0, instCount = 0;
                std::uint64_t selBytes = 0;
                CountSelectedItems(selCount, selBytes, instCount);

                std::wstring sumTitle;
                std::wstring sumBody;
                if (activeOp == SuiteOperationKind::FullUninstall || activeOp == SuiteOperationKind::DetachKeepStandalone) {
                    sumTitle = (activeOp == SuiteOperationKind::DetachKeepStandalone)
                        ? L"Detach Suite & Keep Standalone Applications"
                        : L"Confirm Complete Suite Uninstallation";
                    sumBody = (activeOp == SuiteOperationKind::DetachKeepStandalone)
                        ? (L"Operation: Remove unified Suite umbrella while keeping all " + std::to_wstring(instCount) +
                           L" installed applications visible individually in Windows Installed Apps.\r\n\r\n"
                           L"Your personal files, saved notes, and documents will NOT be deleted.")
                        : (L"Operation: Uninstall " + Utf8ToWide(suite.suiteName) + L" and remove all " + std::to_wstring(instCount) +
                           L" managed Suite packages from this computer.\r\n\r\n"
                           L"Suite Hub Location: " + suiteInstallDir.wstring() + L"\r\n\r\n"
                           L"Your personal files and saved documents are preserved. Click Back if you want to Modify or Repair instead.");
                } else if (activeOp == SuiteOperationKind::RepairInstalled) {
                    sumTitle = L"Confirm Suite Repair & Registration Rescue";
                    sumBody  = L"Operation: Verify and repair installed packages for " + Utf8ToWide(suite.suiteName) + L"\r\n"
                               L"Packages to Verify: " + std::to_wstring(std::max<std::size_t>(1, instCount)) + L" installed package(s)\r\n"
                               L"Suite Hub Folder: " + suiteInstallDir.wstring() + L"\r\n\r\n"
                               L"This also refreshes SuiteSetup.exe (v" + Utf8ToWide(suite.version) + L") and repairs Windows Settings -> Installed Apps Modify & Uninstall entries.";
                } else {
                    std::wstring modeName = openedForMaintenance ? L"Modify Installed Suite" : L"Custom Suite Setup";
                    if (!openedForMaintenance && selectedTypeIdx >= 0 && static_cast<std::size_t>(selectedTypeIdx) < setupTypeCards.size()) {
                        std::wstring rawT = setupTypeCards[static_cast<std::size_t>(selectedTypeIdx)].titleMnemonic;
                        rawT.erase(std::remove(rawT.begin(), rawT.end(), L'&'), rawT.end());
                        modeName = rawT;
                    }
                    sumTitle = openedForMaintenance ? L"Confirm Suite Component Changes" : L"Confirm Suite Installation Settings";
                    sumBody  = L"Setup Mode: " + modeName + L"\r\n"
                               L"Selected Packages: " + std::to_wstring(selCount) + L" of " + std::to_wstring(suite.children.size()) +
                               L" (" + suite_detail::FormatSuiteBytesShort(selBytes) + L")\r\n"
                               L"Suite Hub Folder: " + suiteInstallDir.wstring() + L"\r\n\r\n"
                               L"Rollback Protection: Active — canceling during setup will automatically roll back all newly installed packages from this session.";
                }

                ::SelectObject(dc, hFontSection);
                ::SetTextColor(dc, theme.textPrimary);
                RECT hRc{sumCard.left + S(18), sumCard.top + S(16), sumCard.right - S(18), sumCard.top + S(52)};
                ::DrawTextW(dc, sumTitle.c_str(), -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::SelectObject(dc, hFontBody);
                ::SetTextColor(dc, theme.textSecondary);
                RECT bRc{sumCard.left + S(18), sumCard.top + S(56), sumCard.right - S(18), sumCard.bottom - S(46)};
                ::DrawTextW(dc, sumBody.c_str(), -1, &bRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case SuiteWizardPage::Installing: {
                if (!suite.billboards.empty()) {
                    const auto& bb = suite.billboards[billboardIdx % suite.billboards.size()];
                    RECT bbCard{contentX, bodyTop, contentX + contentW, bodyBot - S(142)};
                    DrawStepSurfaceCard(dc, g, bbCard, S(10));
                    const float badgeSz = static_cast<float>(S(46));
                    ui::DrawOptionBadge48(
                        g, static_cast<float>(bbCard.left + S(16)),
                        static_cast<float>(bbCard.top + S(16)), badgeSz,
                        ParseSetupGlyphName(bb.glyph, ui::GlassGlyph::ShieldCheck),
                        ui::brand::kSkyCyan, ui::brand::kWarmGold, theme);

                    ::SelectObject(dc, hFontSection);
                    ::SetTextColor(dc, theme.textPrimary);
                    const std::wstring bbHead = Utf8ToWide(bb.headline);
                    RECT bhRc{bbCard.left + S(74), bbCard.top + S(14), bbCard.right - S(16), bbCard.top + S(46)};
                    ::DrawTextW(dc, bbHead.c_str(), -1, &bhRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                    ::SelectObject(dc, hFontBody);
                    ::SetTextColor(dc, theme.textSecondary);
                    const std::wstring bbBody = Utf8ToWide(bb.body);
                    RECT bdRc{bbCard.left + S(74), bbCard.top + S(46), bbCard.right - S(16), bbCard.bottom - S(12)};
                    ::DrawTextW(dc, bbBody.c_str(), -1, &bdRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                    RECT progCard{contentX, bodyBot - S(134), contentX + contentW, bodyBot};
                    DrawStepSurfaceCard(dc, g, progCard, S(10));
                } else {
                    RECT heroRc{contentX, bodyTop, contentX + contentW, bodyTop + S(68)};
                    ui::DrawHeroBrandCard(
                        dc, g, heroRc, dpi, theme,
                        Utf8ToWide(suite.manufacturer), Utf8ToWide(suite.tagline), Utf8ToWide(suite.websiteLabel),
                        L"CONFIGURING SUITE", hIconBig);

                    RECT progCard{contentX, bodyTop + S(80), contentX + contentW, bodyBot};
                    DrawStepSurfaceCard(dc, g, progCard, S(10));
                }
                break;
            }

            case SuiteWizardPage::Finished: {
                const bool okDone = (finalExitCode == 0 || finalExitCode == ERROR_SUCCESS_REBOOT_REQUIRED);
                const bool userCanceled = (finalExitCode == ERROR_INSTALL_USEREXIT);
                const wchar_t* pill = okDone
                    ? ((activeOp == SuiteOperationKind::FullUninstall) ? L"SUITE REMOVED" : L"SUITE READY")
                    : (userCanceled ? L"ROLLED BACK" : L"SETUP ISSUE");

                int cardTop = bodyTop;
                if (!isWin95) {
                    RECT heroRc{contentX, bodyTop, contentX + contentW, bodyTop + S(68)};
                    ui::DrawHeroBrandCard(
                        dc, g, heroRc, dpi, theme,
                        Utf8ToWide(suite.manufacturer), Utf8ToWide(suite.tagline), Utf8ToWide(suite.websiteLabel),
                        pill, hIconBig);
                    cardTop = bodyTop + S(80);
                }

                RECT finCard{contentX, cardTop, contentX + contentW, bodyBot};
                DrawStepSurfaceCard(dc, g, finCard, S(10));

                std::wstring finHead;
                std::wstring finBody;
                if (okDone) {
                    if (activeOp == SuiteOperationKind::FullUninstall) {
                        finHead = Utf8ToWide(suite.suiteName) + L" Was Uninstalled";
                        finBody = L"All managed Suite applications, content packs, and Windows Installed Apps entries have been cleanly removed from your computer.\r\n\r\nClick Finish to close Setup.";
                    } else if (activeOp == SuiteOperationKind::DetachKeepStandalone) {
                        finHead = L"Suite Detached — Standalone Apps Kept";
                        finBody = L"The Suite umbrella entry was removed, and your individual member applications remain installed and visible in Windows Installed Apps.\r\n\r\nClick Finish to close Setup.";
                    } else if (activeOp == SuiteOperationKind::RepairInstalled) {
                        finHead = Utf8ToWide(suite.suiteName) + L" Was Repaired";
                        finBody = L"All installed Suite components and Windows Installed Apps Modify/Uninstall registrations have been verified and restored.\r\n\r\nClick Finish to close Setup.";
                    } else {
                        finHead = Utf8ToWide(suite.suiteName) + L" Is Ready!";
                        finBody = Utf8ToWide(suite.suiteName) + L" (v" + Utf8ToWide(suite.version) + L") has been configured successfully.\r\n\r\n"
                                  L"You can add, repair, or remove Suite components at any time from Windows Settings -> Apps -> Installed apps, or by running Suite Setup again." +
                                  std::wstring((finalExitCode == ERROR_SUCCESS_REBOOT_REQUIRED)
                                                   ? L"\r\n\r\nNote: Please restart Windows when convenient to finalize system updates."
                                                   : L"");
                    }
                } else if (userCanceled) {
                    finHead = L"Setup Canceled — All Changes Rolled Back";
                    finBody = L"Setup was canceled at your request.\r\n\r\nAll packages and settings modified during this session were automatically rolled back so your computer remains in its previous state.\r\n\r\nClick Finish to close Setup.";
                } else {
                    std::string errCopy;
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        errCopy = errorUtf8;
                    }
                    finHead = L"Suite Setup Could Not Complete";
                    finBody = Utf8ToWide(errCopy.empty() ? "An unexpected error prevented the Suite operation from completing." : errCopy) +
                              L"\r\n\r\nClick Finish to close Setup.";
                }

                ::SelectObject(dc, hFontTitle);
                ::SetTextColor(dc, theme.textPrimary);
                RECT fhRc{finCard.left + S(18), finCard.top + S(16), finCard.right - S(18), finCard.top + S(68)};
                ::DrawTextW(dc, finHead.c_str(), -1, &fhRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                ::SelectObject(dc, hFontBody);
                ::SetTextColor(dc, theme.textSecondary);
                RECT fbRc{finCard.left + S(18), finCard.top + S(72), finCard.right - S(18), finCard.bottom - S(42)};
                ::DrawTextW(dc, finBody.c_str(), -1, &fbRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            default:
                break;
        }

        // 3. Footer Bar
        if (isWin95) {
            ui::DrawClassicEtchedHorizontalRule(dc, S(12), W - S(12), footerY, theme);
        } else {
            ui::DrawGlassFooterBar(
                dc, g, W, H, footerH, W - S(348), dpi, theme,
                Utf8ToWide(suite.manufacturer), Utf8ToWide(suite.websiteLabel));
        }

        ::SelectObject(dc, oldF);
        ::BitBlt(hdcOut, 0, 0, W, H, dc, 0, 0, SRCCOPY);
        ::SelectObject(dc, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(dc);
    }

    static LRESULT CALLBACK BackdropWndProc(HWND hBack, UINT msg, WPARAM wParam, LPARAM lParam) {
        SuiteSetupWizardApp* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SuiteSetupWizardApp*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hBack, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<SuiteSetupWizardApp*>(::GetWindowLongPtrW(hBack, GWLP_USERDATA));
        }
        if (msg == WM_ERASEBKGND) return 1;
        if (msg == WM_PAINT && self) {
            PAINTSTRUCT ps{};
            HDC hdc = ::BeginPaint(hBack, &ps);
            RECT rc{};
            ::GetClientRect(hBack, &rc);
            if (self->wizardStyle == WizardStyleKind::FullScreenTeal) {
                HBRUSH hTeal = ::CreateSolidBrush(RGB(0, 128, 128));
                ::FillRect(hdc, &rc, hTeal);
                ::DeleteObject(hTeal);
            } else {
                // Classic 1995 Cobalt-to-Midnight vertical wash (`#000080` -> `#000000`) + italic serif title!
                Gdiplus::Graphics g(hdc);
                Gdiplus::LinearGradientBrush grad(
                    Gdiplus::Point(0, 0), Gdiplus::Point(0, std::max<int>(1, rc.bottom)),
                    Gdiplus::Color(255, 0, 0, 148), Gdiplus::Color(255, 0, 0, 12));
                g.FillRectangle(&grad, 0, 0, rc.right, rc.bottom);
                const UINT dpi = ::GetDpiForWindow(hBack);
                HFONT hSerif = ::CreateFontW(
                    -ui::ScalePx(34, dpi, 100), 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    FF_ROMAN, L"Times New Roman");
                ::SetBkMode(hdc, TRANSPARENT);
                HGDIOBJ oldF = ::SelectObject(hdc, hSerif);
                const std::wstring washTitle = Utf8ToWide(self->suite.suiteName) + L" Setup";
                RECT shRc{28, 24, rc.right - 24, 96};
                ::SetTextColor(hdc, RGB(0, 0, 0));
                ::DrawTextW(hdc, washTitle.c_str(), -1, &shRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                RECT fgRc{25, 21, rc.right - 24, 93};
                ::SetTextColor(hdc, RGB(255, 255, 255));
                ::DrawTextW(hdc, washTitle.c_str(), -1, &fgRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                ::SelectObject(hdc, oldF);
                ::DeleteObject(hSerif);
            }
            ::EndPaint(hBack, &ps);
            return 0;
        }
        return ::DefWindowProcW(hBack, msg, wParam, lParam);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<SuiteSetupWizardApp*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SuiteSetupWizardApp*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
            self->taskbar.AttachWindow(hWnd);
            ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);
        if (self->taskbar.HandleMessage(hWnd, msg)) return 0;

        switch (msg) {
            case WM_CREATE: {
                HINSTANCE hInst = ::GetModuleHandleW(nullptr);

                // 1. Optional License RichEdit + `I &accept` Checkbox
                if (self->HasLicenseStep()) {
                    if (!self->hRichEditDll) self->hRichEditDll = ::LoadLibraryW(L"Msftedit.dll");
                    const wchar_t* reCls = self->hRichEditDll ? L"RICHEDIT50W" : L"EDIT";
                    self->hLicEdit = ::CreateWindowExW(
                        WS_EX_CLIENTEDGE, reCls, L"",
                        WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                        0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_LIC_EDIT)), hInst, nullptr);
                    const std::uint8_t* licPtr = nullptr;
                    std::size_t licSz = 0;
                    if (GetRcDataViewLocal(kResIdLicenseRtf, licPtr, licSz) && licSz > 0) {
                        const std::wstring plainW = suite_detail::StripBasicRtfToPlainWide(
                            std::string_view(reinterpret_cast<const char*>(licPtr), licSz));
                        ::SetWindowTextW(self->hLicEdit, plainW.c_str());
                    }
                    self->hLicAccept = ::CreateWindowExW(
                        0, L"BUTTON", L"I &accept the terms in the License Agreement",
                        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                        0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_LIC_ACCEPT)), hInst, nullptr);
                    ui::AttachAccessibleCheckboxSubclass(self->hLicAccept, &self->theme, false);
                }

                // 2. Setup Type Option Cards (`1..4` genuine `BS_AUTORADIOBUTTON` controls!)
                for (std::size_t i = 0; i < self->setupTypeCards.size() && i < 4; ++i) {
                    const DWORD grp = (i == 0) ? WS_GROUP : 0u;
                    self->hTypeCards[i] = ::CreateWindowExW(
                        0, L"BUTTON", self->setupTypeCards[i].titleMnemonic.c_str(),
                        WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON | grp,
                        0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_TYPE_CARD_0 + static_cast<int>(i))), hInst, nullptr);
                    ::SendMessageW(self->hTypeCards[i], BM_SETCHECK,
                                   (static_cast<int>(i) == self->selectedTypeIdx) ? BST_CHECKED : BST_UNCHECKED, 0);
                }

                // 3. Maintenance Mode Option Cards (`&Modify`, `&Repair`, `&Remove`)
                self->hMntModify = ::CreateWindowExW(
                    0, L"BUTTON", L"&Modify (Add or Remove Suite Components)",
                    WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_MNT_MODIFY)), hInst, nullptr);
                self->hMntRepair = ::CreateWindowExW(
                    0, L"BUTTON", L"&Repair (Verify && Restore Installed Suite)",
                    WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_MNT_REPAIR)), hInst, nullptr);
                self->hMntRemove = ::CreateWindowExW(
                    0, L"BUTTON", L"Re&move (Uninstall Entire Suite)",
                    WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_MNT_REMOVE)), hInst, nullptr);
                ::SendMessageW(self->hMntModify, BM_SETCHECK,
                               (self->activeOp == SuiteOperationKind::InstallOrModify) ? BST_CHECKED : BST_UNCHECKED, 0);
                ::SendMessageW(self->hMntRepair, BM_SETCHECK,
                               (self->activeOp == SuiteOperationKind::RepairInstalled) ? BST_CHECKED : BST_UNCHECKED, 0);
                ::SendMessageW(self->hMntRemove, BM_SETCHECK,
                               (self->activeOp == SuiteOperationKind::FullUninstall || self->activeOp == SuiteOperationKind::DetachKeepStandalone)
                                   ? BST_CHECKED : BST_UNCHECKED, 0);

                // 4. `CustomSuite` Enabled `SysListView32` Checklist + `Select &All` + `&Reset` + Optional Folder Row
                self->hListLbl = ::CreateWindowExW(
                    0, L"STATIC", L"&Components to Keep / Install:", WS_CHILD,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_LIST_LBL)), hInst, nullptr);
                self->hList = ::CreateWindowExW(
                    WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Suite Components Checklist",
                    WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_LIST)), hInst, nullptr);
                ListView_SetExtendedListViewStyle(
                    self->hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
                auto addCol = [&](int idx, const wchar_t* text, int cx) {
                    LVCOLUMNW col{};
                    col.mask    = LVCF_TEXT | LVCF_WIDTH;
                    col.pszText = const_cast<wchar_t*>(text);
                    col.cx      = cx;
                    ListView_InsertColumn(self->hList, idx, &col);
                };
                addCol(0, L"Application / Content Pack", 280);
                addCol(1, L"Category",                    86);
                addCol(2, L"Size",                        78);
                addCol(3, L"State",                      132);

                self->hBtnSelAll = ::CreateWindowExW(
                    0, L"BUTTON", L"Select &All", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_SEL_ALL)), hInst, nullptr);
                self->hBtnReset = ::CreateWindowExW(
                    0, L"BUTTON", L"&Reset", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_SEL_RESET)), hInst, nullptr);

                self->hDirLbl = ::CreateWindowExW(
                    0, L"STATIC", L"Suite &hub folder:", WS_CHILD,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_DIR_LBL)), hInst, nullptr);
                self->hDirEdit = ::CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", self->suiteInstallDir.wstring().c_str(),
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_DIR_EDIT)), hInst, nullptr);
                self->hBtnBrowse = ::CreateWindowExW(
                    0, L"BUTTON", L"B&rowse...", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_DIR_BROWSE)), hInst, nullptr);

                // 5. `VerifyReady` Detach Option (Remove Mode) + `Installing` Dual Progress Bars + `Finished` Launch Checkbox
                self->hChkDetach = ::CreateWindowExW(
                    0, L"BUTTON", L"&Keep member applications installed standalone (only remove Suite umbrella)",
                    WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_DETACH_CHK)), hInst, nullptr);
                ::SendMessageW(self->hChkDetach, BM_SETCHECK,
                               (self->activeOp == SuiteOperationKind::DetachKeepStandalone) ? BST_CHECKED : BST_UNCHECKED, 0);

                self->hOvrLbl = ::CreateWindowExW(
                    0, L"STATIC", L"Overall Suite Progress:", WS_CHILD | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_OVR_LBL)), hInst, nullptr);
                self->hOvrPct = ::CreateWindowExW(
                    0, L"STATIC", L"0%", WS_CHILD | SS_RIGHT | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_OVR_PCT)), hInst, nullptr);
                self->hOvrBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Overall Suite Progress", WS_CHILD | PBS_SMOOTH,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_OVR_BAR)), hInst, nullptr);
                ::SendMessageW(self->hOvrBar, PBM_SETRANGE32, 0, 1000);

                self->hCmpLbl = ::CreateWindowExW(
                    0, L"STATIC", L"Current Component: Ready", WS_CHILD | SS_ENDELLIPSIS | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_CMP_LBL)), hInst, nullptr);
                self->hCmpPct = ::CreateWindowExW(
                    0, L"STATIC", L"0%", WS_CHILD | SS_RIGHT | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_CMP_PCT)), hInst, nullptr);
                self->hCmpBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Current Component Progress", WS_CHILD | PBS_SMOOTH,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_CMP_BAR)), hInst, nullptr);
                ::SendMessageW(self->hCmpBar, PBM_SETRANGE32, 0, 1000);

                self->hChkLaunch = ::CreateWindowExW(
                    0, L"BUTTON", Utf8ToWide(self->suite.finishLaunchText).c_str(),
                    WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_FIN_LAUNCH)), hInst, nullptr);
                ::SendMessageW(self->hChkLaunch, BM_SETCHECK, BST_CHECKED, 0);

                // 6. Wizard Footer Navigation Buttons (`< &Back`, Primary, `&Cancel`)
                self->hBtnBack = ::CreateWindowExW(
                    0, L"BUTTON", L"< &Back", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_BACK)), hInst, nullptr);
                self->hBtnPrimary = ::CreateWindowExW(
                    0, L"BUTTON", L"&Next >", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_PRIMARY)), hInst, nullptr);
                self->hBtnCancel = ::CreateWindowExW(
                    0, L"BUTTON", L"&Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWZ_CANCEL)), hInst, nullptr);

                const bool classicFoot = UsesClassic3dDialogSheet(self->wizardStyle);
                ui::AttachAccessibleCheckboxSubclass(self->hChkDetach, &self->theme, true);
                ui::AttachAccessibleCheckboxSubclass(self->hChkLaunch, &self->theme, true);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnSelAll,  &self->theme, false, false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnReset,   &self->theme, false, false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnBrowse,  &self->theme, false, false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnBack,    &self->theme, false, !classicFoot,
                                                       self->wizardStyle == WizardStyleKind::AeroWizard);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnPrimary, &self->theme, true,  !classicFoot);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnCancel,  &self->theme, false, !classicFoot);

                self->ApplyControlThemes();
                self->PopulateSuiteListView();
                self->LayoutAndSyncVisibility();
                return 0;
            }

            case WM_TIMER:
                if (wParam == kBillboardTimer && self->currentPage == SuiteWizardPage::Installing && !self->suite.billboards.empty()) {
                    ++self->billboardIdx;
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                break;

            case WM_SIZE:
                self->LayoutAndSyncVisibility();
                return 0;

            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                               suggested->right - suggested->left, suggested->bottom - suggested->top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutAndSyncVisibility();
                ::InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }

            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    const COLORREF pCol = ui::ParseHexColor(self->suite.accentPrimaryHex, RGB(37, 99, 235));
                    const COLORREF sCol = ui::ParseHexColor(self->suite.accentSecondaryHex, RGB(251, 191, 36));
                    self->theme.Refresh(pCol, sCol);
                    ApplyWizardStyleThemeOverrides(self->wizardStyle, self->theme);
                    ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    self->ApplyControlThemes();
                    self->LayoutAndSyncVisibility();
                    ::InvalidateRect(hWnd, nullptr, TRUE);
                    return 0;
                }
                break;
            }

            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX: {
                HDC hdc     = reinterpret_cast<HDC>(wParam);
                HWND hChild = reinterpret_cast<HWND>(lParam);
                const bool onCard =
                    (msg == WM_CTLCOLOREDIT) ||
                    (msg == WM_CTLCOLORLISTBOX) ||
                    (hChild != nullptr && (hChild == self->hDirEdit || hChild == self->hLicEdit ||
                                           hChild == self->hOvrLbl || hChild == self->hOvrPct ||
                                           hChild == self->hCmpLbl || hChild == self->hCmpPct));
                ::SetTextColor(hdc, self->theme.textPrimary);
                ::SetBkColor(hdc, onCard ? self->theme.cardBg : self->theme.windowBg);
                return reinterpret_cast<LRESULT>(onCard ? self->theme.hbrCard : self->theme.hbrWindow);
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintWindow(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }

            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (nm && nm->idFrom == IDC_SWZ_LIST && !self->populatingList) {
                    if (nm->code == LVN_ITEMCHANGING) {
                        const auto* lvn = reinterpret_cast<const NMLISTVIEW*>(lParam);
                        if ((lvn->uChanged & LVIF_STATE) != 0) {
                            const UINT oldChk = (lvn->uOldState & LVIS_STATEIMAGEMASK) >> 12;
                            const UINT newChk = (lvn->uNewState & LVIS_STATEIMAGEMASK) >> 12;
                            const int idx = lvn->iItem;
                            if (oldChk == 2 && newChk == 1 && idx >= 0 &&
                                static_cast<std::size_t>(idx) < self->suite.children.size() &&
                                self->suite.children[static_cast<std::size_t>(idx)].required) {
                                return TRUE; // Required core Suite packages stay checked!
                            }
                        }
                    } else if (nm->code == LVN_ITEMCHANGED) {
                        const auto* lvn = reinterpret_cast<const NMLISTVIEW*>(lParam);
                        if ((lvn->uChanged & LVIF_STATE) != 0) {
                            const int idx = lvn->iItem;
                            if (idx >= 0 && static_cast<std::size_t>(idx) < self->childChecked.size()) {
                                self->childChecked[static_cast<std::size_t>(idx)] =
                                    (ListView_GetCheckState(self->hList, idx) != FALSE);
                                self->UpdateSelectionSummaryLabel();
                            }
                        }
                    }
                }
                break;
            }

            case WM_SUITE_REQUEST_UAC: {
                auto* u = reinterpret_cast<SuiteUacLaunchCtx*>(wParam);
                if (!u) return 0;
                if (!self->IsHeadlessExport()) {
                    self->taskbar.SetOverallPermille(hWnd, 15, true);
                }
                wchar_t selfExe[MAX_PATH * 4]{};
                ::GetModuleFileNameW(nullptr, selfExe, static_cast<DWORD>(std::size(selfExe)));
                const std::wstring params = L"--elevated-suite-worker \"" + u->jobPath + L"\"";
                const HWND uacOwner = (self->hBackdropWnd && ::IsWindow(self->hBackdropWnd)) ? self->hBackdropWnd : hWnd;

                SHELLEXECUTEINFOW sei{sizeof(sei)};
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.hwnd         = uacOwner;
                sei.lpVerb       = L"runas";
                sei.lpFile       = selfExe;
                sei.lpParameters = params.c_str();
                sei.nShow        = SW_HIDE;
                if (!::ShellExecuteExW(&sei) || !sei.hProcess) {
                    u->win32Error     = ::GetLastError();
                    u->canceledByUser = (u->win32Error == ERROR_CANCELLED);
                } else {
                    u->hProcess = sei.hProcess;
                }
                return 0;
            }

            case WM_SUITE_PROGRESS: {
                SuiteProgressSnapshot snap{};
                {
                    std::lock_guard<std::mutex> lk(self->mtx);
                    snap = self->latestSnap;
                }
                self->ApplyProgressSnapshotToUi(snap);
                return 0;
            }

            case WM_SUITE_DONE: {
                ::KillTimer(hWnd, kBillboardTimer);
                self->running.store(false);
                if (static_cast<int>(wParam) == kUacDeclinedSentinel) {
                    // User clicked "No" on UAC prompt -> return smoothly to `VerifyReady` without closing!
                    self->taskbar.Clear(hWnd);
                    self->NavigateToPage(self->pageBeforeInstall);
                    if (self->hBtnPrimary) ::SetFocus(self->hBtnPrimary);
                    return 0;
                }

                self->finished.store(true);
                if (wParam == 1 || self->finalExitCode == ERROR_INSTALL_USEREXIT) {
                    self->taskbar.Clear(hWnd);
                } else {
                    self->taskbar.SetError(hWnd);
                }
                self->RefreshInstalledChildrenSnapshot(false);
                self->NavigateToPage(SuiteWizardPage::Finished);
                if (self->hBtnPrimary) ::SetFocus(self->hBtnPrimary);
                return 0;
            }

            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id >= IDC_SWZ_TYPE_CARD_0 && id <= IDC_SWZ_TYPE_CARD_3) {
                    self->selectedTypeIdx = id - IDC_SWZ_TYPE_CARD_0;
                    for (int k = 0; k < 4; ++k) {
                        if (self->hTypeCards[k]) ::InvalidateRect(self->hTypeCards[k], nullptr, FALSE);
                    }
                    return 0;
                }
                if (id == IDC_SWZ_MNT_MODIFY || id == IDC_SWZ_MNT_REPAIR || id == IDC_SWZ_MNT_REMOVE) {
                    for (HWND hm : {self->hMntModify, self->hMntRepair, self->hMntRemove}) {
                        if (hm) ::InvalidateRect(hm, nullptr, FALSE);
                    }
                    return 0;
                }
                if (id == IDC_SWZ_LIC_ACCEPT) {
                    self->SyncNavigationButtonStates();
                    return 0;
                }
                if (id == IDC_SWZ_DETACH_CHK && self->currentPage == SuiteWizardPage::VerifyReady) {
                    const bool detach = (::SendMessageW(self->hChkDetach, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    self->activeOp = detach ? SuiteOperationKind::DetachKeepStandalone : SuiteOperationKind::FullUninstall;
                    self->SyncNavigationButtonStates();
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (id == IDC_SWZ_SEL_ALL && !self->running.load()) {
                    for (std::size_t i = 0; i < self->suite.children.size() && i < self->childChecked.size(); ++i) {
                        self->childChecked[i] = true;
                    }
                    self->SyncListViewCheckmarksFromState();
                    self->UpdateSelectionSummaryLabel();
                    return 0;
                }
                if (id == IDC_SWZ_SEL_RESET && !self->running.load()) {
                    for (std::size_t i = 0; i < self->suite.children.size() && i < self->childChecked.size(); ++i) {
                        self->childChecked[i] = self->openedForMaintenance
                            ? (self->suite.children[i].required || self->childAlreadyInstalled[i])
                            : (self->suite.children[i].required || self->suite.children[i].selectedByDefault);
                    }
                    self->SyncListViewCheckmarksFromState();
                    self->UpdateSelectionSummaryLabel();
                    return 0;
                }
                if (id == IDC_SWZ_DIR_BROWSE && !self->running.load()) {
                    IFileOpenDialog* pDlg = nullptr;
                    if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
                        DWORD opts = 0;
                        pDlg->GetOptions(&opts);
                        pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                        if (SUCCEEDED(pDlg->Show(hWnd))) {
                            IShellItem* pItem = nullptr;
                            if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                                PWSTR psz = nullptr;
                                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                                    self->suiteInstallDir = psz;
                                    ::SetWindowTextW(self->hDirEdit, psz);
                                    ::CoTaskMemFree(psz);
                                }
                                pItem->Release();
                            }
                        }
                        pDlg->Release();
                    }
                    return 0;
                }
                if (id == IDC_SWZ_BACK) {
                    self->OnBackClicked();
                    return 0;
                }
                if (id == IDC_SWZ_PRIMARY || id == IDOK) {
                    if (::IsWindowEnabled(self->hBtnPrimary)) {
                        self->OnPrimaryClicked();
                    }
                    return 0;
                }
                if (id == IDC_SWZ_CANCEL || id == IDCANCEL) {
                    self->PromptAndHandleCancel();
                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                self->PromptAndHandleCancel();
                return 0;

            case WM_DESTROY:
                ::KillTimer(hWnd, kBillboardTimer);
                self->taskbar.Release();
                if (self->hBackdropWnd && ::IsWindow(self->hBackdropWnd)) {
                    HWND hBack = self->hBackdropWnd;
                    self->hBackdropWnd = nullptr;
                    ::DestroyWindow(hBack);
                }
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

/// Interactive Multi-Step Suite Wizard & Headless CLI Dispatcher when `kResIdSuiteManifest` (`#230`) is present!
inline int RunSuiteChainerEntrypoint(
    HINSTANCE hInstance,
    int nCmdShow,
    const std::vector<std::wstring>& args,
    std::string_view suiteManifestText)
{
    const SuiteChainerManifest suite = ParseSuiteChainerManifestIni(suiteManifestText);
    const SuiteInstalledRegistrationInfo existingReg = QuerySuiteInstalledRegistration(suite);

    bool silentMode            = false;
    bool cliForceManage        = false;
    bool cliForceUninstall     = false;
    bool cliForceRepair        = false;
    bool cliForceDetach        = false;
    std::string cliStyleOverride;
    std::filesystem::path cliInstallDirOverride;
    std::filesystem::path exportShotDir;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") silentMode = true;
        else if (a == L"--suite-manage" || a == L"/modify" || a == L"--modify") cliForceManage = true;
        else if (a == L"--suite-repair" || a == L"/repair" || a == L"--repair") cliForceRepair = true;
        else if (a == L"--suite-uninstall" || a == L"/uninstall" || a == L"--uninstall" || a == L"/x") cliForceUninstall = true;
        else if (a == L"--suite-detach") cliForceDetach = true;
        else if (a.rfind(L"INSTALLDIR=", 0) == 0) cliInstallDirOverride = a.substr(11);
        else if (a == L"--install-dir" && i + 1 < args.size()) cliInstallDirOverride = args[++i];
        else if (a.rfind(L"--wizard-style=", 0) == 0) cliStyleOverride = WideToUtf8(a.substr(15));
        else if (a == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[++i];
            ui::ForcedThemeOverride() = 1;
        }
    }

    SuiteOperationKind op = SuiteOperationKind::InstallOrModify;
    if (cliForceUninstall)   op = SuiteOperationKind::FullUninstall;
    else if (cliForceDetach) op = SuiteOperationKind::DetachKeepStandalone;
    else if (cliForceRepair) op = SuiteOperationKind::RepairInstalled;

    if (!MeetsWindowsBuildFloor(suite.minWindowsBuild)) return 1603;

    wchar_t baseEnv[MAX_PATH * 2]{};
    const wchar_t* envName = suite.perMachine ? L"ProgramW6432" : L"LOCALAPPDATA";
    DWORD nEnv = ::GetEnvironmentVariableW(envName, baseEnv, static_cast<DWORD>(std::size(baseEnv)));
    if (nEnv == 0) nEnv = ::GetEnvironmentVariableW(L"ProgramFiles", baseEnv, static_cast<DWORD>(std::size(baseEnv)));
    const std::filesystem::path resolvedSuiteInstallDir = !cliInstallDirOverride.empty()
        ? cliInstallDirOverride.lexically_normal()
        : (!existingReg.installLocation.empty()
               ? existingReg.installLocation.lexically_normal()
               : (((nEnv > 0 ? std::filesystem::path(baseEnv) : std::filesystem::path(L"C:\\Program Files")) /
                   Utf8ToWide(suite.defaultSubdir)).lexically_normal()));

    const auto priv = QueryProcessPrivilegeState();
    if (exportShotDir.empty() &&
        (suite.requestedExecLevel == "requireAdministrator" || (silentMode && suite.perMachine)) &&
        !priv.HasEffectiveAdminRights()) {
        if (!silentMode) {
            wchar_t selfBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
            std::wstring joinedArgs;
            for (const auto& a : args) {
                if (!joinedArgs.empty()) joinedArgs += L" ";
                joinedArgs += L"\"" + a + L"\"";
            }
            SHELLEXECUTEINFOW sei{sizeof(sei)};
            sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb       = L"runas";
            sei.lpFile       = selfBuf;
            sei.lpParameters = joinedArgs.c_str();
            sei.nShow        = SW_SHOWNORMAL;
            if (!::ShellExecuteExW(&sei) || !sei.hProcess) return ERROR_INSTALL_USEREXIT;
            ::WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD code = 0;
            ::GetExitCodeProcess(sei.hProcess, &code);
            ::CloseHandle(sei.hProcess);
            return static_cast<int>(code);
        }
        return ERROR_ACCESS_DENIED;
    }

    if (silentMode) {
        std::string err;
        const int rc = ExecuteSuiteChainerOperationDetailed(suite, op, {}, resolvedSuiteInstallDir, nullptr, nullptr, err);
        if (rc == 0 && (op == SuiteOperationKind::FullUninstall || op == SuiteOperationKind::DetachKeepStandalone)) {
            suite_detail::ScheduleDeferredSuiteHubSelfCleanup(resolvedSuiteInstallDir);
        }
        return rc;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int exitRc = 0;
    SuiteOperationKind completedOp = op;
    bool completedSuccessfully = false;
    {
        ui::ScopedGdiPlusSession gpSession;
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
        ::InitCommonControlsEx(&icc);

        SuiteSetupWizardApp app{};
        app.suite                 = suite;
        app.installedReg          = existingReg;
        app.wizardStyle           = ParseWizardStyleKind(cliStyleOverride.empty() ? suite.wizardLayout : cliStyleOverride);
        if (app.wizardStyle == WizardStyleKind::Simple || app.wizardStyle == WizardStyleKind::OneClick ||
            app.wizardStyle == WizardStyleKind::PassiveToast || app.wizardStyle == WizardStyleKind::Autorun16x9) {
            app.wizardStyle = WizardStyleKind::StandardGlass;
        }
        app.suiteInstallDir       = resolvedSuiteInstallDir;
        app.exportShotDir         = exportShotDir;
        app.activeOp              = op;

        // Maintenance & `Settings -> Apps` Routing:
        //   - If launched via `--suite-uninstall` (`Settings -> Apps -> Uninstall`), open directly on `VerifyReady` (`FullUninstall`)!
        //   - If launched via `--suite-repair`, open directly on `VerifyReady` (`RepairInstalled`)!
        //   - If launched via `--suite-manage` OR re-launched while already installed (`existingReg.registered`), open `Maintenance`!
        app.openedForMaintenance  = exportShotDir.empty() &&
                                    (existingReg.registered || cliForceManage || cliForceUninstall || cliForceRepair || cliForceDetach);
        app.openedDirectUninstall = cliForceUninstall || cliForceDetach;

        app.BuildSetupTypeCardSpecs();
        app.RefreshInstalledChildrenSnapshot(true);

        if (app.openedDirectUninstall || cliForceRepair) {
            app.currentPage = SuiteWizardPage::VerifyReady;
        } else if (app.openedForMaintenance) {
            app.currentPage = SuiteWizardPage::Maintenance;
        } else {
            app.currentPage = SuiteWizardPage::Welcome;
        }

        const COLORREF pCol = ui::ParseHexColor(suite.accentPrimaryHex, RGB(37, 99, 235));
        const COLORREF sCol = ui::ParseHexColor(suite.accentSecondaryHex, RGB(251, 191, 36));
        app.theme.Refresh(pCol, sCol);
        ApplyWizardStyleThemeOverrides(app.wizardStyle, app.theme);

        app.hIconBig   = static_cast<HICON>(::LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        app.hIconSmall = static_cast<HICON>(::LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        if (!app.hIconBig) app.hIconBig = ::LoadIconW(nullptr, IDI_APPLICATION);

        const std::uint8_t* artPtr = nullptr;
        std::size_t artSz = 0;
        if (GetRcDataViewLocal(kResIdArtworkPng, artPtr, artSz) && artSz > 0) {
            app.hArtworkBmp = ui::DecodeImageBytesToHBitmap(artPtr, artSz, app.artworkW, app.artworkH);
        }

        constexpr const wchar_t* kBackCls = L"RaggieSoftUnifiedSuiteBackdropWindow";
        WNDCLASSEXW bwc{sizeof(bwc)};
        bwc.lpfnWndProc   = &SuiteSetupWizardApp::BackdropWndProc;
        bwc.hInstance     = hInstance;
        bwc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        bwc.lpszClassName = kBackCls;
        ::RegisterClassExW(&bwc);

        constexpr const wchar_t* kCls = L"RaggieSoftUnifiedSuiteSetupWindow";
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc   = &SuiteSetupWizardApp::WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon         = app.hIconBig;
        wc.hIconSm       = app.hIconSmall ? app.hIconSmall : app.hIconBig;
        wc.lpszClassName = kCls;
        ::RegisterClassExW(&wc);

        if (UsesFullScreenRetroBackdrop(app.wizardStyle) && exportShotDir.empty()) {
            POINT ptCursor{0, 0};
            ::GetCursorPos(&ptCursor);
            HMONITOR hMon = ::MonitorFromPoint(ptCursor, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{sizeof(mi)};
            RECT monRc{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
            if (hMon && ::GetMonitorInfoW(hMon, &mi)) monRc = mi.rcMonitor;
            app.hBackdropWnd = ::CreateWindowExW(
                WS_EX_TOOLWINDOW, kBackCls, L"", WS_POPUP | WS_CLIPCHILDREN,
                monRc.left, monRc.top, monRc.right - monRc.left, monRc.bottom - monRc.top,
                nullptr, nullptr, hInstance, &app);
            if (app.hBackdropWnd) {
                const int doNotRound = 1;
                const COLORREF noBorder = 0xFFFFFFFEu;
                ::DwmSetWindowAttribute(app.hBackdropWnd, 33, &doNotRound, sizeof(doNotRound));
                ::DwmSetWindowAttribute(app.hBackdropWnd, 34, &noBorder, sizeof(noBorder));
                ::ShowWindow(app.hBackdropWnd, SW_SHOWNOACTIVATE);
                ::UpdateWindow(app.hBackdropWnd);
            }
        }

        const UINT dpi = ::GetDpiForSystem();
        const int logW = (app.wizardStyle == WizardStyleKind::FluentMica) ? 720 : 660;
        const int logH = 492;
        RECT wRc{0, 0,
                 ui::ScalePx(logW, dpi, app.theme.textScalePercent),
                 ui::ScalePx(logH, dpi, app.theme.textScalePercent)};
        constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        const DWORD kExStyle   = exportShotDir.empty() ? WS_EX_APPWINDOW : 0u;
        ::AdjustWindowRectExForDpi(&wRc, kStyle, FALSE, kExStyle, dpi);
        const int wW = wRc.right - wRc.left;
        const int wH = wRc.bottom - wRc.top;
        const int wX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - wW) / 2);
        const int wY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - wH) / 2);

        const std::wstring wTitle = Utf8ToWide(suite.suiteName) +
                                    (app.openedForMaintenance ? L" — Suite Maintenance" : L" — Suite Setup");
        HWND hWnd = ::CreateWindowExW(kExStyle, kCls, wTitle.c_str(), kStyle, wX, wY, wW, wH,
                                      app.hBackdropWnd, nullptr, hInstance, &app);
        if (!hWnd) {
            if (app.hBackdropWnd) ::DestroyWindow(app.hBackdropWnd);
            exitRc = 1;
        } else {
            ::ShowWindow(hWnd, nCmdShow);
            ::UpdateWindow(hWnd);
            if (!exportShotDir.empty()) {
                // Export all key multi-step Suite Wizard screens (`SetupType`, `CustomSuite`, `Maintenance`, and `Installing` Dual Bars)!
                app.NavigateToPage(SuiteWizardPage::SetupType);
                const bool okType = ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-suite-setup-type.png");

                app.NavigateToPage(SuiteWizardPage::CustomSuite);
                const bool okList = ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-suite-custom-list.png");

                app.openedForMaintenance        = true;
                app.installedReg.registered     = true;
                app.installedReg.displayVersion = suite.version;
                app.NavigateToPage(SuiteWizardPage::Maintenance);
                const bool okMaint = ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-suite-maintenance.png");

                app.openedForMaintenance = false;
                app.NavigateToPage(SuiteWizardPage::Installing);
                SuiteProgressSnapshot demo{};
                demo.overallPermille   = 580;
                demo.componentPermille = 640;
                demo.stepIndex         = std::min<std::size_t>(1, std::max<std::size_t>(1, suite.children.size()) - 1);
                demo.stepCount         = std::max<std::size_t>(1, suite.children.size());
                demo.componentName     = !suite.children.empty()
                    ? Utf8ToWide(suite.children[demo.stepIndex].name)
                    : L"RaggieSoft Showcase Studio";
                demo.detailStatus      = L"Copying verified binaries...";
                app.ApplyProgressSnapshotToUi(demo);
                ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                const bool okProg = ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-suite-dual-progress.png");
                ::DestroyWindow(hWnd);
                exitRc = (okType && okList && okMaint && okProg) ? 0 : 2;
            } else {
                MSG msg{};
                while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    if (!::IsDialogMessageW(hWnd, &msg)) {
                        ::TranslateMessage(&msg);
                        ::DispatchMessage(&msg);
                    }
                }
                exitRc = app.finalExitCode;
                completedOp = app.activeOp;
                completedSuccessfully = app.finished.load() && (app.finalExitCode == 0 || app.finalExitCode == ERROR_SUCCESS_REBOOT_REQUIRED);
            }
        }
    }
    ::CoUninitialize();

    if (completedSuccessfully &&
        (completedOp == SuiteOperationKind::FullUninstall || completedOp == SuiteOperationKind::DetachKeepStandalone)) {
        suite_detail::ScheduleDeferredSuiteHubSelfCleanup(resolvedSuiteInstallDir);
    }
    return exitRc;
}

} // namespace rsw::compact_suite
