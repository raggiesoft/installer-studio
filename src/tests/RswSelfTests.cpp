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
#include "../common/RswLzmsCompression.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswMsiFeatureTree.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswPrerequisites.hpp"
#include "../common/RswSuiteWorkspace.hpp"
#include "../common/RswVersion.hpp"
#include "../common/RswWcagContrast.hpp"
#include "../common/RswWizardStyles.hpp"
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

    // 1. OS, Silicon, & Live `ImmersiveColorSet` Theme Broadcast Detection
    const auto hp = rsw::QueryHostPlatform();
    const std::string hostArch(hp.NativeArchSlug());
    Check(hp.majorVersion >= 10 && hp.buildNumber >= 22000, "RtlGetVersion detects Windows 11 Build >= 22000");
    Check(hostArch == "arm64" || hostArch == "x64", "IsWow64Process2 detects native host silicon");
    Check(rsw::ui::IsLiveThemeBroadcast(WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet")) &&
          rsw::ui::IsLiveThemeBroadcast(WM_SETTINGCHANGE, SPI_SETHIGHCONTRAST, 0) &&
          rsw::ui::IsLiveThemeBroadcast(WM_SYSCOLORCHANGE, 0, 0) &&
          !rsw::ui::IsLiveThemeBroadcast(WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment")),
          "IsLiveThemeBroadcast filters WM_SETTINGCHANGE(ImmersiveColorSet) & High Contrast live hot-swaps");

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

    // Emit AI specs & enforce permanent Zero-Competitor-Mention policy across `llms.txt`, `llms-full.txt`, & `SKILL.md`!
    std::string err;
    const auto aiDir = testWorkDir / L"ai-spec";
    const bool aiEmitted = rsw::EmitAiSpecifications(aiDir, err) &&
                           std::filesystem::exists(aiDir / L"llms.txt") &&
                           std::filesystem::exists(aiDir / L"llms-full.txt") &&
                           std::filesystem::exists(aiDir / L"SKILL.md");
    Check(aiEmitted, "--emit-ai-spec emits llms.txt, llms-full.txt, and SKILL.md", err);
    if (aiEmitted) {
        auto readLower = [](const std::filesystem::path& p) {
            std::ifstream in(p, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            return rsw::AsciiLower(ss.str());
        };
        std::string combinedAi =
            readLower(aiDir / L"llms.txt") + "\n" +
            readLower(aiDir / L"llms-full.txt") + "\n" +
            readLower(aiDir / L"SKILL.md");
        const auto repoRoot = binDir.parent_path().parent_path();
        if (std::filesystem::exists(repoRoot / L"ROADMAP.md")) {
            combinedAi += "\n" + readLower(repoRoot / L"ROADMAP.md");
        }
        if (std::filesystem::exists(repoRoot / L"README.md")) {
            combinedAi += "\n" + readLower(repoRoot / L"README.md");
        }
        const char* bannedCompetitors[] = {
            "installaware", "installshield", "revenera", "inno setup", ".iss",
            "advanced installer", "wix", "nsis", "orca.exe", "msicode",
            "adminstudio", "pace suite", "web media block", "web media blocks"
        };
        auto hasWholeToken = [](const std::string& hay, std::string_view tok) {
            std::size_t p = 0;
            while ((p = hay.find(tok, p)) != std::string::npos) {
                const bool leftAlpha  = (p > 0) && (hay[p - 1] >= 'a' && hay[p - 1] <= 'z');
                const std::size_t end = p + tok.size();
                const bool rightAlpha = (end < hay.size()) && (hay[end] >= 'a' && hay[end] <= 'z');
                if ((tok.front() == '.' || !leftAlpha) && !rightAlpha) return true;
                p = end;
            }
            return false;
        };
        std::string foundBanned;
        for (const char* b : bannedCompetitors) {
            if (hasWholeToken(combinedAi, b)) {
                foundBanned = b;
                break;
            }
        }
        Check(foundBanned.empty(), "AI specs (llms.txt, llms-full.txt, SKILL.md) contain zero competitor mentions", foundBanned);
    }

    // Verify native C++20 `RaggieSoftHelp.exe --headless-smoke-test` loads `.rshm.json` & renders all HTML5 topics to RTF!
    const auto helpExe = binDir / L"RaggieSoftHelp.exe";
    Check(std::filesystem::exists(helpExe), "RaggieSoftHelp.exe native C++20 Help Viewer present alongside insttests.exe");
    if (std::filesystem::exists(helpExe)) {
        std::wstring hCmd = L"\"" + helpExe.wstring() + L"\" --headless-smoke-test";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        bool helpSmokeOk = false;
        if (::CreateProcessW(nullptr, hCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
            helpSmokeOk = (code == 0);
        }
        Check(helpSmokeOk, "RaggieSoftHelp.exe --headless-smoke-test validates module.rshm.json & HTML5->RTF reader");
    }

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

    // 5B. Verify `v0.3.0` Editions, `LZMS` Solid Compression, `[Prerequisite.*]`, `SelectionTree`,
    //     `ActionText` `[1]` Template Hygiene, & Native 64-Bit `[MergeModule.*]` (`.msm`) Consumption!
    {
        Check(rsw::EditionHasFullPremierParity(rsw::RswIdeEdition::Community) &&
              rsw::EditionHasFullPremierParity(rsw::RswIdeEdition::Premier),
              "IDE Community Edition enforces 100% feature parity with Premier Edition");

        // Verify `v0.4.2` 32-bit ARGB `StudioTreeIcon` vector `HIMAGELIST` (`20` icons, `ILC_COLOR32`)!
        {
            rsw::ui::ScopedGdiPlusSession gpTest;
            rsw::ui::RswTheme treeTheme{};
            treeTheme.Refresh();
            HIMAGELIST hStudioIml = rsw::ui::CreateStudioTreeImageList(96, treeTheme);
            const int icoCount = hStudioIml ? ::ImageList_GetImageCount(hStudioIml) : 0;
            if (hStudioIml) ::ImageList_Destroy(hStudioIml);
            Check(icoCount == static_cast<int>(rsw::ui::StudioTreeIcon::Count),
                  "32-bit PARGB StudioTreeIcon HIMAGELIST renders all 20 vector tree icons cleanly",
                  "count=" + std::to_string(icoCount));
        }

        // Windows `COMPRESS_ALGORITHM_LZMS` (`Cabinet.lib`) solid compression round-trip
        std::vector<std::uint8_t> repData(65536);
        for (std::size_t i = 0; i < repData.size(); ++i) repData[i] = static_cast<std::uint8_t>((i * 7u) & 0x3Fu);
        const auto lzmsComp = rsw::CompressWithLzmsIfSmaller(repData);
        std::vector<std::uint8_t> lzmsDec;
        std::string lzErr;
        Check(lzmsComp.size() < repData.size() / 4 &&
              rsw::DecompressLzmsOrCopyRaw(lzmsComp.data(), lzmsComp.size(), lzmsDec, lzErr) &&
              lzmsDec == repData,
              "Windows LZMS solid compression shrinks payload >75% and restores exact bytes", lzErr);

        // Curated & custom `[Prerequisite.*]` exit-code classification + INI round-trip
        std::ostringstream prManifest;
        prManifest << "[Package]\nName = PrereqApp\nVersion = 1.0.0\nManufacturer = RaggieSoft\n"
                   << "UpgradeCode = {6A0D9B55-1234-4B55-9C11-AA22BB33CC44}\n"
                   << "[Install]\nDefaultSubdir = RaggieSoft\\PrereqApp\n"
                   << "[Feature.Main]\nTitle = Core\nLevel = 1\nDisallowAbsent = true\n"
                   << "[Feature.Docs]\nParent = Main\nTitle = Offline Docs\nLevel = 1\n"
                   << "[Prerequisite.DotNet10]\nPreset = DotNet10Desktop\nDistribution = download\n"
                   << "[Prerequisite.VendorRuntime]\nPreset = Custom\nName = Custom Vendor Runtime\n"
                   << "Distribution = embedded\nSourceFile = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n"
                   << "SuccessExitCodes = 0, 1638\nRebootExitCodes = 3010, 1641, 9999\n"
                   << "[MergeModule.SharedMod]\nSource = " << rsw::WideToUtf8(msmOut.wstring()) << "\n"
                   << "Feature = Main\nTargetRoot = INSTALLDIR\n"
                   << "[Payload.MainExe]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\nFeature = Docs\n";
        auto prDoc = rsw::ParseManifestText(prManifest.str(), testWorkDir / L"prereq-msm.rsip");
        prDoc.SetString("Package", "", "OutputDir", rsw::WideToUtf8((testWorkDir / L"pr-out").wstring()));

        const auto resolvedPr = rsw::ResolveManifestPrerequisites(prDoc, hostArch);
        const auto iniRound   = rsw::ParsePrerequisiteTableIni(rsw::SerializePrerequisiteTableIni(resolvedPr));
        Check(iniRound.size() == 2 &&
              rsw::ClassifyPrerequisiteExitCode(iniRound[0], 3010) == rsw::PrereqExitOutcome::SucceededRebootRequested &&
              rsw::ClassifyPrerequisiteExitCode(iniRound[0], 23)   == rsw::PrereqExitOutcome::SucceededRebootRequested &&
              rsw::ClassifyPrerequisiteExitCode(iniRound[1], 9999) == rsw::PrereqExitOutcome::SucceededRebootRequested &&
              rsw::ClassifyPrerequisiteExitCode(iniRound[1], 1638) == rsw::PrereqExitOutcome::Succeeded &&
              rsw::ClassifyPrerequisiteExitCode(iniRound[1], 1603) == rsw::PrereqExitOutcome::Failed,
              "Prerequisite catalog & custom RebootExitCodes (3010/1641/23/9999) classify accurately");

        // Compile `.rsip` consuming `[MergeModule.SharedMod]` (`.msm`) + embedded LZMS `[Prerequisite.VendorRuntime]`!
        rsw::CompiledMsiSummary msmMergedSum{};
        std::string mergeErr;
        const bool mergedMsiOk = rsw::CompileManifestToMsi(prDoc, hostArch, msmMergedSum, mergeErr);
        std::filesystem::path mergedSetupExe;
        const bool mergedExeOk = mergedMsiOk &&
                                 rsw::PackageSelfExtractingBootstrapper(prDoc, msmMergedSum, binDir, mergedSetupExe, mergeErr);
        Check(mergedMsiOk && mergedExeOk && msmMergedSum.fileCount >= 2 && std::filesystem::exists(mergedSetupExe),
              "Native 64-bit [MergeModule.*] (.msm) & embedded LZMS [Prerequisite.*] compiled into MSI + Setup.exe", mergeErr);

        // Verify `SelectionTree` SQL loader, `DisallowAbsent` lock, & `ADDLOCAL` / `REMOVE` builder!
        std::vector<rsw::MsiFeatureNode> treeNodes;
        const bool loadedTree = rsw::LoadFeatureTreeFromMsiDatabase(msmMergedSum.msiPath, L"", treeNodes);
        if (loadedTree && treeNodes.size() == 2) {
            rsw::ApplyMsiFeatureActionRecursive(treeNodes, 0, rsw::MsiTreeState::Absent); // Should be blocked by `DisallowAbsent = true`!
            rsw::ApplyMsiFeatureActionRecursive(treeNodes, 1, rsw::MsiTreeState::Absent); // Disables `Docs` child feature!
        }
        const std::wstring treeProps = rsw::BuildMsiFeatureSelectionProps(treeNodes);
        Check(loadedTree && treeNodes.size() == 2 &&
              treeNodes[0].currentState != rsw::MsiTreeState::Absent &&
              treeNodes[1].currentState == rsw::MsiTreeState::Absent &&
              treeProps.find(L"ADDLOCAL=Main") != std::wstring::npos &&
              treeProps.find(L"REMOVE=Docs")   != std::wstring::npos,
              "MSI SelectionTree loads Feature costs, honors DisallowAbsent, and emits ADDLOCAL/REMOVE",
              rsw::WideToUtf8(treeProps));

        // Verify `ActionText` table never puts unexpanded `[1]` inside `Description` (only in `Template`!)
        {
            rsw::ScopedMsiHandle hActDb, hActView, hActRec;
            bool descClean = false, tmplPresent = false;
            if (::MsiOpenDatabaseW(msmMergedSum.msiPath.wstring().c_str(), MSIDBOPEN_READONLY, hActDb.Put()) == ERROR_SUCCESS &&
                ::MsiDatabaseOpenViewW(hActDb.Get(), L"SELECT `Description`, `Template` FROM `ActionText` WHERE `Action` = 'InstallFiles'", hActView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hActView.Get(), 0) == ERROR_SUCCESS &&
                ::MsiViewFetch(hActView.Get(), hActRec.Put()) == ERROR_SUCCESS) {
                wchar_t dBuf[256]{}, tBuf[256]{};
                DWORD cd = 256, ct = 256;
                ::MsiRecordGetStringW(hActRec.Get(), 1, dBuf, &cd);
                ::MsiRecordGetStringW(hActRec.Get(), 2, tBuf, &ct);
                descClean   = (std::wstring_view(dBuf).find(L"[1]") == std::wstring_view::npos);
                tmplPresent = (std::wstring_view(tBuf).find(L"[1]") != std::wstring_view::npos &&
                               std::wstring_view(tBuf).find(L"[9]") != std::wstring_view::npos &&
                               std::wstring_view(tBuf).find(L"[6]") != std::wstring_view::npos);
            }
            Check(descClean && tmplPresent,
                  "ActionText separates Description from rich '[1]/[9]/[6]' File/Directory/Size Template");

            // Verify `ProgressDlg` `ActionText`, `ActionData`, and `TimeRemaining` set `NoPrefix` (`0x00020000`)
            // and NEVER set `FormatSize` (`0x00080000` — which would coerce status strings into `"0KB"`!)
            rsw::ScopedMsiHandle hPrgView, hPrgRec;
            int prgChecked = 0;
            bool prgBitsOk = true;
            if (::MsiDatabaseOpenViewW(
                    hActDb.Get(),
                    L"SELECT `Control`, `Attributes` FROM `Control` WHERE `Dialog_` = 'ProgressDlg' AND (`Control` = 'ActionText' OR `Control` = 'ActionData' OR `Control` = 'TimeRemaining')",
                    hPrgView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hPrgView.Get(), 0) == ERROR_SUCCESS) {
                while (::MsiViewFetch(hPrgView.Get(), hPrgRec.Put()) == ERROR_SUCCESS) {
                    const int attr = ::MsiRecordGetInteger(hPrgRec.Get(), 2);
                    if ((attr & 0x00020000) == 0 || (attr & 0x00080000) != 0) {
                        prgBitsOk = false;
                    }
                    ++prgChecked;
                }
            }
            Check(prgChecked == 3 && prgBitsOk,
                  "ProgressDlg ActionText/ActionData/TimeRemaining use NoPrefix (0x20000) without FormatSize (0x80000)");
        }

        // 5C. Verify `v0.4.3` Combined Universal Packaging (`<stem>-offline-setup.exe` + Smart CPU Web Stub `<stem>-setup.exe`)!
        {
            std::ostringstream univManifest;
            univManifest << "[Package]\n"
                         << "Name = UniversalSample\n"
                         << "Version = 0.4.3\n"
                         << "Manufacturer = RaggieSoft\n"
                         << "UpgradeCode = {8B1E4C77-3456-4D22-8A99-CC33DD44EE55}\n"
                         << "Architecture = $(ARCH)\n"
                         << "OutputName = universal-sample-$(ARCH)\n"
                         << "UniversalOutputName = universal-sample\n"
                         << "BuildUniversal = true\n"
                         << "UniversalDownloadBaseUrl = https://assets.raggiesoft.com/samples/universal-sample\n"
                         << "[Install]\n"
                         << "DefaultSubdir = RaggieSoft\\UniversalSample\n"
                         << "[Payload.MainExe]\n"
                         << "Source = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n";
            auto univDoc = rsw::ParseManifestText(univManifest.str(), testWorkDir / L"universal-sample.rsip");
            univDoc.SetString("Package", "", "OutputDir", rsw::WideToUtf8((testWorkDir / L"univ-out").wstring()));

            rsw::CompiledMsiSummary uX64Sum{}, uArm64Sum{};
            std::filesystem::path uX64Setup, uArm64Setup;
            rsw::UniversalPackageOutputs uOut{};
            std::string uErr;
            const bool uBuilt =
                rsw::CompileManifestToMsi(univDoc, "x64", uX64Sum, uErr) &&
                rsw::PackageSelfExtractingBootstrapper(univDoc, uX64Sum, binDir, uX64Setup, uErr) &&
                rsw::CompileManifestToMsi(univDoc, "arm64", uArm64Sum, uErr) &&
                rsw::PackageSelfExtractingBootstrapper(univDoc, uArm64Sum, binDir, uArm64Setup, uErr) &&
                rsw::PackageUniversalInstallers(univDoc, uX64Sum, uX64Setup, uArm64Sum, uArm64Setup, binDir, uOut, uErr);

            const auto webStubBytes = (!uOut.webStubExePath.empty() && std::filesystem::exists(uOut.webStubExePath, ec))
                ? std::filesystem::file_size(uOut.webStubExePath, ec) : 0u;
            Check(uBuilt &&
                  std::filesystem::exists(uOut.offlineSetupExePath, ec) &&
                  std::filesystem::exists(uOut.webStubExePath, ec) &&
                  rsw::IsX64PortableExecutable(uOut.offlineSetupExePath) &&
                  rsw::IsX64PortableExecutable(uOut.webStubExePath) &&
                  webStubBytes > 64 * 1024 && webStubBytes < 1536 * 1024 &&
                  uOut.sha256X64.size() == 64 && uOut.sha256Arm64.size() == 64,
                  "PackageUniversalInstallers emits x64-bootable Combined Offline EXE + compact SHA-256 Web Stub EXE",
                  uErr);

            // Execute `<stem>-offline-setup.exe --extract-msi` (auto-detecting host CPU!) AND `--force-arch=x64 --extract-msi`!
            auto runUniversalExtract = [&](const std::wstring& extraArg, const std::filesystem::path& destDir) -> std::string {
                std::filesystem::create_directories(destDir, ec);
                std::wstring cmd = L"\"" + uOut.offlineSetupExePath.wstring() + L"\" ";
                if (!extraArg.empty()) cmd += extraArg + L" ";
                cmd += L"--extract-msi \"" + destDir.wstring() + L"\"";
                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    return "";
                }
                ::WaitForSingleObject(pi.hProcess, 20000);
                DWORD exitCode = 1;
                ::GetExitCodeProcess(pi.hProcess, &exitCode);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                if (exitCode != 0) return "";

                const auto extractedMsi = destDir / L"UniversalSample.msi";
                rsw::ScopedMsiHandle hSum;
                if (::MsiGetSummaryInformationW(0, extractedMsi.wstring().c_str(), 0, hSum.Put()) != ERROR_SUCCESS) {
                    return "";
                }
                UINT dataType = 0;
                INT iVal = 0;
                FILETIME ft{};
                wchar_t tmplBuf[128]{};
                DWORD cch = static_cast<DWORD>(std::size(tmplBuf));
                if (::MsiSummaryInfoGetPropertyW(hSum.Get(), 7 /* PID_TEMPLATE */, &dataType, &iVal, &ft, tmplBuf, &cch) != ERROR_SUCCESS) {
                    return "";
                }
                return rsw::WideToUtf8(tmplBuf);
            };

            const std::string expectedNativeTmpl = (hp.nativeMachine == IMAGE_FILE_MACHINE_ARM64) ? "Arm64;1033" : "x64;1033";
            const std::string autoTmpl = uBuilt ? runUniversalExtract(L"", testWorkDir / L"univ-extract-auto") : "";
            const std::string x64Tmpl  = uBuilt ? runUniversalExtract(L"--force-arch=x64", testWorkDir / L"univ-extract-x64") : "";
            Check(autoTmpl == expectedNativeTmpl && x64Tmpl == "x64;1033",
                  "Combined Offline Universal EXE pierces Prism via IsWow64Process2 and dispatches native silicon child installer",
                  "auto=" + autoTmpl + " forcedX64=" + x64Tmpl);
        }

        // 5D. Verify `v0.4.4` Plain-English Feature `Inclusion` Tiers (`minimal`, `typical`, `complete`, `hidden`)!
        {
            std::ostringstream tierManifest;
            tierManifest << "[Package]\nName = TierSample\nVersion = 0.4.5\nManufacturer = RaggieSoft\n"
                         << "UpgradeCode = {4C7A9D11-2233-4E88-9B11-776655443322}\n"
                         << "[Install]\nDefaultSubdir = RaggieSoft\\TierSample\n"
                         << "[Feature.Main]\nTitle = Core Engine\nInclusion = minimal\nDisallowAbsent = true\n"
                         << "[Feature.Templates]\nTitle = Standard Templates\nInclusion = typical\n"
                         << "[Feature.SdkBonus]\nTitle = Optional Bonus SDK\nInclusion = complete\n"
                         << "[Feature.Internal]\nTitle = Hidden Internal\nInclusion = hidden\n"
                         << "[Payload.MainExe]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\nFeature = Main\n"
                         << "[Registry.Tpl]\nRoot = HKCU\nKey = Software\\RaggieSoft\\TierSample\nName = Tpl\nValue = 1\nFeature = Templates\n"
                         << "[Registry.Sdk]\nRoot = HKCU\nKey = Software\\RaggieSoft\\TierSample\nName = Sdk\nValue = 1\nFeature = SdkBonus\n"
                         << "[Registry.Hid]\nRoot = HKCU\nKey = Software\\RaggieSoft\\TierSample\nName = Hid\nValue = 1\nFeature = Internal\n";
            auto tierDoc = rsw::ParseManifestText(tierManifest.str(), testWorkDir / L"tier-sample.rsip");
            tierDoc.SetString("Package", "", "OutputDir", rsw::WideToUtf8((testWorkDir / L"tier-out").wstring()));

            const auto valRep = rsw::ValidateManifestDocument(tierDoc, hostArch);
            rsw::CompiledMsiSummary tierSum{};
            std::string tierErr;
            const bool tierOk = valRep.IsOk() && rsw::CompileManifestToMsi(tierDoc, hostArch, tierSum, tierErr);
            std::vector<rsw::MsiFeatureNode> tNodes;
            const bool loadedT = tierOk && rsw::LoadFeatureTreeFromMsiDatabase(tierSum.msiPath, L"", tNodes);

            // Expect 3 visible features (`Main` = Level 1 Local, `Templates` = Level 50 Local, `SdkBonus` = Level 150 Absent; `Internal` = Level 0 hidden!)
            Check(loadedT && tNodes.size() == 3 &&
                  tNodes[0].installLevel == 1   && tNodes[0].defaultState == rsw::MsiTreeState::Local &&
                  tNodes[1].installLevel == 50  && tNodes[1].defaultState == rsw::MsiTreeState::Local &&
                  tNodes[2].installLevel == 150 && tNodes[2].defaultState == rsw::MsiTreeState::Absent,
                  "Plain-English Feature Inclusion (minimal/typical/complete/hidden) maps cleanly to MSI Feature.Level & SelectionTree defaults",
                  tierErr);
        }

        // 5E. Verify `v0.4.5` PE `Machine` `/dlib` SignTool Architecture Matching & `StripExistingPeCertificates`!
        {
            const auto x64StubExe   = binDir.parent_path() / L"x64" / L"SetupWizard.exe";
            const auto arm64StubExe = binDir.parent_path() / L"arm64" / L"SetupWizard.exe";
            const bool bothBuilt    = std::filesystem::exists(x64StubExe, ec) && std::filesystem::exists(arm64StubExe, ec);
            if (bothBuilt) {
                const auto machX64   = rsw::ReadPeMachineArchitecture(x64StubExe);
                const auto machArm64 = rsw::ReadPeMachineArchitecture(arm64StubExe);
                const auto stForX64  = rsw::LocateWindowsSdkSignTool(x64StubExe);
                const bool matchedX64 = stForX64.empty() || (rsw::ReadPeMachineArchitecture(stForX64) == IMAGE_FILE_MACHINE_AMD64);

                const auto stripCopy = testWorkDir / L"strip-cert-test.exe";
                std::filesystem::copy_file(x64StubExe, stripCopy, std::filesystem::copy_options::overwrite_existing, ec);
                const bool strippedOk = rsw::StripExistingPeCertificates(stripCopy) &&
                                        rsw::ReadPeMachineArchitecture(stripCopy) == IMAGE_FILE_MACHINE_AMD64;

                Check(machX64 == IMAGE_FILE_MACHINE_AMD64 &&
                      machArm64 == IMAGE_FILE_MACHINE_ARM64 &&
                      matchedX64 && strippedOk,
                      "v0.4.5 ReadPeMachineArchitecture, x64 /dlib SignTool dispatch on ARM64, and StripExistingPeCertificates succeed");
            }
        }

        // 5F. Verify `v0.5.0` 10 Wizard Styles (`Wizard95`, `FluentMica`, `FullScreenBlue`, `FullScreenTeal` ACME),
        //     `win95_classic` & `[SetupType.*]` Presets, `[Billboard.*]`, and WCAG 2.2 AA Contrast Enforcer!
        {
            bool allStylesValid = true;
            const char* layoutTokens[] = {
                "standard", "wizard95", "wizard97", "aerowizard", "fluentmica",
                "oneclick", "passivetoast", "fullscreenblue", "fullscreenteal", "autorun16x9"
            };
            for (int k = 0; k < 10; ++k) {
                const auto parsedKind = rsw::ParseWizardStyleKind(layoutTokens[k]);
                if (!rsw::EqualsIgnoreCase(rsw::WizardStyleToken(parsedKind), layoutTokens[k])) {
                    allStylesValid = false;
                }
                std::ostringstream layMan;
                layMan << "[Package]\nName = StyleSample\nVersion = 0.5.0\nManufacturer = RaggieSoft\n"
                       << "UpgradeCode = {5D8A1E22-3344-4F99-8A22-887766554433}\n"
                       << "[Install]\nDefaultSubdir = RaggieSoft\\StyleSample\n"
                       << "[UI]\nWizardLayout = " << layoutTokens[k] << "\nSetupTypePreset = win95_classic\n"
                       << "[SetupType.Workstation]\nTitle = &Developer Workstation\nDescription = Full IDE\nTier = complete\nRecommended = true\n"
                       << "[Billboard.Hero]\nHeadline = Retro ACME Setup\nBody = Classic #008080 Teal & #000080 Blue\nGlyph = Sparkle\n"
                       << "[Payload.MainExe]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n";
                auto layDoc = rsw::ParseManifestText(layMan.str(), testWorkDir / L"style-sample.rsip");
                if (!rsw::ValidateManifestDocument(layDoc, hostArch).IsOk()) {
                    allStylesValid = false;
                }
            }

            const auto win95Cards = rsw::BuildEffectiveSetupTypeOptions("win95_classic", {}, L"Exp", L"Cmp", L"Cus");
            const COLORREF fixedYellow = rsw::wcag::SnapAccentToAccessibleBand(RGB(255, 255, 160));
            const COLORREF fixedDark   = rsw::wcag::SnapAccentToAccessibleBand(RGB(6, 8, 18));
            double w1 = 0.0, d1 = 0.0, w2 = 0.0, d2 = 0.0;
            const bool contrastFixed =
                rsw::wcag::MeetsDualThemeAccentContrast(fixedYellow, w1, d1) &&
                rsw::wcag::MeetsDualThemeAccentContrast(fixedDark, w2, d2);

            // Low-contrast `AccentPrimaryHex = #FFFFAA` should trigger WCAG warning `RSW055`!
            std::ostringstream badHexMan;
            badHexMan << "[Package]\nName = LowContrastSample\nVersion = 0.5.0\nManufacturer = RaggieSoft\n"
                      << "UpgradeCode = {5D8A1E22-3344-4F99-8A22-887766554433}\n"
                      << "[Install]\nDefaultSubdir = RaggieSoft\\LowContrast\n"
                      << "[UI]\nAccentPrimaryHex = #FFFFAA\n"
                      << "[Payload.MainExe]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n";
            auto badHexDoc = rsw::ParseManifestText(badHexMan.str(), testWorkDir / L"low-contrast.rsip");
            const auto lowContrastRep = rsw::ValidateManifestDocument(badHexDoc, hostArch);
            bool sawRsw055 = false;
            for (const auto& dg : lowContrastRep.diagnostics) {
                if (dg.code == "RSW055") sawRsw055 = true;
            }

            Check(allStylesValid &&
                  win95Cards.size() == 4 &&
                  win95Cards[0].tier == "typical" &&
                  win95Cards[1].tier == "minimal" &&
                  win95Cards[2].tier == "complete" &&
                  win95Cards[3].tier == "custom" &&
                  contrastFixed && sawRsw055,
                  "v0.5.0 10 WizardLayout styles (incl. FullScreenBlue & ACME FullScreenTeal), win95_classic SetupTypes, & RSW055 WCAG Contrast Enforcer pass");
        }

        // 5G. Verify MSI Error 1324 (`..` in `Directory.DefaultDir`) Prevention & `VerifyCompiledMsiDatabase` Gate!
        {
            const auto nestedSrc = testWorkDir / L"smb-unicode-src" / L"Zo\x00EB" / L"Chapters";
            std::filesystem::create_directories(nestedSrc, ec);
            const auto nestedFile = nestedSrc / L"p001.md";
            {
                std::ofstream nf(nestedFile, std::ios::binary);
                nf << "Sample chapter text\n";
            }

            std::ostringstream dotMan;
            dotMan << "[Package]\nName = DotNormSample\nVersion = 0.5.0\nManufacturer = RaggieSoft\n"
                   << "UpgradeCode = {9E2F1A33-4455-4B66-8C77-112233445566}\n"
                   << "[Install]\nDefaultSubdir = RaggieSoft\\..\\RaggieSoft\\DotNormSample\n"
                   << "[Payload.Nested]\nSource = " << rsw::WideToUtf8((testWorkDir / L"smb-unicode-src").wstring()) << "\n"
                   << "TargetSubdir = Library\\.\\DiscardMe\\..\\Books\n";
            auto dotDoc = rsw::ParseManifestText(dotMan.str(), testWorkDir / L"dot-norm.rsip");
            dotDoc.SetString("Package", "", "OutputDir", rsw::WideToUtf8((testWorkDir / L"dot-out").wstring()));

            rsw::CompiledMsiSummary dotSum{};
            std::string dotErr;
            const bool dotBuilt = rsw::CompileManifestToMsi(dotDoc, hostArch, dotSum, dotErr);

            // Confirm `VerifyCompiledMsiDatabase` catches a corrupted `FILE~1|..` `Directory.DefaultDir` row!
            const auto badDirMsi = testWorkDir / L"dot-out" / L"corrupted-dotdot.msi";
            bool rejectedBadDotDot = false;
            if (dotBuilt) {
                std::filesystem::copy_file(dotSum.msiPath, badDirMsi, std::filesystem::copy_options::overwrite_existing, ec);
                rsw::ScopedMsiHandle hBadDb, hBadView;
                if (::MsiOpenDatabaseW(badDirMsi.wstring().c_str(), MSIDBOPEN_TRANSACT, hBadDb.Put()) == ERROR_SUCCESS &&
                    ::MsiDatabaseOpenViewW(hBadDb.Get(), L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('d_bad', 'INSTALLDIR', 'FILE~999|..')", hBadView.Put()) == ERROR_SUCCESS &&
                    ::MsiViewExecute(hBadView.Get(), 0) == ERROR_SUCCESS) {
                    ::MsiDatabaseCommit(hBadDb.Get());
                }
                hBadView = rsw::ScopedMsiHandle{};
                hBadDb   = rsw::ScopedMsiHandle{};
                std::string badVerifyErr;
                rejectedBadDotDot = !rsw::VerifyCompiledMsiDatabase(badDirMsi, badVerifyErr) &&
                                    badVerifyErr.find("Error 1324") != std::string::npos;
            }

            Check(dotBuilt && rejectedBadDotDot,
                  "Lexical payload subdir slicer + Directory segment sanitizer + VerifyMsi Error 1324 ('..') gate pass",
                  dotErr);
        }
    }

    // 6. Live Windows Installer (`msi.dll`) Engine Verification (`AppSearch`, `Signature`, `LaunchConditions`,
    //    `CostFinalize`, & Full Service Transaction when `msiserver` RPC is accessible outside AppContainer!)
    {
        rsw::ScopedMsiHandle hVerifyDb;
        bool hasSigTable        = false;
        bool uiTextPopulated    = false;
        bool progBarNonFlat     = false;
        bool rollbackCancelEvt  = false;
        bool allCyclesClosed    = true;
        std::string cycleErr;

        if (::MsiOpenDatabaseW(v1Sum.msiPath.wstring().c_str(), MSIDBOPEN_READONLY, hVerifyDb.Put()) == ERROR_SUCCESS) {
            hasSigTable = (::MsiDatabaseIsTablePersistentW(hVerifyDb.Get(), L"Signature") == MSICONDITION_TRUE);

            // Verify `UIText` table contains `MenuLocal`, `MenuAllLocal`, `MenuAbsent`, & `SelLocalLocal`!
            // (Note: MSI SQL `WHERE` clauses use `OR` rather than ANSI `IN (...)`.)
            rsw::ScopedMsiHandle hUiView;
            if (::MsiDatabaseOpenViewW(hVerifyDb.Get(),
                    L"SELECT `Key` FROM `UIText` WHERE `Key` = 'MenuLocal' OR `Key` = 'MenuAllLocal' OR `Key` = 'MenuAbsent' OR `Key` = 'SelLocalLocal'",
                    hUiView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hUiView.Get(), 0) == ERROR_SUCCESS) {
                int cnt = 0;
                rsw::ScopedMsiHandle hRec;
                while (::MsiViewFetch(hUiView.Get(), hRec.Put()) == ERROR_SUCCESS) ++cnt;
                uiTextPopulated = (cnt == 4);
            }

            // Verify `ProgressDlg.ProgressBar` sets `msidbControlAttributesProgress95` (`0x10000`, non-flat!)
            rsw::ScopedMsiHandle hPbView;
            if (::MsiDatabaseOpenViewW(hVerifyDb.Get(),
                    L"SELECT `Attributes` FROM `Control` WHERE `Dialog_` = 'ProgressDlg' AND `Control` = 'ProgressBar'",
                    hPbView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hPbView.Get(), 0) == ERROR_SUCCESS) {
                rsw::ScopedMsiHandle hRec;
                if (::MsiViewFetch(hPbView.Get(), hRec.Put()) == ERROR_SUCCESS) {
                    const int attr = ::MsiRecordGetInteger(hRec.Get(), 1);
                    progBarNonFlat = ((attr & 0x00010000) != 0);
                }
            }

            // Verify `ProgressDlg` subscribes `ActionData`, `TimeRemaining`, & `EnableRollback` in `EventMapping`,
            // AND `VerifyRemoveDlg.RemoveBtn` sets `[REMOVE] = ALL` so standalone MSI uninstall crosses UAC!
            bool actionDataEvt = false, timeRemEvt = false, removePropEvt = false;
            rsw::ScopedMsiHandle hEmView;
            if (::MsiDatabaseOpenViewW(hVerifyDb.Get(),
                    L"SELECT `Control_`, `Event` FROM `EventMapping` WHERE `Dialog_` = 'ProgressDlg'",
                    hEmView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hEmView.Get(), 0) == ERROR_SUCCESS) {
                rsw::ScopedMsiHandle hRec;
                while (::MsiViewFetch(hEmView.Get(), hRec.Put()) == ERROR_SUCCESS) {
                    wchar_t cBuf[64]{}, eBuf[64]{};
                    DWORD cL = 64, eL = 64;
                    ::MsiRecordGetStringW(hRec.Get(), 1, cBuf, &cL);
                    ::MsiRecordGetStringW(hRec.Get(), 2, eBuf, &eL);
                    if (std::wcscmp(cBuf, L"Cancel") == 0 && std::wcscmp(eBuf, L"EnableRollback") == 0) rollbackCancelEvt = true;
                    if (std::wcscmp(cBuf, L"ActionData") == 0 && std::wcscmp(eBuf, L"ActionData") == 0) actionDataEvt = true;
                    if (std::wcscmp(cBuf, L"TimeRemaining") == 0 && std::wcscmp(eBuf, L"TimeRemaining") == 0) timeRemEvt = true;
                }
            }
            rsw::ScopedMsiHandle hRmView;
            if (::MsiDatabaseOpenViewW(hVerifyDb.Get(),
                    L"SELECT `Argument` FROM `ControlEvent` WHERE `Dialog_` = 'VerifyRemoveDlg' AND `Control_` = 'RemoveBtn' AND `Event` = '[REMOVE]'",
                    hRmView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hRmView.Get(), 0) == ERROR_SUCCESS) {
                rsw::ScopedMsiHandle hRec;
                if (::MsiViewFetch(hRmView.Get(), hRec.Put()) == ERROR_SUCCESS) {
                    wchar_t aBuf[64]{};
                    DWORD aL = 64;
                    ::MsiRecordGetStringW(hRec.Get(), 1, aBuf, &aL);
                    removePropEvt = (std::wcscmp(aBuf, L"ALL") == 0);
                }
            }
            rollbackCancelEvt = rollbackCancelEvt && actionDataEvt && timeRemEvt && removePropEvt &&
                                (v1Sum.productCode != v2Sum.productCode);

            // Mathematical Error 2809 Guard: verify EVERY dialog's `Control_Next` pointers form a closed cycle!
            std::map<std::wstring, std::map<std::wstring, std::wstring>> dlgNext;
            rsw::ScopedMsiHandle hCtView;
            if (::MsiDatabaseOpenViewW(hVerifyDb.Get(),
                    L"SELECT `Dialog_`, `Control`, `Control_Next` FROM `Control`",
                    hCtView.Put()) == ERROR_SUCCESS &&
                ::MsiViewExecute(hCtView.Get(), 0) == ERROR_SUCCESS) {
                rsw::ScopedMsiHandle hRec;
                while (::MsiViewFetch(hCtView.Get(), hRec.Put()) == ERROR_SUCCESS) {
                    wchar_t dBuf[128]{}, cBuf[128]{}, nBuf[128]{};
                    DWORD dL = 128, cL = 128, nL = 128;
                    ::MsiRecordGetStringW(hRec.Get(), 1, dBuf, &dL);
                    ::MsiRecordGetStringW(hRec.Get(), 2, cBuf, &cL);
                    ::MsiRecordGetStringW(hRec.Get(), 3, nBuf, &nL);
                    if (nBuf[0] != L'\0') {
                        dlgNext[dBuf][cBuf] = nBuf;
                    }
                }
            }
            for (const auto& [dlgName, edges] : dlgNext) {
                for (const auto& [startCtrl, firstTarget] : edges) {
                    std::wstring cur = firstTarget;
                    bool returnedToStart = false;
                    for (std::size_t step = 0; step <= edges.size(); ++step) {
                        if (cur == startCtrl) { returnedToStart = true; break; }
                        auto it = edges.find(cur);
                        if (it == edges.end()) break;
                        cur = it->second;
                    }
                    if (!returnedToStart) {
                        allCyclesClosed = false;
                        cycleErr = rsw::WideToUtf8(dlgName + L"." + startCtrl);
                        break;
                    }
                }
                if (!allCyclesClosed) break;
            }
        }
        Check(hasSigTable, "Compiled .msi includes persistent 'Signature' table required by AppSearch (no Error 2228)");
        const std::string uiDiag =
            "uiText=" + std::to_string(uiTextPopulated) +
            " prog95=" + std::to_string(progBarNonFlat) +
            " rbEvt=" + std::to_string(rollbackCancelEvt) +
            " cycleErr=" + cycleErr;
        Check(uiTextPopulated && progBarNonFlat && rollbackCancelEvt && allCyclesClosed,
              "Compiled .msi populates UIText, non-flat ProgressBar (0x10000), EnableRollback Cancel lock, and 100% closed Control_Next cycles (zero Error 2809)",
              uiDiag);

        // 6a. Execute `AppSearch -> LaunchConditions -> CostInitialize -> FileCost -> CostFinalize`
        //     directly inside `System32\msi.dll` (`MsiOpenPackageExW` works even in sandboxed CI!)
        ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
        rsw::ScopedMsiHandle hPkg;
        const UINT openPkgRc = ::MsiOpenPackageExW(
            v1Sum.msiPath.wstring().c_str(), MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE, hPkg.Put());
        UINT appSearchRc = ERROR_INSTALL_FAILURE;
        UINT launchCondRc= ERROR_INSTALL_FAILURE;
        UINT costInitRc  = ERROR_INSTALL_FAILURE;
        UINT fileCostRc  = ERROR_INSTALL_FAILURE;
        UINT costFinRc   = ERROR_INSTALL_FAILURE;
        wchar_t osBuildBuf[64]{};
        DWORD osBuildLen = static_cast<DWORD>(std::size(osBuildBuf));

        if (openPkgRc == ERROR_SUCCESS) {
            appSearchRc  = ::MsiDoActionW(hPkg.Get(), L"AppSearch");
            launchCondRc = ::MsiDoActionW(hPkg.Get(), L"LaunchConditions");
            costInitRc   = ::MsiDoActionW(hPkg.Get(), L"CostInitialize");
            fileCostRc   = ::MsiDoActionW(hPkg.Get(), L"FileCost");
            costFinRc    = ::MsiDoActionW(hPkg.Get(), L"CostFinalize");
            ::MsiGetPropertyW(hPkg.Get(), L"RSW_OSBUILD", osBuildBuf, &osBuildLen);
            hPkg.Reset();
        }
        Check(openPkgRc == ERROR_SUCCESS &&
              appSearchRc == ERROR_SUCCESS &&
              launchCondRc == ERROR_SUCCESS &&
              costInitRc == ERROR_SUCCESS &&
              fileCostRc == ERROR_SUCCESS &&
              costFinRc == ERROR_SUCCESS &&
              std::wcstol(osBuildBuf, nullptr, 10) >= 22000,
              "Live msi.dll AppSearch + LaunchConditions + CostFinalize succeed in-process",
              "open=" + std::to_string(openPkgRc) + " appSearch=" + std::to_string(appSearchRc) +
              " launch=" + std::to_string(launchCondRc) + " costFin=" + std::to_string(costFinRc));

        // 6b. When `msiserver` service RPC is reachable (standard Windows desktop / CI runner; returns
        //     `ERROR_INSTALL_SERVICE_FAILURE = 1601` inside restricted AppContainer sandboxes), execute
        //     a full live install + file verification + clean uninstall round-trip!
        const auto liveTargetDir = testWorkDir / L"live-install-target";
        const std::wstring installProps = L"INSTALLDIR=\"" + liveTargetDir.wstring() + L"\" DESKTOP_SHORTCUT=0 REGISTER_ASSOC=0";
        const UINT instRc = ::MsiInstallProductW(v1Sum.msiPath.wstring().c_str(), installProps.c_str());
        if (instRc == ERROR_INSTALL_SERVICE_FAILURE) {
            Check(true, "Live MsiInstallProductW skipped cleanly inside AppContainer sandbox (1601; in-process msi.dll actions verified above)");
        } else {
            const auto installedExe = liveTargetDir / payloadToUse.filename();
            const bool instOk = (instRc == ERROR_SUCCESS && std::filesystem::exists(installedExe));
            UINT uninstRc = ERROR_SUCCESS;
            if (instRc == ERROR_SUCCESS) {
                uninstRc = ::MsiConfigureProductExW(
                    rsw::Utf8ToWide(v1Sum.productCode).c_str(),
                    INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
            }
            Check(instOk && uninstRc == ERROR_SUCCESS && !std::filesystem::exists(installedExe),
                  "Live MsiInstallProductW + MsiConfigureProductExW(INSTALLSTATE_ABSENT) full round-trip",
                  "inst=" + std::to_string(instRc) + " uninst=" + std::to_string(uninstRc));
        }
    }

    std::filesystem::remove_all(testWorkDir, ec);

    std::cout << "--------------------------------------------------------------------\n"
              << "  Summary: " << gPassed << " passed, " << gFailed << " failed.\n"
              << "====================================================================\n";
    return (gFailed == 0) ? 0 : 1;
}
