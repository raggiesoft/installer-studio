// ================================================================================
// FILE: src/patch/RswPatchMain.cpp
// PROJECT: RaggieSoft Patch Compiler (`RsPatch.exe`)
// WEBSITE: https://raggiesoftstudio.com/installer-studio  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `RsPatch.exe`?
//     `RsPatch.exe` is RaggieSoft Installer Studio's standalone native Win32 C++20
//     binary-delta patch compiler, verifier, and transactional applier (`x64` & `ARM64`,
//     static `/MT`, zero external dependencies beyond Windows 11 `msdelta.dll` and `msi.dll`):
//       1. `RsPatch.exe plan <old.msi|.exe> <new.msi|.exe> [--allow-small-update] [--diagnostics=json]`
//          Analyzes `UpgradeCode`, `ProductCode`, `ProductVersion`, file diffs, and
//          Microsoft Component Rules (`RSW080`–`RSW086`) across MSI or **RaggieScript EXE** builds.
//       2. `RsPatch.exe build-msp <base.msi> <upgraded.msi> <out.msp> [--allow-small-update] [--targets=<GUIDs>]`
//          Verifies Component Rules and compiles a genuine Windows Installer `.MSP` patch
//          package (`MSIDBOPEN_PATCHFILE` + `MsiPatchMetadata` + `RswPatch1` OLE substorage).
//       3. `RsPatch.exe build-exe <old-dir-or-exe> <new-dir-or-exe> <out.rswpatch> [--version-from=...] [--version-to=...]`
//          Uses Windows `msdelta.dll` (`CreateDeltaB` PE executable jump/call fixups + `LZMS`)
//          to build a SHA-256-sealed `.rswpatch` delta for **RaggieScript EXE** applications!
//       4. `RsPatch.exe apply <patch.rswpatch> <install-dir> [/s]`
//          Verifies baseline SHA-256 hashes, reconstructs modified files via `ApplyDeltaB`,
//          prunes retired files, updates `Uninstall.rswm` & `DisplayVersion`, and rolls back
//          100% atomically via `.rsw-rollback.jnl` if any file is locked or corrupted!
//       5. `RsPatch.exe inspect <patch.rswpatch> [--diagnostics=json]`
//          Displays the exact Keep / Add / Delta / Delete manifest inside any `.rswpatch` file.
// ================================================================================

#include "../common/RswBinaryDelta.hpp"
#include "../common/RswEditions.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswUpdatePlanner.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void PrintUsage() {
    std::cout
        << rsw::kPatchTitle << " (`RsPatch.exe` v" << rsw::kVersionString << ") — " << rsw::kSuiteTitle << "\n"
        << "Zero-Dependency Win32 C++20 Binary-Delta Patch Compiler & Applier (`msdelta.dll` .MSP & RaggieScript EXE .rswpatch)\n\n"
        << "Usage:\n"
        << "  RsPatch.exe plan <old.msi|.exe> <new.msi|.exe> [--allow-small-update] [--diagnostics=json]\n"
        << "  RsPatch.exe build-msp <base.msi> <upgraded.msi> <out.msp> [--allow-small-update] [--targets=<{GUID1};{GUID2}>] [--edition=<tier>]\n"
        << "  RsPatch.exe build-exe <old-dir-or-exe> <new-dir-or-exe> <out.rswpatch> [--version-from=<X.Y.Z>] [--version-to=<X.Y.Z>] [--upgrade-code=<{GUID}>] [--edition=<tier>]\n"
        << "  RsPatch.exe apply <patch.rswpatch> <install-dir> [/s]\n"
        << "  RsPatch.exe inspect <patch.rswpatch> [--diagnostics=json]\n"
        << "  RsPatch.exe --headless-smoke-test | --version | --help\n\n"
        << "Fictitious Persons & Entities Notice:\n"
        << "  Sample corporations, universities (CPI, VDU), bands, transit agencies, and character names\n"
        << "  referenced in RaggieSoft documentation or examples are works of fiction.\n";
}

const char* OpLabel(rsw::RswPatchOp op) noexcept {
    switch (op) {
        case rsw::RswPatchOp::Keep:   return "keep";
        case rsw::RswPatchOp::Add:    return "add";
        case rsw::RswPatchOp::Delta:  return "delta";
        case rsw::RswPatchOp::Delete: return "delete";
    }
    return "keep";
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    ::SetConsoleOutputCP(CP_UTF8);
    if (argc <= 1) {
        PrintUsage();
        return 1;
    }

    std::vector<std::string> rawArgs;
    rawArgs.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
        rawArgs.push_back(rsw::WideToUtf8(argv[i]));
    }

    bool allowSmall   = false;
    bool jsonDiag     = false;
    bool quietMode    = false;
    int  simFaultOps  = 0;
    std::string cliEdition;
    std::string verFrom;
    std::string verTo;
    std::string upgCode;
    std::string extraTargets;
    std::vector<std::string> pos;

    for (const auto& a : rawArgs) {
        if (a == "--help" || a == "-h" || a == "/?") {
            PrintUsage();
            return 0;
        }
        if (a == "--version" || a == "-v") {
            std::cout << rsw::kPatchTitle << " v" << rsw::kVersionString << " (" << rsw::QueryHostPlatform().NativeArchSlug() << ")\n";
            return 0;
        }
        if (a == "--headless-smoke-test") {
            // Self-verify Windows `msdelta.dll` `CreateDeltaB` + `ApplyDeltaB` round-trip in memory!
            const std::string v1 = "RaggieSoft Installer Studio v0.7.0 baseline executable payload block AAAA BBBB CCCC";
            const std::string v2 = "RaggieSoft Installer Studio v0.8.0 patched executable payload block AAAA DDDD CCCC!";
            std::vector<std::uint8_t> delta, restored;
            std::string err;
            if (!rsw::CreateMsDeltaBytes(
                    reinterpret_cast<const std::uint8_t*>(v1.data()), v1.size(),
                    reinterpret_cast<const std::uint8_t*>(v2.data()), v2.size(),
                    delta, err) ||
                !rsw::ApplyMsDeltaBytes(
                    reinterpret_cast<const std::uint8_t*>(v1.data()), v1.size(),
                    delta.data(), delta.size(),
                    restored, err)) {
                std::cerr << "[FAIL] RsPatch smoke test failed: " << err << "\n";
                return 1;
            }
            const std::string recStr(reinterpret_cast<const char*>(restored.data()), restored.size());
            return (recStr == v2) ? 0 : 1;
        }
        if (a == "--allow-small-update") { allowSmall = true; continue; }
        if (a == "--diagnostics=json")   { jsonDiag   = true; continue; }
        if (a == "/s" || a == "/S" || a == "--silent") { quietMode = true; continue; }
        if (a.rfind("--edition=", 0) == 0)      { cliEdition   = a.substr(10); continue; }
        if (a.rfind("--version-from=", 0) == 0) { verFrom      = a.substr(15); continue; }
        if (a.rfind("--version-to=", 0) == 0)   { verTo        = a.substr(13); continue; }
        if (a.rfind("--upgrade-code=", 0) == 0) { upgCode      = a.substr(15); continue; }
        if (a.rfind("--targets=", 0) == 0)      { extraTargets = a.substr(10); continue; }
        if (a.rfind("--simulate-fault=", 0) == 0) { simFaultOps = std::atoi(a.substr(17).c_str()); continue; }
        pos.push_back(a);
    }

    if (pos.empty()) {
        PrintUsage();
        return 2;
    }

    rsw::RswIdeEdition activeEd = rsw::RswIdeEdition::Community;
    {
        std::string edErr;
        if (!rsw::ResolveEffectiveCliEdition(cliEdition, activeEd, edErr)) {
            std::cerr << "[ERROR] " << edErr << "\n";
            return 2;
        }
    }

    const std::string cmd = rsw::AsciiLower(pos[0]);

    // 1. `plan <old> <new>`
    if (cmd == "plan") {
        if (pos.size() < 3) {
            std::cerr << "[ERROR] Usage: RsPatch.exe plan <old.msi|.exe> <new.msi|.exe> [--allow-small-update] [--diagnostics=json]\n";
            return 2;
        }
        if (!rsw::EditionSupportsMspPatchesAndMstTransforms(activeEd)) {
            std::cerr << "[RSW095] Update diff & patch planning requires Architect or Community Edition (currently running "
                      << rsw::WideToUtf8(rsw::EditionDisplayName(activeEd)) << ").\n";
            return 1;
        }
        rsw::UpdatePlanReport plan{};
        std::string err;
        if (!rsw::AnalyzeInstallerUpdatePlan(
                std::filesystem::path(rsw::Utf8ToWide(pos[1])),
                std::filesystem::path(rsw::Utf8ToWide(pos[2])),
                allowSmall, plan, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        std::cout << (jsonDiag ? rsw::FormatUpdatePlanAsJson(plan) : rsw::FormatUpdatePlanAsText(plan));
        return plan.HasBlockingErrors() ? 1 : 0;
    }

    // 2. `build-msp <base.msi> <upgraded.msi> <out.msp>`
    if (cmd == "build-msp") {
        if (pos.size() < 4) {
            std::cerr << "[ERROR] Usage: RsPatch.exe build-msp <base.msi> <upgraded.msi> <out.msp> [--allow-small-update]\n";
            return 2;
        }
        if (!rsw::EditionSupportsMspPatchesAndMstTransforms(activeEd)) {
            std::cerr << "[RSW095] .MSP patch compilation requires Architect or Community Edition (currently running "
                      << rsw::WideToUtf8(rsw::EditionDisplayName(activeEd)) << ").\n";
            return 1;
        }
        std::string err;
        const std::filesystem::path outMsp(rsw::Utf8ToWide(pos[3]));
        if (!rsw::GenerateMsiPatchPackage(
                std::filesystem::path(rsw::Utf8ToWide(pos[1])),
                std::filesystem::path(rsw::Utf8ToWide(pos[2])),
                outMsp, err, allowSmall, extraTargets)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        std::string sha;
        rsw::ComputeFileSha256Hex(outMsp, sha);
        if (!quietMode) {
            std::cout << "[OK] Compiled Windows Installer Patch (.msp): " << pos[3]
                      << " (SHA-256: " << sha.substr(0, 16) << "...)\n";
        }
        return 0;
    }

    // 3. `build-exe <old-dir-or-exe> <new-dir-or-exe> <out.rswpatch>`
    if (cmd == "build-exe") {
        if (pos.size() < 4) {
            std::cerr << "[ERROR] Usage: RsPatch.exe build-exe <old-dir-or-exe> <new-dir-or-exe> <out.rswpatch>\n";
            return 2;
        }
        if (!rsw::EditionSupportsMspPatchesAndMstTransforms(activeEd)) {
            std::cerr << "[RSW095] RaggieScript EXE binary-delta compilation (.rswpatch) requires Architect or Community Edition (currently running "
                      << rsw::WideToUtf8(rsw::EditionDisplayName(activeEd)) << ").\n";
            return 1;
        }
        rsw::RswPatchPackage pkg{};
        std::string sha, err;
        const std::filesystem::path outPatch(rsw::Utf8ToWide(pos[3]));
        if (!rsw::BuildRswPatchFromSnapshots(
                std::filesystem::path(rsw::Utf8ToWide(pos[1])),
                std::filesystem::path(rsw::Utf8ToWide(pos[2])),
                outPatch, verFrom, verTo, upgCode, pkg, sha, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        if (!quietMode) {
            std::size_t nKeep = 0, nAdd = 0, nDelta = 0, nDel = 0;
            for (const auto& e : pkg.entries) {
                if (e.op == rsw::RswPatchOp::Keep)   ++nKeep;
                if (e.op == rsw::RswPatchOp::Add)    ++nAdd;
                if (e.op == rsw::RswPatchOp::Delta)  ++nDelta;
                if (e.op == rsw::RswPatchOp::Delete) ++nDel;
            }
            std::cout << "[OK] Compiled RaggieScript EXE Binary Delta (" << pkg.productName
                      << " v" << pkg.fromVersion << " -> v" << pkg.toVersion << "):\n"
                      << "     PATCH  : " << pos[3] << " (SHA-256: " << sha.substr(0, 16) << "...)\n"
                      << "     OPS    : ~" << nDelta << " delta, +" << nAdd << " added, -" << nDel << " retired, =" << nKeep << " unchanged\n";
        }
        return 0;
    }

    // 4. `apply <patch.rswpatch> <install-dir>`
    if (cmd == "apply") {
        if (pos.size() < 3) {
            std::cerr << "[ERROR] Usage: RsPatch.exe apply <patch.rswpatch> <install-dir> [/s]\n";
            return 2;
        }
        rsw::RswPatchApplySummary sum{};
        std::string err;
        if (!rsw::ApplyRswPatchTransaction(
                std::filesystem::path(rsw::Utf8ToWide(pos[1])),
                std::filesystem::path(rsw::Utf8ToWide(pos[2])),
                simFaultOps, sum, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        if (!quietMode) {
            std::cout << "[OK] Applied RaggieScript EXE Binary Delta (" << sum.productName
                      << " v" << sum.fromVersion << " -> v" << sum.toVersion << "): ~"
                      << sum.patchedCount << " patched, +" << sum.addedCount << " added, -"
                      << sum.deletedCount << " retired.\n";
        }
        return 0;
    }

    // 5. `inspect <patch.rswpatch>`
    if (cmd == "inspect") {
        if (pos.size() < 2) {
            std::cerr << "[ERROR] Usage: RsPatch.exe inspect <patch.rswpatch> [--diagnostics=json]\n";
            return 2;
        }
        rsw::RswPatchPackage pkg{};
        std::string err;
        if (!rsw::ReadRswPatchFile(std::filesystem::path(rsw::Utf8ToWide(pos[1])), pkg, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        if (jsonDiag) {
            std::ostringstream j;
            j << "{\n"
              << "  \"productName\": \"" << rsw::JsonEscapeString(pkg.productName) << "\",\n"
              << "  \"upgradeCode\": \"" << rsw::JsonEscapeString(pkg.upgradeCode) << "\",\n"
              << "  \"architecture\": \"" << rsw::JsonEscapeString(pkg.architecture) << "\",\n"
              << "  \"fromVersion\": \"" << rsw::JsonEscapeString(pkg.fromVersion) << "\",\n"
              << "  \"toVersion\": \"" << rsw::JsonEscapeString(pkg.toVersion) << "\",\n"
              << "  \"entryCount\": " << pkg.entries.size() << ",\n"
              << "  \"entries\": [";
            for (std::size_t i = 0; i < pkg.entries.size(); ++i) {
                const auto& e = pkg.entries[i];
                j << (i == 0 ? "\n" : ",\n")
                  << "    {\"op\": \"" << OpLabel(e.op) << "\", \"path\": \"" << rsw::JsonEscapeString(e.relPath)
                  << "\", \"deltaBytes\": " << e.payload.size() << ", \"newSize\": " << e.newSize << "}";
            }
            if (!pkg.entries.empty()) j << "\n  ";
            j << "]\n}\n";
            std::cout << j.str();
        } else {
            std::cout << "Patch: " << pkg.productName << " (" << pkg.fromVersion << " -> " << pkg.toVersion << ")\n";
            for (const auto& e : pkg.entries) {
                std::cout << "  [" << OpLabel(e.op) << "] " << e.relPath << " (" << e.payload.size() << " delta bytes)\n";
            }
        }
        return 0;
    }

    std::cerr << "[ERROR] Unknown command '" << pos[0] << "'. Run RsPatch.exe --help.\n";
    return 2;
}
