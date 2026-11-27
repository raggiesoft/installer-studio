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
/// `%ProgramW6432%\<Subdir>`, or `%LOCALAPPDATA%\<Subdir>`).
inline std::filesystem::path ComputeDefaultNativeOrSfxPath(const NativeOrSfxManifest& m) {
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
    opts.createDesktopShortcuts = !EqualsIgnoreCase(kv["DesktopShortcuts"], "false");
    opts.registerFileAssocs     = !EqualsIgnoreCase(kv["RegisterAssocs"], "false");
    opts.suiteManagedFlag       = kv["SuiteManaged"].empty() ? -1 : std::atoi(kv["SuiteManaged"].c_str());
    opts.runningSetupExePath    = selfPath;
    opts.externalPackSearchDir  = selfPath.parent_path();

    std::string runErr;
    const bool ok = ExecuteNativeOrSfxTransaction(
        manifest, pakPtr, pakSz, packs, {}, opts, nullptr,
        [&](int perm, const std::wstring& txt) { writeState(perm, txt, ""); },
        runErr);
    writeState(ok ? 1000 : 0, ok ? L"Completed." : L"Failed.", runErr);
    return ok ? 0 : ERROR_INSTALL_FAILURE;
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
        if (!manifest.perMachine) return false;
        return !QueryProcessPrivilegeState().HasEffectiveAdminRights();
    }

    bool HasPostAction() const noexcept {
        if (IsTempSoftwareInstallerMode()) {
            // Software Installation SFX runs `PostExtractRun`, waits (incl. `_ISDEL`), and cleans `%TEMP%` inline!
            return false;
        }
        if (manifest.mode == "sfx") {
            return !manifest.postExtractRun.empty() || !manifest.postExtractOpen.empty();
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
                const auto statePath = std::filesystem::path(jobPath.wstring() + L".state");
                std::filesystem::remove(statePath, ec);
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
                while (::WaitForSingleObject(sei.hProcess, 100) == WAIT_TIMEOUT) {
                    std::vector<std::uint8_t> stBytes;
                    std::string dummy;
                    if (ReadBinaryFileBytes(statePath, stBytes, dummy) && !stBytes.empty()) {
                        auto skv = native_detail::ParseFlatIniMap(
                            std::string_view(reinterpret_cast<const char*>(stBytes.data()), stBytes.size()));
                        const int perm = std::atoi(skv["Permille"].c_str());
                        {
                            std::lock_guard<std::mutex> lk(mtx);
                            statusText = Utf8ToWide(skv["Status"]);
                        }
                        ::PostMessageW(hWnd, WM_SMP_PROGRESS, static_cast<WPARAM>(perm), 0);
                    }
                }
                DWORD exitCode = 1;
                ::GetExitCodeProcess(sei.hProcess, &exitCode);
                ::CloseHandle(sei.hProcess);
                std::filesystem::remove(jobPath, ec);
                std::filesystem::remove(statePath, ec);
                ::PostMessageW(hWnd, WM_SMP_DONE, (exitCode == 0) ? 1 : 0, 0);
                return;
            }

            NativeExecutionOptions opts{};
            opts.targetDir              = targetDir;
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
            : ((manifest.mode == "sfx") ? L"Self-Extracting Archive (LZMS)" : Utf8ToWide(manifest.manufacturer + " Quick Setup"));
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
                const bool isSfx   = (self->manifest.mode == "sfx");
                const bool tempInst = self->IsTempSoftwareInstallerMode();
                const wchar_t* dirCaption = tempInst
                    ? L"Temporary staging folder (cleaned up automatically after setup):"
                    : (isSfx ? L"E&xtract to folder:" : L"&Destination folder:");
                self->hDirLbl = ::CreateWindowExW(
                    0, L"STATIC", dirCaption, WS_CHILD | WS_VISIBLE,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_DIR_LBL)), hInst, nullptr);
                self->hDirEdit = ::CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", self->targetDir.wstring().c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (self->manifest.allowChangeFolder ? 0 : ES_READONLY),
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_DIR_EDT)), hInst, nullptr);
                self->hBtnBrowse = ::CreateWindowExW(
                    0, L"BUTTON", L"B&rowse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_BROWSE)), hInst, nullptr);
                ::EnableWindow(self->hBtnBrowse, self->manifest.allowChangeFolder ? TRUE : FALSE);

                const wchar_t* initStatus = isSfx ? L"Click Extract to unpack files." : L"Click Install to begin setup.";
                self->hStatusLbl = ::CreateWindowExW(
                    0, L"STATIC", initStatus, WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_STATUS)), hInst, nullptr);
                self->hProgBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Setup Progress", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_PROG)), hInst, nullptr);
                ::SendMessageW(self->hProgBar, PBM_SETRANGE32, 0, 1000);

                const std::wstring launchLbl = isSfx
                    ? (!self->manifest.postExtractRun.empty() ? L"&Run program after extraction" : L"&Open document after extraction")
                    : Utf8ToWide(self->manifest.finishLaunchText);
                self->hChkLaunch = ::CreateWindowExW(
                    0, L"BUTTON", launchLbl.c_str(), WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_LAUNCH)), hInst, nullptr);
                ::SendMessageW(self->hChkLaunch, BM_SETCHECK, self->HasPostAction() ? BST_CHECKED : BST_UNCHECKED, 0);

                const std::wstring cta = Utf8ToWide(self->manifest.installBtnText);
                self->hBtnPrimary = ::CreateWindowExW(
                    0, L"BUTTON", cta.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMP_PRIMARY)), hInst, nullptr);
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
                HDC hdc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(hdc, self->theme.textPrimary);
                ::SetBkColor(hdc, (msg == WM_CTLCOLOREDIT) ? self->theme.cardBg : self->theme.windowBg);
                return reinterpret_cast<LRESULT>((msg == WM_CTLCOLOREDIT) ? self->theme.hbrCard : self->theme.hbrWindow);
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
                    ::SetWindowTextW(self->hStatusLbl, Utf8ToWide(errCopy.empty() ? "Operation canceled." : errCopy).c_str());
                    ::EnableWindow(self->hDirEdit, self->manifest.allowChangeFolder ? TRUE : FALSE);
                    ::EnableWindow(self->hBtnBrowse, self->manifest.allowChangeFolder ? TRUE : FALSE);
                    ::EnableWindow(self->hBtnPrimary, TRUE);
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
                            ExecutePostExtractOrFinishActions(self->manifest, self->targetDir);
                        }
                        ::DestroyWindow(hWnd);
                    } else {
                        self->StartExtractionOrInstall();
                    }
                    return 0;
                }
                if (id == IDC_SMP_CLOSE || id == IDCANCEL) {
                    if (self->running.load()) {
                        self->cancelFlag.store(true);
                    } else {
                        if (self->finished.load() && self->HasPostAction() &&
                            ::SendMessageW(self->hChkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            ExecutePostExtractOrFinishActions(self->manifest, self->targetDir);
                        }
                        ::DestroyWindow(hWnd);
                    }
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                if (self->running.load()) {
                    self->cancelFlag.store(true);
                } else {
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
    int  suiteManaged   = -1;
    std::filesystem::path targetDir = ComputeDefaultNativeOrSfxPath(manifest);
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
            targetDir  = args[++i];
            silentMode = true;
        } else if (a == L"--progress-state-file" && i + 1 < args.size()) {
            progressStateFile = args[++i];
        } else if (a.rfind(L"-o", 0) == 0 && a.size() > 2) {
            targetDir  = a.substr(2);
            silentMode = true;
        } else if (a.rfind(L"INSTALLDIR=", 0) == 0) {
            targetDir = a.substr(11);
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
        NativeExecutionOptions opts{};
        opts.targetDir              = targetDir;
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
        app.pakPtr           = pakPtr;
        app.pakSize          = pakSz;
        app.featurePacks     = featurePacks;
        app.suiteManagedFlag = suiteManaged;
        app.targetDir        = targetDir;
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
                                    ((manifest.mode == "sfx") ? L" — Self-Extractor" : L" Setup");
        HWND hWnd = ::CreateWindowExW(kExStyle, kCls, wTitle.c_str(), kStyle, wX, wY, wW, wH, nullptr, nullptr, hInstance, &app);
        if (!hWnd) {
            exitRc = 1;
        } else {
            ::ShowWindow(hWnd, nCmdShow);
            ::UpdateWindow(hWnd);
            if (!exportShotDir.empty()) {
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
    FullUninstall        = 2
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

    wchar_t baseEnv[MAX_PATH * 2]{};
    const wchar_t* envName = suite.perMachine ? L"ProgramW6432" : L"LOCALAPPDATA";
    DWORD nEnv = ::GetEnvironmentVariableW(envName, baseEnv, static_cast<DWORD>(std::size(baseEnv)));
    if (nEnv == 0) nEnv = ::GetEnvironmentVariableW(L"ProgramFiles", baseEnv, static_cast<DWORD>(std::size(baseEnv)));
    const std::filesystem::path suiteInstallDir = !cliInstallDirOverride.empty()
        ? cliInstallDirOverride.lexically_normal()
        : (((nEnv > 0 ? std::filesystem::path(baseEnv) : std::filesystem::path(L"C:\\Program Files")) /
            Utf8ToWide(suite.defaultSubdir)).lexically_normal());

    const std::string suiteKeyLeaf = (suite.upgradeCode.empty() ? ("RaggieSoft." + suite.suiteName) : suite.upgradeCode) + ".Suite";
    const std::wstring suiteUninstSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + Utf8ToWide(suiteKeyLeaf);
    const HKEY hSuiteHive = suite.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const std::string suiteHiveCode = suite.perMachine ? "HKLM" : "HKCU";
    const auto virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();

    const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                        (L"rsw-suite-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir, ec);

    auto extractSuiteChildBinary = [&](const SuiteChildSpec& ch, std::filesystem::path& outPath) -> bool {
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

        if (!ch.sha256Hex.empty()) {
            std::string actualSha;
            if (!ComputeFileSha256Hex(outPath, actualSha) || !EqualsIgnoreCase(actualSha, ch.sha256Hex)) {
                outError = "SHA-256 verification failed for suite child package '" + ch.name + "'.";
                return false;
            }
        }
        return true;
    };

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
        const auto progFile = tmpDir / (L"child-prog-" + std::to_wstring(stepIdx) + L".ini");
        std::filesystem::remove(progFile, ec);
        ::SetEnvironmentVariableW(L"RSW_CHILD_PROGRESS_FILE", progFile.wstring().c_str());

        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        const BOOL launched = ::CreateProcessW(
            nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, tmpDir.wstring().c_str(), &si, &pi);
        ::SetEnvironmentVariableW(L"RSW_CHILD_PROGRESS_FILE", nullptr);
        if (!launched) return false;

        int bestCompPerm = 50;
        emitProgress(stepIdx, bestCompPerm, compName, initialVerb, false);
        while (::WaitForSingleObject(pi.hProcess, 40) == WAIT_TIMEOUT) {
            if (cancelFlag && cancelFlag->load()) {
                ::TerminateProcess(pi.hProcess, ERROR_INSTALL_USEREXIT);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                std::filesystem::remove(progFile, ec);
                outExitCode = ERROR_INSTALL_USEREXIT;
                return true;
            }
            std::vector<std::uint8_t> rawP;
            std::string dummy;
            if (ReadBinaryFileBytes(progFile, rawP, dummy) && !rawP.empty()) {
                auto pkv = native_detail::ParseFlatIniMap(
                    std::string_view(reinterpret_cast<const char*>(rawP.data()), rawP.size()));
                const int pVal = std::clamp(std::atoi(pkv["Permille"].c_str()), 0, 995);
                bestCompPerm = std::max(bestCompPerm, pVal);
                const std::wstring stW = pkv["Status"].empty() ? initialVerb : Utf8ToWide(pkv["Status"]);
                emitProgress(stepIdx, bestCompPerm, compName, stW, false);
            }
        }
        ::GetExitCodeProcess(pi.hProcess, &outExitCode);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        std::filesystem::remove(progFile, ec);
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
                if (extractSuiteChildBinary(ch, stagedMsi)) {
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

    // `SuiteOperationKind::InstallOrModify`:
    bool anyReboot = false;
    for (std::size_t sIdx = 0; sIdx < plan.size(); ++sIdx) {
        if (cancelFlag && cancelFlag->load()) {
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

        emitProgress(sIdx, 0, wName, L"Staging verified package payload...");
        std::filesystem::path stagedChild;
        if (!extractSuiteChildBinary(ch, stagedChild)) {
            std::filesystem::remove_all(tmpDir, ec);
            return ERROR_INSTALL_PACKAGE_OPEN_FAILED;
        }

        const std::wstring verb = step.alreadyInstalled
            ? (L"Adopting standalone installation into Suite: " + wName)
            : (L"Installing " + wName + L"...");
        emitProgress(sIdx, 35, wName, verb);

        if (ch.engine == "msi") {
            std::wstring props = (step.alreadyInstalled && ch.adoptStandalone)
                ? L"REINSTALL=ALL REINSTALLMODE=vomus RS_SUITE_MANAGED=1"
                : L"RS_SUITE_MANAGED=1";
            if (!ch.installArgs.empty()) {
                props += L" " + Utf8ToWide(ch.installArgs);
            }
            std::string msiErr;
            const UINT rc = runMsiWithLiveTelemetry(sIdx, wName, [&]() {
                return ::MsiInstallProductW(stagedChild.wstring().c_str(), props.c_str());
            }, msiErr);
            if (rc == ERROR_INSTALL_USEREXIT) {
                outError = "Installation canceled by user.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_USEREXIT;
            } else if (rc == ERROR_SUCCESS_REBOOT_REQUIRED) {
                anyReboot = true;
            } else if (rc != ERROR_SUCCESS && rc != ERROR_INSTALL_SERVICE_FAILURE) {
                outError = "MSI installation failed on '" + ch.name + "' (code " + std::to_string(rc) + ")" +
                           (msiErr.empty() ? "." : (": " + msiErr));
                std::filesystem::remove_all(tmpDir, ec);
                return static_cast<int>(rc);
            }
            wchar_t newProdBuf[39]{};
            if (!ch.upgradeCode.empty() &&
                ::MsiEnumRelatedProductsW(Utf8ToWide(ch.upgradeCode).c_str(), 0, 0, newProdBuf) == ERROR_SUCCESS) {
                SetInstalledAppsSystemComponentFlag(newProdBuf, true);
            }
        } else {
            std::wstring cmd = L"\"" + stagedChild.wstring() + L"\" /s RS_SUITE_MANAGED=1";
            if (!ch.installArgs.empty()) cmd += L" " + Utf8ToWide(ch.installArgs);
            DWORD code = 1;
            if (!runNativeProcessWithLiveTelemetry(sIdx, wName, cmd, verb, code)) {
                outError = "Failed launching child setup '" + ch.name + "'.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_FAILURE;
            }
            if (code == ERROR_INSTALL_USEREXIT) {
                outError = "Suite installation canceled by user.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_USEREXIT;
            }
            if (code == 3010 || code == 1641) anyReboot = true;
            else if (code != 0) {
                outError = "Child setup '" + ch.name + "' exited with code " + std::to_string(code) + ".";
                std::filesystem::remove_all(tmpDir, ec);
                return static_cast<int>(code);
            }
            if (!ch.upgradeCode.empty()) {
                SetInstalledAppsSystemComponentFlag(Utf8ToWide(ch.upgradeCode), true);
            }
        }

        // Complete this component's bar at `100%` (`1000` permille) before advancing to the next component!
        emitProgress(sIdx, 1000, wName, L"Completed " + wName + L".");
    }

    // Copy Suite Hub binary to `<suiteInstallDir>\SuiteSetup.exe` & register ONE Suite Maintenance entry!
    if (onSuiteProgress) {
        highWaterOverallPermille = std::max(highWaterOverallPermille, 975);
        onSuiteProgress({highWaterOverallPermille, 1000, plan.size(), std::max<std::size_t>(1, plan.size()), false,
                         Utf8ToWide(suite.suiteName), L"Registering unified Suite Maintenance hub..."});
    }

    std::filesystem::create_directories(suiteInstallDir, ec);
    const auto installedSuiteExe = suiteInstallDir / L"SuiteSetup.exe";
    if (_wcsicmp(selfPath.wstring().c_str(), installedSuiteExe.wstring().c_str()) != 0) {
        std::filesystem::copy_file(selfPath, installedSuiteExe, std::filesystem::copy_options::overwrite_existing, ec);
    }

    const std::wstring manageCmd = L"\"" + installedSuiteExe.wstring() + L"\" --suite-manage";
    const std::wstring quietCmd  = L"\"" + installedSuiteExe.wstring() + L"\" --suite-uninstall /s";
    if (!virtRegRoot.empty()) {
        bool dummy = false;
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"DisplayName",          suite.suiteName, dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"DisplayVersion",       suite.version, dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"Publisher",            suite.manufacturer, dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"InstallLocation",      WideToUtf8(suiteInstallDir.wstring()), dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"ModifyPath",           WideToUtf8(manageCmd), dummy);
        native_detail::VirtualRegWriteValue(virtRegRoot, suiteHiveCode, suiteUninstSubKey, L"UninstallString",      WideToUtf8(manageCmd), dummy);
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
            writeSz(L"DisplayIcon",          installedSuiteExe.wstring());
            writeSz(L"ModifyPath",           manageCmd);
            writeSz(L"UninstallString",      manageCmd);
            writeSz(L"QuietUninstallString", quietCmd);
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
    const auto op = static_cast<SuiteOperationKind>(std::clamp(std::atoi(kv["OpKind"].c_str()), 0, 2));
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
// INTERACTIVE UNIFIED SUITE SETUP & MAINTENANCE WINDOW (`SuiteSetupDialogApp`)
//   - Member Applications `SysListView32` Checklist (`Required` lock + live MB rollup)
//   - Dual Progress Bars:
//       1. `Overall Suite Progress` (`0%–100%` monotonic + Taskbar `ITaskbarList3`!)
//       2. `Current Component` (`0%–100%` active package progress!)
// ================================================================================
class SuiteSetupDialogApp {
public:
    static constexpr UINT WM_SUITE_PROGRESS = WM_APP + 70;
    static constexpr UINT WM_SUITE_DONE     = WM_APP + 71;

    static constexpr int IDC_STE_LIST_LBL   = 1601;
    static constexpr int IDC_STE_LIST       = 1602;
    static constexpr int IDC_STE_SEL_ALL    = 1603;
    static constexpr int IDC_STE_OP_COMBO   = 1604;
    static constexpr int IDC_STE_OVR_LBL    = 1605;
    static constexpr int IDC_STE_OVR_PCT    = 1606;
    static constexpr int IDC_STE_OVR_BAR    = 1607;
    static constexpr int IDC_STE_CMP_LBL    = 1608;
    static constexpr int IDC_STE_CMP_PCT    = 1609;
    static constexpr int IDC_STE_CMP_BAR    = 1610;
    static constexpr int IDC_STE_PRIMARY    = 1611;
    static constexpr int IDC_STE_CLOSE      = 1612;

    SuiteChainerManifest                suite{};
    std::filesystem::path               cliInstallDirOverride;
    std::filesystem::path               exportShotDir;
    SuiteOperationKind                  initialOp = SuiteOperationKind::InstallOrModify;

    ui::RswTheme                        theme{};
    suite_detail::SuiteTaskbarProgress  taskbar{};
    HWND                                hWnd        = nullptr;
    HWND                                hListLbl    = nullptr;
    HWND                                hList       = nullptr;
    HWND                                hBtnSelAll  = nullptr;
    HWND                                hOpCombo    = nullptr;
    HWND                                hOvrLbl     = nullptr;
    HWND                                hOvrPct     = nullptr;
    HWND                                hOvrBar     = nullptr;
    HWND                                hCmpLbl     = nullptr;
    HWND                                hCmpPct     = nullptr;
    HWND                                hCmpBar     = nullptr;
    HWND                                hBtnPrimary = nullptr;
    HWND                                hBtnClose   = nullptr;
    HICON                               hIconBig    = nullptr;
    HICON                               hIconSmall  = nullptr;
    HFONT                               hFontTitle  = nullptr;
    HFONT                               hFontBold   = nullptr;
    HFONT                               hFontBody   = nullptr;

    std::vector<bool>                   childAlreadyInstalled;
    std::vector<bool>                   childChecked;
    bool                                populatingList = false;
    bool                                anyAlreadyInstalled = false;
    int                                 lastAnnouncedDecile = -1;

    std::atomic<bool>                   running{false};
    std::atomic<bool>                   finished{false};
    std::atomic<bool>                   cancelFlag{false};
    int                                 finalExitCode = 0;
    std::mutex                          mtx;
    SuiteProgressSnapshot               latestSnap{};
    std::string                         errorUtf8;
    std::thread                         worker;

    ~SuiteSetupDialogApp() {
        cancelFlag.store(true);
        if (worker.joinable()) worker.join();
        for (HFONT* f : {&hFontTitle, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    bool NeedsAdminElevation() const noexcept {
        return suite.perMachine && !QueryProcessPrivilegeState().HasEffectiveAdminRights();
    }

    SuiteOperationKind SelectedOperation() const noexcept {
        if (!hOpCombo || !::IsWindowVisible(hOpCombo)) return initialOp;
        const int idx = static_cast<int>(::SendMessageW(hOpCombo, CB_GETCURSEL, 0, 0));
        if (idx == 1) return SuiteOperationKind::DetachKeepStandalone;
        if (idx == 2) return SuiteOperationKind::FullUninstall;
        return SuiteOperationKind::InstallOrModify;
    }

    void RebuildFonts(UINT dpi) {
        for (HFONT* f : {&hFontTitle, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        const int ts = theme.textScalePercent;
        hFontTitle = ui::CreateScaledFont(L"Segoe UI", 13, FW_BOLD,     dpi, ts);
        hFontBold  = ui::CreateScaledFont(L"Segoe UI",  9, FW_SEMIBOLD, dpi, ts);
        hFontBody  = ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);

        for (HWND h : {hListLbl, hOvrLbl, hOvrPct, hCmpPct, hBtnPrimary}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hList, hBtnSelAll, hOpCombo, hCmpLbl, hBtnClose}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }
    }

    void UpdateSelectionSummaryLabel() {
        if (!hListLbl) return;
        std::size_t selCount = 0;
        std::uint64_t selBytes = 0;
        for (std::size_t i = 0; i < suite.children.size() && i < childChecked.size(); ++i) {
            if (childChecked[i] || suite.children[i].required) {
                ++selCount;
                selBytes += suite.children[i].uncompressedBytes;
            }
        }
        const std::wstring txt =
            L"&Suite Applications — Selected " + std::to_wstring(selCount) + L" of " +
            std::to_wstring(suite.children.size()) + L" (" + suite_detail::FormatSuiteBytesShort(selBytes) + L"):";
        ::SetWindowTextW(hListLbl, txt.c_str());

        const auto op = SelectedOperation();
        if (!finished.load() && hBtnPrimary) {
            if (op == SuiteOperationKind::DetachKeepStandalone) {
                ::SetWindowTextW(hBtnPrimary, L"&Detach Suite");
            } else if (op == SuiteOperationKind::FullUninstall) {
                ::SetWindowTextW(hBtnPrimary, L"&Uninstall All");
            } else {
                ::SetWindowTextW(hBtnPrimary, Utf8ToWide(suite.installBtnText).c_str());
            }
            ::EnableWindow(hBtnPrimary, (op != SuiteOperationKind::InstallOrModify || selCount > 0) ? TRUE : FALSE);
        }
    }

    void PopulateSuiteList() {
        if (!hList) return;
        populatingList = true;
        ListView_DeleteAllItems(hList);
        childAlreadyInstalled.assign(suite.children.size(), false);
        childChecked.assign(suite.children.size(), true);
        anyAlreadyInstalled = false;

        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            const auto& ch = suite.children[i];
            std::wstring msiProd, natQuiet;
            const bool inst = IsSuiteChildCurrentlyInstalled(ch, msiProd, natQuiet);
            childAlreadyInstalled[i] = inst;
            if (inst) anyAlreadyInstalled = true;
            childChecked[i] = ch.required || ch.selectedByDefault || inst;

            std::wstring colName = Utf8ToWide(ch.name);
            if (!ch.version.empty()) colName += L" (v" + Utf8ToWide(ch.version) + L")";
            if (ch.required) colName += L" [Required]";

            LVITEMW lvi{};
            lvi.mask    = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem   = static_cast<int>(i);
            lvi.pszText = colName.data();
            lvi.lParam  = static_cast<LPARAM>(i);
            const int row = ListView_InsertItem(hList, &lvi);

            std::wstring colEng = EqualsIgnoreCase(ch.engine, "msi") ? L"MSI" : L"Native";
            ListView_SetItemText(hList, row, 1, colEng.data());

            std::wstring colSz = suite_detail::FormatSuiteBytesShort(ch.uncompressedBytes);
            ListView_SetItemText(hList, row, 2, colSz.data());

            std::wstring colSt = L"Ready to Install";
            if (inst) {
                const std::wstring keyLeaf = !msiProd.empty() ? msiProd : Utf8ToWide(ch.upgradeCode);
                colSt = IsInstalledAppHiddenAsSuiteComponent(keyLeaf)
                    ? L"Installed (Managed by Suite)"
                    : L"Installed Standalone (Will Adopt)";
            }
            ListView_SetItemText(hList, row, 3, colSt.data());
            ListView_SetCheckState(hList, row, childChecked[i] ? TRUE : FALSE);
        }
        populatingList = false;
        UpdateSelectionSummaryLabel();
    }

    void ApplyListThemeColors() {
        if (!hList) return;
        ListView_SetBkColor(hList, theme.cardBg);
        ListView_SetTextBkColor(hList, theme.cardBg);
        ListView_SetTextColor(hList, theme.textPrimary);
        ui::AttachDarkAwareListViewSubclass(hList, &theme);
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

        const bool showMaintCombo = anyAlreadyInstalled || initialOp != SuiteOperationKind::InstallOrModify;
        ::MoveWindow(hListLbl,   S(18), S(68), W - S(136), S(20), TRUE);
        ::MoveWindow(hBtnSelAll, W - S(112), S(64), S(94), S(26), TRUE);

        const int progBlockH = suite.showComponentProgressBar ? S(114) : S(64);
        const int footY      = H - S(46);
        const int progTop    = footY - progBlockH - S(10);
        const int comboRowH  = showMaintCombo ? S(34) : 0;
        const int listTop    = S(94);
        const int listH      = std::max(S(96), progTop - listTop - comboRowH - S(10));

        ::MoveWindow(hList, S(18), listTop, W - S(36), listH, TRUE);
        ListView_SetColumnWidth(hList, 0, S(296));
        ListView_SetColumnWidth(hList, 1, S(64));
        ListView_SetColumnWidth(hList, 2, S(76));
        ListView_SetColumnWidth(hList, 3, std::max(S(130), W - S(36) - S(456)));

        if (hOpCombo) {
            ::MoveWindow(hOpCombo, S(18), listTop + listH + S(6), W - S(36), S(220), TRUE);
            ::ShowWindow(hOpCombo, showMaintCombo ? SW_SHOW : SW_HIDE);
        }

        // Bar 1: Overall Suite Progress (Strictly Monotonic `0%..100%` + Taskbar `ITaskbarList3`)
        ::MoveWindow(hOvrLbl, S(18),        progTop,         W - S(210), S(18), TRUE);
        ::MoveWindow(hOvrPct, W - S(190),   progTop,         S(172),     S(18), TRUE);
        ::MoveWindow(hOvrBar, S(18),        progTop + S(20), W - S(36),  S(22), TRUE);

        // Bar 2: Active Suite Component Progress (`0%..100%` per component)
        if (suite.showComponentProgressBar) {
            const int cmpTop = progTop + S(54);
            ::MoveWindow(hCmpLbl, S(18),      cmpTop,         W - S(116), S(18), TRUE);
            ::MoveWindow(hCmpPct, W - S(96),  cmpTop,         S(78),      S(18), TRUE);
            ::MoveWindow(hCmpBar, S(18),      cmpTop + S(20), W - S(36),  S(18), TRUE);
            ::ShowWindow(hCmpLbl, SW_SHOW);
            ::ShowWindow(hCmpPct, SW_SHOW);
            ::ShowWindow(hCmpBar, SW_SHOW);
        } else {
            ::ShowWindow(hCmpLbl, SW_HIDE);
            ::ShowWindow(hCmpPct, SW_HIDE);
            ::ShowWindow(hCmpBar, SW_HIDE);
        }

        ::MoveWindow(hBtnPrimary, W - S(244), footY, S(136), S(32), TRUE);
        ::MoveWindow(hBtnClose,   W - S(100), footY, S(82),  S(32), TRUE);
        ::SendMessageW(hBtnPrimary, BCM_SETSHIELD, 0, (!finished.load() && NeedsAdminElevation()) ? TRUE : FALSE);
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

        // Sync Windows Taskbar `ITaskbarList3` strictly to Overall Suite Progress!
        taskbar.SetOverallPermille(hWnd, ovrPerm, snap.queuePaused);

        // Announce every 10% overall milestone to Narrator/NVDA/JAWS without spamming per-file ticks!
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
                         L"Installing this Suite into Program Files requires Administrator rights, and UAC elevation is disabled on this PC.",
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
        const SuiteOperationKind op = SelectedOperation();
        const std::string selectedCsv = csv.str();

        running.store(true);
        cancelFlag.store(false);
        lastAnnouncedDecile = -1;
        ::EnableWindow(hList, FALSE);
        ::EnableWindow(hBtnSelAll, FALSE);
        if (hOpCombo) ::EnableWindow(hOpCombo, FALSE);
        ::EnableWindow(hBtnPrimary, FALSE);
        ::SetWindowTextW(hBtnClose, L"&Cancel");

        if (worker.joinable()) worker.join();
        worker = std::thread([this, op, selectedIds, selectedCsv]() {
            if (NeedsAdminElevation()) {
                wchar_t selfBuf[MAX_PATH * 4]{};
                ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
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
                       << "InstallDir="  << native_detail::EscapeSingleLine(WideToUtf8(cliInstallDirOverride.wstring())) << "\n";
                }
                const std::wstring params = L"--elevated-suite-worker \"" + jobPath.wstring() + L"\"";
                SHELLEXECUTEINFOW sei{sizeof(sei)};
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.hwnd         = hWnd;
                sei.lpVerb       = L"runas";
                sei.lpFile       = selfBuf;
                sei.lpParameters = params.c_str();
                sei.nShow        = SW_HIDE;
                if (!::ShellExecuteExW(&sei) || !sei.hProcess) {
                    std::filesystem::remove(jobPath, ec);
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        errorUtf8 = "Administrator elevation was canceled.";
                    }
                    finalExitCode = ERROR_INSTALL_USEREXIT;
                    ::PostMessageW(hWnd, WM_SUITE_DONE, 0, 0);
                    return;
                }
                while (::WaitForSingleObject(sei.hProcess, 45) == WAIT_TIMEOUT) {
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
                ::GetExitCodeProcess(sei.hProcess, &code);
                ::CloseHandle(sei.hProcess);
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
                suite, op, selectedIds, cliInstallDirOverride, &cancelFlag,
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

        const int hdrH = S(58);
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
            ::DrawIconEx(dc, S(16), S(12), hIconBig, S(32), S(32), 0, nullptr, DI_NORMAL);
        }

        HGDIOBJ oldF = ::SelectObject(dc, hFontTitle);
        ::SetTextColor(dc, theme.isHighContrast ? theme.textPrimary : theme.bannerTitleText);
        const std::wstring head = Utf8ToWide(suite.suiteName + " v" + suite.version);
        RECT tRc{S(58), S(8), W - S(16), S(32)};
        ::DrawTextW(dc, head.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(dc, hFontBody);
        ::SetTextColor(dc, theme.isHighContrast ? theme.textSecondary : RGB(203, 213, 225));
        const std::wstring sub = !suite.tagline.empty()
            ? Utf8ToWide(suite.tagline)
            : Utf8ToWide(suite.manufacturer + " Unified Suite Installer • Single UAC & Clean Installed Apps");
        RECT sRc{S(58), S(32), W - S(16), S(52)};
        ::DrawTextW(dc, sub.c_str(), -1, &sRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(dc, oldF);
        ::BitBlt(hdcOut, 0, 0, W, H, dc, 0, 0, SRCCOPY);
        ::SelectObject(dc, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(dc);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<SuiteSetupDialogApp*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SuiteSetupDialogApp*>(cs->lpCreateParams);
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
                self->hListLbl = ::CreateWindowExW(
                    0, L"STATIC", L"&Suite Applications:", WS_CHILD | WS_VISIBLE,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_LIST_LBL)), hInst, nullptr);
                self->hList = ::CreateWindowExW(
                    WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Suite Applications Checklist",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_LIST)), hInst, nullptr);
                ListView_SetExtendedListViewStyle(
                    self->hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

                auto addCol = [&](int idx, const wchar_t* text, int cx) {
                    LVCOLUMNW col{};
                    col.mask    = LVCF_TEXT | LVCF_WIDTH;
                    col.pszText = const_cast<wchar_t*>(text);
                    col.cx      = cx;
                    ListView_InsertColumn(self->hList, idx, &col);
                };
                addCol(0, L"Application", 240);
                addCol(1, L"Engine",       68);
                addCol(2, L"Size",         84);
                addCol(3, L"Status",      180);

                self->hBtnSelAll = ::CreateWindowExW(
                    0, L"BUTTON", L"Select &All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_SEL_ALL)), hInst, nullptr);

                self->hOpCombo = ::CreateWindowExW(
                    0, L"COMBOBOX", L"Suite Maintenance Mode",
                    WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    0, 0, 10, 180, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_OP_COMBO)), hInst, nullptr);
                ::SendMessageW(self->hOpCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Action: Install / Update Selected Suite Applications (Single Installed Apps Entry)"));
                ::SendMessageW(self->hOpCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Action: Detach Suite && Keep Standalone Applications Visible in Installed Apps"));
                ::SendMessageW(self->hOpCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Action: Uninstall Entire Suite && All Managed Applications"));
                ::SendMessageW(self->hOpCombo, CB_SETCURSEL, static_cast<WPARAM>(self->initialOp), 0);
                ui::ApplyDarkComboBoxTheme(self->hOpCombo, self->theme);

                self->hOvrLbl = ::CreateWindowExW(
                    0, L"STATIC", L"Overall Suite Progress:", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_OVR_LBL)), hInst, nullptr);
                self->hOvrPct = ::CreateWindowExW(
                    0, L"STATIC", L"0%", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_OVR_PCT)), hInst, nullptr);
                self->hOvrBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Overall Suite Progress", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_OVR_BAR)), hInst, nullptr);
                ::SendMessageW(self->hOvrBar, PBM_SETRANGE32, 0, 1000);

                self->hCmpLbl = ::CreateWindowExW(
                    0, L"STATIC", L"Current Component: Ready", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_CMP_LBL)), hInst, nullptr);
                self->hCmpPct = ::CreateWindowExW(
                    0, L"STATIC", L"0%", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_CMP_PCT)), hInst, nullptr);
                self->hCmpBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Current Component Progress", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_CMP_BAR)), hInst, nullptr);
                ::SendMessageW(self->hCmpBar, PBM_SETRANGE32, 0, 1000);

                const std::wstring cta = Utf8ToWide(self->suite.installBtnText);
                self->hBtnPrimary = ::CreateWindowExW(
                    0, L"BUTTON", cta.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_PRIMARY)), hInst, nullptr);
                self->hBtnClose = ::CreateWindowExW(
                    0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STE_CLOSE)), hInst, nullptr);

                ui::AttachAccessiblePushButtonSubclass(self->hBtnSelAll,  &self->theme, false, false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnPrimary, &self->theme, true,  false);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnClose,   &self->theme, false, false);
                self->ApplyListThemeColors();
                self->PopulateSuiteList();
                self->LayoutControls();
                return 0;
            }
            case WM_SIZE:
                self->LayoutControls();
                return 0;
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
                    const COLORREF pCol = ui::ParseHexColor(self->suite.accentPrimaryHex, RGB(37, 99, 235));
                    const COLORREF sCol = ui::ParseHexColor(self->suite.accentSecondaryHex, RGB(251, 191, 36));
                    self->theme.Refresh(pCol, sCol);
                    ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    ui::ApplyDarkComboBoxTheme(self->hOpCombo, self->theme);
                    self->ApplyListThemeColors();
                    self->LayoutControls();
                    ::InvalidateRect(hWnd, nullptr, TRUE);
                    return 0;
                }
                break;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORLISTBOX: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(hdc, self->theme.textPrimary);
                ::SetBkColor(hdc, (msg == WM_CTLCOLORLISTBOX) ? self->theme.cardBg : self->theme.windowBg);
                return reinterpret_cast<LRESULT>((msg == WM_CTLCOLORLISTBOX) ? self->theme.hbrCard : self->theme.hbrWindow);
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
                if (nm && nm->idFrom == IDC_STE_LIST && !self->populatingList) {
                    if (nm->code == LVN_ITEMCHANGING) {
                        const auto* lvn = reinterpret_cast<const NMLISTVIEW*>(lParam);
                        if ((lvn->uChanged & LVIF_STATE) != 0) {
                            const UINT oldChk = (lvn->uOldState & LVIS_STATEIMAGEMASK) >> 12;
                            const UINT newChk = (lvn->uNewState & LVIS_STATEIMAGEMASK) >> 12;
                            const int idx = lvn->iItem;
                            if (oldChk == 2 && newChk == 1 && idx >= 0 &&
                                static_cast<std::size_t>(idx) < self->suite.children.size() &&
                                self->suite.children[static_cast<std::size_t>(idx)].required) {
                                return TRUE; // Prevent unchecking Required core Suite packages!
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
                self->running.store(false);
                if (wParam == 1) {
                    self->finished.store(true);
                    SuiteProgressSnapshot doneSnap{};
                    doneSnap.overallPermille   = 1000;
                    doneSnap.componentPermille = 1000;
                    doneSnap.stepIndex         = self->suite.children.size();
                    doneSnap.stepCount         = std::max<std::size_t>(1, self->suite.children.size());
                    doneSnap.componentName     = Utf8ToWide(self->suite.suiteName);
                    doneSnap.detailStatus      = (self->finalExitCode == ERROR_SUCCESS_REBOOT_REQUIRED)
                        ? L"Completed (Windows restart recommended)."
                        : L"All selected Suite packages completed successfully.";
                    self->ApplyProgressSnapshotToUi(doneSnap);
                    self->taskbar.Clear(hWnd);
                    self->PopulateSuiteList();
                    ::SetWindowTextW(self->hBtnPrimary, L"&Finish");
                    ::SendMessageW(self->hBtnPrimary, BCM_SETSHIELD, 0, FALSE);
                    ::EnableWindow(self->hBtnPrimary, TRUE);
                    ::SetWindowTextW(self->hBtnClose, L"Close");
                    ::SetFocus(self->hBtnPrimary);
                } else {
                    self->taskbar.SetError(hWnd);
                    std::string errCopy;
                    {
                        std::lock_guard<std::mutex> lk(self->mtx);
                        errCopy = self->errorUtf8;
                    }
                    const std::wstring msgW = Utf8ToWide(errCopy.empty() ? "Suite operation did not complete." : errCopy);
                    ::SetWindowTextW(self->hCmpLbl, msgW.c_str());
                    if (self->finalExitCode != ERROR_INSTALL_USEREXIT) {
                        const std::wstring wTitle = Utf8ToWide(self->suite.suiteName) + L" Setup";
                        ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), wTitle.c_str(),
                                     L"Suite Setup Issue", msgW.c_str(), TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
                    }
                    self->taskbar.Clear(hWnd);
                    ::EnableWindow(self->hList, TRUE);
                    ::EnableWindow(self->hBtnSelAll, TRUE);
                    if (self->hOpCombo) ::EnableWindow(self->hOpCombo, TRUE);
                    ::EnableWindow(self->hBtnPrimary, TRUE);
                    ::SetWindowTextW(self->hBtnClose, L"Close");
                }
                return 0;
            }
            case WM_COMMAND: {
                const int id   = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_STE_OP_COMBO && code == CBN_SELCHANGE) {
                    const bool enableChecks = (self->SelectedOperation() == SuiteOperationKind::InstallOrModify);
                    ::EnableWindow(self->hList, enableChecks ? TRUE : FALSE);
                    ::EnableWindow(self->hBtnSelAll, enableChecks ? TRUE : FALSE);
                    self->UpdateSelectionSummaryLabel();
                    return 0;
                }
                if (id == IDC_STE_SEL_ALL && !self->running.load()) {
                    self->populatingList = true;
                    for (std::size_t i = 0; i < self->suite.children.size(); ++i) {
                        self->childChecked[i] = true;
                        ListView_SetCheckState(self->hList, static_cast<int>(i), TRUE);
                    }
                    self->populatingList = false;
                    self->UpdateSelectionSummaryLabel();
                    return 0;
                }
                if (id == IDC_STE_PRIMARY || id == IDOK) {
                    if (self->finished.load()) {
                        ::DestroyWindow(hWnd);
                    } else {
                        self->StartSuiteExecution();
                    }
                    return 0;
                }
                if (id == IDC_STE_CLOSE || id == IDCANCEL) {
                    if (self->running.load()) {
                        self->cancelFlag.store(true);
                    } else {
                        ::DestroyWindow(hWnd);
                    }
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                if (self->running.load()) {
                    self->cancelFlag.store(true);
                } else {
                    ::DestroyWindow(hWnd);
                }
                return 0;
            case WM_DESTROY:
                self->taskbar.Release();
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

/// Interactive Dual-Progress Window & Headless CLI Dispatcher when `kResIdSuiteManifest` (`#230`) is present!
inline int RunSuiteChainerEntrypoint(
    HINSTANCE hInstance,
    int nCmdShow,
    const std::vector<std::wstring>& args,
    std::string_view suiteManifestText)
{
    const SuiteChainerManifest suite = ParseSuiteChainerManifestIni(suiteManifestText);
    bool silentMode = false;
    SuiteOperationKind op = SuiteOperationKind::InstallOrModify;
    std::filesystem::path cliInstallDirOverride;
    std::filesystem::path exportShotDir;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") silentMode = true;
        else if (a == L"--suite-uninstall") op = SuiteOperationKind::FullUninstall;
        else if (a == L"--suite-detach")    op = SuiteOperationKind::DetachKeepStandalone;
        else if (a.rfind(L"INSTALLDIR=", 0) == 0) cliInstallDirOverride = a.substr(11);
        else if (a == L"--install-dir" && i + 1 < args.size()) cliInstallDirOverride = args[++i];
        else if (a == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[++i];
            ui::ForcedThemeOverride() = 1;
        }
    }

    if (!MeetsWindowsBuildFloor(suite.minWindowsBuild)) return 1603;

    const auto priv = QueryProcessPrivilegeState();
    if (exportShotDir.empty() &&
        (suite.requestedExecLevel == "requireAdministrator" || (silentMode && suite.perMachine)) &&
        !priv.HasEffectiveAdminRights()) {
        if (!silentMode) {
            // Re-launch self elevated (`runas`) when `requireAdministrator` was explicitly requested
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
        return ExecuteSuiteChainerOperationDetailed(suite, op, {}, cliInstallDirOverride, nullptr, nullptr, err);
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int exitRc = 0;
    {
        ui::ScopedGdiPlusSession gpSession;
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
        ::InitCommonControlsEx(&icc);

        SuiteSetupDialogApp app{};
        app.suite                 = suite;
        app.cliInstallDirOverride = cliInstallDirOverride;
        app.exportShotDir         = exportShotDir;
        app.initialOp             = op;

        const COLORREF pCol = ui::ParseHexColor(suite.accentPrimaryHex, RGB(37, 99, 235));
        const COLORREF sCol = ui::ParseHexColor(suite.accentSecondaryHex, RGB(251, 191, 36));
        app.theme.Refresh(pCol, sCol);
        app.hIconBig   = static_cast<HICON>(::LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        app.hIconSmall = static_cast<HICON>(::LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        if (!app.hIconBig) app.hIconBig = ::LoadIconW(nullptr, IDI_APPLICATION);

        constexpr const wchar_t* kCls = L"RaggieSoftUnifiedSuiteSetupWindow";
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &SuiteSetupDialogApp::WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon         = app.hIconBig;
        wc.hIconSm       = app.hIconSmall ? app.hIconSmall : app.hIconBig;
        wc.lpszClassName = kCls;
        ::RegisterClassExW(&wc);

        const UINT dpi = ::GetDpiForSystem();
        RECT wRc{0, 0,
                 ui::ScalePx(660, dpi, app.theme.textScalePercent),
                 ui::ScalePx(468, dpi, app.theme.textScalePercent)};
        constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        const DWORD kExStyle   = exportShotDir.empty() ? WS_EX_APPWINDOW : 0u;
        ::AdjustWindowRectExForDpi(&wRc, kStyle, FALSE, kExStyle, dpi);
        const int wW = wRc.right - wRc.left;
        const int wH = wRc.bottom - wRc.top;
        const int wX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - wW) / 2);
        const int wY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - wH) / 2);

        const std::wstring wTitle = Utf8ToWide(suite.suiteName) + L" — Unified Suite Setup";
        HWND hWnd = ::CreateWindowExW(kExStyle, kCls, wTitle.c_str(), kStyle, wX, wY, wW, wH, nullptr, nullptr, hInstance, &app);
        if (!hWnd) {
            exitRc = 1;
        } else {
            ::ShowWindow(hWnd, nCmdShow);
            ::UpdateWindow(hWnd);
            if (!exportShotDir.empty()) {
                // Render a live mid-suite showcase snapshot demonstrating monotonic Overall + Component Dual Bars!
                SuiteProgressSnapshot demo{};
                demo.overallPermille   = 580;
                demo.componentPermille = 640;
                demo.stepIndex         = std::min<std::size_t>(1, std::max<std::size_t>(1, suite.children.size()) - 1);
                demo.stepCount         = std::max<std::size_t>(1, suite.children.size());
                demo.componentName     = !suite.children.empty()
                    ? Utf8ToWide(suite.children[demo.stepIndex].name)
                    : L"RaggieSoft Showcase Studio";
                demo.detailStatus      = L"Copying verified native binaries...";
                app.ApplyProgressSnapshotToUi(demo);
                ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                const bool okShot = ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-suite-dual-progress.png");
                ::DestroyWindow(hWnd);
                exitRc = okShot ? 0 : 2;
            } else {
                MSG msg{};
                while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    if (!::IsDialogMessageW(hWnd, &msg)) {
                        ::TranslateMessage(&msg);
                        ::DispatchMessage(&msg);
                    }
                }
                exitRc = app.finalExitCode;
            }
        }
    }
    ::CoUninitialize();
    return exitRc;
}

} // namespace rsw::compact_suite
