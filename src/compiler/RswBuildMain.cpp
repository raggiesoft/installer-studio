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
        << "Zero-Dependency Native Win32 C++20 Compiler (`instbuild.exe` -> MSI, MSM, MST, MSP, Setup.exe & VPAT 2.5)\n\n"
        << "Usage:\n"
        << "  instbuild.exe <project.rsip | suite.rsis> [--arch=x64|arm64] [--category=<cat>] [--project=<id>]\n"
        << "  instbuild.exe --validate [--diagnostics=json] <project.rsip | suite.rsis> [--arch=x64|arm64]\n"
        << "  instbuild.exe --emit-ai-spec <output-dir>\n"
        << "  instbuild.exe --emit-vpat <project.rsip> <output-dir>\n"
        << "  instbuild.exe --emit-msm <project.rsip> <output.msm> [--arch=x64|arm64]\n"
        << "  instbuild.exe --generate-mst <base.msi> <target.msi> <output.mst>\n"
        << "  instbuild.exe --generate-msp <base.msi> <upgraded.msi> <output.msp>\n"
        << "  instbuild.exe --verify-msi <file.msi>\n"
        << "  instbuild.exe --version | --help\n";
}

/// Validates or compiles a single `.rsip` project (resolving `@include` `.rsii` & `variables`).
int ProcessSingleProject(
    const std::filesystem::path& manifestPath,
    const std::map<std::string, std::string>& variables,
    const std::string& archOverride,
    bool validateOnly,
    bool jsonDiagnostics,
    bool emitVpatAlongside,
    const std::filesystem::path& compilerExeDir)
{
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

    const auto report = rsw::ValidateManifestDocument(doc, archOverride);
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

    if (rsw::EqualsIgnoreCase(doc.GetString("Package", "", "PackageKind", "msi"), "msm")) {
        std::filesystem::path outMsm;
        std::string err;
        if (!rsw::CompileManifestToMsm(doc, archOverride, {}, outMsm, err)) {
            std::cerr << "[ERROR] " << err << "\n";
            return 1;
        }
        std::cout << "[OK] Compiled Merge Module (.msm): " << rsw::WideToUtf8(outMsm.wstring()) << "\n";
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
        if (rsw::EmitProjectInstallerVpatHtml(doc, summary.msiPath.parent_path(), vpatHtml, buildErr)) {
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

    bool validateOnly = false;
    bool jsonDiagnostics = false;
    bool emitVpatFlag = false;
    std::string archOverride;
    std::string categoryFilter;
    std::string projectFilter;
    std::map<std::string, std::string> cliVariables;
    std::vector<std::string> positional;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h" || a == "/?") {
            PrintUsageBanner();
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
            rsw::RswManifestDocument rawDoc, vpatDoc;
            std::string err;
            if (!rsw::LoadManifestFile(std::filesystem::path(rsw::Utf8ToWide(args[i + 1])), rawDoc, err) ||
                !rsw::ResolveManifestIncludesAndVars(rawDoc, cliVariables, vpatDoc, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::filesystem::path vpatHtml;
            if (!rsw::EmitProjectInstallerVpatHtml(vpatDoc, std::filesystem::path(rsw::Utf8ToWide(args[i + 2])), vpatHtml, err)) {
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
        if (a == "--generate-mst") {
            if (i + 3 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --generate-mst <base.msi> <target.msi> <output.mst>\n";
                return 2;
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
        if (a == "--generate-msp") {
            if (i + 3 >= args.size()) {
                std::cerr << "[ERROR] Usage: instbuild.exe --generate-msp <base.msi> <upgraded.msi> <output.msp>\n";
                return 2;
            }
            std::string err;
            if (!rsw::GenerateMsiPatchPackage(
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 1])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 2])),
                    std::filesystem::path(rsw::Utf8ToWide(args[i + 3])),
                    err)) {
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
        if (a == "--with-vpat") { emitVpatFlag = true; continue; }
        if (a.rfind("--arch=", 0) == 0) { archOverride = a.substr(7); continue; }
        if (a.rfind("--category=", 0) == 0) { categoryFilter = a.substr(11); continue; }
        if (a.rfind("--project=", 0) == 0) { projectFilter = a.substr(10); continue; }
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

    // Dispatch `.rsis` Multi-Project Suite (just like `dotnet build RaggieSoft.slnx`)!
    if (rsw::IsSuiteFilePath(inputPath)) {
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
            for (const auto& arch : archList) {
                const int rc = ProcessSingleProject(
                    proj.resolvedPath, suite.variables, arch,
                    validateOnly, false, emitVpatFlag, compilerExeDir);
                if (rc != 0) {
                    return rc;
                }
            }
        }
        if (matchedCount == 0) {
            std::cerr << "[ERROR] No enabled projects in suite matched filter.\n";
            return 2;
        }
        if (jsonDiagnostics) {
            std::cout << "{\n  \"ok\": true,\n  \"suite\": \"" << suite.name
                      << "\",\n  \"projectsProcessed\": " << matchedCount << "\n}\n";
        } else {
            std::cout << "[OK] Suite '" << suite.name << "' processed " << matchedCount << " project(s).\n";
        }
        return 0;
    }

    return ProcessSingleProject(
        inputPath, cliVariables, archOverride,
        validateOnly, jsonDiagnostics, emitVpatFlag, compilerExeDir);
}
