// ================================================================================
// FILE: src/script/RswScriptMain.cpp
// PROJECT: RaggieSoft Installer Studio & RaggieScript CLI (`raggiescript.exe`)
// WEBSITE: https://raggiesoftstudio.com/raggiescript  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `raggiescript.exe`?
//     `raggiescript.exe` is the standalone zero-dependency Win32 C++20 command-line
//     compiler, static verifier, bytecode disassembler, runtime VM, and single-file
//     Windows micro-app packager for **`RaggieScript` (`.rssc`)**:
//
//       1. `raggiescript.exe check [--diagnostics=json] <file.rssc> [-DKEY=VAL]`
//          - Parses `.rssc` (resolving `include "..."` and `#if`), enforces `RSW701`
//            Wizard Phase Isolation and `RSW702` Capability Sandbox gates in <1ms,
//            and emits either human-readable or structured JSON diagnostics!
//       2. `raggiescript.exe compile <file.rssc> <out.rswbc> [-DKEY=VAL]`
//          - Compiles `.rssc` source into a compact `SHA-256`-verified `RSWBC1`
//            binary bytecode stream.
//       3. `raggiescript.exe disasm <file.rssc | file.rswbc | packed.exe>`
//          - Prints a clean section-by-section `RSWBC1` bytecode disassembly!
//       4. `raggiescript.exe run <file.rssc | file.rswbc> [--silent] [KEY=VAL ...]`
//          - Executes any `.rssc` or `.rswbc` script directly against our 15-namespace
//            Win32 Standard Library + atomic `.rsw-rollback.jnl` rollback ledger!
//       5. `raggiescript.exe pack-exe <file.rssc> <out.exe> [--support-dir=<dir>] [--uac=...]`
//          - Clones `raggiescript.exe` into `<out.exe>`, embeds the compiled `RSWBC1`
//            bytecode (`#225`), packs any `payload` + `_support\` staging files into
//            an `LZMS`-compressed `RSWPAK1` container (`#220`), and stamps custom
//            `VERSIONINFO`, `.ico`, and UAC `RequestedExecutionLevel`!
//          - When a user launches `<out.exe>`, it automatically detects its own `#225`
//            resource, extracts `_support\` if present, and runs as a self-contained
//            zero-dependency native Windows micro-application!
// ================================================================================

#include "../common/RswRaggieScript.hpp"
#include "../common/RswRaggieScriptVm.hpp"
#include "../compiler/RswBootstrapperPackager.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

void PrintBanner() {
    std::cout
        << "RaggieScript Engine & Micro-App Compiler (v" << rsw::kVersionString << " — Style A Curly-Brace)\n"
        << "Zero-Dependency Static /MT Win32 C++20 Runtime across RaggieSoft Studio (`x64` & `ARM64`)\n"
        << "Fictitious Disclaimer: Sample brands/corporations referenced in examples are works of fiction.\n\n"
        << "Usage:\n"
        << "  raggiescript.exe run <script.rssc | app.rswbc> [--silent] [KEY=VALUE ...] [-DDEF=VAL]\n"
        << "  raggiescript.exe check [--diagnostics=json] <script.rssc> [-DDEF=VAL]\n"
        << "  raggiescript.exe compile <script.rssc> <output.rswbc> [-DDEF=VAL]\n"
        << "  raggiescript.exe disasm <script.rssc | output.rswbc | packed.exe>\n"
        << "  raggiescript.exe pack-exe <script.rssc> <output.exe> [--support-dir=<dir>] [--uac=<level>] [--icon=<ico>]\n"
        << "  raggiescript.exe --version | --help\n";
}

std::string EscapeJsonText(std::string_view sv) {
    std::ostringstream oss;
    rsw::rssc::json_detail::EscapeJsonStr(oss, sv);
    return oss.str();
}

void PrintDiagnosticsJson(const std::filesystem::path& p, bool ok, const std::vector<rsw::rssc::ScriptDiagnostic>& diags) {
    std::cout << "{\n"
              << "  \"ok\": " << (ok ? "true" : "false") << ",\n"
              << "  \"file\": " << EscapeJsonText(rsw::WideToUtf8(p.wstring())) << ",\n"
              << "  \"diagnostics\": [";
    for (std::size_t i = 0; i < diags.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "\n    {"
                  << "\"severity\": " << EscapeJsonText(diags[i].isError ? "error" : "warning") << ", "
                  << "\"code\": " << EscapeJsonText(diags[i].code) << ", "
                  << "\"line\": " << diags[i].line << ", "
                  << "\"column\": " << diags[i].col << ", "
                  << "\"message\": " << EscapeJsonText(diags[i].message) << "}";
    }
    if (!diags.empty()) std::cout << "\n  ";
    std::cout << "]\n}\n";
}

bool LoadEmbeddedResourceView(HMODULE hMod, int resId, const std::uint8_t*& outPtr, std::size_t& outSz) noexcept {
    outPtr = nullptr;
    outSz  = 0;
    HRSRC hr = ::FindResourceW(hMod, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hr) return false;
    const DWORD sz = ::SizeofResource(hMod, hr);
    HGLOBAL hg = ::LoadResource(hMod, hr);
    if (!hg || sz == 0) return false;
    outPtr = static_cast<const std::uint8_t*>(::LockResource(hg));
    outSz  = static_cast<std::size_t>(sz);
    return outPtr != nullptr;
}

bool LoadProgramFromAnyInput(
    const std::filesystem::path& inPath,
    const std::map<std::string, std::string>& defines,
    rsw::rssc::ScriptProgram& outProg,
    std::vector<rsw::rssc::ScriptDiagnostic>& outDiags)
{
    const std::string ext = rsw::AsciiLower(rsw::WideToUtf8(inPath.extension().wstring()));
    if (ext == ".rswbc") {
        std::vector<std::uint8_t> raw;
        std::string err;
        if (!rsw::ReadBinaryFileBytes(inPath, raw, err) ||
            !rsw::rssc::LoadRaggieScriptBytecode(raw.data(), raw.size(), outProg, err)) {
            outDiags.push_back({true, "RSW704", 1, 1, rsw::WideToUtf8(inPath.wstring()), err});
            return false;
        }
        return true;
    }
    if (ext == ".exe") {
        HMODULE hPe = ::LoadLibraryExW(
            inPath.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (!hPe) {
            outDiags.push_back({true, "RSW704", 1, 1, rsw::WideToUtf8(inPath.wstring()), "Could not open PE executable."});
            return false;
        }
        const std::uint8_t* ptr = nullptr;
        std::size_t sz = 0;
        std::string err;
        bool ok = false;
        if (LoadEmbeddedResourceView(hPe, rsw::kResIdScriptBytecode, ptr, sz)) {
            std::vector<std::uint8_t> dec;
            if (rsw::DecompressLzmsOrCopyRaw(ptr, sz, dec, err)) {
                ok = rsw::rssc::LoadRaggieScriptBytecode(dec.data(), dec.size(), outProg, err);
            }
        } else {
            err = "Executable does not contain an embedded RSWBC1 (#225) RaggieScript resource.";
        }
        ::FreeLibrary(hPe);
        if (!ok) outDiags.push_back({true, "RSW704", 1, 1, rsw::WideToUtf8(inPath.wstring()), err});
        return ok;
    }
    return rsw::rssc::CompileRaggieScriptFile(inPath, outProg, outDiags, defines);
}

int RunSelfContainedPackedMode(const std::filesystem::path& selfPath, const std::vector<std::string>& args) {
    const std::uint8_t* bcPtr = nullptr;
    std::size_t bcSz = 0;
    if (!LoadEmbeddedResourceView(::GetModuleHandleW(nullptr), rsw::kResIdScriptBytecode, bcPtr, bcSz)) {
        return -1; // Not a packed micro-app!
    }

    // Handle `--native-uninstall` when invoked as `<InstallDir>\Uninstall.exe`!
    for (const auto& a : args) {
        if (a == "--native-uninstall") {
            const auto meta = selfPath.parent_path() / L"Uninstall.rswm";
            std::string unErr;
            return rsw::ExecuteNativeUninstallFromMeta(meta, true, unErr) ? 0 : 1603;
        }
    }

    std::vector<std::uint8_t> bcRaw;
    std::string err;
    rsw::rssc::ScriptProgram prog;
    if (!rsw::DecompressLzmsOrCopyRaw(bcPtr, bcSz, bcRaw, err) ||
        !rsw::rssc::LoadRaggieScriptBytecode(bcRaw.data(), bcRaw.size(), prog, err)) {
        std::cerr << "[ERROR] Packed RaggieScript integrity check failed: " << err << "\n";
        return 1603;
    }

    rsw::rssc::VmExecutionOptions vmOpts{};
    vmOpts.runningExePath = selfPath;
    vmOpts.onStdout       = [](const std::string& ln) { std::cout << ln << "\n"; };

    for (const auto& a : args) {
        if (a == "--silent" || a == "/s" || a == "/S" || a == "/qn" || a == "/quiet") {
            vmOpts.silentMode = true;
        } else if (a.rfind("/l=", 0) == 0 || a.rfind("--log=", 0) == 0) {
            vmOpts.logFilePath = std::filesystem::path(rsw::Utf8ToWide(a.substr(a.find('=') + 1)));
        } else if (a == "--repair") {
            vmOpts.maintenanceMode   = true;
            vmOpts.maintenanceAction = "repair";
        } else if (a == "--uninstall") {
            vmOpts.maintenanceMode   = true;
            vmOpts.maintenanceAction = "uninstall";
        } else if (a.find('=') != std::string::npos && a.front() != '-' && a.front() != '/') {
            const auto eq = a.find('=');
            vmOpts.cliProperties[a.substr(0, eq)] = a.substr(eq + 1);
        } else if (a.find(':') != std::string::npos && a.find(':') >= 2 && a.front() != '-' && a.front() != '/') {
            const auto col = a.find(':');
            const std::string k = a.substr(0, col);
            vmOpts.cliProperties[k] = a.substr(col + 1);
            vmOpts.lockedCliProperties.insert(k);
        } else {
            vmOpts.rawCliSwitches.push_back(a);
        }
    }

    // Check for embedded `RSWPAK1` (`#220`) containing payloads and/or `__support__` files!
    const std::uint8_t* pakPtr = nullptr;
    std::size_t pakSz = 0;
    std::filesystem::path tempSupportRoot;
    std::error_code ec;
    if (LoadEmbeddedResourceView(::GetModuleHandleW(nullptr), rsw::kResIdSfxOrNativeArchive, pakPtr, pakSz)) {
        vmOpts.embeddedPakBytes = pakPtr;
        vmOpts.embeddedPakSize  = pakSz;

        std::vector<rsw::RswPakFileMetadata> metas;
        std::vector<std::uint8_t> payload;
        if (rsw::UnpackRswPakBytesInMemory(pakPtr, pakSz, metas, payload, err)) {
            bool anySupport = false;
            for (const auto& m : metas) {
                if (m.featureId == "__support__") { anySupport = true; break; }
            }
            if (anySupport) {
                tempSupportRoot = std::filesystem::temp_directory_path(ec) /
                                  (L"rssc-support-" + std::to_wstring(::GetCurrentProcessId()));
                vmOpts.supportDirPath = tempSupportRoot / L"_support";
                for (const auto& m : metas) {
                    if (m.featureId != "__support__") continue;
                    const auto dst = (vmOpts.supportDirPath / rsw::Utf8ToWide(m.archiveRelPath)).lexically_normal();
                    std::filesystem::create_directories(dst.parent_path(), ec);
                    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                    if (m.payloadSize > 0) {
                        out.write(reinterpret_cast<const char*>(payload.data() + m.payloadOffset),
                                  static_cast<std::streamsize>(m.payloadSize));
                    }
                }
            }
        }
    }

    rsw::rssc::RaggieScriptVm vm(prog, vmOpts);
    const auto res = vm.ExecuteFullLifecycle();
    if (!tempSupportRoot.empty()) {
        std::filesystem::remove_all(tempSupportRoot, ec);
    }
    if (!res.ok) {
        std::cerr << "[ERROR Line " << res.errorLine << "] " << res.errorMessage << "\n";
    }
    return res.exitCode;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    ::SetConsoleOutputCP(CP_UTF8);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    ::InitCommonControlsEx(&icc);

    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath(selfBuf);

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(rsw::WideToUtf8(argv[i]));

    // 1. Check if this binary is a standalone packed micro-app (`#225` embedded `RSWBC1`)!
    const int packedRc = RunSelfContainedPackedMode(selfPath, args);
    if (packedRc >= 0) {
        return packedRc;
    }

    if (args.empty()) {
        PrintBanner();
        return 1;
    }

    const std::string cmd = args[0];
    if (cmd == "--help" || cmd == "-h" || cmd == "/?") {
        PrintBanner();
        return 0;
    }
    if (cmd == "--version" || cmd == "-v") {
        const auto hp = rsw::QueryHostPlatform();
        std::cout << "raggiescript.exe " << rsw::kVersionString
                  << " [Native Silicon: " << hp.NativeArchSlug()
                  << ", OS Build: " << hp.buildNumber << "]\n";
        return 0;
    }

    std::map<std::string, std::string> defines;
    std::map<std::string, std::string> cliProps;
    std::set<std::string> lockedCliProps;
    std::filesystem::path logFilePath;
    std::vector<std::string> switches;
    std::vector<std::string> pos;
    bool jsonDiag   = false;
    bool silentFlag = false;
    std::string supportDirOverride;
    std::string uacOverride;
    std::string iconOverride;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--diagnostics=json") { jsonDiag = true; continue; }
        if (a == "--silent" || a == "/s" || a == "/S" || a == "/qn") { silentFlag = true; switches.push_back(a); continue; }
        if (a.rfind("/l=", 0) == 0 || a.rfind("--log=", 0) == 0) {
            logFilePath = std::filesystem::path(rsw::Utf8ToWide(a.substr(a.find('=') + 1)));
            continue;
        }
        if (a.rfind("-D", 0) == 0 && a.size() > 2) {
            const auto eq = a.find('=', 2);
            if (eq != std::string::npos) defines[a.substr(2, eq - 2)] = a.substr(eq + 1);
            else defines[a.substr(2)] = "1";
            continue;
        }
        if (a.rfind("--support-dir=", 0) == 0) { supportDirOverride = a.substr(14); continue; }
        if (a.rfind("--uac=", 0) == 0)         { uacOverride = a.substr(6); continue; }
        if (a.rfind("--icon=", 0) == 0)        { iconOverride = a.substr(7); continue; }
        if (cmd == "run" && !pos.empty() && a.find('=') != std::string::npos && a.front() != '-' && a.front() != '/') {
            const auto eq = a.find('=');
            cliProps[a.substr(0, eq)] = a.substr(eq + 1);
            continue;
        }
        if (cmd == "run" && !pos.empty() && a.find(':') != std::string::npos && a.find(':') >= 2 && a.front() != '-' && a.front() != '/') {
            const auto col = a.find(':');
            const std::string k = a.substr(0, col);
            cliProps[k] = a.substr(col + 1);
            lockedCliProps.insert(k);
            continue;
        }
        if (cmd == "run" && !pos.empty() && (a.front() == '-' || a.front() == '/')) {
            switches.push_back(a);
            continue;
        }
        pos.push_back(a);
    }

    // Command 1: `check`
    if (cmd == "check") {
        if (pos.empty()) { std::cerr << "[ERROR] Missing <script.rssc> path.\n"; return 2; }
        const std::filesystem::path p(rsw::Utf8ToWide(pos[0]));
        rsw::rssc::ScriptProgram prog;
        std::vector<rsw::rssc::ScriptDiagnostic> diags;
        const bool ok = rsw::rssc::CompileRaggieScriptFile(p, prog, diags, defines);
        if (jsonDiag) {
            PrintDiagnosticsJson(p, ok, diags);
        } else {
            for (const auto& d : diags) {
                std::cerr << "[" << d.code << " Line " << d.line << "] " << d.message << "\n";
            }
            if (ok) std::cout << "[OK] RaggieScript verified cleanly (RSW701 & RSW702 passed): " << pos[0] << "\n";
        }
        return ok ? 0 : 1;
    }

    // Command 2: `compile`
    if (cmd == "compile") {
        if (pos.size() < 2) { std::cerr << "[ERROR] Usage: raggiescript.exe compile <in.rssc> <out.rswbc>\n"; return 2; }
        const std::filesystem::path inP(rsw::Utf8ToWide(pos[0]));
        const std::filesystem::path outP(rsw::Utf8ToWide(pos[1]));
        rsw::rssc::ScriptProgram prog;
        std::vector<rsw::rssc::ScriptDiagnostic> diags;
        if (!rsw::rssc::CompileRaggieScriptFile(inP, prog, diags, defines)) {
            for (const auto& d : diags) std::cerr << "[" << d.code << " Line " << d.line << "] " << d.message << "\n";
            return 1;
        }
        std::vector<std::uint8_t> bc;
        std::string err;
        if (!rsw::rssc::EmitRaggieScriptBytecode(prog, bc, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        std::error_code ec;
        std::filesystem::create_directories(outP.parent_path(), ec);
        std::ofstream out(outP, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bc.data()), static_cast<std::streamsize>(bc.size()));
        if (!out.good()) { std::cerr << "[ERROR] Failed writing " << pos[1] << "\n"; return 1; }
        std::cout << "[OK] Compiled RSWBC1 bytecode (" << bc.size() << " bytes, SHA-256 verified): " << pos[1] << "\n";
        return 0;
    }

    // Command 3: `disasm`
    if (cmd == "disasm") {
        if (pos.empty()) { std::cerr << "[ERROR] Usage: raggiescript.exe disasm <in.rssc | in.rswbc | packed.exe>\n"; return 2; }
        rsw::rssc::ScriptProgram prog;
        std::vector<rsw::rssc::ScriptDiagnostic> diags;
        if (!LoadProgramFromAnyInput(std::filesystem::path(rsw::Utf8ToWide(pos[0])), defines, prog, diags)) {
            for (const auto& d : diags) std::cerr << "[" << d.code << " Line " << d.line << "] " << d.message << "\n";
            return 1;
        }
        std::cout << rsw::rssc::DisassembleRaggieScriptProgram(prog);
        return 0;
    }

    // Command 4: `run`
    if (cmd == "run") {
        if (pos.empty()) { std::cerr << "[ERROR] Usage: raggiescript.exe run <script.rssc | app.rswbc>\n"; return 2; }
        const std::filesystem::path inP(rsw::Utf8ToWide(pos[0]));
        rsw::rssc::ScriptProgram prog;
        std::vector<rsw::rssc::ScriptDiagnostic> diags;
        if (!LoadProgramFromAnyInput(inP, defines, prog, diags)) {
            for (const auto& d : diags) std::cerr << "[" << d.code << " Line " << d.line << "] " << d.message << "\n";
            return 1;
        }

        rsw::rssc::VmExecutionOptions vmOpts{};
        vmOpts.silentMode          = silentFlag;
        vmOpts.runningExePath      = selfPath;
        vmOpts.logFilePath         = logFilePath;
        vmOpts.cliProperties       = cliProps;
        vmOpts.lockedCliProperties = lockedCliProps;
        vmOpts.rawCliSwitches      = switches;
        if (!prog.package.supportDir.empty()) {
            const std::filesystem::path rawSup(rsw::Utf8ToWide(prog.package.supportDir));
            vmOpts.supportDirPath = rawSup.is_absolute() ? rawSup : (inP.parent_path() / rawSup);
        }
        vmOpts.onStdout = [](const std::string& ln) { std::cout << ln << "\n"; };

        rsw::rssc::RaggieScriptVm vm(prog, vmOpts);
        const auto res = vm.ExecuteFullLifecycle();
        if (!res.ok) {
            std::cerr << "[ERROR Line " << res.errorLine << "] " << res.errorMessage
                      << (res.rolledBack ? " (Rolled back cleanly)" : "") << "\n";
        }
        return res.exitCode;
    }

    // Command 5: `pack-exe` (Standalone zero-dependency Windows micro-app / custom installer!)
    if (cmd == "pack-exe") {
        if (pos.size() < 2) {
            std::cerr << "[ERROR] Usage: raggiescript.exe pack-exe <script.rssc> <out.exe> [--support-dir=<dir>] [--uac=<level>]\n";
            return 2;
        }
        const std::filesystem::path inP(rsw::Utf8ToWide(pos[0]));
        const std::filesystem::path outExe(rsw::Utf8ToWide(pos[1]));

        rsw::rssc::ScriptProgram prog;
        std::vector<rsw::rssc::ScriptDiagnostic> diags;
        if (!rsw::rssc::CompileRaggieScriptFile(inP, prog, diags, defines)) {
            for (const auto& d : diags) std::cerr << "[" << d.code << " Line " << d.line << "] " << d.message << "\n";
            return 1;
        }

        std::vector<std::uint8_t> bcRaw;
        std::string err;
        if (!rsw::rssc::EmitRaggieScriptBytecode(prog, bcRaw, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        const std::vector<std::uint8_t> bcComp = rsw::CompressWithLzmsIfSmaller(bcRaw);

        // Harvest any `payload` declarations + `--support-dir` / `package.supportDir` into `RSWPAK1` (`#220`)!
        std::error_code ec;
        const std::filesystem::path baseDir = inP.parent_path().empty() ? std::filesystem::current_path() : inP.parent_path();
        std::vector<rsw::RswPakInputItem> pakItems;

        const std::string supStr = supportDirOverride.empty() ? prog.package.supportDir : supportDirOverride;
        if (!supStr.empty()) {
            const std::filesystem::path supDir = std::filesystem::path(rsw::Utf8ToWide(supStr)).is_absolute()
                ? std::filesystem::path(rsw::Utf8ToWide(supStr))
                : (baseDir / rsw::Utf8ToWide(supStr));
            if (std::filesystem::is_directory(supDir, ec)) {
                for (const auto& de : std::filesystem::recursive_directory_iterator(supDir, ec)) {
                    if (!de.is_regular_file(ec)) continue;
                    const auto rel = std::filesystem::relative(de.path(), supDir, ec);
                    pakItems.push_back({de.path(), rsw::WideToUtf8(rel.generic_wstring()), "__support__"});
                }
            }
        }

        for (const auto& pl : prog.payloads) {
            const std::filesystem::path rawS(rsw::Utf8ToWide(rsw::ExpandArchToken(pl.sourceGlob, rsw::QueryHostPlatform().NativeArchSlug())));
            const std::filesystem::path absS = rawS.is_absolute() ? rawS : (baseDir / rawS);
            if (std::filesystem::is_regular_file(absS, ec)) {
                pakItems.push_back({absS, rsw::WideToUtf8(absS.filename().wstring()), pl.id});
            } else if (std::filesystem::is_directory(absS, ec)) {
                for (const auto& de : std::filesystem::recursive_directory_iterator(absS, ec)) {
                    if (!de.is_regular_file(ec)) continue;
                    const auto rel = std::filesystem::relative(de.path(), absS, ec);
                    pakItems.push_back({de.path(), rsw::WideToUtf8(rel.generic_wstring()), pl.id});
                }
            }
        }

        std::vector<std::uint8_t> pakBytes;
        std::uint64_t uncompBytes = 0;
        if (!pakItems.empty() && !rsw::BuildRswPakBytes(pakItems, pakBytes, uncompBytes, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }

        // Stage on local NTFS `%TEMP%` before copying out atomically!
        const auto stageExe = std::filesystem::temp_directory_path(ec) /
                              (L"rssc-pack-" + std::to_wstring(::GetCurrentProcessId()) + L".exe");
        std::filesystem::copy_file(selfPath, stageExe, std::filesystem::copy_options::overwrite_existing, ec);
        rsw::StripExistingPeCertificates(stageExe);
        const auto manifestLangs = rsw::QueryExistingPeManifestLangIds(stageExe);

        HANDLE hRes = ::BeginUpdateResourceW(stageExe.wstring().c_str(), FALSE);
        if (!hRes) {
            std::cerr << "[ERROR] BeginUpdateResourceW failed.\n";
            return 1;
        }
        constexpr WORD kLangNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
        bool ok = ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(rsw::kResIdScriptBytecode), kLangNeutral,
            const_cast<std::uint8_t*>(bcComp.data()), static_cast<DWORD>(bcComp.size()));
        if (!pakBytes.empty()) {
            ok = ok && ::UpdateResourceW(
                hRes, RT_RCDATA, MAKEINTRESOURCEW(rsw::kResIdSfxOrNativeArchive), kLangNeutral,
                pakBytes.data(), static_cast<DWORD>(pakBytes.size()));
        }

        const std::string effUac = uacOverride.empty() ? prog.package.uac : uacOverride;
        const std::string icoRaw = iconOverride.empty() ? prog.package.iconFile : iconOverride;
        const std::filesystem::path icoPath = icoRaw.empty()
            ? std::filesystem::path{}
            : (std::filesystem::path(rsw::Utf8ToWide(icoRaw)).is_absolute()
                ? std::filesystem::path(rsw::Utf8ToWide(icoRaw))
                : (baseDir / rsw::Utf8ToWide(icoRaw)));

        ok = ok && rsw::StampIconAndVersionOnExeResource(
            hRes, icoPath, prog.package.publisher,
            prog.package.tagline.empty() ? (prog.package.name + " Micro-App") : prog.package.tagline,
            prog.package.version, rsw::WideToUtf8(outExe.stem().wstring()),
            rsw::WideToUtf8(outExe.filename().wstring()), prog.package.name, err);
        ok = ok && rsw::StampExecutionLevelManifestOnResource(hRes, manifestLangs, effUac);

        if (!::EndUpdateResourceW(hRes, !ok) || !ok) {
            std::filesystem::remove(stageExe, ec);
            std::cerr << "[ERROR] EndUpdateResourceW failed packing micro-app.\n";
            return 1;
        }
        std::filesystem::create_directories(outExe.parent_path(), ec);
        std::filesystem::copy_file(stageExe, outExe, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(stageExe, ec);

        std::cout << "[OK] Packed standalone RaggieScript Windows executable (" << pakItems.size()
                  << " embedded file(s), UAC=" << rsw::NormalizeRequestedExecutionLevel(effUac) << "): "
                  << rsw::WideToUtf8(outExe.wstring()) << "\n";
        return 0;
    }

    std::cerr << "[ERROR] Unknown command '" << cmd << "'. Run 'raggiescript.exe --help' for usage.\n";
    return 2;
}
