// ================================================================================
// FILE: src/compiler/RswBuildMain.cpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `instbuild.exe`?
//     `instbuild.exe` is the headless command-line compiler for **RaggieSoft
//     Installer Studio**. Compiled as a static `/MT` native Win32 C++20 binary
//     (`x64` and `ARM64`) using only `cl.exe` + Windows 11 SDK, it handles:
//       1. Standalone Projects (`instbuild.exe <project.rsip> [--arch=x64|arm64]`):
//          Resolves any `@include "*.rsii"` headers, validates every field, builds
//          `<OutputName>.msi` + `<OutputName>-setup.exe` (or `<OutputName>.msm`),
//          and runs post-build relational SQL & ICE80 verification!
//       2. Multi-Project Suites (`instbuild.exe <suite.rsis> [--validate] [--arch=...]`):
//          Works just like `dotnet build Solution.slnx`—sorts member `.rsip`
//          projects by `BuildOrder`, propagates `[Variable]` tokens (`$(VAR)`),
//          and validates or compiles every matching project!
//       3. `--validate [--diagnostics=json]`: Sub-millisecond schema & mnemonic linter.
//       4. `--emit-ai-spec <dir>`: Emits `llms.txt`, `llms-full.txt`, and `SKILL.md`.
//       5. `--emit-vpat <project.rsip> <dir>`: Emits WCAG 2.2 AA / Section 508 VPAT 2.5 HTML5.
//       6. `--emit-msm`, `--generate-mst`, `--generate-msp`, and `--verify-msi`.
// ================================================================================

#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswSuiteWorkspace.hpp"
#include "../common/RswVersion.hpp"
#include "RswAiSpecEmitter.hpp"
#include "RswBootstrapperPackager.hpp"
#include "RswExtraPackages.hpp"
#include "RswMsiCompiler.hpp"
#include "RswUpdateManifestEmitter.hpp"
#include "RswUpdatePlanner.hpp"
#include "RswValidator.hpp"
#include "RswVpatEmitter.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void PrintUsageBanner() {
    std::cout
        << rsw::kSuiteTitle << " — " << rsw::kProductTagline << " (v" << rsw::kVersionString << ")\n"
        << "Zero-Dependency Native Win32 C++20 Compiler (`instbuild.exe` -> MSI, MSM, MST, MSP, RaggieScript EXE, Universal Setup.exe & VPAT 2.5)\n\n"
        << "Usage:\n"
        << "  instbuild.exe <project.rsip | suite.rsis | script.rssc> [--arch=x64|arm64] [--universal] [--with-vpat] [--out-dir=<dir>]\n"
        << "                [--license-file=<file.rswlic>] [--version-override=<X.Y.Z>] [--output-name=<stem>]\n"
        << "  instbuild.exe --validate [--diagnostics=json] [--license-file=<file.rswlic>] <project.rsip | suite.rsis | script.rssc> [--arch=x64|arm64]\n"
        << "  instbuild.exe --license-status [--license-file=<file.rswlic>] [--diagnostics=json]\n"
        << "  instbuild.exe --plan-update <old.msi|.exe> <new.msi|.exe> [--allow-small-update] [--diagnostics=json]\n"
        << "  instbuild.exe --emit-update-manifest <project.rsip> <artifact-dir> <out-manifest.json> [--web-base-url=<https://...>]\n"
        << "  instbuild.exe --emit-ai-spec <output-dir>\n"
        << "  instbuild.exe --emit-vpat <project.rsip> <output-dir>\n"
        << "  instbuild.exe --emit-msm <project.rsip> <output.msm> [--arch=x64|arm64]\n"
        << "  instbuild.exe --generate-mst <base.msi> <target.msi> <output.mst>\n"
        << "  instbuild.exe --generate-msp <base.msi> <upgraded.msi> <output.msp> [--allow-small-update]\n"
        << "  instbuild.exe --verify-msi <file.msi>\n"
        << "  instbuild.exe --version | --help\n";
}

struct CliPackageOverrides {
    std::string outDir;
    std::string version;
    std::string outputName;
    std::string universalName;
    bool        hasWebBaseUrl = false;
    std::string webBaseUrl;
};

/// Validates or compiles a single `.rsip` project (resolving `@include` `.rsii`, `$(VERSION)`, & CLI overrides).
int ProcessSingleProject(
    const std::filesystem::path& manifestPath,
    const std::map<std::string, std::string>& variables,
    const std::string& archOverride,
    bool forceUniversal,
    bool validateOnly,
    bool jsonDiagnostics,
    bool emitVpatAlongside,
    const rsw::RswLicenseStatus& licenseStatus,
    const CliPackageOverrides& pkgOverrides,
    const std::filesystem::path& compilerExeDir)
{
    const rsw::RswIdeEdition activeEdition = licenseStatus.edition;
    rsw::RswManifestDocument rawDoc;
    std::string parseError;
    if (!rsw::LoadManifestFile(manifestPath, rawDoc, parseError)) {
        if (jsonDiagnostics) {
            rsw::ValidationReport rep;
            rep.manifestPath = manifestPath;
            rep.diagnostics.push_back({rsw::DiagnosticSeverity::Error, "RSW000", 1, "", "", parseError});
            std::cout << rsw::FormatValidationReportAsJson(rep);
        } else {
            std::cerr << "[ERROR] " << parseError << "\n";
        }
        return 1;
    }

    if (!pkgOverrides.version.empty()) {
        rawDoc.SetString("Package", "", "Version", pkgOverrides.version);
    }

    rsw::RswManifestDocument doc;
    if (!rsw::ResolveManifestIncludesAndVars(rawDoc, variables, doc, parseError)) {
        if (jsonDiagnostics) {
            rsw::ValidationReport rep;
            rep.manifestPath = manifestPath;
            rep.diagnostics.push_back({rsw::DiagnosticSeverity::Error, "RSW000", 1, "", "", parseError});
            std::cout << rsw::FormatValidationReportAsJson(rep);
        } else {
            std::cerr << "[ERROR] " << parseError << "\n";
        }
        return 1;
    }

    const std::string effVer = rsw::TrimAscii(doc.GetString("Package", "", "Version"));
    auto expandVer = [&](std::string s) {
        return effVer.empty() ? s : rsw::ExpandNamedToken(s, "VERSION", effVer);
    };
    if (!pkgOverrides.outDir.empty()) {
        doc.SetString("Package", "", "OutputDir", expandVer(pkgOverrides.outDir));
    }
    if (!pkgOverrides.outputName.empty()) {
        doc.SetString("Package", "", "OutputName", expandVer(pkgOverrides.outputName));
    }
    if (!pkgOverrides.universalName.empty()) {
        doc.SetString("Package", "", "UniversalOutputName", expandVer(pkgOverrides.universalName));
    }
    if (pkgOverrides.hasWebBaseUrl) {
        doc.SetString("Package", "", "UniversalDownloadBaseUrl", expandVer(pkgOverrides.webBaseUrl));
    }

    const std::string pkgKind = rsw::AsciiLower(rsw::TrimAscii(doc.GetString("Package", "", "PackageKind", "msi")));
    const std::string engine  = rsw::AsciiLower(rsw::TrimAscii(doc.GetString("Package", "", "Engine", "msi")));
    const bool isRsscExeEng   = (engine == "raggiescript" || engine == "native");
    const bool buildUniversal = archOverride.empty() &&
                                pkgKind != "msm" &&
                                (forceUniversal || doc.GetBool("Package", "", "BuildUniversal", false));

    if (emitVpatAlongside && !rsw::EditionSupportsAutoVpatExport(activeEdition)) {
        std::cerr << "[RSW096] --with-vpat automated HTML5 VPAT 2.5 export requires Architect or Community Edition (currently running "
                  << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) << ").\n";
        return 1;
    }

    if (buildUniversal && !validateOnly) {
        for (const char* arch : {"x64", "arm64"}) {
            const auto rep = rsw::ValidateManifestDocument(doc, arch, activeEdition);
            if (!rep.IsOk()) {
                for (const auto& d : rep.diagnostics) {
                    std::cerr << "[" << d.code << " Line " << d.lineNumber << "] " << d.message << "\n";
                }
                return 1;
            }
        }

        rsw::CompiledMsiSummary x64Sum{}, arm64Sum{};
        std::filesystem::path x64Setup, arm64Setup;
        std::string buildErr;
        if (pkgKind == "suite") {
            if (!rsw::PackageUnifiedSuiteExecutable(doc, "x64", compilerExeDir, x64Sum, x64Setup, buildErr) ||
                !rsw::PackageUnifiedSuiteExecutable(doc, "arm64", compilerExeDir, arm64Sum, arm64Setup, buildErr)) {
                std::cerr << "[ERROR] " << buildErr << "\n";
                return 1;
            }
        } else if (pkgKind == "sfx" || pkgKind == "addon" || isRsscExeEng) {
            if (!rsw::PackageNativeOrSfxExecutable(doc, "x64", compilerExeDir, x64Sum, x64Setup, buildErr) ||
                !rsw::PackageNativeOrSfxExecutable(doc, "arm64", compilerExeDir, arm64Sum, arm64Setup, buildErr)) {
                std::cerr << "[ERROR] " << buildErr << "\n";
                return 1;
            }
        } else {
            if (!rsw::CompileManifestToMsi(doc, "x64", x64Sum, buildErr) ||
                !rsw::PackageSelfExtractingBootstrapper(doc, x64Sum, compilerExeDir, x64Setup, buildErr) ||
                !rsw::CompileManifestToMsi(doc, "arm64", arm64Sum, buildErr) ||
                !rsw::PackageSelfExtractingBootstrapper(doc, arm64Sum, compilerExeDir, arm64Setup, buildErr)) {
                std::cerr << "[ERROR] " << buildErr << "\n";
                return 1;
            }
        }

        rsw::UniversalPackageOutputs univOut{};
        if (!rsw::PackageUniversalInstallers(
                doc, x64Sum, x64Setup, arm64Sum, arm64Setup, compilerExeDir, univOut, buildErr)) {
            std::cerr << "[ERROR] " << buildErr << "\n";
            return 1;
        }

        std::cout << "[OK] Compiled Multi-Arch & Universal Installers (" << x64Sum.productName << " v" << x64Sum.productVersion << "):\n";
        if (pkgKind == "msi" && !isRsscExeEng) {
            std::cout << "     X64 MSI         : " << rsw::WideToUtf8(x64Sum.msiPath.wstring()) << "\n";
        }
        std::cout << "     X64 SETUP       : " << rsw::WideToUtf8(x64Setup.wstring()) << " (SHA-256: " << univOut.sha256X64.substr(0, 16) << "...)\n";
        if (pkgKind == "msi" && !isRsscExeEng) {
            std::cout << "     ARM64 MSI       : " << rsw::WideToUtf8(arm64Sum.msiPath.wstring()) << "\n";
        }
        std::cout << "     ARM64 SETUP     : " << rsw::WideToUtf8(arm64Setup.wstring()) << " (SHA-256: " << univOut.sha256Arm64.substr(0, 16) << "...)\n"
                  << "     OFFLINE UNIV EXE: " << rsw::WideToUtf8(univOut.offlineSetupExePath.wstring()) << " (x64 + ARM64 combined)\n";
        if (!univOut.webStubExePath.empty()) {
            std::cout << "     SMART WEB STUB  : " << rsw::WideToUtf8(univOut.webStubExePath.wstring()) << " (Auto-detects CPU via WinHTTP)\n";
        }
        if (emitVpatAlongside) {
            std::filesystem::path vpatHtml;
            if (rsw::EmitProjectInstallerVpatHtml(doc, x64Sum.msiPath.parent_path(), vpatHtml, buildErr, &licenseStatus)) {
                std::cout << "     VPAT            : " << rsw::WideToUtf8(vpatHtml.wstring()) << "\n";
            }
        }
        return 0;
    }

    const auto report = rsw::ValidateManifestDocument(doc, archOverride, activeEdition);
    if (validateOnly || !report.IsOk()) {
        if (jsonDiagnostics) {
            std::cout << rsw::FormatValidationReportAsJson(report);
        } else {
            for (const auto& d : report.diagnostics) {
                std::cerr << "[" << d.code << " Line " << d.lineNumber << "] " << d.message << "\n";
            }
            if (report.IsOk()) {
                std::cout << "[OK] Manifest is valid (" << report.resolvedArchitecture << "): "
                          << rsw::WideToUtf8(manifestPath.wstring()) << "\n";
            }
        }
        return report.IsOk() ? 0 : 1;
    }

    if (pkgKind == "msm") {
        std::filesystem::path outMsm;
        std::string err;
        if (!rsw::CompileManifestToMsm(doc, archOverride, {}, outMsm, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        std::cout << "[OK] Compiled Merge Module (.msm): " << rsw::WideToUtf8(outMsm.wstring()) << "\n";
        return 0;
    }

    if (pkgKind == "sfx" || pkgKind == "addon" || isRsscExeEng) {
        rsw::CompiledMsiSummary natSum{};
        std::filesystem::path outExe;
        std::string buildErr;
        if (!rsw::PackageNativeOrSfxExecutable(doc, archOverride, compilerExeDir, natSum, outExe, buildErr)) {
            std::cerr << "[ERROR] " << buildErr << "\n";
            return 1;
        }
        const char* kindDesc = (pkgKind == "sfx")
            ? "Self-Extracting Archive (.exe)"
            : ((pkgKind == "addon") ? "Host-Aware Add-On / Content Pack (-setup.exe)" : "RaggieScript EXE Installer (-setup.exe)");
        std::cout << "[OK] Compiled " << kindDesc
                  << " (" << natSum.architecture << ", " << natSum.fileCount << " file(s)):\n"
                  << "     EXE  : " << rsw::WideToUtf8(outExe.wstring()) << "\n";
        for (const auto& fp : natSum.featurePacks) {
            std::cout << "     PACK : " << fp.fileName << " (SHA-256: " << fp.sha256Hex.substr(0, 16) << "...)\n";
        }
        if (emitVpatAlongside) {
            std::filesystem::path vpatHtml;
            if (rsw::EmitProjectInstallerVpatHtml(doc, outExe.parent_path(), vpatHtml, buildErr, &licenseStatus)) {
                std::cout << "     VPAT : " << rsw::WideToUtf8(vpatHtml.wstring()) << "\n";
            }
        }
        return 0;
    }

    if (pkgKind == "suite") {
        std::filesystem::path suiteExe;
        std::string buildErr;
        if (!rsw::PackageUnifiedSuiteExecutable(doc, archOverride, compilerExeDir, suiteExe, buildErr)) {
            std::cerr << "[ERROR] " << buildErr << "\n";
            return 1;
        }
        std::cout << "[OK] Compiled Unified Suite Setup (ARPSYSTEMCOMPONENT adoption): "
                  << rsw::WideToUtf8(suiteExe.wstring()) << "\n";
        return 0;
    }

    rsw::CompiledMsiSummary summary{};
    std::string buildErr;
    if (!rsw::CompileManifestToMsi(doc, archOverride, summary, buildErr)) {
        std::cerr << "[ERROR] " << buildErr << "\n";
        return 1;
    }

    std::filesystem::path setupExePath;
    if (!rsw::PackageSelfExtractingBootstrapper(doc, summary, compilerExeDir, setupExePath, buildErr)) {
        std::cerr << "[ERROR] " << buildErr << "\n";
        return 1;
    }

    std::cout << "[OK] Compiled Enterprise MSI (" << summary.architecture << ", "
              << summary.fileCount << " file(s), " << summary.componentCount << " component(s)):\n"
              << "     MSI  : " << rsw::WideToUtf8(summary.msiPath.wstring()) << "\n";
    if (!setupExePath.empty()) {
        std::cout << "     SETUP: " << rsw::WideToUtf8(setupExePath.wstring()) << "\n";
    }

    if (emitVpatAlongside) {
        std::filesystem::path vpatHtml;
        if (rsw::EmitProjectInstallerVpatHtml(doc, summary.msiPath.parent_path(), vpatHtml, buildErr, &licenseStatus)) {
            std::cout << "     VPAT : " << rsw::WideToUtf8(vpatHtml.wstring()) << "\n";
        }
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    ::SetConsoleOutputCP(CP_UTF8);

    if (argc <= 1) {
        PrintUsageBanner();
        return 1;
    }

    wchar_t exeBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
    const std::filesystem::path compilerExeDir = std::filesystem::path(exeBuf).parent_path();

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) {
        args.push_back(rsw::WideToUtf8(argv[i]));
    }

    bool validateOnly    = false;
    bool jsonDiagnostics = false;
    bool allowSmallUpd   = false;
    bool emitVpatFlag    = false;
    bool universalFlag   = false;
    std::string archOverride;
    std::string cliLicenseFile;
    std::string categoryFilter;
    std::string projectFilter;
    CliPackageOverrides pkgOverrides{};
    std::map<std::string, std::string> cliVariables;
    std::vector<std::string> positional;

    // Pre-scan `--license-file=<path.rswlic>`, `--diagnostics=json`, `--allow-small-update`, & `--web-base-url=`
    // so standalone subcommands (`--license-status`, `--plan-update`, `--generate-msp`, etc.) honor switches anywhere!
    for (const auto& rawArg : args) {
        if (rawArg.rfind("--license-file=", 0) == 0) cliLicenseFile = rawArg.substr(15);
        else if (rawArg == "--diagnostics=json")     jsonDiagnostics = true;
        else if (rawArg == "--allow-small-update")   allowSmallUpd = true;
        else if (rawArg.rfind("--web-base-url=", 0) == 0) {
            pkgOverrides.hasWebBaseUrl = true;
            pkgOverrides.webBaseUrl    = rawArg.substr(15);
        }
    }
    rsw::RswLicenseStatus licenseStatus{};
    {
        std::string licErr;
        if (!rsw::ResolveActiveProductLicense(cliLicenseFile, compilerExeDir, licenseStatus, licErr)) {
            std::cerr << licErr << "\n";
            return 2;
        }
        bool isLicenseStatusOnly = false;
        for (const auto& a : args) {
            if (a == "--license-status" || a == "--help" || a == "-h" || a == "/?" || a == "--version" || a == "-v") {
                isLicenseStatusOnly = true;
                break;
            }
        }
        if (!isLicenseStatusOnly && !rsw::VerifyCommercialCiAgentRights(licenseStatus, licErr)) {
            std::cerr << licErr << "\n";
            return 2;
        }
    }
    const rsw::RswIdeEdition activeEdition = licenseStatus.edition;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h" || a == "/?") {
            PrintUsageBanner();
            return 0;
        }
        if (a == "--license-status") {
            if (jsonDiagnostics) {
                std::cout
                    << "{\n"
                    << "  \"isCommercialLicensed\": " << (licenseStatus.isCommercialLicensed ? "true" : "false") << ",\n"
                    << "  \"edition\": \"" << rsw::EditionSlug(licenseStatus.edition) << "\",\n"
                    << "  \"editionDisplayName\": \"" << rsw::JsonEscapeString(rsw::WideToUtf8(rsw::EditionDisplayName(licenseStatus.edition))) << "\",\n"
                    << "  \"licenseId\": \"" << rsw::JsonEscapeString(licenseStatus.cert.licenseId) << "\",\n"
                    << "  \"licensee\": \"" << rsw::JsonEscapeString(licenseStatus.cert.licensee) << "\",\n"
                    << "  \"source\": \"" << rsw::JsonEscapeString(licenseStatus.sourceDescription) << "\"\n"
                    << "}\n";
            } else {
                std::cout << rsw::WideToUtf8(rsw::BuildLicenseStatusSummaryText(licenseStatus)) << "\n";
            }
            return 0;
        }
        if (a == "--version" || a == "-v") {
            const auto hp = rsw::QueryHostPlatform();
            std::cout << rsw::kGeneratorSignature
                      << " [Host OS: Windows " << hp.majorVersion << "." << hp.minorVersion
                      << " Build " << hp.buildNumber
                      << ", True Silicon: " << hp.NativeArchSlug()
                      << (hp.IsEmulatedProcess() ? " (emulated)" : "") << "]\n";
            return 0;
        }
        if (a == "--emit-ai-spec") {
            if (i + 1 >= args.size()) {
                std::cerr << "[ERROR] --emit-ai-spec requires an <output-dir> path.\n";
                return 2;
            }
            std::string err;
            const std::filesystem::path outDir(rsw::Utf8ToWide(args[i + 1]));
            if (!rsw::EmitAiSpecifications(outDir, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Emitted llms.txt, llms-full.txt, and SKILL.md to: " << args[i + 1] << "\n";
            return 0;
        }
        if (a == "--emit-vpat") {
            if (i + 2 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --emit-vpat <project.rsip> <output-dir>\n";
                return 2;
            }
            if (!rsw::EditionSupportsAutoVpatExport(activeEdition)) {
                std::cerr << "[RSW096] Automated HTML5 VPAT 2.5 report generation (--emit-vpat) requires Architect or Community Edition (currently running "
                          << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) << ").\n";
                return 1;
            }
            rsw::RswManifestDocument rawDoc, vpatDoc;
            std::string err;
            if (!rsw::LoadManifestFile(std::filesystem::path(rsw::Utf8ToWide(args[i + 1])), rawDoc, err) ||
                !rsw::ResolveManifestIncludesAndVars(rawDoc, cliVariables, vpatDoc, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::filesystem::path vpatHtml;
            if (!rsw::EmitProjectInstallerVpatHtml(vpatDoc, std::filesystem::path(rsw::Utf8ToWide(args[i + 2])), vpatHtml, err, &licenseStatus)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Emitted WCAG 2.2 AA & Section 508 VPAT 2.5 HTML5 Report: "
                      << rsw::WideToUtf8(vpatHtml.wstring()) << "\n";
            return 0;
        }
        if (a == "--verify-msi") {
            if (i + 1 >= args.size()) {
                std::cerr << "[ERROR] --verify-msi requires a <file.msi> path.\n";
                return 2;
            }
            std::string err;
            const std::filesystem::path msiPath(rsw::Utf8ToWide(args[i + 1]));
            if (!rsw::VerifyCompiledMsiDatabase(msiPath, err)) {
                std::cerr << "[FAIL] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Verified MSI relational & ICE80 integrity: " << args[i + 1] << "\n";
            return 0;
        }
        if (a == "--wrap-msi") {
            if (i + 2 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --wrap-msi <project.rsip> <existing.msi> [--arch=x64|arm64]\n";
                return 2;
            }
            const std::filesystem::path mPath(rsw::Utf8ToWide(args[i + 1]));
            const std::filesystem::path existingMsi(rsw::Utf8ToWide(args[i + 2]));
            for (std::size_t j = i + 3; j < args.size(); ++j) {
                if (args[j].rfind("--arch=", 0) == 0) archOverride = args[j].substr(7);
            }
            rsw::RswManifestDocument rawDoc, doc;
            std::string err;
            if (!rsw::LoadManifestFile(mPath, rawDoc, err) ||
                !rsw::ResolveManifestIncludesAndVars(rawDoc, cliVariables, doc, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            const auto val = rsw::ValidateManifestDocument(doc, archOverride);
            const std::string arch = val.resolvedArchitecture;
            const std::filesystem::path baseDir = doc.sourcePath.empty()
                ? std::filesystem::current_path()
                : doc.sourcePath.parent_path();
            auto resolveP = [&](std::string_view raw) -> std::filesystem::path {
                if (raw.empty()) return {};
                std::string exp = rsw::ExpandArchToken(raw, arch);
                std::filesystem::path p(rsw::Utf8ToWide(exp));
                return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
            };

            rsw::CompiledMsiSummary sum{};
            sum.msiPath             = existingMsi;
            sum.architecture        = arch;
            sum.productName         = rsw::ExpandArchToken(doc.GetString("Package", "", "Name"), arch);
            sum.productVersion      = rsw::ExpandArchToken(doc.GetString("Package", "", "Version"), arch);
            sum.manufacturer        = rsw::ExpandArchToken(doc.GetString("Package", "", "Manufacturer"), arch);
            sum.perMachine          = rsw::EqualsIgnoreCase(doc.GetString("Package", "", "Scope", "machine"), "machine");
            sum.minWindowsBuild     = doc.GetInt("Requirements", "", "MinWindowsBuild", 22000);
            sum.osLaunchMessage     = doc.GetString("Requirements", "", "OsLaunchMessage", "This application requires Windows 11 (Build 22000) or later.");
            sum.defaultSubdir       = rsw::ExpandArchToken(doc.GetString("Install", "", "DefaultSubdir"), arch);
            sum.mainExeRel          = rsw::ExpandArchToken(doc.GetString("Install", "", "MainExecutable"), arch);
            sum.installBtnText      = doc.GetString("UI", "", "InstallButtonText", "&Install");
            sum.launchChkText       = doc.GetString("UI", "", "FinishLaunchCheckbox", "&Launch application when setup finishes");
            sum.offerLaunchOnFinish = doc.GetBool("UI", "", "OfferLaunchOnFinish", true);
            sum.licenseRtfPath      = resolveP(doc.GetString("UI", "", "LicenseRtf"));
            sum.iconFilePath        = resolveP(doc.GetString("UI", "", "IconFile"));
            rsw::NormalizeMsiGuid(doc.GetString("Package", "", "UpgradeCode"), sum.upgradeCode);
            sum.productCode = rsw::ComputeDeterministicComponentGuid(sum.upgradeCode, arch, "productcode:" + sum.productVersion);

            std::filesystem::path setupOut;
            if (!rsw::PackageSelfExtractingBootstrapper(doc, sum, compilerExeDir, setupOut, err, true /* already signed */)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Wrapped existing signed MSI into Setup.exe: " << rsw::WideToUtf8(setupOut.wstring()) << "\n";
            return 0;
        }
        if (a == "--generate-mst") {
            if (i + 3 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --generate-mst <base.msi> <target.msi> <output.mst>\n";
                return 2;
            }
            if (!rsw::EditionSupportsMspPatchesAndMstTransforms(activeEdition)) {
                std::cerr << "[RSW095] Windows Installer Transform (.mst) generation requires Architect or Community Edition (currently running "
                          << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) << ").\n";
                return 1;
            }
            std::string err;
            if (!rsw::GenerateMsiTransform(
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 1])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 2])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 3])),
                    err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Generated Windows Installer Transform (.mst): " << args[i + 3] << "\n";
            return 0;
        }
        if (a == "--plan-update") {
            if (i + 2 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --plan-update <old.msi|.exe> <new.msi|.exe> [--allow-small-update] [--diagnostics=json]\n";
                return 2;
            }
            if (!rsw::EditionSupportsMspPatchesAndMstTransforms(activeEdition)) {
                std::cerr << "[RSW095] Update diff & patch planning (--plan-update) requires Architect or Community Edition (currently running "
                          << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) << ").\n";
                return 1;
            }
            rsw::UpdatePlanReport plan{};
            std::string err;
            if (!rsw::AnalyzeInstallerUpdatePlan(
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 1])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 2])),
                    allowSmallUpd, plan, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << (jsonDiagnostics ? rsw::FormatUpdatePlanAsJson(plan) : rsw::FormatUpdatePlanAsText(plan));
            return plan.HasBlockingErrors() ? 1 : 0;
        }
        if (a == "--emit-update-manifest") {
            if (i + 3 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --emit-update-manifest <project.rsip> <artifact-dir> <out-manifest.json> [--web-base-url=<https://...>]\n";
                return 2;
            }
            if (!rsw::EditionSupportsRsUpdater(activeEdition)) {
                std::cerr << "[RSW092] RaggieSoft Updater manifest generation (--emit-update-manifest) requires Developer, Architect, or Community Edition.\n";
                return 1;
            }
            rsw::RswManifestDocument rawDoc, uDoc;
            std::string err;
            if (!rsw::LoadManifestFile(std::filesystem::path(rsw::Utf8ToWide(args[i + 1])), rawDoc, err) ||
                !rsw::ResolveManifestIncludesAndVars(rawDoc, cliVariables, uDoc, err) ||
                !rsw::EmitLauncherCompatibleUpdateManifest(
                    uDoc,
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 2])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 3])),
                    pkgOverrides.hasWebBaseUrl ? std::string_view(pkgOverrides.webBaseUrl) : std::string_view{},
                    err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Emitted DRY Launcher & RsUpdate JSON manifest: " << args[i + 3] << "\n";
            return 0;
        }
        if (a == "--generate-msp") {
            if (i + 3 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --generate-msp <base.msi> <upgraded.msi> <output.msp> [--allow-small-update]\n";
                return 2;
            }
            if (!rsw::EditionSupportsMspPatchesAndMstTransforms(activeEdition)) {
                std::cerr << "[RSW095] Binary-delta Patch (.msp) generation requires Architect or Community Edition (currently running "
                          << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) << "; Essentials and Developer service updates via Major Upgrades).\n";
                return 1;
            }
            std::string err;
            if (!rsw::GenerateMsiPatchPackage(
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 1])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 2])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 3])),
                    err,
                    allowSmallUpd)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Generated Windows Installer Patch Package (.msp): " << args[i + 3] << "\n";
            return 0;
        }
        if (a == "--emit-msm") {
            if (i + 2 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --emit-msm <project.rsip> <output.msm> [--arch=x64|arm64]\n";
                return 2;
            }
            if (!rsw::EditionSupportsCompilingMergeModules(activeEdition)) {
                std::cerr << "[RSW091] " << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition))
                          << " can consume existing [MergeModule.*] (.msm) files, while compiling new .MSM Merge Modules requires Developer, Architect, or Community Edition.\n";
                return 1;
            }
            const std::filesystem::path mPath(rsw::Utf8ToWide(args[i + 1]));
            const std::filesystem::path oPath(rsw::Utf8ToWide(args[i + 2]));
            for (std::size_t j = i + 3; j < args.size(); ++j) {
                if (args[j].rfind("--arch=", 0) == 0) archOverride = args[j].substr(7);
            }
            rsw::RswManifestDocument rawDoc, doc;
            std::string err;
            if (!rsw::LoadManifestFile(mPath, rawDoc, err) ||
                !rsw::ResolveManifestIncludesAndVars(rawDoc, cliVariables, doc, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::filesystem::path written;
            if (!rsw::CompileManifestToMsm(doc, archOverride, oPath, written, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Compiled Merge Module (.msm): " << rsw::WideToUtf8(written.wstring()) << "\n";
            return 0;
        }
        if (a == "--validate") { validateOnly = true; continue; }
        if (a == "--diagnostics=json") { jsonDiagnostics = true; continue; }
        if (a == "--allow-small-update") { allowSmallUpd = true; continue; }
        if (a == "--with-vpat") { emitVpatFlag = true; continue; }
        if (a == "--universal") { universalFlag = true; continue; }
        if (a.rfind("--license-file=", 0) == 0) { continue; }
        if (a.rfind("--arch=", 0) == 0) { archOverride = a.substr(7); continue; }
        if (a.rfind("--category=", 0) == 0) { categoryFilter = a.substr(11); continue; }
        if (a.rfind("--project=", 0) == 0) { projectFilter = a.substr(10); continue; }
        if (a.rfind("--out-dir=", 0) == 0) { pkgOverrides.outDir = a.substr(10); continue; }
        if (a.rfind("--version-override=", 0) == 0) { pkgOverrides.version = a.substr(19); continue; }
        if (a.rfind("--output-name=", 0) == 0) { pkgOverrides.outputName = a.substr(14); continue; }
        if (a.rfind("--universal-name=", 0) == 0) { pkgOverrides.universalName = a.substr(17); continue; }
        if (a.rfind("--web-base-url=", 0) == 0) {
            pkgOverrides.hasWebBaseUrl = true;
            pkgOverrides.webBaseUrl    = a.substr(15);
            continue;
        }
        if (a.rfind("-D", 0) == 0 && a.size() > 2) {
            auto eq = a.find('=', 2);
            if (eq != std::string::npos) {
                cliVariables[a.substr(2, eq - 2)] = a.substr(eq + 1);
            }
            continue;
        }
        positional.push_back(a);
    }

    if (positional.empty()) {
        std::cerr << "[ERROR] Missing <project.rsip | suite.rsis> path.\n";
        return 2;
    }

    const std::filesystem::path inputPath(rsw::Utf8ToWide(positional.front()));

    // Dispatch standalone `RaggieScript` (`.rssc`) compilation or `--validate` directly!
    if (rsw::EqualsIgnoreCase(rsw::WideToUtf8(inputPath.extension().wstring()), ".rssc")) {
        if (!rsw::EditionSupportsNativeEngineAndScripting(activeEdition)) {
            const std::string edMsg = "RaggieScript (.rssc) native scripting requires Developer, Architect, or Community Edition (currently running " +
                                      rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) + ").";
            if (jsonDiagnostics) {
                rsw::ValidationReport rep;
                rep.manifestPath         = inputPath;
                rep.resolvedArchitecture = archOverride.empty() ? std::string(rsw::QueryHostPlatform().NativeArchSlug()) : rsw::AsciiLower(archOverride);
                rep.activeEdition        = activeEdition;
                rep.diagnostics.push_back({rsw::DiagnosticSeverity::Error, "RSW092", 1, "RaggieScript", "Edition", edMsg});
                std::cout << rsw::FormatValidationReportAsJson(rep);
            } else {
                std::cerr << "[RSW092] " << edMsg << "\n";
            }
            return 1;
        }
        if (validateOnly) {
            rsw::rssc::ScriptProgram prog;
            std::vector<rsw::rssc::ScriptDiagnostic> diags;
            std::map<std::string, std::string> defs = cliVariables;
            if (!archOverride.empty()) defs["ARCH"] = rsw::AsciiLower(archOverride);
            const bool ok = rsw::rssc::CompileRaggieScriptFile(inputPath, prog, diags, defs);
            if (jsonDiagnostics) {
                rsw::ValidationReport rep;
                rep.manifestPath         = inputPath;
                rep.resolvedArchitecture = defs.count("ARCH") ? defs["ARCH"] : std::string(rsw::QueryHostPlatform().NativeArchSlug());
                for (const auto& d : diags) {
                    rep.diagnostics.push_back({
                        d.isError ? rsw::DiagnosticSeverity::Error : rsw::DiagnosticSeverity::Warning,
                        d.code, d.line, "RaggieScript", "", d.message
                    });
                }
                std::cout << rsw::FormatValidationReportAsJson(rep);
            } else {
                for (const auto& d : diags) {
                    std::cerr << "[" << d.code << " Line " << d.line << "] " << d.message << "\n";
                }
                if (ok) {
                    std::cout << "[OK] RaggieScript is valid (RSW701 & RSW702 passed): "
                              << rsw::WideToUtf8(inputPath.wstring()) << "\n";
                }
            }
            return ok ? 0 : 1;
        }

        if (universalFlag && archOverride.empty()) {
            rsw::CompiledMsiSummary xSum{}, aSum{};
            std::filesystem::path xExe, aExe;
            std::string err;
            if (!rsw::PackageRaggieScriptExecutable(inputPath, "x64", cliVariables, compilerExeDir,
                    pkgOverrides.outDir, pkgOverrides.outputName, pkgOverrides.version, xSum, xExe, err) ||
                !rsw::PackageRaggieScriptExecutable(inputPath, "arm64", cliVariables, compilerExeDir,
                    pkgOverrides.outDir, pkgOverrides.outputName, pkgOverrides.version, aSum, aExe, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            rsw::RswManifestDocument univDoc;
            univDoc.sourcePath = inputPath;
            univDoc.SetString("Package", "", "Name",        xSum.productName);
            univDoc.SetString("Package", "", "Version",     xSum.productVersion);
            univDoc.SetString("Package", "", "Manufacturer", xSum.manufacturer);
            if (!pkgOverrides.universalName.empty()) univDoc.SetString("Package", "", "UniversalOutputName", pkgOverrides.universalName);
            if (pkgOverrides.hasWebBaseUrl) univDoc.SetString("Package", "", "UniversalDownloadBaseUrl", pkgOverrides.webBaseUrl);
            rsw::UniversalPackageOutputs uOut{};
            if (!rsw::PackageUniversalInstallers(univDoc, xSum, xExe, aSum, aExe, compilerExeDir, uOut, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Compiled Universal RaggieScript Installers (" << xSum.productName << " v" << xSum.productVersion << "):\n"
                      << "     X64 SETUP       : " << rsw::WideToUtf8(xExe.wstring()) << "\n"
                      << "     ARM64 SETUP     : " << rsw::WideToUtf8(aExe.wstring()) << "\n"
                      << "     OFFLINE UNIV EXE: " << rsw::WideToUtf8(uOut.offlineSetupExePath.wstring()) << "\n";
            return 0;
        }

        rsw::CompiledMsiSummary sum{};
        std::filesystem::path outExe;
        std::string err;
        if (!rsw::PackageRaggieScriptExecutable(inputPath, archOverride, cliVariables, compilerExeDir,
                pkgOverrides.outDir, pkgOverrides.outputName, pkgOverrides.version, sum, outExe, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        std::cout << "[OK] Compiled RaggieScript Native Installer (" << sum.architecture << ", "
                  << sum.fileCount << " embedded file(s)):\n"
                  << "     SETUP: " << rsw::WideToUtf8(outExe.wstring()) << "\n";
        return 0;
    }

    // Dispatch `.rsis` Multi-Project Suite (just like `dotnet build RaggieSoft.slnx`)!
    if (rsw::IsSuiteFilePath(inputPath)) {
        if (!rsw::EditionSupportsUnifiedSuites(activeEdition)) {
            std::cerr << "[RSW094] Multi-Project Suites (.rsis) require Architect or Community Edition (currently running "
                      << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition)) << ").\n";
            return 1;
        }
        rsw::RswManifestDocument suiteDoc;
        std::string err;
        if (!rsw::LoadManifestFile(inputPath, suiteDoc, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        rsw::RswSuiteWorkspace suite;
        if (!rsw::ParseSuiteWorkspace(suiteDoc, suite, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        for (const auto& [k, v] : cliVariables) {
            suite.variables[k] = v;
        }

        const std::vector<std::string> archList = archOverride.empty()
            ? suite.defaultArchitectures
            : std::vector<std::string>{archOverride};

        int matchedCount = 0;
        for (const auto& proj : suite.projects) {
            if (!proj.enabled) continue;
            if (!categoryFilter.empty() && !rsw::EqualsIgnoreCase(proj.category, categoryFilter)) continue;
            if (!projectFilter.empty() &&
                !rsw::EqualsIgnoreCase(proj.id, projectFilter) &&
                !rsw::EqualsIgnoreCase(proj.resolvedPath.stem().string(), projectFilter)) continue;

            ++matchedCount;
            rsw::RswManifestDocument pRaw, pDoc;
            const bool projWantsUniv =
                !validateOnly && archOverride.empty() &&
                rsw::LoadManifestFile(proj.resolvedPath, pRaw, err) &&
                rsw::ResolveManifestIncludesAndVars(pRaw, suite.variables, pDoc, err) &&
                !rsw::EqualsIgnoreCase(pDoc.GetString("Package", "", "PackageKind", "msi"), "msm") &&
                (universalFlag || pDoc.GetBool("Package", "", "BuildUniversal", false));

            if (projWantsUniv) {
                const int rc = ProcessSingleProject(
                    proj.resolvedPath, suite.variables, "", true,
                    false, false, emitVpatFlag, licenseStatus, pkgOverrides, compilerExeDir);
                if (rc != 0) return rc;
            } else {
                for (const auto& arch : archList) {
                    const int rc = ProcessSingleProject(
                        proj.resolvedPath, suite.variables, arch, false,
                        validateOnly, false, emitVpatFlag, licenseStatus, pkgOverrides, compilerExeDir);
                    if (rc != 0) {
                        return rc;
                    }
                }
            }
        }
        if (matchedCount == 0) {
            std::cerr << "[ERROR] No enabled projects in suite matched filter.\n";
            return 2;
        }

        // Optional One-Command `.rsis` Unified Suite Installer (`[Suite] BuildSuiteInstaller = true`)!
        // After compiling member `.rsip` projects in `BuildOrder`, automatically synthesize & compile
        // the Unified Suite Setup chainer bundling all non-`.msm` / non-`sfx` member outputs!
        if (!validateOnly && suite.buildSuiteInstaller && categoryFilter.empty() && projectFilter.empty()) {
            const std::filesystem::path suiteBaseDir = inputPath.has_parent_path()
                ? inputPath.parent_path()
                : std::filesystem::current_path();
            rsw::RswManifestDocument synthDoc;
            synthDoc.sourcePath = inputPath;
            synthDoc.SetString("Package", "", "Name",                    suite.name);
            synthDoc.SetString("Package", "", "Tagline",                 suite.tagline);
            synthDoc.SetString("Package", "", "Version",                 suite.version);
            synthDoc.SetString("Package", "", "Manufacturer",            suite.manufacturer);
            synthDoc.SetString("Package", "", "PackageKind",             "suite");
            synthDoc.SetString("Package", "", "Scope",                   suite.scope);
            synthDoc.SetString("Package", "", "RequestedExecutionLevel", suite.requestedExecutionLevel);
            synthDoc.SetString("Package", "", "SuiteRollbackPolicy",     suite.rollbackPolicy);
            synthDoc.SetString("Package", "", "UpgradeCode",
                suite.upgradeCode.empty()
                    ? rsw::ComputeDeterministicComponentGuid("{A18F4E20-7C93-4D62-9B44-524147474945}", "suite", suite.name)
                    : suite.upgradeCode);
            if (!suite.outputName.empty()) {
                synthDoc.SetString("Package", "", "OutputName", suite.outputName);
            }
            synthDoc.SetString("Package", "", "OutputDir", suite.outputDir);
            if (!suite.defaultSubdir.empty()) {
                synthDoc.SetString("Install", "", "DefaultSubdir", suite.defaultSubdir);
            }
            synthDoc.SetString("UI", "", "AccentPrimaryHex",         suite.accentPrimaryHex);
            synthDoc.SetString("UI", "", "AccentSecondaryHex",       suite.accentSecondaryHex);
            synthDoc.SetString("UI", "", "ShowComponentProgressBar", suite.showComponentProgressBar ? "true" : "false");

            int bundledChildren = 0;
            for (const auto& proj : suite.projects) {
                if (!proj.enabled || !proj.includeInSuite) continue;
                rsw::RswManifestDocument mRaw, mDoc;
                if (!rsw::LoadManifestFile(proj.resolvedPath, mRaw, err) ||
                    !rsw::ResolveManifestIncludesAndVars(mRaw, suite.variables, mDoc, err)) {
                    continue;
                }
                const std::string mKind = rsw::AsciiLower(mDoc.GetString("Package", "", "PackageKind", "msi"));
                if (mKind == "msm" || mKind == "sfx" || mKind == "suite") continue;

                const std::string mEng  = rsw::AsciiLower(mDoc.GetString("Package", "", "Engine", "msi"));
                const bool mIsAddon     = (mKind == "addon");
                const bool mIsRsscExe   = mIsAddon || (mEng == "raggiescript" || mEng == "native");
                const std::string mName = mDoc.GetString("Package", "", "Name", proj.id);
                const std::string mVer  = mDoc.GetString("Package", "", "Version", "1.0.0");
                const std::string mUpg  = mDoc.GetString("Package", "", "UpgradeCode");
                const std::string mRawOut = mDoc.GetString("Package", "", "OutputName");
                const std::string mStem = mRawOut.empty() ? (rsw::SlugifyName(mName) + "-$(ARCH)") : mRawOut;
                const std::filesystem::path mProjDir = proj.resolvedPath.parent_path();
                const std::filesystem::path mOutDir  = mProjDir / rsw::Utf8ToWide(mDoc.GetString("Package", "", "OutputDir", "out"));
                const std::string artLeaf = mIsRsscExe ? (mStem + "-setup.exe") : (mStem + ".msi");
                const std::filesystem::path artFull = (mOutDir / rsw::Utf8ToWide(artLeaf)).lexically_normal();

                synthDoc.SetString("SuitePackage", proj.id, "Name",              mName);
                synthDoc.SetString("SuitePackage", proj.id, "Description",       mDoc.GetString("Package", "", "Tagline"));
                synthDoc.SetString("SuitePackage", proj.id, "Category",          proj.category);
                synthDoc.SetString("SuitePackage", proj.id, "Version",           mVer);
                synthDoc.SetString("SuitePackage", proj.id, "Source",            rsw::WideToUtf8(artFull.wstring()));
                if (!mUpg.empty()) synthDoc.SetString("SuitePackage", proj.id, "UpgradeCode", mUpg);
                synthDoc.SetString("SuitePackage", proj.id, "Engine",            mIsAddon ? "addon" : (mIsRsscExe ? "raggiescript" : "msi"));
                synthDoc.SetString("SuitePackage", proj.id, "Required",          proj.requiredInSuite ? "true" : "false");
                synthDoc.SetString("SuitePackage", proj.id, "SelectedByDefault", proj.selectedByDefault ? "true" : "false");
                synthDoc.SetString("SuitePackage", proj.id, "RollbackBoundary",  proj.rollbackBoundary ? "true" : "false");
                synthDoc.SetString("SuitePackage", proj.id, "ForwardInstallDir", proj.forwardInstallDir ? "true" : "false");
                ++bundledChildren;
            }

            if (bundledChildren > 0) {
                const bool wantUnivSuite = archOverride.empty() && (universalFlag || suite.buildUniversal) &&
                                           archList.size() >= 2;
                if (wantUnivSuite) {
                    rsw::CompiledMsiSummary xSum{}, aSum{};
                    std::filesystem::path xExe, aExe;
                    rsw::UniversalPackageOutputs uOut{};
                    if (!rsw::PackageUnifiedSuiteExecutable(synthDoc, "x64", compilerExeDir, xSum, xExe, err) ||
                        !rsw::PackageUnifiedSuiteExecutable(synthDoc, "arm64", compilerExeDir, aSum, aExe, err) ||
                        !rsw::PackageUniversalInstallers(synthDoc, xSum, xExe, aSum, aExe, compilerExeDir, uOut, err)) {
                        std::cerr << "[ERROR] " << err << "\n";
                        return 1;
                    }
                    std::cout << "[OK] Compiled Universal Suite Chainer (" << bundledChildren << " member package(s)): "
                              << rsw::WideToUtf8(uOut.offlineSetupExePath.wstring()) << "\n";
                } else {
                    for (const auto& arch : archList) {
                        std::filesystem::path sExe;
                        if (!rsw::PackageUnifiedSuiteExecutable(synthDoc, arch, compilerExeDir, sExe, err)) {
                            std::cerr << "[ERROR] " << err << "\n";
                            return 1;
                        }
                        std::cout << "[OK] Compiled Unified Suite Chainer (" << arch << ", " << bundledChildren << " member package(s)): "
                                  << rsw::WideToUtf8(sExe.wstring()) << "\n";
                    }
                }
            }
            (void)suiteBaseDir;
        }

        if (jsonDiagnostics) {
            std::cout << "{\n  \"ok\": true,\n  \"suite\": \"" << suite.name
                      << "\",\n  \"projectsProcessed\": " << matchedCount << "\n}\n";
        } else {
            std::cout << "[OK] Suite '" << suite.name << "' processed " << matchedCount << " project(s).\n";
        }
        return 0;
    }

    if (!validateOnly && !jsonDiagnostics && licenseStatus.isCommercialLicensed) {
        std::cout << "[LICENSE] " << rsw::WideToUtf8(rsw::EditionDisplayName(activeEdition))
                  << " — " << rsw::FormatCommercialLicenseAttribution(licenseStatus) << "\n";
    }
    return ProcessSingleProject(
        inputPath, cliVariables, archOverride, universalFlag,
        validateOnly, jsonDiagnostics, emitVpatFlag, licenseStatus, pkgOverrides, compilerExeDir);
}
