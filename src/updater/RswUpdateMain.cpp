// ================================================================================
// FILE: src/updater/RswUpdateMain.cpp
// PROJECT: RaggieSoft Updater (`RsUpdate.exe`)
// WEBSITE: https://raggiesoftstudio.com/installer-studio  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `RsUpdate.exe`?
//     `RsUpdate.exe` is RaggieSoft's zero-service, white-label Windows 11 (`x64` &
//     `ARM64`) software updater. It consumes the **exact same `apps-manifest.json`
//     JSON schema** as **RaggieSoft Launcher** (100% DRY!) and supports:
//       - **Interactive Accessible Win32 UI** (Dark Mode, Light Mode, & Windows
//         Contrast Themes `SPI_GETHIGHCONTRAST`, full keyboard `&` mnemonics, and
//         Per-Monitor V2 DPI scaling).
//       - **Headless / Silent Automation**:
//           * `--check`  : Returns `0` when up-to-date, `10` when an update is available!
//           * `--apply`  : Downloads, verifies `SHA-256`, and applies `.rswpatch`, `.msp`,
//                          `.msi`, or `-setup.exe` / **RaggieScript EXE** updates!
//           * `--register-schedule` / `--unregister-schedule` : Opt-in per-user weekly
//             Windows Task Scheduler (`schtasks.exe`) registration (never forced!).
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswUpdaterEngine.hpp"
#include "../common/RswWin32UiTheme.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr UINT WM_UPD_CHECK_DONE = WM_APP + 21;
constexpr UINT WM_UPD_DL_PROG    = WM_APP + 22;
constexpr UINT WM_UPD_APPLY_DONE = WM_APP + 23;

enum UpdaterCtrlId : int {
    IDC_UPD_CHECK_BTN  = 3101,
    IDC_UPD_APPLY_BTN  = 3102,
    IDC_UPD_ABOUT_BTN  = 3103,
    IDC_UPD_CLOSE_BTN  = 3104,
    IDC_UPD_PROG_BAR   = 3105
};

bool RunUserScheduledTaskCommand(bool registerTask, const std::wstring& taskName, const std::wstring& exeCmd) {
    std::wstring cmd = registerTask
        ? (L"schtasks.exe /Create /F /SC WEEKLY /TN \"" + taskName + L"\" /TR \"" + exeCmd + L"\"")
        : (L"schtasks.exe /Delete /F /TN \"" + taskName + L"\"");
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    ::WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return code == 0;
}

class RsUpdaterWindow {
public:
    rsw::updater::InstalledUpdateConfig cfg{};
    rsw::updater::UpdateEvaluationResult eval{};
    rsw::ui::RswTheme                   theme{};

    HWND  hWnd      = nullptr;
    HWND  hBtnCheck = nullptr;
    HWND  hBtnApply = nullptr;
    HWND  hBtnAbout = nullptr;
    HWND  hBtnClose = nullptr;
    HWND  hProg     = nullptr;
    HFONT hFontHead = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontBody = nullptr;

    std::atomic<bool> cancelFlag{false};
    std::mutex        mtx;
    std::wstring      headline = L"Checking for Software Updates...";
    std::wstring      subtext  = L"Connecting to static release manifest...";
    std::thread       worker;

    ~RsUpdaterWindow() {
        cancelFlag.store(true);
        if (worker.joinable()) worker.join();
        for (HFONT* f : {&hFontHead, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    void RebuildFontsAndLayout() {
        if (!hWnd) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        for (HFONT* f : {&hFontHead, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        const int ts = theme.textScalePercent;
        hFontHead = rsw::ui::CreateScaledFont(L"Segoe UI", 13, FW_BOLD,     dpi, ts);
        hFontBold = rsw::ui::CreateScaledFont(L"Segoe UI", 10, FW_SEMIBOLD, dpi, ts);
        hFontBody = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);

        for (HWND b : {hBtnCheck, hBtnAbout, hBtnClose}) {
            if (b) ::SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }
        if (hBtnApply) ::SendMessageW(hBtnApply, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = rc.right - rc.left;
        const int H = rc.bottom - rc.top;
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, ts); };

        if (hProg)     ::MoveWindow(hProg,     S(24), H - S(94), W - S(48), S(20), TRUE);
        if (hBtnAbout) ::MoveWindow(hBtnAbout, S(24), H - S(50), S(96),     S(32), TRUE);
        if (hBtnCheck) ::MoveWindow(hBtnCheck, W - S(348), H - S(50), S(106), S(32), TRUE);
        if (hBtnApply) ::MoveWindow(hBtnApply, W - S(234), H - S(50), S(122), S(32), TRUE);
        if (hBtnClose) ::MoveWindow(hBtnClose, W - S(104), H - S(50), S(80),  S(32), TRUE);
    }

    void StartAsyncCheck() {
        if (worker.joinable()) worker.join();
        cancelFlag.store(false);
        if (hBtnApply) ::EnableWindow(hBtnApply, FALSE);
        if (hBtnCheck) ::EnableWindow(hBtnCheck, FALSE);
        if (hProg)     ::SendMessageW(hProg, PBM_SETPOS, 120, 0);
        {
            std::lock_guard<std::mutex> lk(mtx);
            headline = L"Checking for Software Updates...";
            subtext  = L"Querying apps-manifest.json over HTTPS...";
        }
        ::InvalidateRect(hWnd, nullptr, FALSE);

        worker = std::thread([this]() {
            auto r = rsw::updater::EvaluateAvailableUpdate(cfg, &cancelFlag);
            {
                std::lock_guard<std::mutex> lk(mtx);
                eval = r;
                if (!r.ok) {
                    headline = L"Could Not Check for Updates";
                    subtext  = rsw::Utf8ToWide(r.errorMessage);
                } else if (!r.updateAvailable) {
                    headline = rsw::Utf8ToWide(r.productTitle.empty() ? "Application" : r.productTitle) + L" Is Up to Date";
                    subtext  = L"Installed version v" + rsw::Utf8ToWide(r.installedVersion) + L" is the latest release.";
                } else {
                    const std::wstring chLabel =
                        (r.selectedKind == "rswpatch") ? L"RaggieScript EXE Binary Delta (.rswpatch)" :
                        ((r.selectedKind == "msp")     ? L"Windows Installer Patch (.msp)" :
                                                         L"Full Upgrade Package");
                    headline = L"Update Available: v" + rsw::Utf8ToWide(r.latestVersion);
                    subtext  = rsw::Utf8ToWide(r.productTitle) + L" (installed v" + rsw::Utf8ToWide(r.installedVersion) +
                               L") — Delivery: " + chLabel + L"\r\n" + rsw::Utf8ToWide(r.description);
                }
            }
            ::PostMessageW(hWnd, WM_UPD_CHECK_DONE, 0, 0);
        });
    }

    void StartAsyncApply() {
        if (worker.joinable()) worker.join();
        cancelFlag.store(false);
        if (hBtnApply) ::EnableWindow(hBtnApply, FALSE);
        if (hBtnCheck) ::EnableWindow(hBtnCheck, FALSE);
        {
            std::lock_guard<std::mutex> lk(mtx);
            headline = L"Downloading & Verifying v" + rsw::Utf8ToWide(eval.latestVersion) + L"...";
            subtext  = L"Verifying SHA-256 cryptographic digest via Windows CNG (bcrypt.dll)...";
        }
        ::InvalidateRect(hWnd, nullptr, FALSE);

        worker = std::thread([this]() {
            std::error_code ec;
            const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                                (L"rsupdate-pkg-" + std::to_wstring(::GetCurrentProcessId()));
            std::string ext = ".exe";
            if (eval.selectedKind == "rswpatch") ext = ".rswpatch";
            else if (eval.selectedKind == "msp") ext = ".msp";
            else if (eval.selectedUrl.size() >= 4 &&
                     rsw::EqualsIgnoreCase(eval.selectedUrl.substr(eval.selectedUrl.size() - 4), ".msi")) {
                ext = ".msi";
            }
            const auto pkgFile = tmpDir / rsw::Utf8ToWide("update-payload" + ext);
            std::string err;
            bool ok = rsw::updater::AcquireAndVerifyUpdatePayload(
                eval.selectedUrl, eval.selectedSha256, pkgFile, &cancelFlag,
                [this](std::uint64_t done, std::uint64_t total) {
                    const int perm = (total > 0) ? static_cast<int>(std::min<std::uint64_t>(900, (done * 900u) / total)) : 450;
                    ::PostMessageW(hWnd, WM_UPD_DL_PROG, static_cast<WPARAM>(perm), 0);
                },
                err);
            if (ok) {
                ::PostMessageW(hWnd, WM_UPD_DL_PROG, 950, 0);
                ok = rsw::updater::ApplyDownloadedUpdatePackage(eval, pkgFile, cfg.installDir, false, err);
            }
            std::filesystem::remove_all(tmpDir, ec);
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (ok) {
                    cfg.installedVersion = eval.latestVersion;
                    eval.updateAvailable = false;
                    headline = L"Update Installed Successfully (v" + rsw::Utf8ToWide(eval.latestVersion) + L")";
                    subtext  = L"All updated program files and SHA-256 signatures were verified cleanly.";
                } else {
                    headline = L"Update Could Not Be Installed";
                    subtext  = rsw::Utf8ToWide(err);
                }
            }
            ::PostMessageW(hWnd, WM_UPD_APPLY_DONE, ok ? 1 : 0, 0);
        });
    }

    void Paint(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC memDc = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldB = ::SelectObject(memDc, bmp);
        ::SetBkMode(memDc, TRANSPARENT);
        ::FillRect(memDc, &rc, theme.hbrWindow);

        RECT bar{0, 0, W, S(4)};
        HBRUSH accentBr = ::CreateSolidBrush(theme.accentPrimary);
        ::FillRect(memDc, &bar, accentBr);
        ::DeleteObject(accentBr);

        RECT card{S(20), S(20), W - S(20), H - S(108)};
        HPEN pen = ::CreatePen(PS_SOLID, theme.isHighContrast ? 2 : 1, theme.borderSubtle);
        HGDIOBJ oBr = ::SelectObject(memDc, theme.hbrCard);
        HGDIOBJ oPn = ::SelectObject(memDc, pen);
        ::RoundRect(memDc, card.left, card.top, card.right, card.bottom, S(10), S(10));
        ::SelectObject(memDc, oPn);
        ::SelectObject(memDc, oBr);
        ::DeleteObject(pen);

        std::wstring hTxt, sTxt;
        {
            std::lock_guard<std::mutex> lk(mtx);
            hTxt = headline;
            sTxt = subtext;
        }
        ::SelectObject(memDc, hFontHead);
        ::SetTextColor(memDc, theme.textPrimary);
        RECT hRc{card.left + S(16), card.top + S(14), card.right - S(16), card.top + S(46)};
        ::DrawTextW(memDc, hTxt.c_str(), -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(memDc, hFontBody);
        ::SetTextColor(memDc, theme.textSecondary);
        RECT sRc{card.left + S(16), card.top + S(50), card.right - S(16), card.bottom - S(12)};
        ::DrawTextW(memDc, sTxt.c_str(), -1, &sRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

        ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
        ::SelectObject(memDc, oldB);
        ::DeleteObject(bmp);
        ::DeleteDC(memDc);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<RsUpdaterWindow*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<RsUpdaterWindow*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
            rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE: {
                HINSTANCE hInst = ::GetModuleHandleW(nullptr);
                self->hProg = ::CreateWindowExW(0, PROGRESS_CLASSW, L"Update Progress", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                                0, 0, 100, 20, hWnd, reinterpret_cast<HMENU>(IDC_UPD_PROG_BAR), hInst, nullptr);
                ::SendMessageW(self->hProg, PBM_SETRANGE32, 0, 1000);
                auto mkBtn = [&](const wchar_t* lbl, int id, bool def) {
                    HWND b = ::CreateWindowExW(0, L"BUTTON", lbl, WS_CHILD | WS_VISIBLE | WS_TABSTOP | (def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                               0, 0, 90, 32, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
                    rsw::ui::AttachAccessiblePushButtonSubclass(b, &self->theme, def, false);
                    return b;
                };
                self->hBtnAbout = mkBtn(L"&About...",       IDC_UPD_ABOUT_BTN, false);
                self->hBtnCheck = mkBtn(L"&Check Again",    IDC_UPD_CHECK_BTN, false);
                self->hBtnApply = mkBtn(L"&Install Update", IDC_UPD_APPLY_BTN, true);
                self->hBtnClose = mkBtn(L"Cl&ose",          IDC_UPD_CLOSE_BTN, false);
                self->RebuildFontsAndLayout();
                if (!self->cfg.manifestSource.empty()) {
                    self->StartAsyncCheck();
                } else {
                    self->headline = L"RaggieSoft Updater Ready";
                    self->subtext  = L"Pass --manifest=<https://.../apps-manifest.json> or place UpdateConfig.json alongside RsUpdate.exe.";
                    ::EnableWindow(self->hBtnApply, FALSE);
                }
                return 0;
            }
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED:
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh(RGB(37, 99, 235), RGB(251, 191, 36));
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    self->RebuildFontsAndLayout();
                    ::InvalidateRect(hWnd, nullptr, TRUE);
                }
                break;
            case WM_DPICHANGED: {
                const auto* r = reinterpret_cast<const RECT*>(lParam);
                ::SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->RebuildFontsAndLayout();
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->Paint(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_UPD_CHECK_DONE: {
                ::SendMessageW(self->hProg, PBM_SETPOS, self->eval.updateAvailable ? 1000 : 0, 0);
                ::EnableWindow(self->hBtnCheck, TRUE);
                ::EnableWindow(self->hBtnApply, self->eval.updateAvailable ? TRUE : FALSE);
                ::InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_UPD_DL_PROG:
                ::SendMessageW(self->hProg, PBM_SETPOS, wParam, 0);
                return 0;
            case WM_UPD_APPLY_DONE:
                ::SendMessageW(self->hProg, PBM_SETPOS, wParam ? 1000 : 0, 0);
                ::EnableWindow(self->hBtnCheck, TRUE);
                ::EnableWindow(self->hBtnApply, FALSE);
                ::InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            case WM_COMMAND: {
                switch (LOWORD(wParam)) {
                    case IDC_UPD_CHECK_BTN:
                        if (!self->cfg.manifestSource.empty()) self->StartAsyncCheck();
                        return 0;
                    case IDC_UPD_APPLY_BTN:
                        if (self->eval.updateAvailable) self->StartAsyncApply();
                        return 0;
                    case IDC_UPD_ABOUT_BTN: {
                        const std::wstring aboutBody =
                            L"RaggieSoft Updater (RsUpdate.exe v" + std::wstring(rsw::kVersionStringWide) + L")\n"
                            L"Zero-Service White-Label Static-CDN Updater (100% DRY apps-manifest.json schema shared with RaggieSoft Launcher).\n\n"
                            L"Supports MSI Major/Minor Upgrades, .MSP Patches, and RaggieScript EXE .rswpatch Binary Deltas with SHA-256 verification.\n\n"
                            L"Fictitious Persons & Entities Disclaimer:\n"
                            L"All sample characters, bands, universities (CPI, VDU), corporations (OmniCorp, Apex Records, Prairie Coachworks, Sainte-Claire), transit agencies (BRT, CVTA), and events referenced across RaggieSoft properties are works of fiction.";
                        ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), L"About RaggieSoft Updater",
                                     L"RaggieSoft Updater", aboutBody.c_str(), TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
                        return 0;
                    }
                    case IDC_UPD_CLOSE_BTN:
                    case IDCANCEL:
                        self->cancelFlag.store(true);
                        ::DestroyWindow(hWnd);
                        return 0;
                }
                break;
            }
            case WM_CLOSE:
                self->cancelFlag.store(true);
                ::DestroyWindow(hWnd);
                return 0;
            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.push_back(rsw::WideToUtf8(argv[i]));
        ::LocalFree(argv);
    }

    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath(selfBuf);
    const std::filesystem::path selfDir = selfPath.parent_path();

    bool checkOnly   = false;
    bool applyMode   = false;
    bool silentMode  = false;
    bool regSched    = false;
    bool unregSched  = false;
    std::filesystem::path jsonOutPath;

    rsw::updater::InstalledUpdateConfig cfg{};
    for (const auto& a : args) {
        if (a == "--headless-smoke-test") {
            const char* sampleJson =
                "[{\"id\":\"demo\",\"innoAppId\":\"{11111111-2222-3333-4444-555555555555}\","
                "\"title\":\"Demo App\",\"latestVersion\":\"1.2.0\",\"installerDownloadUrl\":\"https://raggiesoftstudio.com/demo.exe\","
                "\"sha256\":\"00\",\"sizeBytes\":100}]";
            std::vector<rsw::updater::ManifestAppEntry> parsed;
            std::string err;
            return (rsw::updater::ParseAppsManifestJson(sampleJson, parsed, err) &&
                    parsed.size() == 1 && parsed[0].latestVersion == "1.2.0") ? 0 : 1;
        }
        if (a == "--check")  { checkOnly  = true; continue; }
        if (a == "--apply")  { applyMode  = true; continue; }
        if (a == "/s" || a == "/S" || a == "/silent" || a == "--silent" || a == "/qn") { silentMode = true; continue; }
        if (a == "--register-schedule")   { regSched   = true; continue; }
        if (a == "--unregister-schedule") { unregSched = true; continue; }
        if (a.rfind("--manifest=", 0) == 0)          { cfg.manifestSource   = a.substr(11); continue; }
        if (a.rfind("--app-id=", 0) == 0)            { cfg.appId            = a.substr(9);  continue; }
        if (a.rfind("--upgrade-code=", 0) == 0)      { cfg.upgradeCode      = a.substr(15); continue; }
        if (a.rfind("--installed-version=", 0) == 0) { cfg.installedVersion = a.substr(20); continue; }
        if (a.rfind("--arch=", 0) == 0)              { cfg.architecture     = a.substr(7);  continue; }
        if (a.rfind("--install-dir=", 0) == 0)       { cfg.installDir       = std::filesystem::path(rsw::Utf8ToWide(a.substr(14))); continue; }
        if (a.rfind("--json-out=", 0) == 0)          { jsonOutPath          = std::filesystem::path(rsw::Utf8ToWide(a.substr(11))); continue; }
    }

    if (!cfg.installDir.empty()) {
        rsw::updater::LoadLocalUpdateConfigIfPresent(cfg.installDir, cfg);
    }
    rsw::updater::LoadLocalUpdateConfigIfPresent(selfDir, cfg);
    if (cfg.installDir.empty() && std::filesystem::exists(selfDir / L"UpdateConfig.json")) {
        cfg.installDir = selfDir;
    }
    rsw::updater::DetectInstalledStateByUpgradeCode(cfg);

    if (regSched || unregSched) {
        const std::wstring slug = rsw::Utf8ToWide(cfg.appId.empty() ? "App" : cfg.appId);
        const std::wstring taskName = L"RaggieSoft.RsUpdate." + slug;
        const std::wstring trCmd = L"\\\"" + selfPath.wstring() + L"\\\" --check";
        return RunUserScheduledTaskCommand(regSched, taskName, trCmd) ? 0 : 1;
    }

    // Headless `--check` or `--apply` automation mode!
    if (checkOnly || applyMode) {
        auto writeJsonIfRequested = [&](const rsw::updater::UpdateEvaluationResult& r, bool applied) {
            if (jsonOutPath.empty()) return;
            std::error_code ec;
            if (jsonOutPath.has_parent_path()) std::filesystem::create_directories(jsonOutPath.parent_path(), ec);
            std::ofstream jf(jsonOutPath, std::ios::binary | std::ios::trunc);
            const std::string txt = rsw::updater::FormatUpdateEvaluationJson(r, applied);
            jf.write(txt.data(), static_cast<std::streamsize>(txt.size()));
        };

        auto eval = rsw::updater::EvaluateAvailableUpdate(cfg);
        if (!eval.ok) {
            writeJsonIfRequested(eval, false);
            return 1;
        }
        if (!eval.updateAvailable) {
            writeJsonIfRequested(eval, false);
            return 0; // Up to date!
        }
        if (checkOnly && !applyMode) {
            writeJsonIfRequested(eval, false);
            return 10; // Canonical exit code `10` = Update Available!
        }

        std::error_code ec;
        const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                            (L"rsupdate-cli-" + std::to_wstring(::GetCurrentProcessId()));
        std::string ext = ".exe";
        if (eval.selectedKind == "rswpatch") ext = ".rswpatch";
        else if (eval.selectedKind == "msp") ext = ".msp";
        else if (eval.selectedUrl.size() >= 4 &&
                 rsw::EqualsIgnoreCase(eval.selectedUrl.substr(eval.selectedUrl.size() - 4), ".msi")) {
            ext = ".msi";
        }
        const auto pkgPath = tmpDir / rsw::Utf8ToWide("update-payload" + ext);
        std::string applyErr;
        bool ok = rsw::updater::AcquireAndVerifyUpdatePayload(
            eval.selectedUrl, eval.selectedSha256, pkgPath, nullptr, nullptr, applyErr);
        if (ok) {
            ok = rsw::updater::ApplyDownloadedUpdatePackage(eval, pkgPath, cfg.installDir, true, applyErr);
        }
        std::filesystem::remove_all(tmpDir, ec);
        if (!ok) {
            eval.ok           = false;
            eval.errorMessage = applyErr;
            writeJsonIfRequested(eval, false);
            return 9012;
        }
        writeJsonIfRequested(eval, true);
        return 0;
    }

    // Interactive Win32 Accessible Updater Window!
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gpSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    ::InitCommonControlsEx(&icc);

    RsUpdaterWindow win{};
    win.cfg = cfg;
    win.theme.Refresh(RGB(37, 99, 235), RGB(251, 191, 36));

    constexpr const wchar_t* kCls = L"RaggieSoftRsUpdaterWindowClass";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &RsUpdaterWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = ::GetDpiForSystem();
    RECT rc{0, 0,
            rsw::ui::ScalePx(560, dpi, win.theme.textScalePercent),
            rsw::ui::ScalePx(280, dpi, win.theme.textScalePercent)};
    constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    ::AdjustWindowRectExForDpi(&rc, kStyle, FALSE, WS_EX_APPWINDOW, dpi);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int x = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - w) / 2);
    const int y = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - h) / 2);

    HWND hWnd = ::CreateWindowExW(WS_EX_APPWINDOW, kCls, L"RaggieSoft Updater", kStyle,
                                  x, y, w, h, nullptr, nullptr, hInstance, &win);
    if (hWnd) {
        ::ShowWindow(hWnd, nCmdShow);
        ::UpdateWindow(hWnd);
        MSG msg{};
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!::IsDialogMessageW(hWnd, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
    }
    ::CoUninitialize();
    return 0;
}
