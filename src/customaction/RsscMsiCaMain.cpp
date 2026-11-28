// ================================================================================
// FILE: src/customaction/RsscMsiCaMain.cpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this file do?
//     This file compiles into `RsscMsiCa.dll` — a tiny, zero-dependency static `/MT`
//     C++20 Windows Installer Type 1 Custom Action DLL (`x64` and `ARM64`) that
//     `instbuild.exe` embeds directly inside `Product.msi`'s `Binary` SQL table
//     (`RsscCaDll`) alongside compiled `RSWBC2` RaggieScript bytecode (`RsscBytecode`).
//
//   Why did legacy 2000s script-driven MSI tools fail in Enterprise IT (`GPO` / `Intune`)?
//     1. They required installing an external COM scripting engine into `Common Files`
//        during setup (causing infamous `Error 1607 / 1628: Unable to install Scripting
//        Runtime` failures on locked-down corporate desktops), OR
//     2. They ran their script logic ONLY inside `Setup.exe`, so when an IT admin
//        extracted `Product.msi` and deployed `msiexec /i Product.msi /qn`, half the
//        configuration silently never ran!
//
//   How does `RsscMsiCa.dll` solve both problems cleanly?
//     - Static `/MT` native C++20 DLL with ZERO external runtime, CRT DLL, PowerShell,
//       VBScript, or JScript dependencies (`msi.dll` extracts it straight from `Binary`
//       and calls `StageRaggieScriptCa` / `RunRaggieScriptCa` in-process).
//     - Full support for ALL FOUR Windows Installer Custom Action execution phases:
//         1. `immediate` : Reads and writes live `MSIHANDLE` properties & feature states
//                          via `msi.session.getProperty` / `setProperty` / `setFeatureState`.
//         2. `deferred`  : Receives an automatic `CustomActionData` JSON snapshot staged
//                          by `StageRaggieScriptCa`, runs elevated (`SYSTEM` or impersonated)
//                          inside the MSI script transaction, and records every file, folder,
//                          registry, INI, and shortcut mutation to an atomic `.jnl` ledger!
//         3. `rollback`  : Automatically replays the `.jnl` ledger in strict LIFO order
//                          (restoring `.rswbak` backups and removing newly created items)
//                          AND invokes an optional `RollbackFunction` if `msiexec` aborts!
//         4. `commit`    : Cleans up temporary `.rswbak` and `.jnl` files once
//                          `InstallFinalize` commits cleanly!
//
//   INPUTS:
//     Live `MSIHANDLE hInstall` passed by Windows Installer (`msiexec.exe`).
//
//   OUTPUTS:
//     Standard Windows Installer Custom Action return codes:
//       - `ERROR_SUCCESS` (`0`)
//       - `ERROR_INSTALL_USEREXIT` (`1602`)
//       - `ERROR_INSTALL_FAILURE` (`1603`)
// ================================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "../../../raggiescript/include/raggiescript/RaggieScriptSdk.hpp"

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "rstrtmgr.lib")
#pragma comment(lib, "cabinet.lib")
#pragma comment(lib, "dwmapi.lib")

// Export undecorated entry-point names across both `x64` and `ARM64` PE64 targets!
#pragma comment(linker, "/EXPORT:StageRaggieScriptCa")
#pragma comment(linker, "/EXPORT:RunRaggieScriptCa")

namespace {

using namespace rsw::rssc;

/// RAII wrapper that guarantees every opened `MSIHANDLE` (`hDb`, `hView`, `hRec`) is closed cleanly.
struct ScopedMsiHandle {
    MSIHANDLE h = 0;
    ~ScopedMsiHandle() { if (h) ::MsiCloseHandle(h); }
    MSIHANDLE* Put() { if (h) ::MsiCloseHandle(h); h = 0; return &h; }
    MSIHANDLE Get() const noexcept { return h; }
};

/// Writes a structured `[RaggieScript CA]` diagnostic or error line into the active `msiexec` log.
void LogToMsiSession(MSIHANDLE hInstall, const std::string& text, bool isError = false) {
    if (hInstall == 0) return;
    ScopedMsiHandle hRec;
    hRec.h = ::MsiCreateRecord(2);
    if (!hRec.h) return;
    ::MsiRecordSetStringW(hRec.h, 0, isError ? L"[RaggieScript CA ERROR] [1]" : L"[RaggieScript CA] [1]");
    ::MsiRecordSetStringW(hRec.h, 1, plat::Utf8ToWide(text).c_str());
    ::MsiProcessMessage(hInstall, isError ? INSTALLMESSAGE_INFO : INSTALLMESSAGE_INFO, hRec.h);
}

/// Reads a string property of arbitrary length from `hInstall` (`""` if unset).
std::string ReadSessionPropertyUtf8(MSIHANDLE hInstall, const wchar_t* propName) {
    if (hInstall == 0 || !propName) return {};
    wchar_t buf[512]{};
    DWORD cch = static_cast<DWORD>(std::size(buf));
    UINT rc = ::MsiGetPropertyW(hInstall, propName, buf, &cch);
    if (rc == ERROR_MORE_DATA) {
        std::wstring big(cch + 1, L'\0');
        ++cch;
        rc = ::MsiGetPropertyW(hInstall, propName, big.data(), &cch);
        if (rc == ERROR_SUCCESS) {
            big.resize(cch);
            return plat::WideToUtf8(big);
        }
        return {};
    }
    if (rc == ERROR_SUCCESS) {
        return plat::WideToUtf8(std::wstring_view(buf, cch));
    }
    return {};
}

/// Reads a column string from an `MSIHANDLE` record (`""` if null).
std::string ReadRecordFieldUtf8(MSIHANDLE hRec, UINT fieldIdx) {
    if (!hRec || ::MsiRecordIsNull(hRec, fieldIdx)) return {};
    wchar_t buf[512]{};
    DWORD cch = static_cast<DWORD>(std::size(buf));
    UINT rc = ::MsiRecordGetStringW(hRec, fieldIdx, buf, &cch);
    if (rc == ERROR_MORE_DATA) {
        std::wstring big(cch + 1, L'\0');
        ++cch;
        if (::MsiRecordGetStringW(hRec, fieldIdx, big.data(), &cch) == ERROR_SUCCESS) {
            big.resize(cch);
            return plat::WideToUtf8(big);
        }
        return {};
    }
    return (rc == ERROR_SUCCESS) ? plat::WideToUtf8(std::wstring_view(buf, cch)) : std::string{};
}

/// Extracts `Binary.RsscBytecode` from the active MSI database to `%TEMP%` (or returns cached path).
bool ExtractBytecodeStreamToTemp(MSIHANDLE hInstall, std::filesystem::path& outBcPath, std::string& outErr) {
    const std::string existing = ReadSessionPropertyUtf8(hInstall, L"RSW_CA_BYTECODE_FILE");
    std::error_code ec;
    if (!existing.empty()) {
        std::filesystem::path p(plat::Utf8ToWide(existing));
        if (std::filesystem::is_regular_file(p, ec)) {
            outBcPath = std::move(p);
            return true;
        }
    }

    ScopedMsiHandle hDb, hView, hRec;
    hDb.h = ::MsiGetActiveDatabase(hInstall);
    if (!hDb.h) {
        outErr = "MsiGetActiveDatabase returned null while extracting Binary.RsscBytecode.";
        return false;
    }
    if (::MsiDatabaseOpenViewW(hDb.h, L"SELECT `Data` FROM `Binary` WHERE `Name`='RsscBytecode'", hView.Put()) != ERROR_SUCCESS ||
        ::MsiViewExecute(hView.h, 0) != ERROR_SUCCESS ||
        ::MsiViewFetch(hView.h, hRec.Put()) != ERROR_SUCCESS) {
        outErr = "Binary.RsscBytecode stream was not found in Product.msi.";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    char chunk[8192]{};
    for (;;) {
        DWORD cb = static_cast<DWORD>(sizeof(chunk));
        const UINT rrc = ::MsiRecordReadStream(hRec.h, 1, chunk, &cb);
        if (rrc != ERROR_SUCCESS) {
            outErr = "MsiRecordReadStream failed reading Binary.RsscBytecode.";
            return false;
        }
        if (cb == 0) break;
        bytes.insert(bytes.end(), reinterpret_cast<const std::uint8_t*>(chunk),
                     reinterpret_cast<const std::uint8_t*>(chunk) + cb);
    }

    outBcPath = std::filesystem::temp_directory_path(ec) /
                (L"rsw-ca-bc-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                 std::to_wstring(::GetTickCount64() & 0xFFFFFFu) + L".rswbc");
    std::ofstream out(outBcPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outErr = "Could not write staged RSWBC2 file to %TEMP%.";
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    out.close();
    ::MsiSetPropertyW(hInstall, L"RSW_CA_BYTECODE_FILE", outBcPath.wstring().c_str());
    return true;
}

/// Collects all public uppercase properties from `Property` table + standard MSI session folders
/// so Deferred, Rollback, and Commit Custom Actions have full property visibility via `CustomActionData`!
std::map<std::string, Value> HarvestPublicPropertiesMap(MSIHANDLE hInstall) {
    std::map<std::string, Value> mp;
    const wchar_t* kWellKnown[] = {
        L"INSTALLDIR", L"ProductCode", L"ProductName", L"ProductVersion", L"Manufacturer",
        L"UpgradeCode", L"TempFolder", L"ProgramFiles64Folder", L"CommonFiles64Folder",
        L"CommonAppDataFolder", L"LocalAppDataFolder", L"AppDataFolder", L"WindowsFolder",
        L"System64Folder", L"Installed", L"REMOVE", L"ADDLOCAL", L"REINSTALL",
        L"UPGRADINGPRODUCTCODE", L"UILevel", L"OriginalDatabase"
    };
    for (const wchar_t* wk : kWellKnown) {
        const std::string val = ReadSessionPropertyUtf8(hInstall, wk);
        if (!val.empty()) {
            mp[plat::WideToUtf8(wk)] = Value::FromStr(val);
        }
    }

    // Also harvest every uppercase public property declared in `Property` OR listed in `SecureCustomProperties`!
    auto harvestCsvOrSemiList = [&](const std::string& listStr) {
        std::size_t start = 0;
        while (start <= listStr.size()) {
            const auto sep = listStr.find_first_of(";, ", start);
            const std::string key = plat::TrimAscii(
                (sep == std::string::npos) ? listStr.substr(start) : listStr.substr(start, sep - start));
            if (!key.empty()) {
                const std::string v = ReadSessionPropertyUtf8(hInstall, plat::Utf8ToWide(key).c_str());
                if (!v.empty()) mp[key] = Value::FromStr(v);
            }
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    };
    harvestCsvOrSemiList(ReadSessionPropertyUtf8(hInstall, L"SecureCustomProperties"));

    ScopedMsiHandle hDb, hView, hRec;
    hDb.h = ::MsiGetActiveDatabase(hInstall);
    if (hDb.h &&
        ::MsiDatabaseOpenViewW(hDb.h, L"SELECT `Property` FROM `Property`", hView.Put()) == ERROR_SUCCESS &&
        ::MsiViewExecute(hView.h, 0) == ERROR_SUCCESS) {
        while (::MsiViewFetch(hView.h, hRec.Put()) == ERROR_SUCCESS) {
            const std::string propName = ReadRecordFieldUtf8(hRec.h, 1);
            if (propName.empty()) continue;
            bool isPublic = true;
            for (char c : propName) {
                if (c >= 'a' && c <= 'z') { isPublic = false; break; }
            }
            if (isPublic) {
                const std::string curVal = ReadSessionPropertyUtf8(hInstall, plat::Utf8ToWide(propName).c_str());
                mp[propName] = Value::FromStr(curVal);
            }
        }
    }
    return mp;
}

/// Loads `RSWBC2` bytecode from disk into `(outProg, outMod)`.
bool LoadBytecodeFile(
    const std::filesystem::path& bcPath,
    ScriptProgram& outProg,
    CompiledBytecodeModule& outMod,
    std::string& outErr)
{
    std::ifstream in(bcPath, std::ios::binary);
    if (!in) {
        outErr = "Could not open staged RSWBC2 bytecode file: " + plat::WideToUtf8(bcPath.wstring());
        return false;
    }
    std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return LoadRaggieScriptBytecode(raw.data(), raw.size(), outProg, outMod, outErr);
}

/// Converts a RaggieScript function return value (`bool` `false` or `int` `1602`/`1603`) into an MSI `UINT` status.
UINT CoerceReturnValueToMsiStatus(const Value& retVal) noexcept {
    if (retVal.kind == ValKind::Bool && !retVal.boolVal) return ERROR_INSTALL_FAILURE;
    if (retVal.kind == ValKind::Int) {
        if (retVal.intVal == ERROR_INSTALL_USEREXIT) return ERROR_INSTALL_USEREXIT;
        if (retVal.intVal != 0) return ERROR_INSTALL_FAILURE;
    }
    return ERROR_SUCCESS;
}

} // namespace

// ================================================================================
// EXPORT 1: `StageRaggieScriptCa` (Immediate Helper Scheduled Before `InstallInitialize`)
//   1. Extracts `Binary.RsscBytecode` once to `%TEMP%`.
//   2. Queries `_RswScriptCa` and populates `CustomActionData` JSON properties for
//      every Deferred (`<Id>`), Rollback (`<Id>_Rollback`), and Commit (`<Id>_Commit`) CA!
// ================================================================================
extern "C" __declspec(dllexport) UINT __stdcall StageRaggieScriptCa(MSIHANDLE hInstall) {
    std::filesystem::path bcPath;
    std::string err;
    if (!ExtractBytecodeStreamToTemp(hInstall, bcPath, err)) {
        LogToMsiSession(hInstall, err, true);
        return ERROR_INSTALL_FAILURE;
    }

    const auto propsMap = HarvestPublicPropertiesMap(hInstall);
    const std::string prodCode = ReadSessionPropertyUtf8(hInstall, L"ProductCode");
    std::string cleanProd = "pkg";
    for (char c : prodCode) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) cleanProd.push_back(c);
    }

    ScopedMsiHandle hDb, hView, hRec;
    hDb.h = ::MsiGetActiveDatabase(hInstall);
    if (!hDb.h) return ERROR_SUCCESS;

    if (::MsiDatabaseOpenViewW(
            hDb.h,
            L"SELECT `Action`, `Mode`, `Function`, `RollbackFn` FROM `_RswScriptCa`",
            hView.Put()) != ERROR_SUCCESS ||
        ::MsiViewExecute(hView.h, 0) != ERROR_SUCCESS) {
        return ERROR_SUCCESS;
    }

    std::error_code ec;
    const auto tempRoot = std::filesystem::temp_directory_path(ec);
    const std::string bcUtf8 = plat::WideToUtf8(bcPath.wstring());

    auto makePacketJson = [&](const std::string& mode, const std::string& fn,
                              const std::string& rbFn, const std::string& jnlUtf8) {
        std::ostringstream oss;
        vm_json::StringifyRec(oss, Value::FromMap({
            {"bcPath", Value::FromStr(bcUtf8)},
            {"mode",   Value::FromStr(mode)},
            {"fn",     Value::FromStr(fn)},
            {"rbFn",   Value::FromStr(rbFn)},
            {"jnl",    Value::FromStr(jnlUtf8)},
            {"props",  Value::FromMap(propsMap)}
        }), 0, 0);
        return oss.str();
    };

    while (::MsiViewFetch(hView.h, hRec.Put()) == ERROR_SUCCESS) {
        const std::string actId = ReadRecordFieldUtf8(hRec.h, 1);
        const std::string mode  = plat::AsciiLower(ReadRecordFieldUtf8(hRec.h, 2));
        const std::string fn    = ReadRecordFieldUtf8(hRec.h, 3);
        const std::string rbFn  = ReadRecordFieldUtf8(hRec.h, 4);
        if (actId.empty() || mode == "immediate") continue;

        const auto jnlPath = tempRoot / plat::Utf8ToWide("rsw-ca-" + actId + "-" + cleanProd + ".jnl");
        const std::string jnlUtf8 = plat::WideToUtf8(jnlPath.wstring());

        if (mode == "deferred") {
            const std::string defJson = makePacketJson("deferred", fn, rbFn, jnlUtf8);
            const std::string rbJson  = makePacketJson("rollback", rbFn, rbFn, jnlUtf8);
            const std::string cmJson  = makePacketJson("commit_cleanup", "", "", jnlUtf8);

            ::MsiSetPropertyW(hInstall, plat::Utf8ToWide(actId).c_str(),                  plat::Utf8ToWide(defJson).c_str());
            ::MsiSetPropertyW(hInstall, plat::Utf8ToWide(actId + "_Rollback").c_str(),    plat::Utf8ToWide(rbJson).c_str());
            ::MsiSetPropertyW(hInstall, plat::Utf8ToWide(actId + "_Commit").c_str(),      plat::Utf8ToWide(cmJson).c_str());
        } else {
            const std::string pkt = makePacketJson(mode, fn, rbFn, jnlUtf8);
            ::MsiSetPropertyW(hInstall, plat::Utf8ToWide(actId).c_str(), plat::Utf8ToWide(pkt).c_str());
        }
    }
    return ERROR_SUCCESS;
}

// ================================================================================
// EXPORT 2: `RunRaggieScriptCa` (Unified Immediate / Deferred / Rollback / Commit Runner)
// ================================================================================
extern "C" __declspec(dllexport) UINT __stdcall RunRaggieScriptCa(MSIHANDLE hInstall) {
    const bool isRollback = (::MsiGetMode(hInstall, MSIRUNMODE_ROLLBACK) != FALSE);
    const bool isCommit   = (::MsiGetMode(hInstall, MSIRUNMODE_COMMIT)   != FALSE);
    const bool isDeferred = (::MsiGetMode(hInstall, MSIRUNMODE_SCHEDULED)!= FALSE);

    // ----------------------------------------------------------------------------
    // PATH A: DEFERRED, ROLLBACK, OR COMMIT EXECUTION (`CustomActionData` JSON)
    // ----------------------------------------------------------------------------
    if (isDeferred || isRollback || isCommit) {
        const std::string rawCad = ReadSessionPropertyUtf8(hInstall, L"CustomActionData");
        if (rawCad.empty()) return ERROR_SUCCESS;

        vm_json::Reader jr{rawCad, 0};
        const Value root = jr.ParseVal();
        if (root.kind != ValKind::Map || !root.mapVal) {
            LogToMsiSession(hInstall, "Invalid CustomActionData JSON packet.", true);
            return isRollback || isCommit ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
        }

        auto& mp = *root.mapVal;
        const std::string bcUtf8  = mp["bcPath"].AsString();
        const std::string pktMode = mp["mode"].AsString();
        const std::string fnName  = mp["fn"].AsString();
        const std::string jnlUtf8 = mp["jnl"].AsString();
        const std::filesystem::path jnlPath(plat::Utf8ToWide(jnlUtf8));

        std::map<std::string, std::string> cadProps;
        if (mp.count("props") && mp["props"].kind == ValKind::Map && mp["props"].mapVal) {
            for (const auto& [pk, pv] : *mp["props"].mapVal) {
                cadProps[pk] = pv.AsString();
            }
        }

        // 1. ROLLBACK PHASE: Replay `.jnl` LIFO undo ledger FIRST, then run optional `RollbackFunction`!
        if (isRollback || pktMode == "rollback") {
            if (!jnlPath.empty()) {
                RollbackLedger::ReplayRollbackFromJournalFile(jnlPath);
            }
            if (!fnName.empty() && !bcUtf8.empty()) {
                ScriptProgram prog{};
                CompiledBytecodeModule mod{};
                std::string err;
                if (LoadBytecodeFile(std::filesystem::path(plat::Utf8ToWide(bcUtf8)), prog, mod, err)) {
                    VmExecutionOptions opts{};
                    opts.silentMode            = true;
                    opts.activeMsiSessionHandle = hInstall;
                    opts.msiCaIsRollback       = true;
                    opts.rawCustomActionData   = rawCad;
                    opts.customActionDataProps = cadProps;
                    RaggieScriptVm vm(prog, std::move(mod), opts);
                    vm.InitializeForMsiCustomAction(cadProps, err);
                    (void)vm.InvokeFunction(fnName, {}, err);
                }
            }
            return ERROR_SUCCESS;
        }

        // 2. COMMIT CLEANUP PHASE: Delete `.rswbak` backup files and `.jnl` once `InstallFinalize` commits!
        if (isCommit || pktMode == "commit_cleanup" || pktMode == "commit") {
            if (!jnlPath.empty()) {
                RollbackLedger::CleanupCommittedJournalFile(jnlPath);
            }
            if (!fnName.empty() && pktMode == "commit" && !bcUtf8.empty()) {
                ScriptProgram prog{};
                CompiledBytecodeModule mod{};
                std::string err;
                if (LoadBytecodeFile(std::filesystem::path(plat::Utf8ToWide(bcUtf8)), prog, mod, err)) {
                    VmExecutionOptions opts{};
                    opts.silentMode            = true;
                    opts.activeMsiSessionHandle = hInstall;
                    opts.msiCaIsCommit         = true;
                    opts.rawCustomActionData   = rawCad;
                    opts.customActionDataProps = cadProps;
                    RaggieScriptVm vm(prog, std::move(mod), opts);
                    vm.InitializeForMsiCustomAction(cadProps, err);
                    (void)vm.InvokeFunction(fnName, {}, err);
                }
            }
            return ERROR_SUCCESS;
        }

        // 3. DEFERRED EXECUTION PHASE: Track mutations in `jnlPath` & roll back immediately if `<fn>` faults!
        ScriptProgram prog{};
        CompiledBytecodeModule mod{};
        std::string err;
        if (!LoadBytecodeFile(std::filesystem::path(plat::Utf8ToWide(bcUtf8)), prog, mod, err)) {
            LogToMsiSession(hInstall, err, true);
            return ERROR_INSTALL_FAILURE;
        }

        VmExecutionOptions opts{};
        opts.silentMode            = true;
        opts.activeMsiSessionHandle = hInstall;
        opts.msiCaIsDeferred       = true;
        opts.rawCustomActionData   = rawCad;
        opts.customActionDataProps = cadProps;

        RaggieScriptVm vm(prog, std::move(mod), opts);
        if (!vm.InitializeForMsiCustomAction(cadProps, err)) {
            LogToMsiSession(hInstall, "Custom Action initialization failed: " + err, true);
            return ERROR_INSTALL_FAILURE;
        }
        if (!jnlPath.empty()) {
            vm.Ledger().BeginWithExplicitJournal(jnlPath);
        }

        const Value retVal = vm.InvokeFunction(fnName, {}, err);
        const UINT status  = err.empty() ? CoerceReturnValueToMsiStatus(retVal) : ERROR_INSTALL_FAILURE;
        if (status != ERROR_SUCCESS) {
            LogToMsiSession(hInstall, "Deferred Custom Action '" + fnName + "' failed: " + err, true);
            vm.Ledger().RollbackLifo();
            return status;
        }
        if (!jnlPath.empty()) {
            vm.Ledger().SaveToJournalFile();
        }
        return ERROR_SUCCESS;
    }

    // ----------------------------------------------------------------------------
    // PATH B: IMMEDIATE EXECUTION (`RSW_ACTIVE_IMMEDIATE_CA = "<ActionId>|<Fn>"`)
    // ----------------------------------------------------------------------------
    const std::string activeSpec = ReadSessionPropertyUtf8(hInstall, L"RSW_ACTIVE_IMMEDIATE_CA");
    const auto barPos = activeSpec.find('|');
    const std::string fnName = (barPos != std::string::npos) ? activeSpec.substr(barPos + 1) : activeSpec;
    if (fnName.empty()) {
        LogToMsiSession(hInstall, "Immediate Custom Action invoked with empty RSW_ACTIVE_IMMEDIATE_CA.", true);
        return ERROR_INSTALL_FAILURE;
    }

    std::filesystem::path bcPath;
    std::string err;
    if (!ExtractBytecodeStreamToTemp(hInstall, bcPath, err)) {
        LogToMsiSession(hInstall, err, true);
        return ERROR_INSTALL_FAILURE;
    }

    ScriptProgram prog{};
    CompiledBytecodeModule mod{};
    if (!LoadBytecodeFile(bcPath, prog, mod, err)) {
        LogToMsiSession(hInstall, err, true);
        return ERROR_INSTALL_FAILURE;
    }

    std::map<std::string, std::string> liveProps;
    for (const auto& [k, v] : HarvestPublicPropertiesMap(hInstall)) {
        liveProps[k] = v.AsString();
    }

    VmExecutionOptions opts{};
    opts.silentMode             = (std::atoi(ReadSessionPropertyUtf8(hInstall, L"UILevel").c_str()) <= 2);
    opts.activeMsiSessionHandle = hInstall;
    opts.customActionDataProps  = liveProps;

    RaggieScriptVm vm(prog, std::move(mod), opts);
    if (!vm.InitializeForMsiCustomAction(liveProps, err)) {
        LogToMsiSession(hInstall, err, true);
        return ERROR_INSTALL_FAILURE;
    }

    const Value retVal = vm.InvokeFunction(fnName, {}, err);
    if (!err.empty()) {
        LogToMsiSession(hInstall, "Immediate Custom Action '" + fnName + "' error: " + err, true);
        return ERROR_INSTALL_FAILURE;
    }

    // Push any mutated `param` globals & `msi.setSessionProperty` entries back into live `hInstall`!
    for (const auto& p : prog.params) {
        Value v;
        if (vm.GlobalScope()->TryGet(p.name, v)) {
            std::string msiKey = p.name;
            bool hasLower = false;
            for (char c : msiKey) if (c >= 'a' && c <= 'z') hasLower = true;
            if (!hasLower) {
                ::MsiSetPropertyW(hInstall, plat::Utf8ToWide(msiKey).c_str(), plat::Utf8ToWide(v.AsString()).c_str());
            }
        }
    }
    for (const auto& [sk, sv] : vm.MsiSessionProperties()) {
        if (!sk.empty() && sk.front() != '_') {
            ::MsiSetPropertyW(hInstall, plat::Utf8ToWide(sk).c_str(), plat::Utf8ToWide(sv).c_str());
        }
    }

    const UINT status = CoerceReturnValueToMsiStatus(retVal);
    // Note: if `StageRaggieScriptCa` already ran before this Immediate CA, refresh staged Deferred packets
    // so any properties modified by this Immediate CA propagate into later Deferred `CustomActionData`!
    if (status == ERROR_SUCCESS && !ReadSessionPropertyUtf8(hInstall, L"RSW_CA_BYTECODE_FILE").empty()) {
        (void)StageRaggieScriptCa(hInstall);
    }
    return status;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
