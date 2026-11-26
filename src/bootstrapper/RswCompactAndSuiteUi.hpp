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
                    (selfPath.stem().wstring() + L"-" + std::to_wstring(::GetCurrentProcessId()))).lexically_normal();
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
    std::error_code ec;
    if (!m.postExtractOpen.empty()) {
        const auto docPath = (targetDir / Utf8ToWide(m.postExtractOpen)).lexically_normal();
        if (std::filesystem::exists(docPath, ec)) {
            SHELLEXECUTEINFOW sei{sizeof(sei)};
            sei.fMask        = SEE_MASK_FLAG_NO_UI; // Gracefully ignore if no compatible viewer is installed!
            sei.lpVerb       = L"open";
            const std::wstring wDoc = docPath.wstring();
            const std::wstring wDir = docPath.parent_path().wstring();
            sei.lpFile       = wDoc.c_str();
            sei.lpDirectory  = wDir.c_str();
            sei.nShow        = SW_SHOWNORMAL;
            ::ShellExecuteExW(&sei);
        }
    }

    const std::string exeRel = !m.postExtractRun.empty() ? m.postExtractRun : m.mainExecutable;
    if (!exeRel.empty()) {
        const auto exePath = (targetDir / Utf8ToWide(exeRel)).lexically_normal();
        if (std::filesystem::exists(exePath, ec)) {
            const std::wstring wExe  = exePath.wstring();
            const std::wstring wArgs = Utf8ToWide(m.postExtractArgs);
            const std::wstring wDir  = exePath.parent_path().wstring();
            ::ShellExecuteW(nullptr, L"open", wExe.c_str(), wArgs.empty() ? nullptr : wArgs.c_str(), wDir.c_str(), SW_SHOWNORMAL);
        }
    }
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

    std::filesystem::path targetDir;
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

    bool NeedsAdminElevation() const noexcept {
        if (manifest.mode == "sfx") return false;
        if (!manifest.perMachine) return false;
        return !QueryProcessPrivilegeState().HasEffectiveAdminRights();
    }

    bool HasPostAction() const noexcept {
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
            const bool ok = ExecuteNativeOrSfxTransaction(
                manifest, pakPtr, pakSize, featurePacks, {}, opts, &cancelFlag,
                [this](int perm, const std::wstring& st) {
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        statusText = st;
                    }
                    ::PostMessageW(hWnd, WM_SMP_PROGRESS, static_cast<WPARAM>(perm), 0);
                },
                err);
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
                const bool isSfx = (self->manifest.mode == "sfx");
                const wchar_t* dirCaption = isSfx ? L"E&xtract to folder:" : L"&Destination folder:";
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
                return 0;
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

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") {
            silentMode = true;
        } else if ((a == L"--extract-to" || a == L"--install-dir") && i + 1 < args.size()) {
            targetDir  = args[++i];
            silentMode = true;
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

        std::string err;
        if (!ExecuteNativeOrSfxTransaction(manifest, pakPtr, pakSz, featurePacks, {}, opts, nullptr, nullptr, err)) {
            return ERROR_INSTALL_FAILURE;
        }
        if (runPostActions) {
            ExecutePostExtractOrFinishActions(manifest, targetDir);
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
            }
        }
    }
    ::CoUninitialize();
    return exitRc;
}

// ================================================================================
// UNIFIED SUITE CHAINER EXECUTION & DYNAMIC `ARPSYSTEMCOMPONENT` ADOPTION
// ================================================================================
enum class SuiteOperationKind {
    InstallOrModify,
    DetachKeepStandalone,
    FullUninstall
};

/// Executes a Unified Suite Chainer operation (`InstallOrModify`, `DetachKeepStandalone`, or `FullUninstall`),
/// extracting embedded child packages (`#400+i`), dynamically adopting existing standalone siblings via
/// `RS_SUITE_MANAGED=1` / `SystemComponent=1`, and registering ONE clean Suite Maintenance entry!
inline int ExecuteSuiteChainerOperation(
    const SuiteChainerManifest& suite,
    SuiteOperationKind opKind,
    const std::set<std::string>& selectedChildIdsLower,
    const std::filesystem::path& cliInstallDirOverride,
    const std::function<void(int permille, const std::wstring& status)>& onProgress,
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

    ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);

    if (opKind == SuiteOperationKind::DetachKeepStandalone) {
        // Restore standalone visibility (`SystemComponent = 0` / `RS_SUITE_MANAGED=0`) on all installed children,
        // then remove ONLY the Suite umbrella entry!
        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            const auto& ch = suite.children[i];
            std::wstring msiProdCode, nativeQuiet;
            if (!IsSuiteChildCurrentlyInstalled(ch, msiProdCode, nativeQuiet)) continue;
            if (onProgress) {
                onProgress(static_cast<int>(((i + 1) * 900u) / std::max<std::size_t>(1, suite.children.size())),
                           L"Restoring standalone Installed Apps visibility: " + Utf8ToWide(ch.name));
            }
            if (!msiProdCode.empty()) {
                std::filesystem::path stagedMsi;
                if (extractSuiteChildBinary(ch, stagedMsi)) {
                    ::MsiInstallProductW(stagedMsi.wstring().c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus RS_SUITE_MANAGED=0");
                }
                SetInstalledAppsSystemComponentFlag(msiProdCode, false);
            }
            if (!ch.upgradeCode.empty()) {
                SetInstalledAppsSystemComponentFlag(Utf8ToWide(ch.upgradeCode), false);
            }
        }
        if (!virtRegRoot.empty()) native_detail::VirtualRegDeleteTree(virtRegRoot, suiteHiveCode, suiteUninstSubKey);
        else ::RegDeleteTreeW(hSuiteHive, suiteUninstSubKey.c_str());
        if (_wcsicmp(selfPath.wstring().c_str(), (suiteInstallDir / L"SuiteSetup.exe").wstring().c_str()) != 0) {
            std::filesystem::remove(suiteInstallDir / L"SuiteSetup.exe", ec);
            std::filesystem::remove(suiteInstallDir, ec);
        }
        std::filesystem::remove_all(tmpDir, ec);
        if (onProgress) onProgress(1000, L"Suite detached; member apps kept standalone.");
        return 0;
    }

    if (opKind == SuiteOperationKind::FullUninstall) {
        for (std::size_t i = 0; i < suite.children.size(); ++i) {
            const auto& ch = suite.children[i];
            std::wstring msiProdCode, nativeQuiet;
            if (!IsSuiteChildCurrentlyInstalled(ch, msiProdCode, nativeQuiet)) continue;
            if (onProgress) {
                onProgress(static_cast<int>(((i + 1) * 900u) / std::max<std::size_t>(1, suite.children.size())),
                           L"Uninstalling " + Utf8ToWide(ch.name) + L"...");
            }
            if (!msiProdCode.empty()) {
                ::MsiConfigureProductExW(msiProdCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
            } else if (!nativeQuiet.empty()) {
                std::wstring cmd = nativeQuiet;
                STARTUPINFOW si{sizeof(si)};
                PROCESS_INFORMATION pi{};
                if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    ::WaitForSingleObject(pi.hProcess, INFINITE);
                    ::CloseHandle(pi.hThread);
                    ::CloseHandle(pi.hProcess);
                }
            }
        }
        if (!virtRegRoot.empty()) native_detail::VirtualRegDeleteTree(virtRegRoot, suiteHiveCode, suiteUninstSubKey);
        else ::RegDeleteTreeW(hSuiteHive, suiteUninstSubKey.c_str());
        if (_wcsicmp(selfPath.wstring().c_str(), (suiteInstallDir / L"SuiteSetup.exe").wstring().c_str()) != 0) {
            std::filesystem::remove(suiteInstallDir / L"SuiteSetup.exe", ec);
            std::filesystem::remove(suiteInstallDir, ec);
        }
        std::filesystem::remove_all(tmpDir, ec);
        if (onProgress) onProgress(1000, L"Suite uninstalled.");
        return 0;
    }

    // `SuiteOperationKind::InstallOrModify`:
    bool anyReboot = false;
    const std::size_t nTotal = std::max<std::size_t>(1, suite.children.size());
    for (std::size_t i = 0; i < suite.children.size(); ++i) {
        const auto& ch = suite.children[i];
        const bool wantInstalled = ch.required ||
                                   selectedChildIdsLower.empty() ||
                                   (selectedChildIdsLower.count(AsciiLower(ch.id)) > 0);
        std::wstring msiProdCode, nativeQuiet;
        const bool alreadyInst = IsSuiteChildCurrentlyInstalled(ch, msiProdCode, nativeQuiet);

        if (!wantInstalled) {
            // If deselected during Modify AND currently installed, remove it cleanly!
            if (alreadyInst) {
                if (!msiProdCode.empty()) {
                    ::MsiConfigureProductExW(msiProdCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
                } else if (!nativeQuiet.empty()) {
                    std::wstring cmd = nativeQuiet;
                    STARTUPINFOW si{sizeof(si)};
                    PROCESS_INFORMATION pi{};
                    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                        ::WaitForSingleObject(pi.hProcess, INFINITE);
                        ::CloseHandle(pi.hThread);
                        ::CloseHandle(pi.hProcess);
                    }
                }
            }
            continue;
        }

        std::filesystem::path stagedChild;
        if (!extractSuiteChildBinary(ch, stagedChild)) {
            std::filesystem::remove_all(tmpDir, ec);
            return ERROR_INSTALL_PACKAGE_OPEN_FAILED;
        }

        if (onProgress) {
            const int perm = static_cast<int>(((i * 900u) / nTotal) + 50u);
            onProgress(perm, (alreadyInst ? L"Adopting into Suite: " : L"Installing: ") + Utf8ToWide(ch.name));
        }

        if (ch.engine == "msi") {
            std::wstring props = alreadyInst && ch.adoptStandalone
                ? L"REINSTALL=ALL REINSTALLMODE=vomus RS_SUITE_MANAGED=1"
                : L"RS_SUITE_MANAGED=1";
            if (!ch.installArgs.empty()) {
                props += L" " + Utf8ToWide(ch.installArgs);
            }
            const UINT rc = ::MsiInstallProductW(stagedChild.wstring().c_str(), props.c_str());
            if (rc == ERROR_SUCCESS_REBOOT_REQUIRED) {
                anyReboot = true;
            } else if (rc != ERROR_SUCCESS && rc != ERROR_INSTALL_SERVICE_FAILURE) {
                outError = "MSI installation failed on '" + ch.name + "' (code " + std::to_string(rc) + ").";
                std::filesystem::remove_all(tmpDir, ec);
                return static_cast<int>(rc);
            }
            // Refresh installed `ProductCode` and guarantee `SystemComponent = 1` under `Uninstall\<ProductCode>`!
            wchar_t newProdBuf[39]{};
            if (!ch.upgradeCode.empty() &&
                ::MsiEnumRelatedProductsW(Utf8ToWide(ch.upgradeCode).c_str(), 0, 0, newProdBuf) == ERROR_SUCCESS) {
                SetInstalledAppsSystemComponentFlag(newProdBuf, true);
            }
        } else {
            std::wstring cmd = L"\"" + stagedChild.wstring() + L"\" /s RS_SUITE_MANAGED=1";
            if (!ch.installArgs.empty()) cmd += L" " + Utf8ToWide(ch.installArgs);
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmpDir.wstring().c_str(), &si, &pi)) {
                outError = "Failed launching child setup '" + ch.name + "'.";
                std::filesystem::remove_all(tmpDir, ec);
                return ERROR_INSTALL_FAILURE;
            }
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
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
    }

    // Copy Suite Hub binary to `<suiteInstallDir>\SuiteSetup.exe` & register ONE Suite Maintenance entry!
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
    if (onProgress) onProgress(1000, L"Suite configuration complete.");
    return anyReboot ? ERROR_SUCCESS_REBOOT_REQUIRED : 0;
}

/// Interactive & Headless CLI Dispatcher when `kResIdSuiteManifest` (`#230`) is present!
inline int RunSuiteChainerEntrypoint(
    HINSTANCE hInstance,
    int /*nCmdShow*/,
    const std::vector<std::wstring>& args,
    std::string_view suiteManifestText)
{
    const SuiteChainerManifest suite = ParseSuiteChainerManifestIni(suiteManifestText);
    bool silentMode = false;
    SuiteOperationKind op = SuiteOperationKind::InstallOrModify;
    std::filesystem::path cliInstallDirOverride;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") silentMode = true;
        else if (a == L"--suite-uninstall") op = SuiteOperationKind::FullUninstall;
        else if (a == L"--suite-detach")    op = SuiteOperationKind::DetachKeepStandalone;
        else if (a.rfind(L"INSTALLDIR=", 0) == 0) cliInstallDirOverride = a.substr(11);
        else if (a == L"--install-dir" && i + 1 < args.size()) cliInstallDirOverride = args[++i];
    }

    if (!MeetsWindowsBuildFloor(suite.minWindowsBuild)) return 1603;

    const auto priv = QueryProcessPrivilegeState();
    if (suite.perMachine && !priv.HasEffectiveAdminRights()) {
        if (silentMode) return ERROR_ACCESS_DENIED;
        // Re-launch self elevated (`runas`) with identical arguments so the entire Suite runs under ONE UAC prompt!
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

    if (!silentMode) {
        // Accessible Windows 11 `TaskDialogIndirect` Suite Hub (Install / Modify, Detach Standalone, or Uninstall All!)
        std::wstring summary = L"Included applications (" + std::to_wstring(suite.children.size()) + L"):\n";
        for (const auto& ch : suite.children) {
            std::wstring msiCode, natQuiet;
            const bool inst = IsSuiteChildCurrentlyInstalled(ch, msiCode, natQuiet);
            summary += L"  • " + Utf8ToWide(ch.name) + (inst ? L"  [Installed — Managed by Suite]\n" : L"  [Ready to Install]\n");
        }
        summary += L"\nAll selected tools share one unified Suite entry in Windows Installed Apps.";

        const TASKDIALOG_BUTTON buttons[] = {
            {1001, L"&Install / Repair All Suite Applications\nInstalls or adopts all suite tools under a single Installed Apps entry."},
            {1002, L"&Detach Suite && Keep Standalone Apps\nRemoves the Suite umbrella and restores individual Installed Apps entries."},
            {1003, L"&Uninstall Entire Suite\nCleanly removes the Suite and all managed member applications."}
        };
        const std::wstring wWinTitle = Utf8ToWide(suite.suiteName) + L" Setup";
        const std::wstring wHead     = Utf8ToWide(suite.suiteName + " (v" + suite.version + ")");

        TASKDIALOGCONFIG tdc{sizeof(tdc)};
        tdc.hInstance       = hInstance;
        tdc.dwFlags         = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        tdc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        tdc.pszWindowTitle  = wWinTitle.c_str();
        tdc.pszMainIcon     = TD_INFORMATION_ICON;
        tdc.pszMainInstruction = wHead.c_str();
        tdc.pszContent      = summary.c_str();
        tdc.pButtons        = buttons;
        tdc.cButtons        = static_cast<UINT>(std::size(buttons));
        tdc.nDefaultButton  = 1001;

        int chosen = IDCANCEL;
        if (FAILED(::TaskDialogIndirect(&tdc, &chosen, nullptr, nullptr)) || chosen == IDCANCEL) {
            return ERROR_INSTALL_USEREXIT;
        }
        if (chosen == 1001) op = SuiteOperationKind::InstallOrModify;
        else if (chosen == 1002) op = SuiteOperationKind::DetachKeepStandalone;
        else if (chosen == 1003) op = SuiteOperationKind::FullUninstall;
    }

    std::string err;
    const int rc = ExecuteSuiteChainerOperation(suite, op, {}, cliInstallDirOverride, nullptr, err);
    if (!silentMode && rc != 0 && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
        const std::wstring wTitle = Utf8ToWide(suite.suiteName) + L" Setup";
        const std::wstring wErr   = Utf8ToWide(err.empty() ? "Suite operation failed." : err);
        ::TaskDialog(nullptr, hInstance, wTitle.c_str(), L"Suite Setup Error", wErr.c_str(), TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
    }
    return rc;
}

} // namespace rsw::compact_suite
