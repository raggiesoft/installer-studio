// ================================================================================
// FILE: src/tests/RswSelfTests.cpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does `insttests.exe` verify automatically on every build?
//     1. True Windows OS Build (`RtlGetVersion`) & `IsWow64Process2` CPU Silicon.
//     2. Lossless `.rsip` AST parsing, visual field mutation, and 100% comment &
//        `@include` preservation.
//     3. `@include "*.rsii"` shared header inheritance & local `.rsip` overrides.
//     4. `.rsis` Multi-Project Suite parsing, `$(VAR)` substitution, & `BuildOrder`
//        dependency sorting (`.msm` before `.msi`).
//     5. Deterministic SHA-1 UUIDv5 Component GUID repeatability (`bcrypt.h`).
//     6. Validator `RSW*` error detection & `--diagnostics=json` output.
//     7. Full native creation & post-build verification of `.msi`, `.msm`, `.mst`,
//        `.msp`, `-setup.exe`, `--emit-vpat`, and `msiprism.exe --inspect-summary`!
// ================================================================================

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswSuiteWorkspace.hpp"
#include "../common/RswVersion.hpp"
#include "../compiler/RswAiSpecEmitter.hpp"
#include "../compiler/RswBootstrapperPackager.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswMsiCompiler.hpp"
#include "../compiler/RswValidator.hpp"
#include "../compiler/RswVpatEmitter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace {

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const char* label, const std::string& detail = "") {
    if (cond) {
        ++gPassed;
        std::cout << "  [PASS] " << label << "\n";
    } else {
        ++gFailed;
        std::cerr << "  [FAIL] " << label;
        if (!detail.empty()) std::cerr << " -> " << detail;
        std::cerr << "\n";
    }
}

} // namespace

int wmain() {
    ::SetConsoleOutputCP(CP_UTF8);
    std::cout << "====================================================================\n"
              << "  RaggieSoft Installer Studio — Automated C++20 Self-Test Suite\n"
              << "====================================================================\n";

    wchar_t exeBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
    const std::filesystem::path binDir = std::filesystem::path(exeBuf).parent_path();

    // 1. OS & Silicon Detection
    const auto hp = rsw::QueryHostPlatform();
    const std::string hostArch(hp.NativeArchSlug());
    Check(hp.majorVersion >= 10 && hp.buildNumber >= 22000, "RtlGetVersion detects Windows 11 Build >= 22000");
    Check(hostArch == "arm64" || hostArch == "x64", "IsWow64Process2 detects native host silicon");

    // 2. Lossless AST Comment & `@include` Round-Trip
    const std::string sampleRsip =
        "; Top teaching comment\r\n"
        "@include \"shared.rsii\"\r\n"
        "[Package] ; Inline section comment\r\n"
        "Name        = Demo App ; Keep this inline comment!\r\n"
        "Version     = 1.0.0\r\n"
        "# Hash-style comment\r\n"
        "Manufacturer= RaggieSoft\r\n";
    auto ast = rsw::ParseManifestText(sampleRsip);
    Check(ast.Serialize() == sampleRsip, "Lossless .rsip AST exact byte-for-byte parse/serialize round-trip");

    ast.SetString("Package", "", "Name", "Updated Demo App");
    const std::string mutated = ast.Serialize();
    Check(mutated.find("; Top teaching comment") != std::string::npos &&
          mutated.find("@include \"shared.rsii\"") != std::string::npos &&
          mutated.find("; Keep this inline comment!") != std::string::npos &&
          mutated.find("# Hash-style comment") != std::string::npos &&
          ast.GetString("Package", "", "Name") == "Updated Demo App",
          "AST SetString preserves 100% of ';', '#', and '@include' lines");

    // 3. Deterministic SHA-1 UUIDv5 Component GUIDs
    const std::string g1 = rsw::ComputeDeterministicComponentGuid("{A18F4E20-7C93-4D62-9B44-524147474945}", "arm64", "file:App.exe");
    const std::string g2 = rsw::ComputeDeterministicComponentGuid("{A18F4E20-7C93-4D62-9B44-524147474945}", "arm64", "file:App.exe");
    const std::string gX = rsw::ComputeDeterministicComponentGuid("{A18F4E20-7C93-4D62-9B44-524147474945}", "x64",   "file:App.exe");
    Check(!g1.empty() && g1 == g2 && g1 != gX, "Deterministic SHA-1 UUIDv5 Component GUIDs (arch-isolated & repeatable)");

    // 4. Validator Linter Rules
    const std::string invalidRsip =
        "[Package]\n"
        "Name = BadPkg\n"
        "Version = 1.0.0.99\n"
        "Manufacturer = RaggieSoft\n"
        "UpgradeCode = NOT-A-GUID\n"
        "[Install]\n"
        "DefaultSubdir = RaggieSoft\\Bad\n"
        "[UI]\n"
        "InstallButtonText = InstallWithoutAmpersand\n";
    const auto badDoc = rsw::ParseManifestText(invalidRsip);
    const auto badRep = rsw::ValidateManifestDocument(badDoc, "arm64");
    const std::string badJson = rsw::FormatValidationReportAsJson(badRep);
    Check(!badRep.IsOk() && badRep.ErrorCount() >= 4 && badJson.find("\"ok\": false") != std::string::npos,
          "Validator catches 4-part version, bad GUID, missing payload, and missing '&' mnemonic");

    // 4b. Verify Microsoft Official Component Rules (`RSW031` Single-Directory & `RSW032` ICE38 User-Profile KeyPath!)
    {
        const std::string badCompRsip =
            "[Package]\nName = BadComp\nVersion = 1.0.0\nManufacturer = RaggieSoft\n"
            "UpgradeCode = {A18F4E20-7C93-4D62-9B44-524147474945}\n"
            "[Install]\nDefaultSubdir = RaggieSoft\\BadComp\n"
            "[Component.UserComp]\nTargetRoot = AppDataFolder\nKeyPathKind = file\nKeyPathTarget = a.exe\n"
            "[Component.SharedComp]\nTargetRoot = INSTALLDIR\nTargetSubdir = Bin\n"
            "[Payload.P1]\nSource = " + rsw::WideToUtf8(std::filesystem::path(exeBuf).wstring()) +
            "\nTargetRoot = INSTALLDIR\nTargetSubdir = OtherSubdir\nComponent = SharedComp\n";
        const auto bcDoc = rsw::ParseManifestText(badCompRsip);
        const auto bcRep = rsw::ValidateManifestDocument(bcDoc, hostArch);
        bool saw31 = false, saw32 = false;
        for (const auto& d : bcRep.diagnostics) {
            if (d.code == "RSW031") saw31 = true;
            if (d.code == "RSW032") saw32 = true;
        }
        Check(saw31 && saw32, "Validator enforces Microsoft Single-Directory (RSW031) & ICE38 HKCU KeyPath (RSW032) rules");
    }

    // 5. Temp workspace for `@include` `.rsii`, `.rsis` Suite, & End-to-End MSI/MSM/MST/MSP/VPAT tests
    std::error_code ec;
    const auto testWorkDir = std::filesystem::temp_directory_path(ec) / L"installer-studio-selftest";
    std::filesystem::remove_all(testWorkDir, ec);
    std::filesystem::create_directories(testWorkDir, ec);

    // 5a. Verify `@include "common.rsii"` inheritance + local `.rsip` override + `$(PUBLISHER)` variable!
    {
        std::ofstream incOut(testWorkDir / L"common.rsii", std::ios::binary);
        incOut << "[Package]\nManufacturer = $(PUBLISHER)\nWebsiteLabel = raggiesoftstudio.com\n"
               << "[UI]\nAccentPrimaryHex = #112233\n";
        incOut.close();

        const std::string childTxt =
            "@include \"common.rsii\"\n"
            "[Package]\n"
            "Name = Included Child\n"
            "[UI]\n"
            "AccentPrimaryHex = #2563EB\n";
        auto childRaw = rsw::ParseManifestText(childTxt, testWorkDir / L"child.rsip");
        rsw::RswManifestDocument resolvedChild;
        std::string incErr;
        const std::map<std::string, std::string> vars{{"PUBLISHER", "RaggieSoft Enterprise"}};
        const bool incOk = rsw::ResolveManifestIncludesAndVars(childRaw, vars, resolvedChild, incErr);
        Check(incOk &&
              resolvedChild.GetString("Package", "", "Manufacturer") == "RaggieSoft Enterprise" &&
              resolvedChild.GetString("Package", "", "WebsiteLabel") == "raggiesoftstudio.com" &&
              resolvedChild.GetString("UI", "", "AccentPrimaryHex") == "#2563EB",
              "@include .rsii inheritance, local .rsip override, & $(VAR) expansion", incErr);
    }

    // 5b. Verify `.rsis` Multi-Project Suite parsing & `BuildOrder` dependency sorting!
    {
        const std::string suiteTxt =
            "[Suite]\n"
            "Name = Sample Suite\n"
            "Architectures = arm64|x64\n"
            "[Variable]\n"
            "CHANNEL = Stable\n"
            "[Project.DesktopApp]\n"
            "Path = apps\\app.rsip\n"
            "Category = CoreApps\n"
            "BuildOrder = 20\n"
            "[Project.SharedMsm]\n"
            "Path = shared\\runtime.rsip\n"
            "Category = MergeModules\n"
            "BuildOrder = 10\n";
        auto sDoc = rsw::ParseManifestText(suiteTxt, testWorkDir / L"Sample.rsis");
        rsw::RswSuiteWorkspace sw;
        std::string sErr;
        const bool sOk = rsw::ParseSuiteWorkspace(sDoc, sw, sErr);
        Check(sOk &&
              sw.projects.size() == 2 &&
              sw.projects[0].id == "SharedMsm" &&
              sw.projects[1].id == "DesktopApp" &&
              sw.variables["CHANNEL"] == "Stable",
              ".rsis Multi-Project Suite parses [Variable] and sorts BuildOrder (10 -> 20)", sErr);
    }

    const auto samplePayload = binDir / L"HelloRaggieSoft.exe";
    const auto payloadToUse  = std::filesystem::exists(samplePayload) ? samplePayload : std::filesystem::path(exeBuf);

    // Emit AI specs
    std::string err;
    const auto aiDir = testWorkDir / L"ai-spec";
    Check(rsw::EmitAiSpecifications(aiDir, err) &&
          std::filesystem::exists(aiDir / L"llms.txt") &&
          std::filesystem::exists(aiDir / L"llms-full.txt") &&
          std::filesystem::exists(aiDir / L"SKILL.md"),
          "--emit-ai-spec emits llms.txt, llms-full.txt, and SKILL.md", err);

    // Build v1.0.0 Enterprise `.msi` + `-setup.exe` exercising `v0.3` Components, CreateFolder,
    // FileAssociations (both HKMU & Advertised ProgId/Verb!), Express/Custom MSI Dialogs, & Custom [MsiDialog.*]!
    std::ostringstream v1Text;
    v1Text << "[Package]\n"
           << "Name = SelfTest Suite App\n"
           << "Version = 1.0.0\n"
           << "Manufacturer = RaggieSoft\n"
           << "UpgradeCode = {C34A9B12-4411-4822-9A11-524147474947}\n"
           << "Architecture = " << hostArch << "\n"
           << "Scope = user\n"
           << "OutputName = selftest-v1\n"
           << "OutputDir = " << rsw::WideToUtf8(testWorkDir.wstring()) << "\n"
           << "BuildBootstrapper = true\n"
           << "[Install]\n"
           << "DefaultSubdir = RaggieSoft\\SelfTest\n"
           << "MainExecutable = " << rsw::WideToUtf8(payloadToUse.filename().wstring()) << "\n"
           << "[UI]\n"
           << "WizardLayout = aerowizard\n"
           << "MsiDialogPreset = express_custom\n"
           << "InstallButtonText = &Install\n"
           << "[Feature.Main]\n"
           << "Title = Core Engine\n"
           << "Level = 1\n"
           << "[Feature.Extras]\n"
           << "Title = Optional Templates\n"
           << "Parent = Main\n"
           << "Level = 2\n"
           << "[Component.CoreExeComp]\n"
           << "Feature = Main\n"
           << "TargetRoot = INSTALLDIR\n"
           << "KeyPathKind = file\n"
           << "KeyPathTarget = " << rsw::WideToUtf8(payloadToUse.filename().wstring()) << "\n"
           << "[Component.EmptyCacheComp]\n"
           << "Feature = Extras\n"
           << "TargetRoot = INSTALLDIR\n"
           << "TargetSubdir = Cache\n"
           << "KeyPathKind = folder\n"
           << "[Folder.PluginDir]\n"
           << "TargetRoot = AppDataFolder\n"
           << "TargetSubdir = RaggieSoft\\SelfTestPlugins\n"
           << "Feature = Extras\n"
           << "[Payload.Core]\n"
           << "Source = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n"
           << "Component = CoreExeComp\n"
           << "[FileAssociation.CleanExt]\n"
           << "Extension = .stclean\n"
           << "ProgId = RaggieSoft.SelfTest.Clean\n"
           << "Description = SelfTest Clean Document\n"
           << "TargetFile = " << rsw::WideToUtf8(payloadToUse.filename().wstring()) << "\n"
           << "Verbs = open:&Open:\"%1\"|edit:&Edit:--edit \"%1\"\n"
           << "Advertise = false\n"
           << "[FileAssociation.AdvExt]\n"
           << "Extension = .stadv\n"
           << "ProgId = RaggieSoft.SelfTest.Adv\n"
           << "Description = SelfTest Advertised Document\n"
           << "TargetFile = " << rsw::WideToUtf8(payloadToUse.filename().wstring()) << "\n"
           << "ContentType = application/x-selftest-adv\n"
           << "Advertise = true\n"
           << "[MsiDialog.CustomEndpointDlg]\n"
           << "HeaderTitle = Custom Endpoint\n"
           << "PrevDialog = SetupTypeDlg\n"
           << "NextDialog = VerifyReadyDlg\n"
           << "[MsiControl.UrlBox]\n"
           << "Dialog = CustomEndpointDlg\n"
           << "Type = Edit\n"
           << "X = 20\n"
           << "Y = 70\n"
           << "Width = 330\n"
           << "Height = 18\n"
           << "Property = CUSTOM_ENDPOINT\n"
           << "DefaultValue = https://raggiesoftstudio.com\n";

    auto v1Doc = rsw::ParseManifestText(v1Text.str(), testWorkDir / L"v1.rsip");
    rsw::CompiledMsiSummary v1Sum{};
    err.clear();
    Check(rsw::CompileManifestToMsi(v1Doc, hostArch, v1Sum, err),
          "Compiled v1.0.0 standalone Enterprise .msi + passed SQL/ICE80 verification", err);

    std::filesystem::path v1SetupExe;
    err.clear();
    Check(rsw::PackageSelfExtractingBootstrapper(v1Doc, v1Sum, binDir, v1SetupExe, err) &&
          std::filesystem::exists(v1SetupExe),
          "Packaged branded self-extracting Setup.exe via UpdateResourceW", err);

    const auto setupPe = rsw::HarvestPeVersionAndLanguage(v1SetupExe);
    Check(setupPe.version == "1.0.0.0", "Stamped 100% author-owned VERSIONINFO (1.0.0.0) onto Setup.exe", setupPe.version);

    // Test `<Product>-setup.exe --self-check` & `--extract-msi <dir>` round-trip!
    {
        std::wstring chkCmd = L"\"" + v1SetupExe.wstring() + L"\" --self-check";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, chkCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
            Check(code == 0, "Setup.exe --self-check verifies GDI+ Glass bootstrapper stub health");
        } else {
            Check(false, "Launched Setup.exe --self-check");
        }
    }
    const auto extractedDir = testWorkDir / L"extracted";
    {
        std::wstring cmd = L"\"" + v1SetupExe.wstring() + L"\" --extract-msi \"" + extractedDir.wstring() + L"\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
            std::string verErr;
            Check(code == 0 && rsw::VerifyCompiledMsiDatabase(extractedDir / L"SelfTest-Suite-App.msi", verErr),
                  "Setup.exe --extract-msi cleanly extracts valid Enterprise .msi", verErr);
        } else {
            Check(false, "Launched Setup.exe --extract-msi");
        }
    }

    // Build `.msm` Merge Module
    std::filesystem::path msmOut;
    err.clear();
    Check(rsw::CompileManifestToMsm(v1Doc, hostArch, testWorkDir / L"selftest.msm", msmOut, err),
          "Compiled and verified Windows Installer Merge Module (.msm)", err);

    // Build v1.1.0 `.msi` to test `.mst` Transform & `.msp` Patch!
    auto v2Doc = v1Doc;
    v2Doc.SetString("Package", "", "Version", "1.1.0");
    v2Doc.SetString("Package", "", "OutputName", "selftest-v2");
    v2Doc.SetString("Registry", "OrgPolicy", "Root", "HKCU");
    v2Doc.SetString("Registry", "OrgPolicy", "Key", "Software\\RaggieSoft\\SelfTest");
    v2Doc.SetString("Registry", "OrgPolicy", "Name", "Channel");
    v2Doc.SetString("Registry", "OrgPolicy", "Value", "Enterprise");
    rsw::CompiledMsiSummary v2Sum{};
    err.clear();
    Check(rsw::CompileManifestToMsi(v2Doc, hostArch, v2Sum, err),
          "Compiled v1.1.0 target .msi for Transform (.mst) & Patch (.msp) generation", err);

    const auto mstPath = testWorkDir / L"enterprise-org.mst";
    err.clear();
    Check(rsw::GenerateMsiTransform(v1Sum.msiPath, v2Sum.msiPath, mstPath, err) &&
          std::filesystem::exists(mstPath),
          "Generated Windows Installer Transform (.mst) via MsiDatabaseGenerateTransformW", err);

    const auto mspPath = testWorkDir / L"selftest-1.0.0-to-1.1.0.msp";
    err.clear();
    Check(rsw::GenerateMsiPatchPackage(v1Sum.msiPath, v2Sum.msiPath, mspPath, err) &&
          std::filesystem::exists(mspPath),
          "Generated Windows Installer Patch Package (.msp) via MSIDBOPEN_PATCHFILE", err);

    std::filesystem::path vpatHtmlPath;
    err.clear();
    Check(rsw::EmitProjectInstallerVpatHtml(v1Doc, testWorkDir / L"vpat", vpatHtmlPath, err) &&
          std::filesystem::exists(vpatHtmlPath),
          "Generated WCAG 2.2 AA & Revised Section 508 VPAT 2.5 HTML5 Report (--emit-vpat)", err);

    // Verify `msiprism.exe --inspect-summary` opens `.msi`, `.msm`, and `.msp` cleanly!
    const auto prismExe = binDir / L"msiprism.exe";
    Check(std::filesystem::exists(prismExe), "msiprism.exe binary present alongside insttests.exe");
    if (std::filesystem::exists(prismExe)) {
        auto runPrism = [&](const std::filesystem::path& dbFile) -> bool {
            std::wstring cmd = L"\"" + prismExe.wstring() + L"\" --inspect-summary \"" + dbFile.wstring() + L"\"";
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                return false;
            }
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
            return code == 0;
        };
        Check(runPrism(v1Sum.msiPath) && runPrism(msmOut) && runPrism(mspPath),
              "msiprism.exe --inspect-summary inspects .msi, .msm, and .msp databases cleanly");
    }

    std::filesystem::remove_all(testWorkDir, ec);

    std::cout << "--------------------------------------------------------------------\n"
              << "  Summary: " << gPassed << " passed, " << gFailed << " failed.\n"
              << "====================================================================\n";
    return (gFailed == 0) ? 0 : 1;
}
