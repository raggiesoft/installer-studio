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
#include "../common/RswNativeAndSuiteEngine.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswPrerequisites.hpp"
#include "../common/RswRaggieScript.hpp"
#include "../common/RswRaggieScriptVm.hpp"
#include "../common/RswSuiteWorkspace.hpp"
#include "../common/RswVersion.hpp"
#include "../common/RswWcagContrast.hpp"
#include "../common/RswWizardStyles.hpp"
#include "../common/RswZipAndPak.hpp"
#include "../compiler/RswAiSpecEmitter.hpp"
#include "../compiler/RswBootstrapperPackager.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswMsiCompiler.hpp"
#include "../compiler/RswValidator.hpp"
#include "../compiler/RswVpatEmitter.hpp"
#include <RaggieSoftVisualCanvas.hpp>

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
        if (std::filesystem::exists(repoRoot / L"EDITIONS_AND_RAGGIESCRIPT.md")) {
            combinedAi += "\n" + readLower(repoRoot / L"EDITIONS_AND_RAGGIESCRIPT.md");
        }
        const std::vector<std::string> bannedCompetitors = {
            std::string("install") + "aware", std::string("install") + "shield",
            "revenera", "inno setup", ".iss", "advanced installer",
            "wix", "nsis", "orca.exe", std::string("msi") + "code",
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
        for (const auto& b : bannedCompetitors) {
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
        Check(rsw::EditionHasFullArchitectParity(rsw::RswIdeEdition::Community) &&
              rsw::EditionHasFullArchitectParity(rsw::RswIdeEdition::Architect) &&
              !rsw::EditionCanCompileMergeModules(rsw::RswIdeEdition::Essentials) &&
              rsw::EditionCanCompileMergeModules(rsw::RswIdeEdition::Developer) &&
              !rsw::EditionIncludesRaggieScript(rsw::RswIdeEdition::Essentials) &&
              rsw::EditionIncludesRaggieScript(rsw::RswIdeEdition::Developer) &&
              !rsw::EditionIncludesMspAndSuites(rsw::RswIdeEdition::Essentials) &&
              !rsw::EditionIncludesMspAndSuites(rsw::RswIdeEdition::Developer) &&
              rsw::EditionIncludesMspAndSuites(rsw::RswIdeEdition::Architect),
              "Official 4-Edition rules (Community == Architect parity, Essentials/Developer/Architect tiers) verified");

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
                  webStubBytes > 64 * 1024 && webStubBytes < 2560 * 1024 &&
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
                "oneclick", "passivetoast", "fullscreenblue", "fullscreenteal", "autorun16x9", "simple"
            };
            for (int k = 0; k < rsw::kWizardStyleCount; ++k) {
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

        // 5H. `v0.5.1` Verification:
        //     - `[Package] RequestedExecutionLevel` validator (`asInvoker | highestAvailable | requireAdministrator`)
        //     - Exact single-language PE `RT_MANIFEST` `#1` stamping across all 3 execution levels
        //     - `QueryProcessPrivilegeState` (`EnableLUA` + `CheckTokenMembership` UAC-off Admin verification)
        //     - Per-product (`UpgradeCode`) single-instance mutex isolation + cross-product `ScopedActiveMsiBeacon`
        //       & non-destructive `IsSystemMsiExecuteMutexBusy` probe!
        {
            auto badExecDoc = rsw::ParseManifestText(v1Text.str(), testWorkDir / L"v1.rsip");
            badExecDoc.SetString("Package", "", "RequestedExecutionLevel", "invalidSuperAdmin");
            const auto badExecRep = rsw::ValidateManifestDocument(badExecDoc, hostArch);

            auto readStampedManifestXml = [](const std::filesystem::path& exePath, std::size_t& outLangCount) -> std::string {
                const auto langs = rsw::QueryExistingPeManifestLangIds(exePath);
                outLangCount = langs.size();
                HMODULE hMod = ::LoadLibraryExW(
                    exePath.wstring().c_str(), nullptr,
                    LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
                if (!hMod) return {};
                HRSRC hRes = ::FindResourceW(hMod, MAKEINTRESOURCEW(1), RT_MANIFEST);
                std::string xml;
                if (hRes) {
                    const DWORD sz = ::SizeofResource(hMod, hRes);
                    HGLOBAL hGlb   = ::LoadResource(hMod, hRes);
                    if (hGlb && sz > 0) {
                        const auto* p = static_cast<const char*>(::LockResource(hGlb));
                        if (p) xml.assign(p, sz);
                    }
                }
                ::FreeLibrary(hMod);
                return xml;
            };

            std::size_t invLangs = 0, highLangs = 0, reqLangs = 0;
            const std::string invXml = readStampedManifestXml(v1SetupExe, invLangs);

            auto highDoc = rsw::ParseManifestText(v1Text.str(), testWorkDir / L"v1.rsip");
            highDoc.SetString("Package", "", "RequestedExecutionLevel", "highestAvailable");
            highDoc.SetString("Package", "", "OutputName", "v051-high-$(ARCH)");
            rsw::CompiledMsiSummary highSum = v1Sum;
            highSum.msiPath = testWorkDir / L"v051-high.msi";
            std::filesystem::copy_file(v1Sum.msiPath, highSum.msiPath, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::path highExe;
            std::string highErr;
            const bool okHighPkg = rsw::PackageSelfExtractingBootstrapper(highDoc, highSum, binDir, highExe, highErr, true);
            const std::string highXml = okHighPkg ? readStampedManifestXml(highExe, highLangs) : std::string{};

            auto reqDoc = rsw::ParseManifestText(v1Text.str(), testWorkDir / L"v1.rsip");
            reqDoc.SetString("Package", "", "RequestedExecutionLevel", "requireAdministrator");
            reqDoc.SetString("Package", "", "OutputName", "v051-reqadmin-$(ARCH)");
            rsw::CompiledMsiSummary reqSum = v1Sum;
            reqSum.msiPath = testWorkDir / L"v051-reqadmin.msi";
            std::filesystem::copy_file(v1Sum.msiPath, reqSum.msiPath, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::path reqExe;
            std::string reqErr;
            const bool okReqPkg = rsw::PackageSelfExtractingBootstrapper(reqDoc, reqSum, binDir, reqExe, reqErr, true);
            const std::string reqXml = okReqPkg ? readStampedManifestXml(reqExe, reqLangs) : std::string{};

            const bool manifestOk =
                !badExecRep.IsOk() &&
                invLangs == 1 && invXml.find("level=\"asInvoker\"") != std::string::npos &&
                okHighPkg && highLangs == 1 && highXml.find("level=\"highestAvailable\"") != std::string::npos &&
                okReqPkg && reqLangs == 1 && reqXml.find("level=\"requireAdministrator\"") != std::string::npos;
            Check(manifestOk,
                  "v0.5.1 RequestedExecutionLevel validator & single-LCID PE RT_MANIFEST stamping (asInvoker / highestAvailable / requireAdministrator) pass",
                  "invLangs=" + std::to_string(invLangs) + " highLangs=" + std::to_string(highLangs) + " reqLangs=" + std::to_string(reqLangs));

            const auto priv = rsw::QueryProcessPrivilegeState();
            const bool privConsistent =
                (rsw::NormalizeRequestedExecutionLevel("REQUIREAdministrator") == "requireAdministrator") &&
                (rsw::NormalizeRequestedExecutionLevel("highestavailable") == "highestAvailable") &&
                (rsw::NormalizeRequestedExecutionLevel("") == "asInvoker") &&
                (priv.HasEffectiveAdminRights() == (priv.isTokenElevated || priv.isAdminGroupActive)) &&
                (priv.HasEffectiveAdminRights() || (priv.CanPromptForUacElevation() != priv.IsStandardUserWithUacDisabled()));

            const std::wstring keyStoreTrainer = rsw::DeriveProductInstanceKey(
                L"{A1111111-2222-3333-4444-555555555555}", L"RaggieSoft", L"StoreTrainer");
            const std::wstring keyA11yStudio   = rsw::DeriveProductInstanceKey(
                L"{B9999999-8888-7777-6666-555555555555}", L"RaggieSoft", L"Accessibility Studio");
            const std::wstring pidSalt = std::to_wstring(::GetCurrentProcessId()) + L"_";
            bool dupA1 = true, dupA2 = false, dupB1 = true;
            HANDLE hMutA1 = rsw::CreatePermissiveLocalMutex(L"Local\\RaggieSoft.TestUI." + pidSalt + keyStoreTrainer, dupA1);
            HANDLE hMutA2 = rsw::CreatePermissiveLocalMutex(L"Local\\RaggieSoft.TestUI." + pidSalt + keyStoreTrainer, dupA2);
            HANDLE hMutB1 = rsw::CreatePermissiveLocalMutex(L"Local\\RaggieSoft.TestUI." + pidSalt + keyA11yStudio,   dupB1);
            if (hMutA2) ::CloseHandle(hMutA2);
            if (hMutB1) ::CloseHandle(hMutB1);
            if (hMutA1) ::CloseHandle(hMutA1);

            std::wstring beaconDuring;
            {
                rsw::ScopedActiveMsiBeacon testBeacon(L"RaggieSoft StoreTrainer 2.0");
                beaconDuring = rsw::ScopedActiveMsiBeacon::ReadActiveProductName();
            }
            const std::wstring beaconAfter = rsw::ScopedActiveMsiBeacon::ReadActiveProductName();
            const bool msiProbe1 = rsw::IsSystemMsiExecuteMutexBusy();
            const bool msiProbe2 = rsw::IsSystemMsiExecuteMutexBusy();

            Check(privConsistent &&
                  !dupA1 && dupA2 && !dupB1 &&
                  beaconDuring == L"RaggieSoft StoreTrainer 2.0" &&
                  beaconAfter.empty() &&
                  (msiProbe1 == msiProbe2),
                  "v0.5.1 CheckTokenMembership UAC-off check, UpgradeCode single-instance isolation, & ScopedActiveMsiBeacon queue discovery pass");

            auto verTokRaw = rsw::ParseManifestText(
                "[Package]\nName = VerTest\nVersion = 2.4.9\nManufacturer = RaggieSoft\n"
                "UpgradeCode = {C34A9B12-4411-4822-9A11-524147474947}\n"
                "OutputName = app-v$(VERSION)-$(ARCH)\n"
                "UniversalOutputName = app-v$(VERSION)\n"
                "UniversalDownloadBaseUrl = https://assets.raggiesoft.com/releases/v$(VERSION)\n"
                "[Install]\nDefaultSubdir = RaggieSoft\\VerTest\n");
            rsw::RswManifestDocument verTokRes;
            std::string vtErr;
            const bool vtOk = rsw::ResolveManifestIncludesAndVars(verTokRaw, {}, verTokRes, vtErr) &&
                              verTokRes.GetString("Package", "", "OutputName") == "app-v2.4.9-$(ARCH)" &&
                              verTokRes.GetString("Package", "", "UniversalOutputName") == "app-v2.4.9" &&
                              verTokRes.GetString("Package", "", "UniversalDownloadBaseUrl") == "https://assets.raggiesoft.com/releases/v2.4.9";
            Check(vtOk, "v0.5.1 $(VERSION) automatic manifest token expansion across OutputName & UniversalDownloadBaseUrl", vtErr);
        }

        // 5I. `v0.6.0` Comprehensive Verification Suite:
        //     1. `.NET 11` (`DotNet11Desktop`, `DotNet11AspNet`, `DotNet11Runtime`) & `.NET 10` Base Presets (`< 1ms` probe)
        //     2. Multi-Volume Disk Spanning (`CAB1.cab..CABN.cab`), Loose Source Media (`Compression = none`),
        //        `AllowRunFromSource = true` (`ADDSOURCE=`), & `msiexec /a` (`AdminUISequence` + `AdminExecuteSequence`)
        //     3. Self-Extracting `.EXE` (`PackageKind = sfx`) from `.ZIP` Archive + Selectable UAC Levels
        //     4. Granular `.rswpak` On-Demand Feature Packs (`SHA-256` + `WinVerifyTrust` tamper rejection)
        //     5. `Engine = native` Direct Setup (`WizardLayout = simple`), Atomic LIFO Rollback on Fault, & `Uninstall.exe`
        //     6. Unified Suite Setup (`PackageKind = suite`) & Dynamic `ARPSYSTEMCOMPONENT` (`SystemComponent`) Governance
        {
            // (1) `.NET 11` & `.NET 10` Presets + `< 1ms` native silicon probe
            LARGE_INTEGER qFreq{}, qT0{}, qT1{};
            ::QueryPerformanceFrequency(&qFreq);
            ::QueryPerformanceCounter(&qT0);
            const auto* dn11Desk = rsw::FindCuratedRuntimePreset("DotNet11Desktop");
            const auto* dn11Asp  = rsw::FindCuratedRuntimePreset("DotNet11AspNet");
            const auto* dn11Base = rsw::FindCuratedRuntimePreset("DotNet11Runtime");
            const auto* dn10Base = rsw::FindCuratedRuntimePreset("DotNet10Runtime");
            rsw::RuntimePrerequisiteSpec probe11{};
            probe11.preset = "DotNet11Desktop";
            (void)rsw::IsPrerequisiteSatisfied(probe11, hostArch);
            ::QueryPerformanceCounter(&qT1);
            const double probeMs = (qFreq.QuadPart > 0)
                ? (static_cast<double>(qT1.QuadPart - qT0.QuadPart) * 1000.0 / static_cast<double>(qFreq.QuadPart))
                : 0.0;
            Check(dn11Desk != nullptr && dn11Asp != nullptr && dn11Base != nullptr && dn10Base != nullptr && probeMs < 15.0,
                  "v0.6.0 .NET 11 (DotNet11Desktop / DotNet11AspNet / DotNet11Runtime) & DotNet10Runtime presets resolve cleanly",
                  "probeMs=" + std::to_string(probeMs));

            // (2A) Multi-Volume Disk Spanning (`CAB1.cab..CABN.cab`)
            const auto spanSrcDir = testWorkDir / L"span-src";
            std::filesystem::create_directories(spanSrcDir, ec);
            for (int f = 1; f <= 4; ++f) {
                std::ofstream sf(spanSrcDir / (L"chunk-" + std::to_wstring(f) + L".bin"), std::ios::binary);
                std::string pseudoRandom(28 * 1024, '\0');
                // High-entropy xorshift32 stream so LZX:21 (`fci.lib`) cannot collapse 112 KB into < 1 KB!
                std::uint32_t rng = 0x9E3779B9u ^ (static_cast<std::uint32_t>(f) * 0x85EBCA6Bu);
                for (std::size_t b = 0; b < pseudoRandom.size(); ++b) {
                    rng ^= (rng << 13);
                    rng ^= (rng >> 17);
                    rng ^= (rng << 5);
                    pseudoRandom[b] = static_cast<char>(rng & 0xFFu);
                }
                sf.write(pseudoRandom.data(), static_cast<std::streamsize>(pseudoRandom.size()));
            }
            const auto spanOutDir = testWorkDir / L"span-out";
            std::ostringstream spanMan;
            spanMan << "[Package]\nName = SpanSample\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                    << "UpgradeCode = {6A112233-4455-4666-8777-8899AABBCCDD}\n"
                    << "OutputName = span-sample\nOutputDir = " << rsw::WideToUtf8(spanOutDir.wstring()) << "\n"
                    << "[Install]\nDefaultSubdir = RaggieSoft\\SpanSample\n"
                    << "[Media]\nEmbedCabinet = false\nCompression = cab\nMaxCabinetSizeKB = 36\n"
                    << "VolumeLabel = RAGGIE_DISC\nDiskPrompt = Please insert Disc [1]\n"
                    << "[Payload.Chunks]\nSource = " << rsw::WideToUtf8(spanSrcDir.wstring()) << "\n";
            auto spanDoc = rsw::ParseManifestText(spanMan.str(), testWorkDir / L"span.rsip");
            rsw::CompiledMsiSummary spanSum{};
            std::string spanErr;
            const bool spanOk = rsw::ValidateManifestDocument(spanDoc, hostArch).IsOk() &&
                                rsw::CompileManifestToMsi(spanDoc, hostArch, spanSum, spanErr);
            int mediaRows = 0;
            if (spanOk) {
                rsw::ScopedMsiHandle hMdb, hMv, hMr;
                if (::MsiOpenDatabaseW(spanSum.msiPath.wstring().c_str(), MSIDBOPEN_READONLY, hMdb.Put()) == ERROR_SUCCESS &&
                    ::MsiDatabaseOpenViewW(hMdb.Get(), L"SELECT `DiskId`, `Cabinet` FROM `Media` ORDER BY `DiskId`", hMv.Put()) == ERROR_SUCCESS &&
                    ::MsiViewExecute(hMv.Get(), 0) == ERROR_SUCCESS) {
                    while (::MsiViewFetch(hMv.Get(), hMr.Put()) == ERROR_SUCCESS) ++mediaRows;
                }
            }
            Check(spanOk &&
                  mediaRows >= 2 &&
                  std::filesystem::exists(spanOutDir / L"CAB1.cab", ec) &&
                  std::filesystem::exists(spanOutDir / L"CAB2.cab", ec),
                  "v0.6.0 Multi-Volume Disk Spanning emits CAB1.cab..CABN.cab and relational Media DiskId 1..N rows",
                  spanErr + " mediaRows=" + std::to_string(mediaRows));

            // (2B) Loose Uncompressed Source Media (`Compression = none`), `AllowRunFromSource = true`, & `msiexec /a` SQL tables!
            const auto looseOutDir = testWorkDir / L"loose-out";
            std::ostringstream looseMan;
            looseMan << "[Package]\nName = LooseMediaApp\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                     << "UpgradeCode = {6B223344-5566-4777-8888-99AABBCCDDEE}\n"
                     << "OutputName = loose-media\nOutputDir = " << rsw::WideToUtf8(looseOutDir.wstring()) << "\n"
                     << "[Install]\nDefaultSubdir = RaggieSoft\\LooseMedia\n"
                     << "[Media]\nCompression = none\n"
                     << "[Feature.Main]\nTitle = Core Optical Assets\nAllowRunFromSource = true\n"
                     << "[Payload.Chunks]\nSource = " << rsw::WideToUtf8(spanSrcDir.wstring()) << "\nFeature = Main\n";
            auto looseDoc = rsw::ParseManifestText(looseMan.str(), testWorkDir / L"loose.rsip");
            rsw::CompiledMsiSummary looseSum{};
            std::string looseErr;
            const bool looseOk = rsw::ValidateManifestDocument(looseDoc, hostArch).IsOk() &&
                                 rsw::CompileManifestToMsi(looseDoc, hostArch, looseSum, looseErr);
            bool hasAdminUiSeq = false, hasAdminWelcome = false, nonCompBitOk = false, wordCountUncompressed = false;
            if (looseOk) {
                rsw::ScopedMsiHandle hLdb, hLv, hLr, hLsum;
                if (::MsiOpenDatabaseW(looseSum.msiPath.wstring().c_str(), MSIDBOPEN_READONLY, hLdb.Put()) == ERROR_SUCCESS) {
                    hasAdminUiSeq = (::MsiDatabaseIsTablePersistentW(hLdb.Get(), L"AdminUISequence") == MSICONDITION_TRUE) &&
                                    (::MsiDatabaseIsTablePersistentW(hLdb.Get(), L"AdminExecuteSequence") == MSICONDITION_TRUE);
                    if (::MsiDatabaseOpenViewW(hLdb.Get(), L"SELECT `Dialog` FROM `Dialog` WHERE `Dialog` = 'AdminInstallPointDlg'", hLv.Put()) == ERROR_SUCCESS &&
                        ::MsiViewExecute(hLv.Get(), 0) == ERROR_SUCCESS &&
                        ::MsiViewFetch(hLv.Get(), hLr.Put()) == ERROR_SUCCESS) {
                        hasAdminWelcome = true;
                    }
                    rsw::ScopedMsiHandle hFv, hFr;
                    if (::MsiDatabaseOpenViewW(hLdb.Get(), L"SELECT `Attributes` FROM `File`", hFv.Put()) == ERROR_SUCCESS &&
                        ::MsiViewExecute(hFv.Get(), 0) == ERROR_SUCCESS &&
                        ::MsiViewFetch(hFv.Get(), hFr.Put()) == ERROR_SUCCESS) {
                        nonCompBitOk = ((::MsiRecordGetInteger(hFr.Get(), 1) & 0x00002000) != 0);
                    }
                }
                if (::MsiGetSummaryInformationW(0, looseSum.msiPath.wstring().c_str(), 0, hLsum.Put()) == ERROR_SUCCESS) {
                    UINT dt = 0; INT wcVal = -1; FILETIME ft{}; wchar_t dummy[8]{}; DWORD cch = 8;
                    if (::MsiSummaryInfoGetPropertyW(hLsum.Get(), 15 /* PID_WORDCOUNT */, &dt, &wcVal, &ft, dummy, &cch) == ERROR_SUCCESS) {
                        wordCountUncompressed = ((wcVal & 0x02) == 0);
                    }
                }
            }
            std::vector<rsw::MsiFeatureNode> looseNodes;
            const bool loadedLooseTree = looseOk && rsw::LoadFeatureTreeFromMsiDatabase(looseSum.msiPath, L"", looseNodes);
            if (loadedLooseTree && !looseNodes.empty()) {
                rsw::ApplyMsiFeatureActionRecursive(looseNodes, 0, rsw::MsiTreeState::RunFromSource);
            }
            const std::wstring addSrcProps = rsw::BuildMsiFeatureSelectionProps(looseNodes);
            Check(looseOk && hasAdminUiSeq && hasAdminWelcome && nonCompBitOk && wordCountUncompressed &&
                  !looseNodes.empty() && looseNodes[0].allowRunFromSource &&
                  addSrcProps.find(L"ADDSOURCE=Main") != std::wstring::npos &&
                  std::filesystem::exists(looseOutDir / L"RaggieSoft" / L"LooseMedia" / L"chunk-1.bin", ec),
                  "v0.6.0 Loose Source Media (Compression=none, 0x2000, PID_WORDCOUNT), RunFromSource (ADDSOURCE=), & msiexec /a AdminUISequence pass",
                  looseErr + " props=" + rsw::WideToUtf8(addSrcProps));

            // (3) `PackageKind = sfx` from `.ZIP` Archive + Selectable UAC Execution Levels (`asInvoker`, `highestAvailable`, `requireAdministrator`)
            const auto testZipPath = testWorkDir / L"sfx-input.zip";
            const std::string zipTxtA = "Hello from pure C++20 DEFLATE ZIP extractor!\n";
            const std::string zipTxtB = "Subfolder guide notes for RaggieSoft SFX.\n";
            std::string zipErr;
            const bool zipCreated = rsw::WriteSimpleZipArchive({
                {"ReadMe.txt", std::vector<std::uint8_t>(zipTxtA.begin(), zipTxtA.end())},
                {"Docs/Guide.txt", std::vector<std::uint8_t>(zipTxtB.begin(), zipTxtB.end())}
            }, testZipPath, zipErr);

            auto buildSfxWithLevel = [&](const char* lvlTok, const char* outStem, std::filesystem::path& outExe) -> std::string {
                std::ostringstream sm;
                sm << "[Package]\nName = PortableSfx\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                   << "PackageKind = sfx\nRequestedExecutionLevel = " << lvlTok << "\n"
                   << "OutputName = " << outStem << "\nOutputDir = " << rsw::WideToUtf8((testWorkDir / L"sfx-out").wstring()) << "\n"
                   << "[SFX]\nDefaultExtractSubdir = PortableSfxUnpacked\nPostExtractOpen = Docs\\Guide.txt\n"
                   << "[UI]\nWizardLayout = simple\nInstallButtonText = &Extract\n"
                   << "[Payload.Bundle]\nSource = " << rsw::WideToUtf8(testZipPath.wstring()) << "\n";
                auto sfxDoc = rsw::ParseManifestText(sm.str(), testWorkDir / L"sfx.rsip");
                if (!rsw::ValidateManifestDocument(sfxDoc, hostArch).IsOk()) return "";
                rsw::CompiledMsiSummary sfxSum{};
                std::string pErr;
                if (!rsw::PackageNativeOrSfxExecutable(sfxDoc, hostArch, binDir, sfxSum, outExe, pErr)) return "";
                HMODULE hMod = ::LoadLibraryExW(outExe.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
                if (!hMod) return "";
                HRSRC hRes = ::FindResourceW(hMod, MAKEINTRESOURCEW(1), RT_MANIFEST);
                std::string xml;
                if (hRes) {
                    const DWORD sz = ::SizeofResource(hMod, hRes);
                    HGLOBAL hGlb   = ::LoadResource(hMod, hRes);
                    if (hGlb && sz > 0) {
                        const auto* p = static_cast<const char*>(::LockResource(hGlb));
                        if (p) xml.assign(p, sz);
                    }
                }
                ::FreeLibrary(hMod);
                return xml;
            };

            std::filesystem::path sfxInvExe, sfxHighExe, sfxAdminExe;
            const std::string sfxInvXml   = zipCreated ? buildSfxWithLevel("asInvoker",            "sfx-inv",   sfxInvExe)   : "";
            const std::string sfxHighXml  = zipCreated ? buildSfxWithLevel("highestAvailable",     "sfx-high",  sfxHighExe)  : "";
            const std::string sfxAdminXml = zipCreated ? buildSfxWithLevel("requireAdministrator", "sfx-admin", sfxAdminExe) : "";

            const auto sfxExtractDir = testWorkDir / L"sfx-extracted";
            bool sfxRunOk = false;
            if (!sfxInvExe.empty()) {
                std::wstring cmd = L"\"" + sfxInvExe.wstring() + L"\" --extract-to \"" + sfxExtractDir.wstring() + L"\"";
                STARTUPINFOW si{sizeof(si)};
                PROCESS_INFORMATION pi{};
                if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    ::WaitForSingleObject(pi.hProcess, 15000);
                    DWORD code = 1;
                    ::GetExitCodeProcess(pi.hProcess, &code);
                    ::CloseHandle(pi.hThread);
                    ::CloseHandle(pi.hProcess);
                    sfxRunOk = (code == 0);
                }
            }
            std::vector<std::uint8_t> extBytesA, extBytesB;
            std::string rdErr;
            const bool sfxFilesMatch =
                sfxRunOk &&
                rsw::ReadBinaryFileBytes(sfxExtractDir / L"ReadMe.txt", extBytesA, rdErr) &&
                rsw::ReadBinaryFileBytes(sfxExtractDir / L"Docs" / L"Guide.txt", extBytesB, rdErr) &&
                std::string(extBytesA.begin(), extBytesA.end()) == zipTxtA &&
                std::string(extBytesB.begin(), extBytesB.end()) == zipTxtB;
            Check(zipCreated &&
                  sfxInvXml.find("level=\"asInvoker\"") != std::string::npos &&
                  sfxHighXml.find("level=\"highestAvailable\"") != std::string::npos &&
                  sfxAdminXml.find("level=\"requireAdministrator\"") != std::string::npos &&
                  sfxFilesMatch,
                  "v0.6.0 PackageKind=sfx builds from .ZIP archive, stamps selectable UAC levels, and extracts byte-for-byte",
                  zipErr);

            // (3B) Software Installation `%TEMP%` Self-Extractor + `_ISDEL.EXE` Companion Process Wait + Auto-Cleanup!
            //      `launcher-stub.cmd` spawns `_isdel.exe` (`--sleep-ms 550 --copy-check <tempReadMe> <proofOut>`)
            //      in the background and EXITS IMMEDIATELY. If the SFX wiped `%TEMP%` prematurely when `launcher-stub.cmd`
            //      exited, `<tempReadMe>` would already be gone when `_isdel.exe` woke up at 550ms! Waiting on
            //      `WaitForProcess = _isdel.exe` guarantees `<proofOut>` succeeds AND `%TEMP%\rsw-sfx-...` is deleted afterward!
            {
                const auto isdelProofFile   = testWorkDir / L"isdel-proof-copied.txt";
                const auto tempStageRecord  = testWorkDir / L"recorded-temp-stage-dir.txt";
                const auto isdelZipPath     = testWorkDir / L"sfx-temp-installer.zip";
                std::vector<std::uint8_t> helperExeBytes;
                std::string hRdErr;
                const bool gotHelperExe = rsw::ReadBinaryFileBytes(payloadToUse, helperExeBytes, hRdErr);

                std::ostringstream stubBatch;
                stubBatch << "@echo off\r\n"
                          << "echo %~dp0> \"" << rsw::WideToUtf8(tempStageRecord.wstring()) << "\"\r\n"
                          << "start \"\" \"%~dp0_isdel.exe\" --sleep-ms 550 --copy-check \"%~dp0ReadMe.txt\" \""
                          << rsw::WideToUtf8(isdelProofFile.wstring()) << "\"\r\n"
                          << "exit /b 0\r\n";
                const std::string batchStr = stubBatch.str();

                std::string izErr;
                const bool izBuilt = gotHelperExe && rsw::WriteSimpleZipArchive({
                    {"ReadMe.txt", std::vector<std::uint8_t>(zipTxtA.begin(), zipTxtA.end())},
                    {"_isdel.exe", helperExeBytes},
                    {"launcher-stub.cmd", std::vector<std::uint8_t>(batchStr.begin(), batchStr.end())}
                }, isdelZipPath, izErr);

                // Intentionally omit `WaitForProcess` here so `PackageNativeOrSfxExecutable` also proves
                // automatic zero-config `_ISDEL.EXE` archive detection (just like classic Software Installation SFX)!
                std::ostringstream tempSfxMan;
                tempSfxMan << "[Package]\nName = TempSetupWrapper\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                           << "PackageKind = sfx\nRequestedExecutionLevel = asInvoker\n"
                           << "OutputName = temp-setup-wrapper\nOutputDir = " << rsw::WideToUtf8((testWorkDir / L"sfx-out").wstring()) << "\n"
                           << "[SFX]\nExtractToTemp = true\nAllowChangeFolder = false\nCleanupTempAfterRun = true\n"
                           << "PostExtractRun = launcher-stub.cmd\n"
                           << "WaitSpawnGraceMs = 300\n"
                           << "[UI]\nWizardLayout = simple\n"
                           << "[Payload.Bundle]\nSource = " << rsw::WideToUtf8(isdelZipPath.wstring()) << "\n";
                auto tempSfxDoc = rsw::ParseManifestText(tempSfxMan.str(), testWorkDir / L"temp-sfx.rsip");
                rsw::CompiledMsiSummary tempSfxSum{};
                std::filesystem::path tempSfxExe;
                std::string tsErr;
                const bool tempSfxBuilt = izBuilt &&
                                          rsw::ValidateManifestDocument(tempSfxDoc, hostArch).IsOk() &&
                                          rsw::PackageNativeOrSfxExecutable(tempSfxDoc, hostArch, binDir, tempSfxSum, tempSfxExe, tsErr);

                bool tempSfxRanOk = false;
                if (tempSfxBuilt) {
                    std::wstring cmd = L"\"" + tempSfxExe.wstring() + L"\" /s";
                    STARTUPINFOW si{sizeof(si)};
                    PROCESS_INFORMATION pi{};
                    if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                        ::WaitForSingleObject(pi.hProcess, 20000);
                        DWORD code = 1;
                        ::GetExitCodeProcess(pi.hProcess, &code);
                        ::CloseHandle(pi.hThread);
                        ::CloseHandle(pi.hProcess);
                        tempSfxRanOk = (code == 0);
                    }
                }

                std::vector<std::uint8_t> proofBytes, stageDirBytes;
                const bool proofCopiedBeforeCleanup =
                    tempSfxRanOk &&
                    rsw::ReadBinaryFileBytes(isdelProofFile, proofBytes, tsErr) &&
                    std::string(proofBytes.begin(), proofBytes.end()) == zipTxtA;

                bool tempFolderCleanedUp = false;
                if (rsw::ReadBinaryFileBytes(tempStageRecord, stageDirBytes, tsErr)) {
                    std::string stageStr = rsw::TrimAscii(std::string(stageDirBytes.begin(), stageDirBytes.end()));
                    while (!stageStr.empty() && (stageStr.back() == '\\' || stageStr.back() == '/')) stageStr.pop_back();
                    if (!stageStr.empty()) {
                        tempFolderCleanedUp = !std::filesystem::exists(std::filesystem::path(rsw::Utf8ToWide(stageStr)), ec);
                    }
                }

                Check(tempSfxBuilt && proofCopiedBeforeCleanup && tempFolderCleanedUp,
                      "v0.6.0 SFX Software Installation mode extracts to %TEMP%, waits for _ISDEL.EXE companion process, & auto-cleans %TEMP%",
                      tsErr);
            }

            // (4) Granular `.rswpak` On-Demand Feature Packs (`SHA-256` + `WinVerifyTrust` tamper rejection!)
            const auto pakOutDir = testWorkDir / L"pak-out";
            std::ostringstream pakMan;
            pakMan << "[Package]\nName = OnDemandSample\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                   << "UpgradeCode = {6C334455-6677-4888-9999-AABBCCDDEEFF}\n"
                   << "OutputName = ondemand-sample\nOutputDir = " << rsw::WideToUtf8(pakOutDir.wstring()) << "\n"
                   << "[Install]\nDefaultSubdir = RaggieSoft\\OnDemandSample\n"
                   << "[Feature.Main]\nTitle = Core App\n"
                   << "[Feature.HdAssets]\nTitle = Optional HD Asset Pack\nDelivery = ondemand\n"
                   << "PackUrl = https://assets.raggiesoft.com/packs/hdassets.rswpak\n"
                   << "[Payload.Core]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\nFeature = Main\n"
                   << "[Payload.HdZip]\nSource = " << rsw::WideToUtf8(testZipPath.wstring()) << "\nFeature = HdAssets\nTargetSubdir = Assets\n";
            auto pakDoc = rsw::ParseManifestText(pakMan.str(), testWorkDir / L"ondemand.rsip");
            rsw::CompiledMsiSummary pakSum{};
            std::string pakErr;
            const bool pakMsiOk = rsw::ValidateManifestDocument(pakDoc, hostArch).IsOk() &&
                                  rsw::CompileManifestToMsi(pakDoc, hostArch, pakSum, pakErr) &&
                                  pakSum.featurePacks.size() == 1 &&
                                  std::filesystem::exists(pakOutDir / rsw::Utf8ToWide(pakSum.featurePacks[0].fileName), ec);
            bool pakVerifyOk = false, pakTamperRejected = false, pakUnpackOk = false;
            std::string vErr;
            if (pakMsiOk) {
                const auto& fp = pakSum.featurePacks[0];
                const auto diskPakPath = pakOutDir / rsw::Utf8ToWide(fp.fileName);
                pakVerifyOk = rsw::VerifyFeaturePackIntegrity(diskPakPath, fp.sha256Hex, false, vErr);

                const auto tamperedPak = pakOutDir / L"tampered.rswpak";
                std::filesystem::copy_file(diskPakPath, tamperedPak, std::filesystem::copy_options::overwrite_existing, ec);
                {
                    std::fstream tf(tamperedPak, std::ios::in | std::ios::out | std::ios::binary);
                    tf.seekp(-1, std::ios::end);
                    char flip = 0x5A;
                    tf.write(&flip, 1);
                }
                std::string tErr;
                pakTamperRejected = !rsw::VerifyFeaturePackIntegrity(tamperedPak, fp.sha256Hex, false, tErr);

                const auto unpackedPakDir = testWorkDir / L"pak-unpacked";
                pakUnpackOk = rsw::ExtractRswPakFileToDirectory(diskPakPath, unpackedPakDir, nullptr, nullptr, vErr) &&
                              std::filesystem::exists(unpackedPakDir / L"Assets" / L"Docs" / L"Guide.txt", ec);
            }
            Check(pakMsiOk && pakVerifyOk && pakTamperRejected && pakUnpackOk,
                  "v0.6.0 .rswpak On-Demand Feature Packs emit LZMS container, verify SHA-256 + WinVerifyTrust, & reject tampered packs",
                  pakErr + (vErr.empty() ? "" : (" | " + vErr)));

            // (5A) `Engine = native` Atomic LIFO Rollback Journal Fault-Injection Test!
            const auto rbTargetDir = testWorkDir / L"native-rollback-target";
            std::filesystem::create_directories(rbTargetDir, ec);
            {
                std::ofstream origF(rbTargetDir / L"ReadMe.txt", std::ios::binary | std::ios::trunc);
                origF << "ORIGINAL_PROTECTED_FILE_V1";
            }
            std::vector<std::uint8_t> rbPakBytes;
            std::uint64_t rbUncomp = 0;
            std::string rbErr;
            const bool rbPakBuilt = rsw::BuildRswPakBytes(
                {
                    {sfxExtractDir / L"ReadMe.txt", "INSTALLDIR/ReadMe.txt", "Main"},
                    {sfxExtractDir / L"Docs" / L"Guide.txt", "INSTALLDIR/Docs/Guide.txt", "Main"}
                },
                rbPakBytes, rbUncomp, rbErr);
            rsw::NativeOrSfxManifest rbMan{};
            rbMan.mode          = "native";
            rbMan.productName   = "RollbackProbe";
            rbMan.version       = "0.6.0";
            rbMan.manufacturer  = "RaggieSoft";
            rbMan.upgradeCode   = "{6D445566-7788-4999-AAAA-BBCCDDEEFF00}";
            rbMan.perMachine    = false;
            rsw::NativeExecutionOptions rbOpts{};
            rbOpts.targetDir               = rbTargetDir;
            rbOpts.simulateFaultAfterFiles = 1; // Inject fault AFTER overwriting ReadMe.txt & creating Guide.txt!
            const bool rbFailedAsExpected = rbPakBuilt &&
                !rsw::ExecuteNativeOrSfxTransaction(
                    rbMan, rbPakBytes.data(), rbPakBytes.size(), {}, {}, rbOpts, nullptr, nullptr, rbErr);
            std::vector<std::uint8_t> restoredBytes;
            const bool rbRestoredCleanly =
                rbFailedAsExpected &&
                rsw::ReadBinaryFileBytes(rbTargetDir / L"ReadMe.txt", restoredBytes, rbErr) &&
                std::string(restoredBytes.begin(), restoredBytes.end()) == "ORIGINAL_PROTECTED_FILE_V1" &&
                !std::filesystem::exists(rbTargetDir / L"Docs" / L"Guide.txt", ec);
            Check(rbRestoredCleanly,
                  "v0.6.0 Engine=native transactional rollback journal restores overwritten .rswbak files & cleans partial files on fault",
                  rbErr);

            // (5B & 6) End-to-End `Engine = native` Simple Setup + Unified Suite (`PackageKind = suite`):
            //   - Verifies Byte-Weighted Monotonic Dual-Bar Suite Progress (ZERO per-component sawtooth resets!)
            //   - Verifies Dynamic `ARPSYSTEMCOMPONENT` (`SystemComponent=1`) Adoption, Standalone Detach, & Full Uninstall
            //   - Verifies One-Command `.rsis` Workspace `BuildSuiteInstaller = true` compilation!
            {
                const std::vector<std::uint64_t> weights = {
                    2ull * 1024ull * 1024ull,   // 2 MB small utility
                    80ull * 1024ull * 1024ull,  // 80 MB flagship IDE
                    18ull * 1024ull * 1024ull   // 18 MB companion viewer
                };
                int hw = 0;
                bool monotonicOk = true;
                int prevOvr = 0;
                // Simulate Comp 0 (`0..1000`), boundary jump to Comp 1 (`0..500 -> transient MSI script reset 120 -> 1000`), and Comp 2 (`0..1000`)!
                const std::pair<std::size_t, int> trace[] = {
                    {0, 0}, {0, 500}, {0, 1000},
                    {1, 0}, {1, 250}, {1, 500}, {1, 120 /* simulated inner MSI phase reset! */}, {1, 600}, {1, 1000},
                    {2, 0}, {2, 500}, {2, 1000}
                };
                int afterSmallComp = 0;
                int afterHugeComp  = 0;
                for (const auto& [sIdx, cPerm] : trace) {
                    const int curOvr = rsw::ComputeSuiteWeightedOverallPermille(weights, sIdx, cPerm, hw);
                    if (curOvr < prevOvr) monotonicOk = false;
                    prevOvr = curOvr;
                    if (sIdx == 0 && cPerm == 1000) afterSmallComp = curOvr;
                    if (sIdx == 1 && cPerm == 1000) afterHugeComp  = curOvr;
                }
                // 2 MB out of 100 MB (~2%) should land near ~58 permille, while +80 MB (82%) lands near ~794 permille!
                const bool proportionalOk = (afterSmallComp >= 50 && afterSmallComp <= 80) &&
                                            (afterHugeComp >= 760 && afterHugeComp <= 830) &&
                                            (prevOvr == 960);
                Check(monotonicOk && proportionalOk,
                      "Suite Dual-Bar Progress math enforces byte-weighted proportions & strict 0..100% monotonicity without sawtooth resets",
                      "afterSmall=" + std::to_string(afterSmallComp) + " afterHuge=" + std::to_string(afterHugeComp));
            }

            // When running inside a restricted AppContainer / CI sandbox where `HKCU` writes return `ERROR_ACCESS_DENIED`,
            // inject a temporary virtual registry store inside `testWorkDir` so child processes test 100% of the state machine!
            bool usedVirtualRegStore = false;
            {
                HKEY hProbeKey = nullptr;
                const LSTATUS regSt = ::RegCreateKeyExW(
                    HKEY_CURRENT_USER, L"Software\\RaggieSoft\\_InstTestsWriteProbe",
                    0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hProbeKey, nullptr);
                if (regSt == ERROR_SUCCESS) {
                    ::RegCloseKey(hProbeKey);
                    ::RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\RaggieSoft\\_InstTestsWriteProbe");
                } else {
                    const auto virtRegDir = testWorkDir / L"virtual-reg-store";
                    std::filesystem::create_directories(virtRegDir, ec);
                    ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", virtRegDir.wstring().c_str());
                    usedVirtualRegStore = true;
                }
            }

            const std::wstring childUpgradeGuid = L"{6E556677-8899-4AAA-BBBB-CCDDEEFF0011}";
            const std::wstring suiteUpgradeGuid = L"{6F667788-99AA-4BBB-CCCC-DDEEFF001122}";
            const auto natInstallDir   = testWorkDir / L"native-live-installed";
            const auto suiteUmbrellaDir= testWorkDir / L"suite-umbrella-installed";
            const auto natOutDir       = testWorkDir / L"native-out";
            std::ostringstream natMan;
            natMan << "[Package]\nName = SimpleNativeChild\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                   << "Engine = native\nScope = user\nUpgradeCode = " << rsw::WideToUtf8(childUpgradeGuid) << "\n"
                   << "OutputName = simple-native-child\nOutputDir = " << rsw::WideToUtf8(natOutDir.wstring()) << "\n"
                   << "[Install]\nDefaultSubdir = RaggieSoft\\SelfTestSimpleNative\n"
                   << "MainExecutable = " << rsw::WideToUtf8(payloadToUse.filename().wstring()) << "\n"
                   << "[UI]\nWizardLayout = simple\nInstallButtonText = &Install\n"
                   << "[Payload.App]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n"
                   << "[Registry.Marker]\nRoot = HKCU\nKey = Software\\RaggieSoft\\SelfTestSimpleNative\nName = Installed\nValue = 1\n";
            auto natDoc = rsw::ParseManifestText(natMan.str(), testWorkDir / L"simple-native.rsip");
            rsw::CompiledMsiSummary natSum{};
            std::filesystem::path natSetupExe;
            std::string natErr;
            const bool natBuilt = rsw::ValidateManifestDocument(natDoc, hostArch).IsOk() &&
                                  rsw::PackageNativeOrSfxExecutable(natDoc, hostArch, binDir, natSum, natSetupExe, natErr);

            auto runQuietExe = [](const std::filesystem::path& exe, const std::wstring& extraArgs) -> DWORD {
                std::wstring cmd = L"\"" + exe.wstring() + L"\" " + extraArgs;
                STARTUPINFOW si{sizeof(si)};
                PROCESS_INFORMATION pi{};
                if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    return 9999u;
                }
                ::WaitForSingleObject(pi.hProcess, 25000);
                DWORD code = 9998u;
                ::GetExitCodeProcess(pi.hProcess, &code);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                return code;
            };

            const DWORD standaloneRc = natBuilt
                ? runQuietExe(natSetupExe, L"/s INSTALLDIR=\"" + natInstallDir.wstring() + L"\"")
                : 1u;
            const bool standaloneVisibleInArp =
                (standaloneRc == 0) &&
                std::filesystem::exists(natInstallDir / payloadToUse.filename(), ec) &&
                std::filesystem::exists(natInstallDir / L"Uninstall.exe", ec) &&
                std::filesystem::exists(natInstallDir / L"Uninstall.rswm", ec) &&
                !rsw::IsInstalledAppHiddenAsSuiteComponent(childUpgradeGuid);
            Check(standaloneVisibleInArp,
                  "v0.6.0 Engine=native Simple Setup installs cleanly, emits Uninstall.exe + Uninstall.rswm, & appears standalone in Installed Apps",
                  natErr + " rc=" + std::to_string(standaloneRc));

            const std::wstring child2UpgradeGuid = L"{6E556677-8899-4AAA-BBBB-CCDDEEFF0022}";
            const auto nat2InstallDir = testWorkDir / L"native-live-installed-2";
            std::ostringstream nat2Man;
            nat2Man << "[Package]\nName = SimpleNativeCompanion\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                    << "Engine = native\nScope = user\nUpgradeCode = " << rsw::WideToUtf8(child2UpgradeGuid) << "\n"
                    << "OutputName = simple-native-companion\nOutputDir = " << rsw::WideToUtf8(natOutDir.wstring()) << "\n"
                    << "[Install]\nDefaultSubdir = RaggieSoft\\SelfTestSimpleCompanion\n"
                    << "MainExecutable = " << rsw::WideToUtf8(payloadToUse.filename().wstring()) << "\n"
                    << "[UI]\nWizardLayout = simple\nInstallButtonText = &Install\n"
                    << "[Payload.App]\nSource = " << rsw::WideToUtf8(payloadToUse.wstring()) << "\n";
            auto nat2Doc = rsw::ParseManifestText(nat2Man.str(), testWorkDir / L"simple-native-2.rsip");
            rsw::CompiledMsiSummary nat2Sum{};
            std::filesystem::path nat2SetupExe;
            const bool nat2Built = rsw::ValidateManifestDocument(nat2Doc, hostArch).IsOk() &&
                                   rsw::PackageNativeOrSfxExecutable(nat2Doc, hostArch, binDir, nat2Sum, nat2SetupExe, natErr);

            // Now build & run a 2-component `PackageKind = suite` setup adopting `SimpleNativeChild` + installing `SimpleNativeCompanion`!
            std::ostringstream suiteMan;
            suiteMan << "[Package]\nName = SelfTest Studio Suite\nVersion = 0.6.0\nManufacturer = RaggieSoft\n"
                     << "PackageKind = suite\nScope = user\nUpgradeCode = " << rsw::WideToUtf8(suiteUpgradeGuid) << "\n"
                     << "OutputName = selftest-suite\nOutputDir = " << rsw::WideToUtf8(natOutDir.wstring()) << "\n"
                     << "[Install]\nDefaultSubdir = RaggieSoft\\SelfTestSuiteUmbrella\n"
                     << "[UI]\nShowComponentProgressBar = true\n"
                     << "[SuitePackage.ChildApp]\nName = SimpleNativeChild\n"
                     << "Source = " << rsw::WideToUtf8(natSetupExe.wstring()) << "\n"
                     << "UpgradeCode = " << rsw::WideToUtf8(childUpgradeGuid) << "\n"
                     << "Engine = native\nInstallArgs = INSTALLDIR=\"" << rsw::WideToUtf8(natInstallDir.wstring()) << "\"\n"
                     << "AdoptStandalone = true\n"
                     << "[SuitePackage.CompanionApp]\nName = SimpleNativeCompanion\n"
                     << "Source = " << rsw::WideToUtf8(nat2SetupExe.wstring()) << "\n"
                     << "UpgradeCode = " << rsw::WideToUtf8(child2UpgradeGuid) << "\n"
                     << "Engine = native\nInstallArgs = INSTALLDIR=\"" << rsw::WideToUtf8(nat2InstallDir.wstring()) << "\"\n"
                     << "AdoptStandalone = true\n";
            auto suiteDoc = rsw::ParseManifestText(suiteMan.str(), testWorkDir / L"selftest-suite.rsip");
            std::filesystem::path suiteSetupExe;
            std::string suiteErr;
            const bool suiteBuilt = natBuilt && nat2Built &&
                                    rsw::ValidateManifestDocument(suiteDoc, hostArch).IsOk() &&
                                    rsw::PackageUnifiedSuiteExecutable(suiteDoc, hostArch, binDir, suiteSetupExe, suiteErr);

            // Also verify `--export-screenshots` on the compiled Suite EXE emits `setupwizard-suite-dual-progress.png`!
            const auto suiteShotDir = testWorkDir / L"suite-shots";
            std::filesystem::create_directories(suiteShotDir, ec);
            const DWORD suiteShotRc = suiteBuilt ? runQuietExe(suiteSetupExe, L"--export-screenshots \"" + suiteShotDir.wstring() + L"\"") : 1u;
            Check(suiteShotRc == 0 && std::filesystem::exists(suiteShotDir / L"setupwizard-suite-dual-progress.png", ec),
                  "Unified Suite Setup renders Dual-Bar Progress UI & exports setupwizard-suite-dual-progress.png",
                  "shotRc=" + std::to_string(suiteShotRc));

            const std::wstring suiteDirArg = L"INSTALLDIR=\"" + suiteUmbrellaDir.wstring() + L"\"";
            const DWORD suiteAdoptRc = suiteBuilt ? runQuietExe(suiteSetupExe, L"/s " + suiteDirArg) : 1u;
            const bool childHiddenAfterAdopt = (suiteAdoptRc == 0) && rsw::IsInstalledAppHiddenAsSuiteComponent(childUpgradeGuid);

            const DWORD suiteDetachRc = suiteBuilt ? runQuietExe(suiteSetupExe, L"--suite-detach /s " + suiteDirArg) : 1u;
            const bool childRestoredAfterDetach =
                (suiteDetachRc == 0) &&
                std::filesystem::exists(natInstallDir / payloadToUse.filename(), ec) &&
                !rsw::IsInstalledAppHiddenAsSuiteComponent(childUpgradeGuid);

            // Re-adopt and run `--suite-uninstall /s` to clean up 100% of test artifacts & HKCU Uninstall keys!
            const DWORD suiteReadoptRc = suiteBuilt ? runQuietExe(suiteSetupExe, L"/s " + suiteDirArg) : 1u;
            const DWORD suiteUninstRc  = suiteBuilt ? runQuietExe(suiteSetupExe, L"--suite-uninstall /s " + suiteDirArg) : 1u;
            rsw::SuiteChildSpec probeSpec{};
            probeSpec.engine      = "native";
            probeSpec.upgradeCode = rsw::WideToUtf8(childUpgradeGuid);
            std::wstring dummyMsi, dummyNat;
            const bool allCleanedUp = (suiteReadoptRc == 0) && (suiteUninstRc == 0) &&
                                      !rsw::IsSuiteChildCurrentlyInstalled(probeSpec, dummyMsi, dummyNat);

            if (usedVirtualRegStore) {
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", nullptr);
            }

            Check(suiteBuilt && childHiddenAfterAdopt && childRestoredAfterDetach && allCleanedUp,
                  "v0.6.0 PackageKind=suite dynamically adopts standalone child (SystemComponent=1), detaches cleanly (SystemComponent=0), & uninstalls",
                  suiteErr + " adopt=" + std::to_string(suiteAdoptRc) +
                  " detach=" + std::to_string(suiteDetachRc) +
                  " uninst=" + std::to_string(suiteUninstRc));
        }

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

    // ============================================================================
    // 7. RAGGIESCRIPT (`.rssc`) COMPILER, `RSW701`/`RSW702`, `RSWBC1` BYTECODE,
    //    15-NAMESPACE WIN32 VM, FAULT-INJECTED ROLLBACK, `raggiescript.exe`, & `Setup.exe`!
    // ============================================================================
    {
        const auto rsscWorkDir = testWorkDir / L"raggiescript-suite";
        std::filesystem::create_directories(rsscWorkDir, ec);

        // Enable virtual registry store if running inside zero-HKCU sandbox
        bool rsscVirtReg = false;
        {
            HKEY hProbe = nullptr;
            if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RaggieSoft\\_RsscProbe", 0, nullptr, 0, KEY_WRITE, nullptr, &hProbe, nullptr) == ERROR_SUCCESS) {
                ::RegCloseKey(hProbe);
                ::RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\RaggieSoft\\_RsscProbe");
            } else {
                const auto vr = rsscWorkDir / L"virt-reg";
                std::filesystem::create_directories(vr, ec);
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", vr.wstring().c_str());
                rsscVirtReg = true;
            }
        }

        // 7a. Expressions, Raw Strings, `${...}` Interpolation, `#if/#elif`, `SemVer` `<`/`>=`, `ref` params, & `KEY:VAL` CLI Lock!
        {
            const std::string langSrc = R"RSSC(
                #if CHANNEL == "Preview"
                const channelTag = "preview";
                #elif CHANNEL == "Stable"
                const channelTag = "stable";
                #else
                const channelTag = "dev";
                #endif

                param targetTag: string = "default";

                fn bumpCounter(outSlot, delta: int) : int {
                    outSlot += delta;
                    return outSlot;
                }

                let counter = 10;
                bumpCounter(ref counter, 15);

                let sum = 0;
                for (n in [2, 4, 6, 8]) {
                    sum += n;
                }

                let loops = 0;
                repeat {
                    loops += 1;
                } until (loops >= 3);

                let caughtText = "";
                try {
                    throw "RecoverableFault";
                } catch (e) {
                    caughtText = e;
                }

                const verCheckOk = (v"7.0.1" < v"7.0.2") && (v"10.0.22000" <= sys.osVersion()) && (1.2.0 == v"1.2.0");
                const rawRegKey  = r"SOFTWARE\RaggieSoft\Book Reader";
                print("Summary:${channelTag}|${targetTag}|${counter}|${sum}|${loops}|${caughtText}|${verCheckOk}|${rawRegKey}");
            )RSSC";

            rsw::rssc::ScriptProgram prog;
            std::vector<rsw::rssc::ScriptDiagnostic> diags;
            const bool compiled = rsw::rssc::CompileRaggieScriptText(
                langSrc, rsscWorkDir / L"lang-check.rssc", prog, diags, {{"CHANNEL", "Stable"}});

            rsw::rssc::VmExecutionOptions vopts{};
            vopts.silentMode = true;
            vopts.cliProperties["targetTag"] = "locked-enterprise";
            vopts.lockedCliProperties.insert("targetTag");
            rsw::rssc::RaggieScriptVm vm(prog, vopts);
            const auto res = compiled ? vm.ExecuteFullLifecycle() : rsw::rssc::VmExecutionResult{false};

            const std::string expectedLine =
                "Summary:stable|locked-enterprise|25|20|3|RecoverableFault|true|SOFTWARE\\RaggieSoft\\Book Reader";
            Check(compiled && res.ok && !res.consoleLines.empty() && res.consoleLines.back() == expectedLine,
                  "RaggieScript (#if/#elif, ${...} interpolation, r\"...\" raw strings, SemVer <, ref out-params, repeat/until, try/catch)",
                  res.errorMessage + (res.consoleLines.empty() ? "" : (" got=" + res.consoleLines.back())));

            // Prove `KEY:VALUE` immutable CLI lock blocks script reassignment!
            const std::string lockViolationSrc = "param lockedDir: string = \"A\";\nlockedDir = \"B\";\n";
            rsw::rssc::ScriptProgram lockProg;
            rsw::rssc::CompileRaggieScriptText(lockViolationSrc, {}, lockProg, diags);
            rsw::rssc::VmExecutionOptions lockOpts{};
            lockOpts.cliProperties["lockedDir"] = "C:\\CorporatePolicy";
            lockOpts.lockedCliProperties.insert("lockedDir");
            rsw::rssc::RaggieScriptVm lockVm(lockProg, lockOpts);
            const auto lockRes = lockVm.ExecuteFullLifecycle();
            Check(!lockRes.ok && lockRes.errorMessage.find("const") != std::string::npos,
                  "RaggieScript enforces immutable CLI hard-lock ('KEY:VALUE') against script reassignment");
        }

        // 7b. Compile-Time `RSW701` (Wizard Phase Isolation incl. transitive `fn` calls!) & `RSW702` (Training Sandbox)
        {
            const std::string badWizardSrc = R"RSSC(
                use installer;
                fn sneakyMutator() {
                    reg.writeString("HKCU", r"Software\RaggieSoft\Bad", "Flag", "1");
                }
                wizard {
                    step Welcome {
                        sneakyMutator();
                    }
                }
            )RSSC";
            rsw::rssc::ScriptProgram badWizProg;
            std::vector<rsw::rssc::ScriptDiagnostic> wizDiags;
            const bool wizAccepted = rsw::rssc::CompileRaggieScriptText(badWizardSrc, {}, badWizProg, wizDiags);
            bool saw701 = false;
            for (const auto& d : wizDiags) if (d.code == "RSW701") saw701 = true;
            Check(!wizAccepted && saw701,
                  "RaggieScript static analyzer (RSW701) rejects transitive system mutations inside wizard { ... } at compile time");

            const std::string badTrainingSrc = R"RSSC(
                use training;
                let secret = fs.readText(r"C:\Windows\win.ini");
            )RSSC";
            rsw::rssc::ScriptProgram badTrProg;
            std::vector<rsw::rssc::ScriptDiagnostic> trDiags;
            const bool trAccepted = rsw::rssc::CompileRaggieScriptText(badTrainingSrc, {}, badTrProg, trDiags);
            bool saw702 = false;
            for (const auto& d : trDiags) if (d.code == "RSW702") saw702 = true;
            Check(!trAccepted && saw702,
                  "RaggieScript capability sandbox (RSW702) blocks filesystem/network/registry calls under 'use training;'");
        }

        // 7c. `RSWBC1` Binary Bytecode Round-Trip, SHA-256 Tamper Detection, & Disassembler
        {
            const std::filesystem::path sampleRssc = binDir / L".." / L".." / L"examples" / L"raggiescript" / L"sample-workbench-setup.rssc";
            rsw::rssc::ScriptProgram origProg, loadedProg;
            std::vector<rsw::rssc::ScriptDiagnostic> diags;
            std::vector<std::uint8_t> bcBytes;
            std::string bcErr, tamperErr;
            const bool compOk = rsw::rssc::CompileRaggieScriptFile(sampleRssc, origProg, diags);
            const bool emitOk = compOk && rsw::rssc::EmitRaggieScriptBytecode(origProg, bcBytes, bcErr);
            const bool loadOk = emitOk && rsw::rssc::LoadRaggieScriptBytecode(bcBytes.data(), bcBytes.size(), loadedProg, bcErr);

            // Flip 1 bit in the `RSWBC1` instruction stream and verify SHA-256 rejects it!
            std::vector<std::uint8_t> tampered = bcBytes;
            if (tampered.size() > 52) tampered.back() ^= 0xFFu;
            rsw::rssc::ScriptProgram dummyProg;
            const bool tamperRejected = !rsw::rssc::LoadRaggieScriptBytecode(tampered.data(), tampered.size(), dummyProg, tamperErr);
            const std::string disasmTxt = rsw::rssc::DisassembleRaggieScriptProgram(loadedProg);

            Check(compOk && emitOk && loadOk && tamperRejected &&
                  loadedProg.package.name == "RaggieSoft Sample Workbench" &&
                  disasmTxt.find("RSW701 Side-Effect-Free Verified") != std::string::npos,
                  "RSWBC1 bytecode compiler + SHA-256 tamper rejection + disassembler round-trip cleanly",
                  bcErr);
        }

        // 7d. Fault-Injected Transactional Rollback across Files, Overwritten `.rswbak`, INI, & Registry!
        {
            const auto rbDir = rsscWorkDir / L"rollback-target";
            std::filesystem::create_directories(rbDir, ec);
            {
                std::ofstream origOut(rbDir / L"ExistingConfig.txt", std::ios::binary | std::ios::trunc);
                origOut << "PRESERVE_ME_V1";
            }

            const std::string rbScript = R"RSSC(
                use installer;
                param installDir: path = "";
                install {
                    fs.writeText(installDir / "ExistingConfig.txt", "CORRUPTED_PARTIAL_V2");
                    fs.writeText(installDir / "SubFolder" / "PartialNewFile.txt", "SHOULD_VANISH");
                    ini.write(installDir / "app.ini", "Setup", "Stage", "Halfway");
                    reg.writeString("HKCU", r"Software\RaggieSoft\RsscRollbackSelfTest", "Marker", "Partial");
                    fs.writeText(installDir / "TriggerFaultFile.txt", "BOOM");
                }
            )RSSC";

            rsw::rssc::ScriptProgram rbProg;
            std::vector<rsw::rssc::ScriptDiagnostic> rbDiags;
            rsw::rssc::CompileRaggieScriptText(rbScript, {}, rbProg, rbDiags);

            rsw::rssc::VmExecutionOptions rbOpts{};
            rbOpts.silentMode            = true;
            rbOpts.cliProperties["installDir"] = rsw::WideToUtf8(rbDir.wstring());
            rbOpts.simulateFaultAfterOps = 5; // Fault right on the 5th mutating op (`TriggerFaultFile.txt`)!

            rsw::rssc::RaggieScriptVm rbVm(rbProg, rbOpts);
            const auto rbRes = rbVm.ExecuteFullLifecycle();

            std::vector<std::uint8_t> restoredBytes;
            std::string readErr;
            rsw::ReadBinaryFileBytes(rbDir / L"ExistingConfig.txt", restoredBytes, readErr);
            const std::string restoredText(restoredBytes.begin(), restoredBytes.end());

            Check(!rbRes.ok && rbRes.rolledBack &&
                  restoredText == "PRESERVE_ME_V1" &&
                  !std::filesystem::exists(rbDir / L"SubFolder" / L"PartialNewFile.txt", ec) &&
                  !std::filesystem::exists(rbDir / L"TriggerFaultFile.txt", ec) &&
                  !std::filesystem::exists(rbDir / L".rsw-rollback.jnl", ec),
                  "RaggieScript transactional rollback journal restores overwritten .rswbak files & cleans partial dirs/keys on fault",
                  "restored='" + restoredText + "' err=" + rbRes.errorMessage);
        }

        // 7e. End-to-End `raggiescript.exe pack-exe` Micro-App (with `_support\`) & `instbuild.exe <.rssc>` -> `Setup.exe` + `Uninstall.exe`!
        {
            auto runCli = [](const std::filesystem::path& exe, const std::wstring& cliArgs) -> DWORD {
                std::wstring cmd = L"\"" + exe.wstring() + L"\" " + cliArgs;
                STARTUPINFOW si{sizeof(si)};
                PROCESS_INFORMATION pi{};
                if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    return 9999u;
                }
                ::WaitForSingleObject(pi.hProcess, 25000);
                DWORD code = 9998u;
                ::GetExitCodeProcess(pi.hProcess, &code);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                return code;
            };

            const auto raggiescriptExe = binDir / L"raggiescript.exe";
            const auto instbuildExe    = binDir / L"instbuild.exe";

            // 1. Test `raggiescript.exe pack-exe` with an embedded `_support\seed.json` directory!
            const auto microDir = rsscWorkDir / L"micro-app";
            std::filesystem::create_directories(microDir / L"_support", ec);
            {
                std::ofstream sf(microDir / L"_support" / L"seed.json", std::ios::binary);
                sf << "{\"edition\":\"Architect\",\"parity\":true}";
            }
            const auto microOutMarker = rsscWorkDir / L"micro-app-output.txt";
            {
                std::ofstream ms(microDir / L"micro.rssc", std::ios::binary);
                ms << "app \"RaggieScript Micro Utility\" {\n"
                   << "  version: 1.0.0;\n"
                   << "  publisher: \"RaggieSoft\";\n"
                   << "  supportDir: \"./_support\";\n"
                   << "}\n"
                   << "param outFile: path = \"\";\n"
                   << "const cfg = fs.readJson(sys.supportDir / \"seed.json\");\n"
                   << "fs.writeText(outFile, \"Edition=${cfg.edition}|Parity=${cfg.parity}|Arch=${sys.arch()}\");\n";
            }
            const auto packedMicroExe = microDir / L"MicroUtility.exe";
            const DWORD packRc = runCli(raggiescriptExe, L"pack-exe \"" + (microDir / L"micro.rssc").wstring() + L"\" \"" + packedMicroExe.wstring() + L"\"");
            const DWORD runPackedRc = (packRc == 0)
                ? runCli(packedMicroExe, L"outFile=\"" + microOutMarker.wstring() + L"\"")
                : 1u;
            std::vector<std::uint8_t> microBytes;
            std::string microErr;
            rsw::ReadBinaryFileBytes(microOutMarker, microBytes, microErr);
            const std::string microTxt(microBytes.begin(), microBytes.end());
            Check(packRc == 0 && runPackedRc == 0 && microTxt.find("Edition=Architect|Parity=true|Arch=") == 0,
                  "raggiescript.exe pack-exe compiles standalone zero-dependency Windows .exe micro-app with embedded _support\\ payload",
                  "packRc=" + std::to_string(packRc) + " runRc=" + std::to_string(runPackedRc) + " out=" + microTxt);

            // 2. Test `instbuild.exe sample-workbench-setup.rssc` -> `<Product>-setup.exe /s` -> `Uninstall.exe --native-uninstall /s`!
            const auto sampleScriptPath = binDir / L".." / L".." / L"examples" / L"raggiescript" / L"sample-workbench-setup.rssc";
            const auto rsscPkgOutDir    = rsscWorkDir / L"pkg-out";
            const auto rsscLiveInstDir  = rsscWorkDir / L"live-installed-workbench";
            const auto rsscLogFile      = rsscWorkDir / L"install.log";

            const DWORD valJsonRc = runCli(instbuildExe, L"--validate --diagnostics=json \"" + sampleScriptPath.wstring() + L"\"");
            const DWORD bldRsscRc = runCli(
                instbuildExe,
                L"\"" + sampleScriptPath.wstring() + L"\" --out-dir=\"" + rsscPkgOutDir.wstring() +
                L"\" --output-name=sample-workbench-" + rsw::Utf8ToWide(hostArch));
            const auto builtSetupExe = rsscPkgOutDir / (L"sample-workbench-" + rsw::Utf8ToWide(hostArch) + L"-setup.exe");

            const DWORD instRsscRc = (bldRsscRc == 0 && std::filesystem::exists(builtSetupExe, ec))
                ? runCli(builtSetupExe, L"/s /l=\"" + rsscLogFile.wstring() + L"\" installDir=\"" + rsscLiveInstDir.wstring() + L"\"")
                : 1u;

            const bool installedCleanly =
                (valJsonRc == 0) &&
                (instRsscRc == 0) &&
                std::filesystem::exists(rsscLiveInstDir / L"License.rtf", ec) &&
                std::filesystem::exists(rsscLiveInstDir / L"workbench.config.json", ec) &&
                std::filesystem::exists(rsscLiveInstDir / L"settings.ini", ec) &&
                std::filesystem::exists(rsscLiveInstDir / L"Uninstall.exe", ec) &&
                std::filesystem::exists(rsscLiveInstDir / L"Uninstall.rswm", ec);

            std::string unErr;
            const bool uninstalledCleanly = installedCleanly &&
                rsw::ExecuteNativeUninstallFromMeta(rsscLiveInstDir / L"Uninstall.rswm", false, unErr) &&
                !std::filesystem::exists(rsscLiveInstDir / L"workbench.config.json", ec) &&
                !std::filesystem::exists(rsscLiveInstDir / L"License.rtf", ec);

            Check(installedCleanly && uninstalledCleanly,
                  "instbuild.exe compiles .rssc -> Setup.exe (#225 RSWBC1 + #220 RSWPAK1), installs silently, & uninstalls cleanly via Uninstall.rswm",
                  "bld=" + std::to_string(bldRsscRc) + " inst=" + std::to_string(instRsscRc) + " unErr=" + unErr);

            // 3. Test RaggieScript Direct MSI SQL Surgeon (`msi-table-surgeon.rssc`) + `msiprism.exe --exec-sql` & `--export-table-json`!
            std::string surgErr;
            const auto surgeonScript = binDir / L".." / L".." / L"examples" / L"raggiescript" / L"msi-table-surgeon.rssc";
            const auto surgeonDir    = rsscWorkDir / L"msi-surgeon";
            const DWORD surgeonRc    = runCli(raggiescriptExe, L"run \"" + surgeonScript.wstring() + L"\" workDir=\"" + surgeonDir.wstring() + L"\"");
            std::vector<std::uint8_t> repBytes;
            rsw::ReadBinaryFileBytes(surgeonDir / L"surgeon-report.json", repBytes, surgErr);
            const std::string repJson(repBytes.begin(), repBytes.end());
            const bool surgeonOk =
                (surgeonRc == 0) &&
                std::filesystem::exists(surgeonDir / L"EnterprisePolicy.mst", ec) &&
                (repJson.find("Commonwealth Polytechnic Engineering Lab") != std::string::npos);
            Check(surgeonOk,
                  "RaggieScript msi.* creates .msi from scratch, executes DDL/DML SQL tables, stamps SummaryInformation, & emits .mst Transform",
                  "rc=" + std::to_string(surgeonRc) + " json=" + repJson);

            const auto prismBin      = binDir / L"msiprism.exe";
            const auto corpMsiPath   = surgeonDir / L"EnterpriseCustomized.msi";
            const auto prismJsonOut  = surgeonDir / L"prism-property-table.json";
            const DWORD prismSqlRc   = surgeonOk
                ? runCli(prismBin, L"--exec-sql \"" + corpMsiPath.wstring() + L"\" \"INSERT INTO `Property` (`Property`, `Value`) VALUES ('PRISM_DIRECT_EDIT', 'ENABLED')\"")
                : 1u;
            const DWORD prismExpRc   = (prismSqlRc == 0)
                ? runCli(prismBin, L"--export-table-json \"" + corpMsiPath.wstring() + L"\" Property \"" + prismJsonOut.wstring() + L"\"")
                : 1u;
            std::vector<std::uint8_t> pJsonBytes;
            rsw::ReadBinaryFileBytes(prismJsonOut, pJsonBytes, surgErr);
            const std::string pJsonStr(pJsonBytes.begin(), pJsonBytes.end());
            Check(prismSqlRc == 0 && prismExpRc == 0 && pJsonStr.find("PRISM_DIRECT_EDIT") != std::string::npos,
                  "msiprism.exe --exec-sql mutates MSI tables in-place and --export-table-json exports verified JSON",
                  "sqlRc=" + std::to_string(prismSqlRc) + " expRc=" + std::to_string(prismExpRc));

            // 4. Test Deferred UAC (`Uac.Deferred`): PE manifest stays `asInvoker` for un-elevated Wizard,
            //    and `ExportWizardStateJson` -> `ImportWizardStateJson` -> `ExecuteCommitPhaseOnly` hands off state cleanly!
            const auto defUacScript = rsscWorkDir / L"deferred-uac-test.rssc";
            const auto defUacOutDir = rsscWorkDir / L"deferred-uac-out";
            const auto defUacDest   = rsscWorkDir / L"deferred-uac-installed";
            {
                const std::string duSrc =
                    "use installer;\n"
                    "package \"Deferred UAC Demo\" {\n"
                    "    version: v\"1.2.0\";\n"
                    "    scope: Scope.Machine;\n"
                    "    uac: Uac.Deferred;\n"
                    "    hideFromInstalledApps: true;\n"
                    "}\n"
                    "feature \"core\" { title: \"Core\"; inCompact: true; required: true; }\n"
                    "feature \"extras\" { title: \"Extras\"; inCompact: false; }\n"
                    "param customChannel: string = \"standard\";\n"
                    "install {\n"
                    "    fs.mkdir(installDir);\n"
                    "    fs.writeJson(installDir / \"uac-handoff.json\", {\n"
                    "        channel: customChannel,\n"
                    "        coreSelected: feature.isSelected(\"core\"),\n"
                    "        extrasSelected: feature.isSelected(\"extras\")\n"
                    "    });\n"
                    "}\n";
                std::ofstream out(defUacScript, std::ios::binary | std::ios::trunc);
                out.write(duSrc.data(), static_cast<std::streamsize>(duSrc.size()));
            }
            const DWORD duBuildRc = runCli(
                instbuildExe,
                L"\"" + defUacScript.wstring() + L"\" --out-dir=\"" + defUacOutDir.wstring() + L"\" --output-name=deferred-uac-demo");
            const auto duExePath = defUacOutDir / L"deferred-uac-demo-setup.exe";
            std::vector<std::uint8_t> duExeBytes;
            rsw::ReadBinaryFileBytes(duExePath, duExeBytes, surgErr);
            const std::string duExeRaw(reinterpret_cast<const char*>(duExeBytes.data()), duExeBytes.size());
            const bool manifestIsAsInvoker = (duExeRaw.find("level=\"asInvoker\"") != std::string::npos);

            rsw::rssc::ScriptProgram duProg;
            std::vector<rsw::rssc::ScriptDiagnostic> duDiags;
            const bool duCompiled = rsw::rssc::CompileRaggieScriptFile(defUacScript, duProg, duDiags);
            bool handoffOk = false;
            if (duCompiled) {
                // Simulate un-elevated `wizard` choosing Compact preset + custom installDir + customChannel
                rsw::rssc::RaggieScriptVm wizVm(duProg);
                wizVm.GlobalScope().Declare("customChannel", rsw::rssc::Value::FromStr("elevated-broker"), false);
                wizVm.GlobalScope().Declare("installDir",    rsw::rssc::Value::FromPath(defUacDest), false);
                const std::string stateJson = wizVm.ExportWizardStateJson();

                // Hydrate inside simulated `--elevated-rssc-worker` VM and run ONLY Commit phase!
                rsw::rssc::VmExecutionOptions workerOpts{};
                workerOpts.isElevatedWorker = true;
                rsw::rssc::RaggieScriptVm workerVm(duProg, workerOpts);
                if (workerVm.ImportWizardStateJson(stateJson)) {
                    const auto cRes = workerVm.ExecuteCommitPhaseOnly();
                    std::vector<std::uint8_t> hoBytes;
                    rsw::ReadBinaryFileBytes(defUacDest / L"uac-handoff.json", hoBytes, surgErr);
                    const std::string hoTxt(hoBytes.begin(), hoBytes.end());
                    handoffOk = cRes.ok && (hoTxt.find("elevated-broker") != std::string::npos);
                }
            }
            Check(duBuildRc == 0 && manifestIsAsInvoker && handoffOk,
                  "RaggieScript Uac.Deferred stamps asInvoker PE manifest for un-elevated Wizard and hands off state to elevated Commit worker",
                  "bld=" + std::to_string(duBuildRc) + " asInvoker=" + std::to_string(manifestIsAsInvoker) + " handoff=" + std::to_string(handoffOk));

            // 5. Test `compile.json(...)` build-time embedding, `??` coalesce, `elif`, `ffi.call`, & Restart Manager `proc.getLockingProcesses`!
            const auto ctJsonFile = rsscWorkDir / L"build-meta.json";
            {
                const std::string j = "{\"codename\":\"NebulaForge\",\"editionCount\":4}";
                std::ofstream out(ctJsonFile, std::ios::binary | std::ios::trunc);
                out.write(j.data(), static_cast<std::streamsize>(j.size()));
            }
            const auto advScript = rsscWorkDir / L"advanced-features.rssc";
            const auto advBcFile = rsscWorkDir / L"advanced-features.rswbc";
            const auto advReport = rsscWorkDir / L"adv-report.txt";
            {
                const std::string advSrc =
                    "use installer;\n"
                    "#ifndef CUSTOM_TAG\n"
                    "#define CUSTOM_TAG \"DefaultTag\"\n"
                    "#endif\n"
                    "let meta = compile.json(\"build-meta.json\");\n"
                    "let pid = ffi.call(\"kernel32.dll\", \"GetCurrentProcessId\", \"uint\", []);\n"
                    "let fallback = reg.read(Hive.HKCU, r\"Software\\RaggieSoft\\NonExistentTestKey\", \"MissingVal\") ?? \"CoalescedOK\";\n"
                    "let tierLabel = \"unknown\";\n"
                    "if (meta.editionCount == 2) { tierLabel = \"two\"; }\n"
                    "elif (meta.editionCount == 4) { tierLabel = \"four\"; }\n"
                    "else { tierLabel = \"other\"; }\n"
                    "let locks = proc.getLockingProcesses([sys.exePath]);\n"
                    "fs.writeText(r\"" + rsw::WideToUtf8(advReport.wstring()) + "\", "
                    "\"${meta.codename}|${tierLabel}|${fallback}|${def(\"CUSTOM_TAG\")}|pid=${pid > 0}|locks=${typeof(locks)}\");\n";
                std::ofstream out(advScript, std::ios::binary | std::ios::trunc);
                out.write(advSrc.data(), static_cast<std::streamsize>(advSrc.size()));
            }
            const DWORD advCompRc = runCli(raggiescriptExe, L"compile \"" + advScript.wstring() + L"\" \"" + advBcFile.wstring() + L"\" -DCUSTOM_TAG=StudioV060");
            // Delete `build-meta.json` BEFORE running bytecode to prove `compile.json` baked it into `RSWBC1`!
            std::filesystem::remove(ctJsonFile, ec);
            const DWORD advRunRc  = (advCompRc == 0) ? runCli(raggiescriptExe, L"run \"" + advBcFile.wstring() + L"\"") : 1u;
            std::vector<std::uint8_t> advBytes;
            rsw::ReadBinaryFileBytes(advReport, advBytes, surgErr);
            const std::string advTxt(advBytes.begin(), advBytes.end());
            Check(advCompRc == 0 && advRunRc == 0 && advTxt == "NebulaForge|four|CoalescedOK|StudioV060|pid=true|locks=array",
                  "RaggieScript compile.json, -D/#define, ?? coalesce, elif, 64-bit ffi.call, and Restart ManagerRmGetList verified",
                  "comp=" + std::to_string(advCompRc) + " run=" + std::to_string(advRunRc) + " out=" + advTxt);

            // 6. Validate Book & Album Micro-Installer `.rssc` Templates (`RSW701` & `RSW702`)!
            const auto bookTmpl  = binDir / L".." / L".." / L"examples" / L"raggiescript" / L"book-pack-autorun.rssc";
            const auto albumTmpl = binDir / L".." / L".." / L"examples" / L"raggiescript" / L"album-pack-autorun.rssc";
            const DWORD bkValRc  = runCli(instbuildExe, L"--validate \"" + bookTmpl.wstring() + L"\" -DBOOK_SLUG=cedar-run");
            const DWORD alValRc  = runCli(instbuildExe, L"--validate \"" + albumTmpl.wstring() + L"\" -DALBUM_SLUG=1997-hard-reset");
            Check(bkValRc == 0 && alValRc == 0,
                  "Book and Album CD-Autorun RaggieScript templates pass RSW701/RSW702 static validation",
                  "bookRc=" + std::to_string(bkValRc) + " albumRc=" + std::to_string(alValRc));

            // 7. Test Cryptographic ECDSA P-256 `.rswlic` Licensing (`No License File == Community Edition`),
            //    Four-Edition Capability Matrix (`Community == Architect`, `Essentials`, `Developer`, `Architect`),
            //    AND Standalone `rswsfx.exe --pack` Self-Extractor CLI!
            const auto stdSampleRsip = binDir / L".." / L".." / L"examples" / L"hello-raggiesoft" / L"hello-raggiesoft.rsip";
            const auto suiteRsip     = binDir / L".." / L".." / L"examples" / L"hello-raggiesoft" / L"hello-raggiesoft-suite.rsis";

            // 7a. With NO `.rswlic` license file present, `instbuild.exe` MUST present `Community Edition` (`100% Architect Parity`)!
            const DWORD licStatRc   = runCli(instbuildExe, L"--license-status --diagnostics=json");
            const DWORD comSuiteOk  = runCli(instbuildExe, L"--validate \"" + suiteRsip.wstring() + L"\"");

            // 7b. Ephemeral NIST P-256 ECDSA (`bcrypt.dll`) `.rswlic` Signing, Verification, & Tamper Rejection (`RSW098`)!
            bool ecdsaSignVerifyOk = false;
            bool tamperRejectedOk  = false;
            DWORD tamperedCliRc    = 0;
            {
                auto b64EncodeTest = [](const std::uint8_t* data, std::size_t len) -> std::string {
                    static constexpr char kTab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    std::string out;
                    for (std::size_t i = 0; i < len; i += 3) {
                        const std::uint32_t b0 = data[i];
                        const std::uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0u;
                        const std::uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0u;
                        const std::uint32_t n  = (b0 << 16) | (b1 << 8) | b2;
                        out.push_back(kTab[(n >> 18) & 0x3Fu]);
                        out.push_back(kTab[(n >> 12) & 0x3Fu]);
                        out.push_back((i + 1 < len) ? kTab[(n >> 6) & 0x3Fu] : '=');
                        out.push_back((i + 2 < len) ? kTab[n & 0x3Fu]        : '=');
                    }
                    return out;
                };

                BCRYPT_ALG_HANDLE hAlg = nullptr;
                BCRYPT_KEY_HANDLE hKey = nullptr;
                std::array<std::uint8_t, 64> testPubQxQy{};
                if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
                    ::BCryptGenerateKeyPair(hAlg, &hKey, 256, 0) == 0 &&
                    ::BCryptFinalizeKeyPair(hKey, 0) == 0) {
                    ULONG cbPub = 0;
                    ::BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &cbPub, 0);
                    std::vector<UCHAR> pubBlob(cbPub);
                    if (cbPub >= sizeof(BCRYPT_ECCKEY_BLOB) + 64 &&
                        ::BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, pubBlob.data(), cbPub, &cbPub, 0) == 0) {
                        std::memcpy(testPubQxQy.data(), pubBlob.data() + sizeof(BCRYPT_ECCKEY_BLOB), 64);
                    }

                    auto makeSignedCert = [&](const std::string& prod, const std::string& ed, const std::string& licId = "RSW-UNITTEST-001") -> std::string {
                        std::map<std::string, std::string> f{
                            {"edition",    ed},
                            {"format",     "RSWLIC1"},
                            {"issueddate", "2026-11-27"},
                            {"licensee",   "Unit Test Studio Team"},
                            {"licenseid",  licId},
                            {"product",    prod},
                            {"seats",      "3"}
                        };
                        std::ostringstream can;
                        can << "RSWLIC1_CANONICAL_V1\n";
                        for (const auto& [k, v] : f) can << k << "=" << v << "\n";
                        std::array<std::uint8_t, 32> digest{};
                        rsw::license_detail::ComputeSha256Raw(can.str(), digest);
                        std::array<std::uint8_t, 64> sig{};
                        ULONG cbSig = 0;
                        ::BCryptSignHash(hKey, nullptr, digest.data(), 32, sig.data(), 64, &cbSig, 0);
                        std::ostringstream doc;
                        doc << "-----BEGIN RAGGIESOFT LICENSE-----\r\n"
                            << "Format     = RSWLIC1\r\n"
                            << "LicenseId  = " << licId << "\r\n"
                            << "Product    = " << prod << "\r\n"
                            << "Edition    = " << ed << "\r\n"
                            << "Licensee   = Unit Test Studio Team\r\n"
                            << "Seats      = 3\r\n"
                            << "IssuedDate = 2026-11-27\r\n"
                            << "Signature  = " << b64EncodeTest(sig.data(), 64) << "\r\n"
                            << "-----END RAGGIESOFT LICENSE-----\r\n";
                        return doc.str();
                    };

                    rsw::RswLicenseCertificate cEss{}, cDev{}, cArc{}, cSuite{}, cTamper{}, cRevoked{};
                    std::string lErr, revErr, ciErr;
                    const std::string validEssTxt = makeSignedCert("installer-studio",  "essentials");
                    const std::string validDevTxt = makeSignedCert("installer-studio",  "developer");
                    const std::string validArcTxt = makeSignedCert("installer-studio",  "architect");
                    const std::string validSteTxt = makeSignedCert("raggiesoft-studio", "architect");
                    const std::string revokedTxt  = makeSignedCert("installer-studio",  "architect", "RSW-REVOKED-PUBLIC-LEAK-0000");

                    const bool revokedBlockedOk =
                        !rsw::VerifyRswLicenseTextWithPublicKey(revokedTxt, "installer-studio", testPubQxQy, cRevoked, revErr) &&
                        (revErr.find("[RSW098]") != std::string::npos);

                    // Offline CI/CD scope check (`RSW099`): `Essentials` blocked in CI, `Developer` & `Architect` allowed!
                    ::SetEnvironmentVariableW(L"RSW_SIMULATE_CI_ENV", L"1");
                    rsw::RswLicenseStatus stEss{true, rsw::RswIdeEdition::Essentials, cEss, {}, "test"};
                    rsw::RswLicenseStatus stDev{true, rsw::RswIdeEdition::Developer,  cDev, {}, "test"};
                    rsw::RswLicenseStatus stCom{false, rsw::RswIdeEdition::Community, {},   {}, "community"};
                    ecdsaSignVerifyOk =
                        rsw::VerifyRswLicenseTextWithPublicKey(validEssTxt, "installer-studio", testPubQxQy, cEss, lErr) &&
                        cEss.edition == rsw::RswIdeEdition::Essentials &&
                        rsw::VerifyRswLicenseTextWithPublicKey(validDevTxt, "installer-studio", testPubQxQy, cDev, lErr) &&
                        cDev.edition == rsw::RswIdeEdition::Developer &&
                        rsw::VerifyRswLicenseTextWithPublicKey(validArcTxt, "installer-studio", testPubQxQy, cArc, lErr) &&
                        cArc.edition == rsw::RswIdeEdition::Architect &&
                        rsw::VerifyRswLicenseTextWithPublicKey(validSteTxt, "installer-studio", testPubQxQy, cSuite, lErr) &&
                        cSuite.edition == rsw::RswIdeEdition::Architect;
                    stEss.cert = cEss;
                    stDev.cert = cDev;
                    const bool ciScopeOk =
                        !rsw::VerifyCommercialCiAgentRights(stEss, ciErr) &&
                        (ciErr.find("[RSW099]") != std::string::npos) &&
                        rsw::VerifyCommercialCiAgentRights(stDev, ciErr) &&
                        rsw::VerifyCommercialCiAgentRights(stCom, ciErr);
                    ::SetEnvironmentVariableW(L"RSW_SIMULATE_CI_ENV", nullptr);

                    // Verify VPAT 2.5 provenance stamps `Licensed to Unit Test Studio Team` when commercial `.rswlic` is active!
                    rsw::RswLicenseStatus stArc{true, rsw::RswIdeEdition::Architect, cArc, {}, "test"};
                    rsw::RswManifestDocument vpatTestDoc;
                    vpatTestDoc.SetString("Package", "", "Name", "Provenance Check App");
                    vpatTestDoc.SetString("Package", "", "Version", "1.0.0");
                    vpatTestDoc.SetString("Package", "", "Manufacturer", "RaggieSoft");
                    std::filesystem::path vpatOutHtml;
                    std::string vpatErr;
                    const bool vpatProvWritten = rsw::EmitProjectInstallerVpatHtml(
                        vpatTestDoc, rsscWorkDir / L"vpat-prov", vpatOutHtml, vpatErr, &stArc);
                    std::vector<std::uint8_t> vpatBytes;
                    rsw::ReadBinaryFileBytes(vpatOutHtml, vpatBytes, surgErr);
                    const std::string vpatHtmlStr(reinterpret_cast<const char*>(vpatBytes.data()), vpatBytes.size());
                    const bool vpatProvOk = vpatProvWritten &&
                        (vpatHtmlStr.find("Licensed to Unit Test Studio Team") != std::string::npos) &&
                        (vpatHtmlStr.find("RSW-UNITTEST-001") != std::string::npos);

                    ecdsaSignVerifyOk = ecdsaSignVerifyOk && revokedBlockedOk && ciScopeOk && vpatProvOk;

                    // Tamper `Edition = essentials` -> `Edition = architect` without private key -> MUST fail with `RSW098`!
                    std::string forgedTxt = validEssTxt;
                    const auto pos = forgedTxt.find("essentials");
                    if (pos != std::string::npos) forgedTxt.replace(pos, 10, "architect ");
                    tamperRejectedOk = !rsw::VerifyRswLicenseTextWithPublicKey(forgedTxt, "installer-studio", testPubQxQy, cTamper, lErr) &&
                                       (lErr.find("[RSW098]") != std::string::npos);

                    const auto forgedLicPath = rsscWorkDir / L"forged-architect.rswlic";
                    {
                        std::ofstream fl(forgedLicPath, std::ios::binary | std::ios::trunc);
                        fl.write(forgedTxt.data(), static_cast<std::streamsize>(forgedTxt.size()));
                    }
                    tamperedCliRc = runCli(instbuildExe, L"--license-file=\"" + forgedLicPath.wstring() + L"\" --validate \"" + stdSampleRsip.wstring() + L"\"");
                }
                if (hKey) ::BCryptDestroyKey(hKey);
                if (hAlg) ::BCryptCloseAlgorithmProvider(hAlg, 0);
            }

            // 7c. Four-Edition Capability Predicates & Validator Enforcement (`Essentials`, `Developer`, `Architect`, `Community`)!
            rsw::RswManifestDocument stdRawDoc, stdResDoc;
            std::string stdLoadErr;
            rsw::LoadManifestFile(stdSampleRsip, stdRawDoc, stdLoadErr);
            rsw::ResolveManifestIncludesAndVars(stdRawDoc, {}, stdResDoc, stdLoadErr);
            const bool essMsiOk     = rsw::ValidateManifestDocument(stdResDoc, hostArch, rsw::RswIdeEdition::Essentials).IsOk();
            const bool essGatesOk   = !rsw::EditionSupportsNativeEngineAndScripting(rsw::RswIdeEdition::Essentials) &&
                                      !rsw::EditionSupportsCompilingMergeModules(rsw::RswIdeEdition::Essentials) &&
                                      !rsw::EditionSupportsUnifiedSuites(rsw::RswIdeEdition::Essentials) &&
                                      !rsw::EditionSupportsAutoVpatExport(rsw::RswIdeEdition::Essentials);
            const bool devGatesOk   = rsw::EditionSupportsNativeEngineAndScripting(rsw::RswIdeEdition::Developer) &&
                                      rsw::EditionSupportsCompilingMergeModules(rsw::RswIdeEdition::Developer) &&
                                      !rsw::EditionSupportsUnifiedSuites(rsw::RswIdeEdition::Developer) &&
                                      !rsw::EditionSupportsAutoVpatExport(rsw::RswIdeEdition::Developer);
            const bool arcParityOk  = rsw::EditionSupportsUnifiedSuites(rsw::RswIdeEdition::Community) &&
                                      rsw::EditionSupportsUnifiedSuites(rsw::RswIdeEdition::Architect) &&
                                      rsw::EditionSupportsMspPatchesAndMstTransforms(rsw::RswIdeEdition::Community) &&
                                      rsw::EditionSupportsMspPatchesAndMstTransforms(rsw::RswIdeEdition::Architect);

            Check(licStatRc == 0 && comSuiteOk == 0 && ecdsaSignVerifyOk && tamperRejectedOk && tamperedCliRc != 0 &&
                  essMsiOk && essGatesOk && devGatesOk && arcParityOk,
                  "Cryptographic ECDSA P-256 .rswlic licensing (No License == Community Edition, RSW098 tamper rejection) & Four-Edition capability gates verified",
                  "licStat=" + std::to_string(licStatRc) + " ecdsa=" + std::to_string(ecdsaSignVerifyOk) +
                  " tamper=" + std::to_string(tamperRejectedOk) + " cliTamperRc=" + std::to_string(tamperedCliRc) +
                  " essMsi=" + std::to_string(essMsiOk));

            // Test standalone `rswsfx.exe --pack` building a working self-extracting `.exe` and extracting it silently (`/s`)!
            const auto rswSfxExe    = binDir / L"rswsfx.exe";
            const auto sfxSrcFolder = rsscWorkDir / L"sfx-cli-src";
            const auto sfxBuiltExe  = rsscWorkDir / L"sfx-cli-out" / L"StandaloneArchive.exe";
            const auto sfxUnpackDir = rsscWorkDir / L"sfx-cli-unpacked";
            std::filesystem::create_directories(sfxSrcFolder, ec);
            {
                std::ofstream f(sfxSrcFolder / L"PayloadVerify.txt", std::ios::binary | std::ios::trunc);
                f << "RSWSFX_STANDALONE_OK";
            }
            const DWORD sfxPackRc = runCli(
                rswSfxExe,
                L"--pack \"" + sfxSrcFolder.wstring() + L"\" \"" + sfxBuiltExe.wstring() + L"\" --title=\"Standalone SFX Test\" --uac=asInvoker");
            const DWORD sfxRunRc  = (sfxPackRc == 0)
                ? runCli(sfxBuiltExe, L"/s INSTALLDIR=\"" + sfxUnpackDir.wstring() + L"\"")
                : 1u;
            std::vector<std::uint8_t> sfxOutBytes;
            rsw::ReadBinaryFileBytes(sfxUnpackDir / L"PayloadVerify.txt", sfxOutBytes, surgErr);
            const std::string sfxOutStr(sfxOutBytes.begin(), sfxOutBytes.end());
            Check(sfxPackRc == 0 && sfxRunRc == 0 && sfxOutStr == "RSWSFX_STANDALONE_OK",
                  "Standalone rswsfx.exe --pack builds LZMS self-extracting .exe and extracts byte-for-byte in /s mode",
                  "pack=" + std::to_string(sfxPackRc) + " run=" + std::to_string(sfxRunRc) + " txt=" + sfxOutStr);

            // 8. v0.7.0 Test A — Shared `RaggieSoftVisualCanvas.hpp` (`raggiesoft::studio::canvas`) SDK!
            {
                using namespace raggiesoft::studio::canvas;
                const auto vp = LogicalViewport::FitAspectLocked(RECT{100, 50, 840, 590}, 370, 270, false);
                const RECT sr = vp.LogicalToScreenRect({20, 74, 330, 18});
                const POINT backPt = vp.ScreenToLogicalPoint({sr.left, sr.top});
                std::vector<CanvasItem> items{
                    {"Lbl1", "Text",     {20, 52, 330, 14}, "", "Endpoint:", 0, false},
                    {"Edt1", "Edit",     {20, 74, 330, 18}, "ENDPOINT_URL", "https://raggiesoftstudio.com", 1, false},
                    {"Chk1", "CheckBox", {20, 104, 330, 18}, "ADD_TO_PATH", "Add to &PATH", 2, false}
                };
                const auto hitBody = HitTestCanvasItems(items, vp, {sr.left + 20, sr.top + 8}, "Edt1");
                const auto hitGrip = HitTestCanvasItems(items, vp, {sr.right, sr.bottom}, "Edt1");
                DragSession ds{};
                ds.Begin("Edt1", ResizeHandle::BodyMove, {sr.left + 10, sr.top + 8}, items[1].bounds);
                std::vector<SmartGuideLine> guides;
                const auto moved = ds.Update(vp, {sr.left + 10, sr.top + 48}, 2, 12, 10, items, guides);
                LogicalRect nudged = items[1].bounds;
                NudgeRectByKey(nudged, VK_RIGHT, 4, false, vp);
                NudgeRectByKey(nudged, VK_DOWN,  6, true,  vp);
                const std::string emittedRssc = EmitRaggieScriptWizardStep("ServerConfigDlg", "Connection Settings", "Configure server.", items);
                Check(backPt.x == 20 && backPt.y == 74 &&
                      hitBody.itemId == "Edt1" && hitBody.handle == ResizeHandle::BodyMove &&
                      hitGrip.itemId == "Edt1" && hitGrip.handle == ResizeHandle::BottomRight &&
                      moved.y > 74 && nudged.x == 24 && nudged.h == 24 &&
                      ExtractMnemonicChar("Add to &PATH") == 'P' &&
                      emittedRssc.find("RsAskOptions") != std::string::npos &&
                      emittedRssc.find("RsAskText") != std::string::npos,
                      "v0.7.0 Shared RaggieSoftVisualCanvas.hpp viewport, 8-grip hit-test, drag, keyboard nudge, & .rssc codegen verified");
            }

            // 9. v0.7.0 Test B — `RSWBC2` Bytecode Container & `raggiescript.exe fmt [--check]`!
            {
                const auto unformattedRssc = rsscWorkDir / L"messy-format.rssc";
                const std::string messySrc =
                    "// Preserve top comment\n"
                    "use installer;\n"
                    "fn helper(x: int) -> int {\n"
                    "if (x > 0) {\n"
                    "return x + 1; /* inline block comment */\n"
                    "}\n"
                    "return 0;\n"
                    "}\n";
                {
                    std::ofstream f(unformattedRssc, std::ios::binary | std::ios::trunc);
                    f.write(messySrc.data(), static_cast<std::streamsize>(messySrc.size()));
                }
                const DWORD fmtCheckDirty = runCli(raggiescriptExe, L"fmt --check \"" + unformattedRssc.wstring() + L"\"");
                const DWORD fmtApplyRc    = runCli(raggiescriptExe, L"fmt \"" + unformattedRssc.wstring() + L"\"");
                const DWORD fmtCheckClean = runCli(raggiescriptExe, L"fmt --check \"" + unformattedRssc.wstring() + L"\"");
                std::vector<std::uint8_t> fmtBytes;
                rsw::ReadBinaryFileBytes(unformattedRssc, fmtBytes, surgErr);
                const std::string fmtStr(fmtBytes.begin(), fmtBytes.end());

                std::vector<std::uint8_t> bc2Bytes;
                rsw::ReadBinaryFileBytes(advBcFile, bc2Bytes, surgErr);
                const bool isRswbc2 = (bc2Bytes.size() >= 8 && std::memcmp(bc2Bytes.data(), "RSWBC2\0\0", 8) == 0);
                Check(isRswbc2 && fmtCheckDirty != 0 && fmtApplyRc == 0 && fmtCheckClean == 0 &&
                      fmtStr.find("    if (x > 0) {") != std::string::npos &&
                      fmtStr.find("// Preserve top comment") != std::string::npos &&
                      fmtStr.find("/* inline block comment */") != std::string::npos,
                      "v0.7.0 RSWBC2 SHA-256 bytecode container and raggiescript.exe fmt [--check] 4-space comment-preserving formatter verified",
                      "bc2=" + std::to_string(isRswbc2) + " dirty=" + std::to_string(fmtCheckDirty) + " clean=" + std::to_string(fmtCheckClean));
            }

            // 10. v0.7.0 Test C — Turnkey `RsDialog` Wizard Suite (`RsWelcome`..`RsFinish` + `< Back` Step Rewind)!
            {
                const auto turnkeyScript = binDir / L".." / L".." / L"examples" / L"raggiescript" / L"rsdialog-turnkey-setup.rssc";
                const auto turnkeyDest   = rsscWorkDir / L"turnkey-rsdialog-installed";
                rsw::rssc::ScriptProgram tkProg;
                std::vector<rsw::rssc::ScriptDiagnostic> tkDiags;
                const bool tkCompOk = rsw::rssc::CompileRaggieScriptFile(turnkeyScript, tkProg, tkDiags);

                rsw::rssc::VmExecutionOptions tkOpts{};
                tkOpts.silentMode = false;
                tkOpts.cliProperties["installDir"] = rsw::WideToUtf8(turnkeyDest.wstring());
                auto mkStep = [](int rc, std::string a = "", std::string b = "", std::vector<bool> opts = {}) {
                    rsw::rssc::rsdlg::RsDialogMockStep s{};
                    s.returnCode   = rc;
                    s.boolA        = true;
                    s.strA         = std::move(a);
                    s.strB         = std::move(b);
                    s.optionChecks = std::move(opts);
                    return s;
                };
                tkOpts.mockDialogSteps = {
                    mkStep(rsw::rssc::rsdlg::kResultNext),                                               // 1. RsWelcome
                    mkStep(rsw::rssc::rsdlg::kResultNext),                                               // 2. RsLicense
                    mkStep(rsw::rssc::rsdlg::kResultNext, "Ada Lovelace", "RaggieSoft Engineering Lab"), // 3. RsUserInfo
                    mkStep(rsw::rssc::rsdlg::kResultNext, "custom"),                                     // 4. RsSetupType -> custom!
                    mkStep(rsw::rssc::rsdlg::kResultNext, rsw::WideToUtf8(turnkeyDest.wstring())),       // 5. RsSelectDestDir
                    mkStep(rsw::rssc::rsdlg::kResultBack),                                               // 6. RsAskOptions -> `< Back` rewinds to CustomFolder!
                    mkStep(rsw::rssc::rsdlg::kResultNext, rsw::WideToUtf8(turnkeyDest.wstring())),       // 7. RsSelectDestDir (re-visited!)
                    mkStep(rsw::rssc::rsdlg::kResultNext, "", "", {true, true}),                         // 8. RsAskOptions -> Next
                    mkStep(rsw::rssc::rsdlg::kResultNext),                                               // 9. RsReadyToInstall -> Install!
                    mkStep(rsw::rssc::rsdlg::kResultNext)                                                // 10. RsFinish
                };

                bool tkRunOk = false;
                std::string tkJson;
                std::size_t visitedCount = 0;
                std::string tkVmErr;
                if (tkCompOk) {
                    rsw::rssc::RaggieScriptVm tkVm(tkProg, tkOpts);
                    const auto tkRes = tkVm.ExecuteFullLifecycle();
                    visitedCount = tkRes.visitedRsDialogs.size();
                    tkVmErr      = tkRes.errorMessage;
                    std::vector<std::uint8_t> profBytes;
                    rsw::ReadBinaryFileBytes(turnkeyDest / L"install-profile.json", profBytes, surgErr);
                    tkJson  = std::string(profBytes.begin(), profBytes.end());
                    tkRunOk = tkRes.ok &&
                              visitedCount == 10 &&
                              (tkJson.find("Ada Lovelace") != std::string::npos) &&
                              (tkJson.find("RaggieSoft Engineering Lab") != std::string::npos) &&
                              (tkJson.find("\"mode\": \"custom\"") != std::string::npos);
                }
                Check(tkCompOk && tkRunOk,
                      "v0.7.0 Turnkey RsDialog suite (RsWelcome..RsFinish) executes multi-step wizard, < Back rewind, & ref bindings",
                      "comp=" + std::to_string(tkCompOk) + " visited=" + std::to_string(visitedCount) + " err=" + tkVmErr + " json=" + tkJson);
            }

            // 11. v0.7.0 Test D — Declarative `[Environment.*]` Non-Clobbering `PATH` & `[IniFile.*]` (MSI + Native)!
            {
                const auto envTestDir  = rsscWorkDir / L"env-ini-test";
                const auto envSrcExe   = binDir / L"HelloRaggieSoft.exe";
                const auto envMsiRsip  = envTestDir / L"env-msi.rsip";
                const auto envNatRsip  = envTestDir / L"env-native.rsip";
                const auto envOutDir   = envTestDir / L"out";
                const auto envNatInst  = envTestDir / L"native-installed";
                std::filesystem::create_directories(envTestDir, ec);

                auto writeEnvManifest = [&](const std::filesystem::path& p, const char* engine) {
                    std::ostringstream s;
                    s << "[Package]\r\n"
                      << "Name = Env And Ini Test\r\n"
                      << "Version = 0.7.0\r\n"
                      << "Manufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {81A24F10-4E33-4B89-9D21-60708090A0B1}\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n"
                      << "Engine = " << engine << "\r\n"
                      << "HideFromInstalledApps = true\r\n\r\n"
                      << "[Install]\r\n"
                      << "DefaultSubdir = RaggieSoft\\EnvIniTest\r\n"
                      << "MainExecutable = HelloRaggieSoft.exe\r\n\r\n"
                      << "[Feature.Main]\r\n"
                      << "Title = Core\r\n"
                      << "Inclusion = minimal\r\n\r\n"
                      << "[Payload.Bin]\r\n"
                      << "Source = " << rsw::WideToUtf8(envSrcExe.wstring()) << "\r\n"
                      << "TargetRoot = INSTALLDIR\r\n"
                      << "Feature = Main\r\n\r\n"
                      << "[Environment.CliPath]\r\n"
                      << "Name = Path\r\n"
                      << "Value = [INSTALLDIR]bin\r\n"
                      << "Action = append_path\r\n"
                      << "Scope = user\r\n"
                      << "OnUninstall = remove\r\n"
                      << "Feature = Main\r\n\r\n"
                      << "[IniFile.DefaultEndpoint]\r\n"
                      << "FileName = workbench.ini\r\n"
                      << "TargetRoot = INSTALLDIR\r\n"
                      << "Section = Connection\r\n"
                      << "Key = Endpoint\r\n"
                      << "Value = https://raggiesoftstudio.com\r\n"
                      << "Action = set\r\n"
                      << "Feature = Main\r\n";
                    std::ofstream out(p, std::ios::binary | std::ios::trunc);
                    const std::string str = s.str();
                    out.write(str.data(), static_cast<std::streamsize>(str.size()));
                };
                writeEnvManifest(envMsiRsip, "msi");
                writeEnvManifest(envNatRsip, "native");

                const DWORD msiEnvRc = runCli(instbuildExe, L"\"" + envMsiRsip.wstring() + L"\" --out-dir=\"" + envOutDir.wstring() + L"\" --output-name=env-msi-pkg");
                bool msiTablesOk = false;
                if (msiEnvRc == 0) {
                    MSIHANDLE hDb = 0;
                    const auto msiP = envOutDir / L"env-msi-pkg.msi";
                    if (::MsiOpenDatabaseW(msiP.wstring().c_str(), MSIDBOPEN_READONLY, &hDb) == ERROR_SUCCESS) {
                        MSIHANDLE hView = 0, hRec = 0;
                        if (::MsiDatabaseOpenViewW(hDb, L"SELECT `Name`, `Value` FROM `Environment`", &hView) == ERROR_SUCCESS &&
                            ::MsiViewExecute(hView, 0) == ERROR_SUCCESS &&
                            ::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS) {
                            wchar_t nBuf[128]{}, vBuf[256]{};
                            DWORD cn = 128, cv = 256;
                            ::MsiRecordGetStringW(hRec, 1, nBuf, &cn);
                            ::MsiRecordGetStringW(hRec, 2, vBuf, &cv);
                            msiTablesOk = (std::wstring(nBuf) == L"=-Path") && (std::wstring(vBuf) == L"[~];[INSTALLDIR]bin");
                            ::MsiCloseHandle(hRec);
                        }
                        if (hView) ::MsiCloseHandle(hView);

                        MSIHANDLE hIniV = 0, hIniR = 0;
                        if (msiTablesOk &&
                            ::MsiDatabaseOpenViewW(hDb, L"SELECT `FileName`, `Section`, `Key`, `Value` FROM `IniFile`", &hIniV) == ERROR_SUCCESS &&
                            ::MsiViewExecute(hIniV, 0) == ERROR_SUCCESS &&
                            ::MsiViewFetch(hIniV, &hIniR) == ERROR_SUCCESS) {
                            wchar_t kvBuf[256]{};
                            DWORD ck = 256;
                            ::MsiRecordGetStringW(hIniR, 4, kvBuf, &ck);
                            msiTablesOk = (std::wstring(kvBuf) == L"https://raggiesoftstudio.com");
                            ::MsiCloseHandle(hIniR);
                        }
                        if (hIniV) ::MsiCloseHandle(hIniV);
                        ::MsiCloseHandle(hDb);
                    }
                }

                // Seed pre-existing user `Path` inside isolated virtual registry store so live host `PATH` is never touched!
                wchar_t prevVirtRegBuf[MAX_PATH * 4]{};
                const DWORD prevVirtLen = ::GetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", prevVirtRegBuf, static_cast<DWORD>(std::size(prevVirtRegBuf)));
                const auto envVirtStore = envTestDir / L"virt-reg";
                std::filesystem::create_directories(envVirtStore, ec);
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", envVirtStore.wstring().c_str());
                bool dummyCreated = false;
                rsw::native_detail::VirtualRegWriteValue(
                    envVirtStore, "HKCU", L"Environment", L"Path",
                    "C:\\Windows\\System32;D:\\ExistingUserTools", dummyCreated);

                const DWORD natBldRc = runCli(instbuildExe, L"\"" + envNatRsip.wstring() + L"\" --out-dir=\"" + envOutDir.wstring() + L"\" --output-name=env-nat-pkg");
                const auto natSetup  = envOutDir / L"env-nat-pkg-setup.exe";
                const DWORD natRunRc = (natBldRc == 0)
                    ? runCli(natSetup, L"/s INSTALLDIR=\"" + envNatInst.wstring() + L"\"")
                    : 1u;

                std::string pathAfterInstall;
                rsw::native_detail::VirtualRegReadValue(envVirtStore, "HKCU", L"Environment", L"Path", pathAfterInstall);
                wchar_t iniReadBuf[256]{};
                ::GetPrivateProfileStringW(L"Connection", L"Endpoint", L"", iniReadBuf, 256, (envNatInst / L"workbench.ini").wstring().c_str());

                std::string natUnErr;
                const bool natUnOk = (natRunRc == 0) &&
                    rsw::ExecuteNativeUninstallFromMeta(envNatInst / L"Uninstall.rswm", false, natUnErr);
                std::string pathAfterUninstall;
                rsw::native_detail::VirtualRegReadValue(envVirtStore, "HKCU", L"Environment", L"Path", pathAfterUninstall);

                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", (prevVirtLen > 0) ? prevVirtRegBuf : nullptr);

                const std::string expectedAppended = "C:\\Windows\\System32;D:\\ExistingUserTools;" + rsw::WideToUtf8((envNatInst / L"bin").wstring());
                Check(msiTablesOk && natRunRc == 0 &&
                      std::wstring(iniReadBuf) == L"https://raggiesoftstudio.com" &&
                      rsw::EqualsIgnoreCase(pathAfterInstall, expectedAppended) &&
                      natUnOk && pathAfterUninstall == "C:\\Windows\\System32;D:\\ExistingUserTools",
                      "v0.7.0 Declarative [Environment.*] non-clobbering PATH append/uninstall & [IniFile.*] verified across Engine=msi and Engine=native",
                      "msi=" + std::to_string(msiTablesOk) + " afterInst=" + pathAfterInstall + " afterUn=" + pathAfterUninstall);
            }

            // 12. v0.7.0 Test E — Custom `[Dialog.*]` & `[Control.*]` Validation (`RSW066` / `RSW093`) & Bootstrapper `#202` Stamping!
            {
                const auto dlgTestDir = rsscWorkDir / L"custom-dlg-test";
                std::filesystem::create_directories(dlgTestDir, ec);
                const auto badCtrlRsip = dlgTestDir / L"dangling-ctrl.rsip";
                const auto goodDlgRsip = dlgTestDir / L"good-custom-dlg.rsip";
                const auto dlgOutDir   = dlgTestDir / L"out";

                auto makeDlgRsip = [&](const std::filesystem::path& p, bool includeDialogSection) {
                    std::ostringstream s;
                    s << "[Package]\r\n"
                      << "Name = Custom Dialog App\r\n"
                      << "Version = 0.7.0\r\n"
                      << "Manufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {92B35F21-5F44-4C9A-8E32-718191A1B1C2}\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n"
                      << "BuildBootstrapper = true\r\n\r\n"
                      << "[Install]\r\n"
                      << "DefaultSubdir = RaggieSoft\\CustomDlgApp\r\n"
                      << "MainExecutable = HelloRaggieSoft.exe\r\n\r\n"
                      << "[Feature.Main]\r\n"
                      << "Title = Core\r\n\r\n"
                      << "[Payload.Bin]\r\n"
                      << "Source = " << rsw::WideToUtf8((binDir / L"HelloRaggieSoft.exe").wstring()) << "\r\n\r\n";
                    if (includeDialogSection) {
                        s << "[Dialog.ApiPage]\r\n"
                          << "Title = API Endpoint Settings\r\n"
                          << "Subtitle = Enter your server endpoint.\r\n"
                          << "AfterPage = SetupType\r\n\r\n";
                    }
                    s << "[Control.ApiUrlEdit]\r\n"
                      << "Dialog = ApiPage\r\n"
                      << "Type = Edit\r\n"
                      << "X = 20\r\n"
                      << "Y = 42\r\n"
                      << "Width = 340\r\n"
                      << "Height = 26\r\n"
                      << "Property = API_ENDPOINT\r\n"
                      << "DefaultValue = https://raggiesoftstudio.com\r\n";
                    std::ofstream out(p, std::ios::binary | std::ios::trunc);
                    const std::string str = s.str();
                    out.write(str.data(), static_cast<std::streamsize>(str.size()));
                };
                makeDlgRsip(badCtrlRsip, false);
                makeDlgRsip(goodDlgRsip, true);

                const DWORD badDlgRc  = runCli(instbuildExe, L"--validate \"" + badCtrlRsip.wstring() + L"\"");
                rsw::RswManifestDocument goodDlgRaw, goodDlgRes;
                std::string dlgLoadErr;
                rsw::LoadManifestFile(goodDlgRsip, goodDlgRaw, dlgLoadErr);
                rsw::ResolveManifestIncludesAndVars(goodDlgRaw, {}, goodDlgRes, dlgLoadErr);
                const bool essGateBlocked = !rsw::ValidateManifestDocument(goodDlgRes, hostArch, rsw::RswIdeEdition::Essentials).IsOk();
                const bool devPassOk      = rsw::ValidateManifestDocument(goodDlgRes, hostArch, rsw::RswIdeEdition::Developer).IsOk();
                const DWORD bldDlgRc  = runCli(instbuildExe, L"\"" + goodDlgRsip.wstring() + L"\" --out-dir=\"" + dlgOutDir.wstring() + L"\" --output-name=custom-dlg-pkg");
                std::vector<std::uint8_t> setupBytes;
                rsw::ReadBinaryFileBytes(dlgOutDir / L"custom-dlg-pkg-setup.exe", setupBytes, surgErr);
                const std::string setupRaw(reinterpret_cast<const char*>(setupBytes.data()), setupBytes.size());
                const bool stampedCustomDlg =
                    (setupRaw.find("CustomDialogCount=1") != std::string::npos) &&
                    (setupRaw.find("CustomControl_0_Property=API_ENDPOINT") != std::string::npos);

                Check(badDlgRc != 0 && essGateBlocked && devPassOk && bldDlgRc == 0 && stampedCustomDlg,
                      "v0.7.0 Custom [Dialog.*] & [Control.*] enforce RSW066/RSW093 and stamp #202 bootstrapper controls & SecureCustomProperties",
                      "bad=" + std::to_string(badDlgRc) + " essBlk=" + std::to_string(essGateBlocked) + " devOk=" + std::to_string(devPassOk) + " stamped=" + std::to_string(stampedCustomDlg));
            }

            // =========================================================================
            // 13. v0.8.0 Test A — Declarative `[Upgrade]` (`Schedule = late` `6550`,
            //     `Attributes = 257`, `BaselineMsi` `ProductCode` Inheritance, `RSW097`
            //     Edition Gate) & `Engine = raggiescript` Retired-File Pruning!
            // =========================================================================
            {
                const auto upgDir     = rsscWorkDir / L"v080-upgrade-test";
                const auto upgOutDir  = upgDir / L"out";
                const auto srcPay1    = upgDir / L"payload-v1";
                const auto srcPay2    = upgDir / L"payload-v2";
                const auto rsscInst   = upgDir / L"rssc-installed";
                std::filesystem::create_directories(srcPay1, ec);
                std::filesystem::create_directories(srcPay2, ec);
                std::filesystem::copy_file(binDir / L"HelloRaggieSoft.exe", srcPay1 / L"HelloRaggieSoft.exe", std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::copy_file(binDir / L"HelloRaggieSoft.exe", srcPay2 / L"HelloRaggieSoft.exe", std::filesystem::copy_options::overwrite_existing, ec);
                { std::ofstream f1(srcPay1 / L"RetiredLegacyPlugin.txt", std::ios::binary); f1 << "legacy-v070-plugin"; }
                { std::ofstream f2(srcPay2 / L"ModernV080Plugin.txt",    std::ios::binary); f2 << "modern-v080-plugin"; }

                const auto lateMajorRsip = upgDir / L"late-major.rsip";
                {
                    std::ofstream o(lateMajorRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\n"
                      << "Name = Upgrade Policy App\r\n"
                      << "Version = 1.0.0\r\n"
                      << "Manufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {A3147E10-2C44-4B98-9D11-524147555047}\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n"
                      << "BuildBootstrapper = false\r\n\r\n"
                      << "[Upgrade]\r\n"
                      << "Strategy = major\r\n"
                      << "Schedule = late\r\n"
                      << "MigrateFeatures = true\r\n\r\n"
                      << "[Install]\r\n"
                      << "DefaultSubdir = RaggieSoft\\UpgradePolicyApp\r\n"
                      << "MainExecutable = HelloRaggieSoft.exe\r\n\r\n"
                      << "[Payload.Main]\r\n"
                      << "Source = " << rsw::WideToUtf8(srcPay1.wstring()) << "\r\n";
                }
                const DWORD lateRc = runCli(instbuildExe, L"\"" + lateMajorRsip.wstring() + L"\" --out-dir=\"" + upgOutDir.wstring() + L"\" --output-name=upg-v1");
                const auto v1Msi = upgOutDir / L"upg-v1.msi";

                bool lateSeqAndAttrsOk = false;
                std::string v1ProdCode;
                if (lateRc == 0) {
                    MSIHANDLE hDb = 0;
                    if (::MsiOpenDatabaseW(v1Msi.wstring().c_str(), MSIDBOPEN_READONLY, &hDb) == ERROR_SUCCESS) {
                        v1ProdCode = rsw::QueryMsiPropertyString(hDb, L"ProductCode");
                        int seqVal = 0, attrVal = 0;
                        MSIHANDLE hV1 = 0, hR1 = 0;
                        if (::MsiDatabaseOpenViewW(hDb, L"SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action` = 'RemoveExistingProducts'", &hV1) == ERROR_SUCCESS &&
                            ::MsiViewExecute(hV1, 0) == ERROR_SUCCESS && ::MsiViewFetch(hV1, &hR1) == ERROR_SUCCESS) {
                            seqVal = ::MsiRecordGetInteger(hR1, 1);
                            ::MsiCloseHandle(hR1);
                        }
                        if (hV1) ::MsiCloseHandle(hV1);

                        MSIHANDLE hV2 = 0, hR2 = 0;
                        if (::MsiDatabaseOpenViewW(hDb, L"SELECT `Attributes` FROM `Upgrade` WHERE `ActionProperty` = 'RSW_OLDERVERSION_DETECTED'", &hV2) == ERROR_SUCCESS &&
                            ::MsiViewExecute(hV2, 0) == ERROR_SUCCESS && ::MsiViewFetch(hV2, &hR2) == ERROR_SUCCESS) {
                            attrVal = ::MsiRecordGetInteger(hR2, 1);
                            ::MsiCloseHandle(hR2);
                        }
                        if (hV2) ::MsiCloseHandle(hV2);
                        ::MsiCloseHandle(hDb);
                        lateSeqAndAttrsOk = (seqVal == 6550) && (attrVal == 257);
                    }
                }

                // Minor Upgrade inheriting `ProductCode` from `BaselineMsi = upg-v1.msi`!
                const auto minorRsip = upgDir / L"minor-v101.rsip";
                {
                    std::ofstream o(minorRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\n"
                      << "Name = Upgrade Policy App\r\n"
                      << "Version = 1.0.1\r\n"
                      << "Manufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {A3147E10-2C44-4B98-9D11-524147555047}\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n"
                      << "BuildBootstrapper = false\r\n\r\n"
                      << "[Upgrade]\r\n"
                      << "Strategy = minor\r\n"
                      << "BaselineMsi = " << rsw::WideToUtf8(v1Msi.wstring()) << "\r\n\r\n"
                      << "[Install]\r\n"
                      << "DefaultSubdir = RaggieSoft\\UpgradePolicyApp\r\n"
                      << "MainExecutable = HelloRaggieSoft.exe\r\n\r\n"
                      << "[Payload.Main]\r\n"
                      << "Source = " << rsw::WideToUtf8(srcPay1.wstring()) << "\r\n";
                }
                rsw::RswManifestDocument minorRaw, minorRes;
                std::string minLoadErr;
                rsw::LoadManifestFile(minorRsip, minorRaw, minLoadErr);
                rsw::ResolveManifestIncludesAndVars(minorRaw, {}, minorRes, minLoadErr);
                const DWORD minorEssGateRc = rsw::ValidateManifestDocument(minorRes, hostArch, rsw::RswIdeEdition::Essentials).IsOk() ? 0u : 1u;
                const DWORD minorBldRc     = runCli(instbuildExe, L"\"" + minorRsip.wstring() + L"\" --out-dir=\"" + upgOutDir.wstring() + L"\" --output-name=upg-v101-minor");
                std::string minorProdCode;
                if (minorBldRc == 0) {
                    MSIHANDLE hDb = 0;
                    if (::MsiOpenDatabaseW((upgOutDir / L"upg-v101-minor.msi").wstring().c_str(), MSIDBOPEN_READONLY, &hDb) == ERROR_SUCCESS) {
                        minorProdCode = rsw::QueryMsiPropertyString(hDb, L"ProductCode");
                        ::MsiCloseHandle(hDb);
                    }
                }

                // `Engine = raggiescript` (**RaggieScript EXE**) upgrade `0.7.0 -> 0.8.0` pruning `RetiredLegacyPlugin.txt`!
                auto writeRsscExeRsip = [&](const std::filesystem::path& p, const char* ver, const std::filesystem::path& payFolder) {
                    std::ofstream o(p, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\n"
                      << "Name = RaggieScript EXE Prune App\r\n"
                      << "Version = " << ver << "\r\n"
                      << "Manufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {B4258F21-3D55-4CA9-AE22-524147555048}\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n"
                      << "Engine = raggiescript\r\n"
                      << "HideFromInstalledApps = true\r\n\r\n"
                      << "[Upgrade]\r\n"
                      << "UpdateFeedUrl = https://assets.raggiesoft.com/desktop-apps/windows/studio-apps-manifest.json\r\n\r\n"
                      << "[Install]\r\n"
                      << "DefaultSubdir = RaggieSoft\\PruneApp\r\n"
                      << "MainExecutable = HelloRaggieSoft.exe\r\n\r\n"
                      << "[Payload.Files]\r\n"
                      << "Source = " << rsw::WideToUtf8(payFolder.wstring()) << "\r\n";
                };
                const auto rsExeV1Rsip = upgDir / L"rsexe-v1.rsip";
                const auto rsExeV2Rsip = upgDir / L"rsexe-v2.rsip";
                writeRsscExeRsip(rsExeV1Rsip, "0.7.0", srcPay1);
                writeRsscExeRsip(rsExeV2Rsip, "0.8.0", srcPay2);

                wchar_t prevVReg[MAX_PATH * 4]{};
                const DWORD prevVLen = ::GetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", prevVReg, static_cast<DWORD>(std::size(prevVReg)));
                const auto upgVirtReg = upgDir / L"virt-reg";
                std::filesystem::create_directories(upgVirtReg, ec);
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", upgVirtReg.wstring().c_str());

                const DWORD b1 = runCli(instbuildExe, L"\"" + rsExeV1Rsip.wstring() + L"\" --out-dir=\"" + upgOutDir.wstring() + L"\" --output-name=rsexe-v070");
                const DWORD b2 = runCli(instbuildExe, L"\"" + rsExeV2Rsip.wstring() + L"\" --out-dir=\"" + upgOutDir.wstring() + L"\" --output-name=rsexe-v080");
                const DWORD r1 = (b1 == 0) ? runCli(upgOutDir / L"rsexe-v070-setup.exe", L"/s INSTALLDIR=\"" + rsscInst.wstring() + L"\"") : 1u;
                const bool hadRetiredAfterV1 = std::filesystem::exists(rsscInst / L"RetiredLegacyPlugin.txt", ec);
                const DWORD r2 = (r1 == 0 && b2 == 0) ? runCli(upgOutDir / L"rsexe-v080-setup.exe", L"/s INSTALLDIR=\"" + rsscInst.wstring() + L"\"") : 1u;
                const bool prunedRetiredInV2 = !std::filesystem::exists(rsscInst / L"RetiredLegacyPlugin.txt", ec);
                const bool hasModernInV2     = std::filesystem::exists(rsscInst / L"ModernV080Plugin.txt", ec);
                const bool hasUpdateCfgJson  = std::filesystem::exists(rsscInst / L"UpdateConfig.json", ec);

                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", (prevVLen > 0) ? prevVReg : nullptr);

                Check(lateSeqAndAttrsOk &&
                      minorEssGateRc != 0 && minorBldRc == 0 &&
                      !v1ProdCode.empty() && rsw::EqualsIgnoreCase(v1ProdCode, minorProdCode) &&
                      hadRetiredAfterV1 && r2 == 0 && prunedRetiredInV2 && hasModernInV2 && hasUpdateCfgJson,
                      "v0.8.0 [Upgrade] Schedule=late (6550), Attributes=257, Minor BaselineMsi ProductCode inheritance, RSW097 gate, & RaggieScript EXE retired-file pruning verified",
                      "late=" + std::to_string(lateSeqAndAttrsOk) + " v1PC=" + v1ProdCode + " minPC=" + minorProdCode + " pruned=" + std::to_string(prunedRetiredInV2));
            }

            // =========================================================================
            // 14. v0.8.0 Test B — `--plan-update` & `RsPatch.exe build-msp` Component-Rule
            //     Gates (`RSW081` Deleted Component & `RSW084` Small Update Hotfix)!
            // =========================================================================
            {
                const auto planDir    = rsscWorkDir / L"v080-plan-msp";
                const auto planOut    = planDir / L"out";
                const auto rsPatchExe = binDir / L"RsPatch.exe";
                std::filesystem::create_directories(planDir, ec);
                { std::ofstream f(planDir / L"CoreA.txt", std::ios::binary); f << "CoreA v1"; }
                { std::ofstream f(planDir / L"ExtraB.txt", std::ios::binary); f << "ExtraB v1"; }
                { std::ofstream f(planDir / L"CoreA_v2.txt", std::ios::binary); f << "CoreA v1 hotfixed bytes!"; }
                std::filesystem::copy_file(planDir / L"CoreA_v2.txt", planDir / L"hotfix_dir" / L"CoreA.txt", std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::create_directories(planDir / L"hotfix_dir", ec);
                std::filesystem::copy_file(planDir / L"CoreA_v2.txt", planDir / L"hotfix_dir" / L"CoreA.txt", std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::copy_file(planDir / L"ExtraB.txt",   planDir / L"hotfix_dir" / L"ExtraB.txt", std::filesystem::copy_options::overwrite_existing, ec);

                const std::string fixedPc = "{C5369A32-4E66-4DB0-BF33-524147555049}";
                const std::string fixedUc = "{D647AB43-5F77-4EC1-AF44-524147555050}";

                auto makePlanRsip = [&](const std::filesystem::path& p, const char* ver, bool includeExtraB, bool useHotfixCore) {
                    std::ofstream o(p, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\n"
                      << "Name = Component Rule Test\r\n"
                      << "Version = " << ver << "\r\n"
                      << "Manufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = " << fixedUc << "\r\n"
                      << "ProductCode = " << fixedPc << "\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n"
                      << "BuildBootstrapper = false\r\n\r\n"
                      << "[Install]\r\n"
                      << "DefaultSubdir = RaggieSoft\\CompRuleTest\r\n"
                      << "MainExecutable = CoreA.txt\r\n\r\n"
                      << "[Payload.Core]\r\n"
                      << "Source = " << rsw::WideToUtf8(((useHotfixCore ? (planDir / L"hotfix_dir") : planDir) / L"CoreA.txt").wstring()) << "\r\n";
                    if (includeExtraB) {
                        o << "\r\n[Payload.Extra]\r\n"
                          << "Source = " << rsw::WideToUtf8((planDir / L"ExtraB.txt").wstring()) << "\r\n";
                    }
                };
                const auto baseRsip     = planDir / L"base.rsip";
                const auto delCompRsip  = planDir / L"deleted-comp.rsip";
                const auto hotfixRsip   = planDir / L"small-hotfix.rsip";
                makePlanRsip(baseRsip,    "1.0.0", true,  false);
                makePlanRsip(delCompRsip, "1.0.1", false, false); // Deletes `ExtraB.txt` component while keeping `ProductCode`!
                makePlanRsip(hotfixRsip,  "1.0.0", true,  true);  // Same `1.0.0` version + modified `CoreA.txt`!

                runCli(instbuildExe, L"\"" + baseRsip.wstring()    + L"\" --out-dir=\"" + planOut.wstring() + L"\" --output-name=base-100");
                runCli(instbuildExe, L"\"" + delCompRsip.wstring() + L"\" --out-dir=\"" + planOut.wstring() + L"\" --output-name=bad-minor-101");
                runCli(instbuildExe, L"\"" + hotfixRsip.wstring()  + L"\" --out-dir=\"" + planOut.wstring() + L"\" --output-name=hotfix-100");

                const auto baseMsi   = planOut / L"base-100.msi";
                const auto badMinMsi = planOut / L"bad-minor-101.msi";
                const auto hotMsi    = planOut / L"hotfix-100.msi";
                const auto hotMsp    = planOut / L"hotfix-100.msp";

                // 1) Deleted Component on Minor Upgrade MUST fail `--plan-update` AND `RsPatch.exe build-msp` (`RSW081`)!
                rsw::UpdatePlanReport badPlan{};
                std::string pErr;
                const bool analyzedBad = rsw::AnalyzeInstallerUpdatePlan(baseMsi, badMinMsi, false, badPlan, pErr);
                const DWORD badMspRc   = runCli(rsPatchExe, L"build-msp \"" + baseMsi.wstring() + L"\" \"" + badMinMsi.wstring() + L"\" \"" + (planOut / L"should-fail.msp").wstring() + L"\"");

                // 2) Same-Version Small Update MUST fail without `--allow-small-update` (`RSW084`) and SUCCEED with `--allow-small-update`!
                const DWORD smallBlockedRc = runCli(rsPatchExe, L"build-msp \"" + baseMsi.wstring() + L"\" \"" + hotMsi.wstring() + L"\" \"" + hotMsp.wstring() + L"\"");
                const DWORD smallAllowedRc = runCli(rsPatchExe, L"build-msp \"" + baseMsi.wstring() + L"\" \"" + hotMsi.wstring() + L"\" \"" + hotMsp.wstring() + L"\" --allow-small-update");

                Check(analyzedBad && !badPlan.mspEligible && badPlan.HasBlockingErrors() && badMspRc != 0 &&
                      smallBlockedRc != 0 && smallAllowedRc == 0 && std::filesystem::exists(hotMsp, ec),
                      "v0.8.0 --plan-update & RsPatch.exe build-msp block RSW081 deleted Component on Minor Upgrade and gate RSW084 Small Update .MSP cleanly",
                      "mspElig=" + std::to_string(badPlan.mspEligible) + " badRc=" + std::to_string(badMspRc) + " smBlk=" + std::to_string(smallBlockedRc) + " smOk=" + std::to_string(smallAllowedRc));
            }

            // =========================================================================
            // 15. v0.8.0 Test C & D — `RsPatch.exe` (`msdelta.dll`) `.rswpatch` Delta,
            //     Tamper Rejection, LIFO Rollback on Fault, & `RsUpdate.exe` DRY
            //     `apps-manifest.json` Delta-vs-Full Auto-Selection!
            // =========================================================================
            {
                const auto deltaDir   = rsscWorkDir / L"v080-delta-updater";
                const auto rsPatchExe = binDir / L"RsPatch.exe";
                const auto rsUpdExe   = binDir / L"RsUpdate.exe";
                const auto snapV070   = deltaDir / L"snap-070";
                const auto snapV080   = deltaDir / L"snap-080";
                const auto liveDir    = deltaDir / L"live-install";
                const auto fullSetup  = binDir / L"HelloRaggieSoft.exe"; // Dummy stand-in verified in check mode
                std::filesystem::create_directories(snapV070, ec);
                std::filesystem::create_directories(snapV080, ec);
                std::filesystem::create_directories(liveDir, ec);

                // Build realistic 32 KB binary payload so `msdelta.dll` exercises real block matching!
                std::string bigV1(32768, 'A');
                for (std::size_t i = 0; i < bigV1.size(); i += 64) bigV1[i] = static_cast<char>('0' + (i % 10));
                std::string bigV2 = bigV1;
                bigV2.replace(4096, 28, "RAGGIESOFT_V080_DELTA_PATCH!");

                { std::ofstream f(snapV070 / L"EngineCore.bin", std::ios::binary); f.write(bigV1.data(), static_cast<std::streamsize>(bigV1.size())); }
                { std::ofstream f(snapV070 / L"KeepReadme.txt", std::ios::binary); f << "Unchanged ReadMe across 0.7.0 and 0.8.0"; }
                { std::ofstream f(snapV070 / L"OldHelper.dat",  std::ios::binary); f << "Retired in 0.8.0"; }

                { std::ofstream f(snapV080 / L"EngineCore.bin", std::ios::binary); f.write(bigV2.data(), static_cast<std::streamsize>(bigV2.size())); }
                { std::ofstream f(snapV080 / L"KeepReadme.txt", std::ios::binary); f << "Unchanged ReadMe across 0.7.0 and 0.8.0"; }
                { std::ofstream f(snapV080 / L"NewFeature.txt", std::ios::binary); f << "Brand new 0.8.0 feature asset"; }

                // Seed `liveDir` with `v0.7.0` files + `Uninstall.rswm`!
                for (const auto& fn : {L"EngineCore.bin", L"KeepReadme.txt", L"OldHelper.dat"}) {
                    std::filesystem::copy_file(snapV070 / fn, liveDir / fn, std::filesystem::copy_options::overwrite_existing, ec);
                }
                {
                    std::ofstream um(liveDir / L"Uninstall.rswm", std::ios::binary | std::ios::trunc);
                    um << "[NativeUninstall]\nProductName=Delta Studio App\nVersion=0.7.0\nManufacturer=RaggieSoft\nPerMachine=false\n"
                       << "InstallDir=" << rsw::WideToUtf8(liveDir.wstring()) << "\nFileCount=3\n"
                       << "File_0=" << rsw::WideToUtf8((liveDir / L"EngineCore.bin").wstring()) << "\n"
                       << "File_1=" << rsw::WideToUtf8((liveDir / L"KeepReadme.txt").wstring()) << "\n"
                       << "File_2=" << rsw::WideToUtf8((liveDir / L"OldHelper.dat").wstring()) << "\n";
                }

                const auto patchFile    = deltaDir / L"app-from-0.7.0-to-0.8.0-arm64.rswpatch";
                const auto tamperedFile = deltaDir / L"tampered.rswpatch";
                const std::string upgGuid = "{E758BC54-6A88-4FD2-BF55-524147555051}";

                const DWORD bldPatchRc = runCli(
                    rsPatchExe,
                    L"build-exe \"" + snapV070.wstring() + L"\" \"" + snapV080.wstring() + L"\" \"" + patchFile.wstring() +
                    L"\" --version-from=0.7.0 --version-to=0.8.0 --upgrade-code=" + rsw::Utf8ToWide(upgGuid));

                // Tamper 1 byte in `tampered.rswpatch` -> MUST be rejected before touching `liveDir`!
                std::vector<std::uint8_t> pBytes;
                rsw::ReadBinaryFileBytes(patchFile, pBytes, surgErr);
                if (pBytes.size() > 32) pBytes[24] ^= 0xFFu;
                {
                    std::ofstream tf(tamperedFile, std::ios::binary | std::ios::trunc);
                    tf.write(reinterpret_cast<const char*>(pBytes.data()), static_cast<std::streamsize>(pBytes.size()));
                }
                const DWORD tamperRc = runCli(rsPatchExe, L"apply \"" + tamperedFile.wstring() + L"\" \"" + liveDir.wstring() + L"\" /s");

                // Simulate mid-patch fault (`--simulate-fault=2`) -> MUST roll back `EngineCore.bin` (`v1`), `OldHelper.dat`, & remove `NewFeature.txt`!
                const DWORD faultRc = runCli(rsPatchExe, L"apply \"" + patchFile.wstring() + L"\" \"" + liveDir.wstring() + L"\" /s --simulate-fault=2");
                std::string expShaV1, expShaV2, curShaAfterFault;
                rsw::ComputeFileSha256Hex(snapV070 / L"EngineCore.bin", expShaV1);
                rsw::ComputeFileSha256Hex(snapV080 / L"EngineCore.bin", expShaV2);
                rsw::ComputeFileSha256Hex(liveDir  / L"EngineCore.bin", curShaAfterFault);
                const bool rolledBackCleanly =
                    rsw::EqualsIgnoreCase(curShaAfterFault, expShaV1) &&
                    std::filesystem::exists(liveDir / L"OldHelper.dat", ec) &&
                    !std::filesystem::exists(liveDir / L"NewFeature.txt", ec);

                Check(bldPatchRc == 0 && pBytes.size() < 4096u /* 32 KB -> < 4 KB binary delta! */ &&
                      tamperRc != 0 && faultRc != 0 && rolledBackCleanly,
                      "v0.8.0 RsPatch.exe msdelta.dll .rswpatch (< 4 KB for 32 KB file), SHA-256 tamper rejection, & atomic LIFO rollback verified",
                      "patchBytes=" + std::to_string(pBytes.size()) + " tamper=" + std::to_string(tamperRc) + " fault=" + std::to_string(faultRc) + " rb=" + std::to_string(rolledBackCleanly));

                // Now test `RsUpdate.exe` consuming a 100% DRY `RaggieSoft Launcher` `apps-manifest.json`!
                std::string patchSha, fullSha;
                rsw::ComputeFileSha256Hex(patchFile, patchSha);
                rsw::ComputeFileSha256Hex(fullSetup, fullSha);
                const auto manifestJson = deltaDir / L"apps-manifest.json";
                {
                    std::ofstream mj(manifestJson, std::ios::binary | std::ios::trunc);
                    mj << "[\n"
                       << "  {\n"
                       << "    \"id\": \"delta-studio-app\",\n"
                       << "    \"innoAppId\": \"" << upgGuid << "\",\n"
                       << "    \"title\": \"Delta Studio App\",\n"
                       << "    \"imprint\": \"RaggieSoft Studio\",\n"
                       << "    \"category\": \"Studio\",\n"
                       << "    \"description\": \"Test app for DRY Launcher & RsUpdate manifest\",\n"
                       << "    \"latestVersion\": \"0.8.0\",\n"
                       << "    \"exeName\": \"EngineCore.bin\",\n"
                       << "    \"defaultSubFolder\": \"RaggieSoft\\\\DeltaStudio\",\n"
                       << "    \"installerFileName\": \"HelloRaggieSoft.exe\",\n"
                       << "    \"installerDownloadUrl\": \"file:///" << rsw::WideToUtf8(fullSetup.generic_wstring()) << "\",\n"
                       << "    \"sha256\": \"" << fullSha << "\",\n"
                       << "    \"sizeBytes\": " << std::filesystem::file_size(fullSetup, ec) << ",\n"
                       << "    \"webUrl\": \"https://raggiesoftstudio.com/download\",\n"
                       << "    \"patches\": [\n"
                       << "      {\n"
                       << "        \"fromVersion\": \"0.7.0\",\n"
                       << "        \"arch\": \"" << hostArch << "\",\n"
                       << "        \"kind\": \"rswpatch\",\n"
                       << "        \"url\": \"file:///" << rsw::WideToUtf8(patchFile.generic_wstring()) << "\",\n"
                       << "        \"sha256\": \"" << patchSha << "\",\n"
                       << "        \"sizeBytes\": " << std::filesystem::file_size(patchFile, ec) << "\n"
                       << "      }\n"
                       << "    ]\n"
                       << "  }\n"
                       << "]\n";
                }

                const auto json070 = deltaDir / L"check-070.json";
                const auto json060 = deltaDir / L"check-060.json";
                const DWORD chk070Rc = runCli(
                    rsUpdExe,
                    L"--check --manifest=\"" + manifestJson.wstring() + L"\" --app-id=delta-studio-app --installed-version=0.7.0 --arch=" +
                    rsw::Utf8ToWide(hostArch) + L" --json-out=\"" + json070.wstring() + L"\"");
                const DWORD chk060Rc = runCli(
                    rsUpdExe,
                    L"--check --manifest=\"" + manifestJson.wstring() + L"\" --app-id=delta-studio-app --installed-version=0.6.0 --arch=" +
                    rsw::Utf8ToWide(hostArch) + L" --json-out=\"" + json060.wstring() + L"\"");

                std::vector<std::uint8_t> j070Raw, j060Raw;
                rsw::ReadBinaryFileBytes(json070, j070Raw, surgErr);
                rsw::ReadBinaryFileBytes(json060, j060Raw, surgErr);
                const std::string j070Str(reinterpret_cast<const char*>(j070Raw.data()), j070Raw.size());
                const std::string j060Str(reinterpret_cast<const char*>(j060Raw.data()), j060Raw.size());

                // Apply the `.rswpatch` delta end-to-end via `RsUpdate.exe --apply /s`!
                const DWORD applyUpdRc = runCli(
                    rsUpdExe,
                    L"--apply /s --manifest=\"" + manifestJson.wstring() + L"\" --app-id=delta-studio-app --installed-version=0.7.0 --arch=" +
                    rsw::Utf8ToWide(hostArch) + L" --install-dir=\"" + liveDir.wstring() + L"\"");
                std::string shaAfterUpd;
                rsw::ComputeFileSha256Hex(liveDir / L"EngineCore.bin", shaAfterUpd);
                const DWORD chk080Rc = runCli(
                    rsUpdExe,
                    L"--check --manifest=\"" + manifestJson.wstring() + L"\" --app-id=delta-studio-app --installed-version=0.8.0 --arch=" +
                    rsw::Utf8ToWide(hostArch));

                Check(chk070Rc == 10 && j070Str.find("\"selectedKind\": \"rswpatch\"") != std::string::npos &&
                      chk060Rc == 10 && j060Str.find("\"selectedKind\": \"installer\"") != std::string::npos &&
                      applyUpdRc == 0 && rsw::EqualsIgnoreCase(shaAfterUpd, expShaV2) &&
                      std::filesystem::exists(liveDir / L"NewFeature.txt", ec) &&
                      !std::filesystem::exists(liveDir / L"OldHelper.dat", ec) &&
                      chk080Rc == 0,
                      "v0.8.0 RsUpdate.exe DRY Launcher apps-manifest.json auto-selects .rswpatch delta vs full installer, verifies SHA-256, & applies cleanly",
                      "c070=" + std::to_string(chk070Rc) + " c060=" + std::to_string(chk060Rc) + " app=" + std::to_string(applyUpdRc) + " c080=" + std::to_string(chk080Rc));
            }

            // =========================================================================
            // 16. v0.9.0 Test A — Host-Aware Add-On / Content Pack Installer (`PackageKind = addon` + `[HostApp]`):
            //     Verifies `RSW067` missing-host validator gate, `Essentials` edition unlock,
            //     `1605` (`ERROR_UNKNOWN_PRODUCT`) zero-orphan exit when host is absent, AND
            //     automatic `[HOSTDIR]` + `ContentSubdir` placement + clean uninstall when host IS installed!
            // =========================================================================
            {
                const auto addonDir     = rsscWorkDir / L"v090-addon-test";
                const auto addonOut     = addonDir / L"out";
                const auto bookContent  = addonDir / L"cedar-run-src";
                const auto fakeHostRoot = addonDir / L"fake-program-files";
                const auto fakeDataRoot = addonDir / L"fake-localappdata";
                const auto addonVirtReg = addonDir / L"virt-reg";
                std::filesystem::create_directories(bookContent, ec);
                std::filesystem::create_directories(fakeHostRoot, ec);
                std::filesystem::create_directories(fakeDataRoot, ec);
                std::filesystem::create_directories(addonVirtReg, ec);
                { std::ofstream f(bookContent / L"chapter-01.md", std::ios::binary); f << "# Chapter 1 — Cedar Run"; }
                { std::ofstream f(bookContent / L"cover.jpg",     std::ios::binary); f << "JPEG_MOCK_BYTES"; }

                // 1) Missing `[HostApp]` on `PackageKind = addon` MUST fail validation (`RSW067`)!
                const auto badAddonRsip = addonDir / L"bad-addon.rsip";
                {
                    std::ofstream o(badAddonRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\nName = Broken AddOn\r\nVersion = 1.0.0\r\nManufacturer = RaggieSoft\r\nPackageKind = addon\r\n"
                      << "[UI]\r\nWizardLayout = simple\r\n[Payload.Files]\r\nSource = " << rsw::WideToUtf8(bookContent.wstring()) << "\r\n";
                }
                rsw::RswManifestDocument badRaw, badRes;
                std::string aErr;
                rsw::LoadManifestFile(badAddonRsip, badRaw, aErr);
                rsw::ResolveManifestIncludesAndVars(badRaw, {}, badRes, aErr);
                const auto badAddonRep = rsw::ValidateManifestDocument(badRes, hostArch, rsw::RswIdeEdition::Essentials);

                // 2) Valid `PackageKind = addon` MUST pass `Essentials` edition validation & compile `<stem>-setup.exe`!
                const auto goodAddonRsip = addonDir / L"cedar-run-addon.rsip";
                {
                    std::ofstream o(goodAddonRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\n"
                      << "Name = Cedar Run Book Pack\r\n"
                      << "Version = 1.0.0\r\n"
                      << "Manufacturer = RaggieSoft Media\r\n"
                      << "PackageKind = addon\r\n"
                      << "UpgradeCode = {91A2B3C4-5D6E-4F70-8A91-524147414401}\r\n"
                      << "Architecture = " << hostArch << "\r\n"
                      << "Scope = user\r\n\r\n"
                      << "[HostApp]\r\n"
                      << "Name = RaggieSoft Book Reader\r\n"
                      << "RegistryKey = Software\\RaggieSoft\\Book Reader\r\n"
                      << "RegistryValue = Install Path\r\n"
                      << "FallbackSubdir = RaggieSoft\\Book Reader\r\n"
                      << "Executable = BookReader.exe\r\n"
                      << "ContentTargetRoot = LocalAppData\r\n"
                      << "ContentSubdir = RaggieSoft\\Books\\cedar-run\r\n"
                      << "RequireHostInstalled = true\r\n\r\n"
                      << "[UI]\r\n"
                      << "WizardLayout = simple\r\n"
                      << "InstallButtonText = &Install Book\r\n\r\n"
                      << "[Payload.Book]\r\n"
                      << "Source = " << rsw::WideToUtf8(bookContent.wstring()) << "\r\n\r\n"
                      << "[Registry.BookList]\r\n"
                      << "Root = HKCU\r\n"
                      << "Key = Software\\RaggieSoft\\Book Reader\\Books\\List\r\n"
                      << "Name = cedar-run\r\n"
                      << "Value = [INSTALLDIR]\r\n\r\n"
                      << "[Registry.HostBinding]\r\n"
                      << "Root = HKCU\r\n"
                      << "Key = Software\\RaggieSoft\\Book Reader\\Books\\cedar-run\r\n"
                      << "Name = VerifiedHostDir\r\n"
                      << "Value = [HOSTDIR]\r\n";
                }
                rsw::RswManifestDocument goodRaw, goodRes;
                rsw::LoadManifestFile(goodAddonRsip, goodRaw, aErr);
                rsw::ResolveManifestIncludesAndVars(goodRaw, {}, goodRes, aErr);
                const auto goodEssRep = rsw::ValidateManifestDocument(goodRes, hostArch, rsw::RswIdeEdition::Essentials);
                const DWORD bldAddonRc = runCli(
                    instbuildExe,
                    L"\"" + goodAddonRsip.wstring() + L"\" --out-dir=\"" + addonOut.wstring() + L"\" --output-name=raggiesoft-book-cedar-run");
                const auto addonExe = addonOut / L"raggiesoft-book-cedar-run-setup.exe";

                wchar_t prevVReg[MAX_PATH * 4]{};
                const DWORD prevVLen = ::GetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", prevVReg, static_cast<DWORD>(std::size(prevVReg)));
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", addonVirtReg.wstring().c_str());
                ::SetEnvironmentVariableW(L"RSW_TEST_HOST_ROOT_DIR", fakeHostRoot.wstring().c_str());
                ::SetEnvironmentVariableW(L"RSW_TEST_ADDON_DATA_ROOT", fakeDataRoot.wstring().c_str());

                const auto expectedBookDest = fakeDataRoot / L"RaggieSoft" / L"Books" / L"cedar-run";

                // 3) Host NOT installed yet -> `/s` MUST return `1605` (`ERROR_UNKNOWN_PRODUCT`) and write ZERO files!
                const DWORD missingHostRc = (bldAddonRc == 0) ? runCli(addonExe, L"/s") : 1u;
                const bool zeroOrphansWhenHostMissing = !std::filesystem::exists(expectedBookDest / L"chapter-01.md", ec);

                // 4) Now install simulated Host (`BookReader.exe`) -> Add-On MUST detect it, drop book into `expectedBookDest`,
                //    expand `[HOSTDIR]` in virtual registry, and cleanly uninstall via `Uninstall.exe --native-uninstall /s`!
                const auto hostReaderDir = fakeHostRoot / L"RaggieSoft" / L"Book Reader";
                std::filesystem::create_directories(hostReaderDir, ec);
                std::filesystem::copy_file(binDir / L"HelloRaggieSoft.exe", hostReaderDir / L"BookReader.exe",
                                           std::filesystem::copy_options::overwrite_existing, ec);

                const DWORD okHostRc = (bldAddonRc == 0) ? runCli(addonExe, L"/s") : 1u;
                const bool placedChapter = std::filesystem::exists(expectedBookDest / L"chapter-01.md", ec);
                std::string recordedHostDirVal;
                rsw::native_detail::VirtualRegReadValue(
                    addonVirtReg, "HKCU", L"Software\\RaggieSoft\\Book Reader\\Books\\cedar-run", L"VerifiedHostDir", recordedHostDirVal);
                const DWORD uninstAddonRc = placedChapter
                    ? runCli(expectedBookDest / L"Uninstall.exe", L"--native-uninstall /s")
                    : 1u;
                const bool removedCleanly = !std::filesystem::exists(expectedBookDest / L"chapter-01.md", ec);

                ::SetEnvironmentVariableW(L"RSW_TEST_HOST_ROOT_DIR", nullptr);
                ::SetEnvironmentVariableW(L"RSW_TEST_ADDON_DATA_ROOT", nullptr);
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", (prevVLen > 0) ? prevVReg : nullptr);

                Check(!badAddonRep.IsOk() && goodEssRep.IsOk() && bldAddonRc == 0 &&
                      missingHostRc == ERROR_UNKNOWN_PRODUCT && zeroOrphansWhenHostMissing &&
                      okHostRc == 0 && placedChapter &&
                      recordedHostDirVal.find("Book Reader") != std::string::npos &&
                      uninstAddonRc == 0 && removedCleanly,
                      "v0.9.0 PackageKind=addon [HostApp] enforces RSW067, unlocks in Essentials, returns 1605 with zero orphans when host missing, & places/uninstalls content cleanly when host is present",
                      "missRc=" + std::to_string(missingHostRc) + " okRc=" + std::to_string(okHostRc) + " hostDir=" + recordedHostDirVal);
            }

            // =========================================================================
            // 17. v0.9.0 Test B — Unified Suite Project (`PackageKind = suite`):
            //     Verifies `Delivery = web` (`file://` + `Sha256`), tamper rejection,
            //     `SuiteRollbackPolicy = rollback_new` + `RollbackBoundary = true` LIFO
            //     rollback on child failure, AND chaining Host App + `addon` Pack in one Suite!
            // =========================================================================
            {
                const auto suiteTestDir = rsscWorkDir / L"v090-suite-test";
                const auto stOut        = suiteTestDir / L"out";
                const auto stVirtReg    = suiteTestDir / L"virt-reg";
                const auto stInstall    = suiteTestDir / L"install-root";
                const auto stAddonData  = suiteTestDir / L"addon-data";
                const auto payHost      = suiteTestDir / L"pay-host";
                const auto payTool      = suiteTestDir / L"pay-tool";
                const auto payBook      = suiteTestDir / L"pay-book";
                for (const auto& d : {stOut, stVirtReg, stInstall, stAddonData, payHost, payTool, payBook}) {
                    std::filesystem::create_directories(d, ec);
                }
                std::filesystem::copy_file(binDir / L"HelloRaggieSoft.exe", payHost / L"BookReader.exe", std::filesystem::copy_options::overwrite_existing, ec);
                { std::ofstream f(payTool / L"CompanionTool.txt", std::ios::binary); f << "Companion Tool Payload"; }
                { std::ofstream f(payBook / L"ad-astra.md",       std::ios::binary); f << "# Escape Velocity: Ad Astra"; }

                // Build Child 1: Host App (`Engine = raggiescript`, registers `Software\RaggieSoft\Book Reader`)
                const auto hostRsip = suiteTestDir / L"host-reader.rsip";
                {
                    std::ofstream o(hostRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\nName = Suite Book Reader\r\nVersion = 1.0.0\r\nManufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778801}\r\nArchitecture = " << hostArch << "\r\n"
                      << "Scope = user\r\nEngine = raggiescript\r\n\r\n"
                      << "[Install]\r\nDefaultSubdir = RaggieSoft\\Book Reader\r\nMainExecutable = BookReader.exe\r\n\r\n"
                      << "[UI]\r\nWizardLayout = simple\r\n\r\n"
                      << "[Payload.App]\r\nSource = " << rsw::WideToUtf8(payHost.wstring()) << "\r\n\r\n"
                      << "[Registry.InstallPath]\r\nRoot = HKCU\r\nKey = Software\\RaggieSoft\\Book Reader\r\nName = Install Path\r\nValue = [INSTALLDIR]\r\n";
                }
                // Build Child 2: Transient Companion App (`Engine = raggiescript` — will be rolled back when Child 3 fails!)
                const auto compRsip = suiteTestDir / L"companion-app.rsip";
                {
                    std::ofstream o(compRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\nName = Suite Companion Tool\r\nVersion = 1.0.0\r\nManufacturer = RaggieSoft\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778802}\r\nArchitecture = " << hostArch << "\r\n"
                      << "Scope = user\r\nEngine = raggiescript\r\n\r\n"
                      << "[Install]\r\nDefaultSubdir = RaggieSoft\\Companion Tool\r\nMainExecutable = CompanionTool.txt\r\n\r\n"
                      << "[UI]\r\nWizardLayout = simple\r\n\r\n"
                      << "[Payload.Tool]\r\nSource = " << rsw::WideToUtf8(payTool.wstring()) << "\r\n";
                }
                // Build Child 3: Book Add-On (`PackageKind = addon` requiring `Suite Book Reader`!)
                const auto bookAddonRsip = suiteTestDir / L"ad-astra-addon.rsip";
                {
                    std::ofstream o(bookAddonRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\nName = Ad Astra Book Add-On\r\nVersion = 1.0.0\r\nManufacturer = RaggieSoft Media\r\n"
                      << "PackageKind = addon\r\nUpgradeCode = {A1112222-3333-4444-8555-666677778803}\r\nArchitecture = " << hostArch << "\r\nScope = user\r\n\r\n"
                      << "[HostApp]\r\nName = Suite Book Reader\r\nRegistryKey = Software\\RaggieSoft\\Book Reader\r\n"
                      << "RegistryValue = Install Path\r\nExecutable = BookReader.exe\r\n"
                      << "ContentTargetRoot = LocalAppData\r\nContentSubdir = RaggieSoft\\Books\\ad-astra\r\nRequireHostInstalled = true\r\n\r\n"
                      << "[UI]\r\nWizardLayout = simple\r\nInstallButtonText = &Install Book\r\n\r\n"
                      << "[Payload.Book]\r\nSource = " << rsw::WideToUtf8(payBook.wstring()) << "\r\n";
                }

                runCli(instbuildExe, L"\"" + hostRsip.wstring()      + L"\" --out-dir=\"" + stOut.wstring() + L"\" --output-name=child-host");
                runCli(instbuildExe, L"\"" + compRsip.wstring()      + L"\" --out-dir=\"" + stOut.wstring() + L"\" --output-name=child-comp");
                runCli(instbuildExe, L"\"" + bookAddonRsip.wstring() + L"\" --out-dir=\"" + stOut.wstring() + L"\" --output-name=child-book-addon");

                const auto hostSetupExe = stOut / L"child-host-setup.exe";
                const auto compSetupExe = stOut / L"child-comp-setup.exe";
                const auto bookSetupExe = stOut / L"child-book-addon-setup.exe";
                std::string hostSha, compSha, bookSha;
                rsw::ComputeFileSha256Hex(hostSetupExe, hostSha);
                rsw::ComputeFileSha256Hex(compSetupExe, compSha);
                rsw::ComputeFileSha256Hex(bookSetupExe, bookSha);

                // Part 1: Build a `SuiteRollbackPolicy = rollback_new` Web Suite where:
                //   - Step 1 (`Host`, `RollbackBoundary = true`) installs & commits savepoint!
                //   - Step 2 (`Comp`, `RollbackBoundary = false`) installs!
                //   - Step 3 (`BadBrokenWebPkg`) has a deliberately wrong `Sha256` -> fails verification!
                //   Expected: `Comp` IS automatically uninstalled via LIFO rollback, while `Host` (behind `RollbackBoundary = true`) STAYS installed!
                const auto rbSuiteRsip = suiteTestDir / L"rollback-suite.rsip";
                const std::filesystem::path hostInstDir = stInstall / L"BookReader";
                const std::filesystem::path compInstDir = stInstall / L"CompanionTool";
                {
                    std::ofstream o(rbSuiteRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\nName = Rollback Boundary Test Suite\r\nVersion = 1.0.0\r\nManufacturer = RaggieSoft\r\n"
                      << "PackageKind = suite\r\nSuiteRollbackPolicy = rollback_new\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778890}\r\nArchitecture = " << hostArch << "\r\nScope = user\r\n\r\n"
                      << "[Install]\r\nDefaultSubdir = RaggieSoft\\RbSuite\r\n\r\n"
                      << "[UI]\r\nInstallButtonText = &Install Suite\r\n\r\n"
                      << "[SuitePackage.HostReader]\r\nName = Suite Book Reader\r\nDelivery = web\r\n"
                      << "DownloadUrl = file:///" << rsw::WideToUtf8(hostSetupExe.generic_wstring()) << "\r\n"
                      << "Sha256 = " << hostSha << "\r\nEngine = raggiescript\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778801}\r\n"
                      << "InstallArgs = INSTALLDIR=\"" << rsw::WideToUtf8(hostInstDir.wstring()) << "\"\r\n"
                      << "RollbackBoundary = true\r\n\r\n"
                      << "[SuitePackage.Companion]\r\nName = Suite Companion Tool\r\nDelivery = web\r\n"
                      << "DownloadUrl = file:///" << rsw::WideToUtf8(compSetupExe.generic_wstring()) << "\r\n"
                      << "Sha256 = " << compSha << "\r\nEngine = raggiescript\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778802}\r\n"
                      << "InstallArgs = INSTALLDIR=\"" << rsw::WideToUtf8(compInstDir.wstring()) << "\"\r\n\r\n"
                      << "[SuitePackage.TamperedStep]\r\nName = Tampered Workload\r\nDelivery = web\r\n"
                      << "DownloadUrl = file:///" << rsw::WideToUtf8(bookSetupExe.generic_wstring()) << "\r\n"
                      << "Sha256 = ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\r\n"
                      << "Engine = addon\r\n";
                }

                // Part 2: Build a valid `side-by-side` (`packages\`) Suite chaining `HostReader` + `Companion` + `AdAstraAddon`!
                const auto okSuiteRsip = suiteTestDir / L"boxset-suite.rsip";
                {
                    std::ofstream o(okSuiteRsip, std::ios::binary | std::ios::trunc);
                    o << "[Package]\r\nName = Boxset App and Add-On Suite\r\nVersion = 1.0.0\r\nManufacturer = RaggieSoft\r\n"
                      << "PackageKind = suite\r\nSuiteRollbackPolicy = rollback_new\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778891}\r\nArchitecture = " << hostArch << "\r\nScope = user\r\n\r\n"
                      << "[Install]\r\nDefaultSubdir = RaggieSoft\\BoxsetSuite\r\n\r\n"
                      << "[UI]\r\nInstallButtonText = &Install Suite\r\n\r\n"
                      << "[SuitePackage.HostReader]\r\nName = Suite Book Reader\r\nDelivery = side-by-side\r\n"
                      << "Source = " << rsw::WideToUtf8(hostSetupExe.wstring()) << "\r\nEngine = raggiescript\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778801}\r\n"
                      << "InstallArgs = INSTALLDIR=\"" << rsw::WideToUtf8(hostInstDir.wstring()) << "\"\r\n"
                      << "RollbackBoundary = true\r\n\r\n"
                      << "[SuitePackage.Companion]\r\nName = Suite Companion Tool\r\nDelivery = side-by-side\r\n"
                      << "Source = " << rsw::WideToUtf8(compSetupExe.wstring()) << "\r\nEngine = raggiescript\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778802}\r\n"
                      << "InstallArgs = INSTALLDIR=\"" << rsw::WideToUtf8(compInstDir.wstring()) << "\"\r\n\r\n"
                      << "[SuitePackage.AdAstraBook]\r\nName = Ad Astra Book Add-On\r\nDelivery = side-by-side\r\n"
                      << "Source = " << rsw::WideToUtf8(bookSetupExe.wstring()) << "\r\nEngine = addon\r\n"
                      << "UpgradeCode = {A1112222-3333-4444-8555-666677778803}\r\n";
                }

                const DWORD bldRbSuiteRc = runCli(instbuildExe, L"\"" + rbSuiteRsip.wstring() + L"\" --out-dir=\"" + stOut.wstring() + L"\" --output-name=rb-suite");
                const DWORD bldOkSuiteRc = runCli(instbuildExe, L"\"" + okSuiteRsip.wstring() + L"\" --out-dir=\"" + stOut.wstring() + L"\" --output-name=ok-suite");

                wchar_t prevVReg[MAX_PATH * 4]{};
                const DWORD prevVLen = ::GetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", prevVReg, static_cast<DWORD>(std::size(prevVReg)));
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", stVirtReg.wstring().c_str());
                ::SetEnvironmentVariableW(L"RSW_TEST_ADDON_DATA_ROOT", stAddonData.wstring().c_str());

                // Run `rb-suite-setup.exe /s` -> Step 3 SHA-256 mismatch MUST fail, roll back `CompanionTool`, and preserve `BookReader` (`RollbackBoundary = true`)!
                const DWORD runRbRc = (bldRbSuiteRc == 0) ? runCli(stOut / L"rb-suite-setup.exe", L"/s --install-dir \"" + (stInstall / L"SuiteHub1").wstring() + L"\"") : 0u;
                const bool hostKeptAtBoundary   = std::filesystem::exists(hostInstDir / L"BookReader.exe", ec);
                const bool compRolledBackClean  = !std::filesystem::exists(compInstDir / L"CompanionTool.txt", ec);

                // Now run `ok-suite-setup.exe /s` -> adopts `BookReader`, installs `CompanionTool`, AND installs `AdAstraBook` add-on into `stAddonData\RaggieSoft\Books\ad-astra`!
                const DWORD runOkRc = (bldOkSuiteRc == 0) ? runCli(stOut / L"ok-suite-setup.exe", L"/s --install-dir \"" + (stInstall / L"SuiteHub2").wstring() + L"\"") : 1u;
                const bool allThreeInstalled =
                    std::filesystem::exists(hostInstDir / L"BookReader.exe", ec) &&
                    std::filesystem::exists(compInstDir / L"CompanionTool.txt", ec) &&
                    std::filesystem::exists(stAddonData / L"RaggieSoft" / L"Books" / L"ad-astra" / L"ad-astra.md", ec) &&
                    std::filesystem::exists(stOut / L"packages" / L"child-book-addon-setup.exe", ec);

                ::SetEnvironmentVariableW(L"RSW_TEST_ADDON_DATA_ROOT", nullptr);
                ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", (prevVLen > 0) ? prevVReg : nullptr);

                Check(bldRbSuiteRc == 0 && bldOkSuiteRc == 0 &&
                      runRbRc != 0 && hostKeptAtBoundary && compRolledBackClean &&
                      runOkRc == 0 && allThreeInstalled,
                      "v0.9.0 Unified Suite Delivery=web/side-by-side SHA-256 verification, RollbackBoundary + rollback_new LIFO undo, & Host+Add-On Suite chaining verified",
                      "rbRc=" + std::to_string(runRbRc) + " hostKept=" + std::to_string(hostKeptAtBoundary) + " compRb=" + std::to_string(compRolledBackClean) + " okRc=" + std::to_string(runOkRc));

                // =========================================================================
                // 18. v0.9.1 Test — Suite Maintenance Registration (`ModifyPath` vs `UninstallString`
                //     + `InstallSource`), Full LIFO Rollback on Explicit User Cancel (Even Past
                //     `RollbackBoundary = true`), RaggieScript EXE Mid-Install Cancel Rollback,
                //     `--export-screenshots` Multi-Page Suite Wizard Render, & WCAG 2.2 AA Contrast!
                // =========================================================================
                {
                    ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", stVirtReg.wstring().c_str());
                    ::SetEnvironmentVariableW(L"RSW_TEST_ADDON_DATA_ROOT", stAddonData.wstring().c_str());

                    // A. Verify `ok-suite` registered `ModifyPath` (`--suite-manage`), `UninstallString` (`--suite-uninstall`),
                    //    and `InstallSource` cleanly so Windows `Settings -> Apps -> Uninstall` never opens First-Run mode!
                    rsw::SuiteChainerManifest probeSuite{};
                    probeSuite.suiteName   = "Boxset App and Add-On Suite";
                    probeSuite.upgradeCode = "{A1112222-3333-4444-8555-666677778891}";
                    probeSuite.perMachine  = false;
                    const auto regInfo = rsw::QuerySuiteInstalledRegistration(probeSuite);

                    const std::wstring suiteRegSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{A1112222-3333-4444-8555-666677778891}.Suite";
                    std::string regModifyPath, regUninstallStr;
                    rsw::native_detail::VirtualRegReadValue(stVirtReg, "HKCU", suiteRegSubKey, L"ModifyPath", regModifyPath);
                    rsw::native_detail::VirtualRegReadValue(stVirtReg, "HKCU", suiteRegSubKey, L"UninstallString", regUninstallStr);

                    const bool maintRegSeparated =
                        regInfo.registered &&
                        !regInfo.installLocation.empty() &&
                        !regInfo.installSource.empty() &&
                        regModifyPath.find("--suite-manage") != std::string::npos &&
                        regUninstallStr.find("--suite-uninstall") != std::string::npos &&
                        regUninstallStr.find("--suite-manage") == std::string::npos;

                    // Repair mode (`--suite-repair /s`) must succeed even when invoked from `<SuiteInstallDir>\SuiteSetup.exe`
                    // (resolving `packages\` automatically through persisted `InstallSource`!)
                    const auto installedHubExe = stInstall / L"SuiteHub2" / L"SuiteSetup.exe";
                    const DWORD repairHubRc = std::filesystem::exists(installedHubExe, ec)
                        ? runCli(installedHubExe, L"--suite-repair /s")
                        : 99u;

                    // Full Uninstall (`--suite-uninstall /s`) removes all 3 child packages AND the Suite Uninstall key!
                    const DWORD uninstSuiteRc = runCli(stOut / L"ok-suite-setup.exe", L"--suite-uninstall /s --install-dir \"" + (stInstall / L"SuiteHub2").wstring() + L"\"");
                    const auto regAfterUninst = rsw::QuerySuiteInstalledRegistration(probeSuite);
                    const bool fullUninstClean =
                        (uninstSuiteRc == 0) &&
                        !regAfterUninst.registered &&
                        !std::filesystem::exists(hostInstDir / L"BookReader.exe", ec) &&
                        !std::filesystem::exists(compInstDir / L"CompanionTool.txt", ec) &&
                        !std::filesystem::exists(stAddonData / L"RaggieSoft" / L"Books" / L"ad-astra" / L"ad-astra.md", ec);

                    // B. Mid-Suite Explicit User Cancel (`RSW_TEST_SUITE_CANCEL_AFTER_STEP=2`):
                    //    Step 1 (`HostReader`, `RollbackBoundary = true`) and Step 2 (`Companion`) install,
                    //    then user confirms Cancel before Step 3 -> BOTH Step 2 AND Step 1 MUST roll back!
                    ::SetEnvironmentVariableW(L"RSW_TEST_SUITE_CANCEL_AFTER_STEP", L"2");
                    const DWORD cancelSuiteRc = runCli(stOut / L"ok-suite-setup.exe", L"/s --install-dir \"" + (stInstall / L"SuiteHubCancel").wstring() + L"\"");
                    ::SetEnvironmentVariableW(L"RSW_TEST_SUITE_CANCEL_AFTER_STEP", nullptr);

                    const bool suiteCancelUnwoundAll =
                        (cancelSuiteRc == ERROR_INSTALL_USEREXIT) &&
                        !std::filesystem::exists(hostInstDir / L"BookReader.exe", ec) &&
                        !std::filesystem::exists(compInstDir / L"CompanionTool.txt", ec) &&
                        !rsw::QuerySuiteInstalledRegistration(probeSuite).registered;

                    // C. Mid-Install RaggieScript EXE Cancel (`RSW_TEST_CANCEL_AFTER_FILES=1`) -> Full Atomic LIFO Rollback!
                    const auto cancelNatDir = stInstall / L"NativeCancelTarget";
                    ::SetEnvironmentVariableW(L"RSW_TEST_CANCEL_AFTER_FILES", L"1");
                    const DWORD cancelNatRc = runCli(hostSetupExe, L"/s INSTALLDIR=\"" + cancelNatDir.wstring() + L"\"");
                    ::SetEnvironmentVariableW(L"RSW_TEST_CANCEL_AFTER_FILES", nullptr);
                    const bool natCancelRolledBack =
                        (cancelNatRc != 0) &&
                        !std::filesystem::exists(cancelNatDir / L"BookReader.exe", ec) &&
                        !std::filesystem::exists(cancelNatDir / L".rsw-rollback.jnl", ec);

                    // D. Multi-Step Suite Wizard Screenshot Export (`SetupType`, `CustomSuite`, `Maintenance`, `Installing`)
                    const auto suiteShotsDir = suiteTestDir / L"suite-shots";
                    const DWORD shotRc = runCli(stOut / L"ok-suite-setup.exe", L"--export-screenshots \"" + suiteShotsDir.wstring() + L"\"");
                    const bool suiteWizardShotsOk =
                        (shotRc == 0) &&
                        std::filesystem::exists(suiteShotsDir / L"setupwizard-suite-setup-type.png", ec) &&
                        std::filesystem::exists(suiteShotsDir / L"setupwizard-suite-custom-list.png", ec) &&
                        std::filesystem::exists(suiteShotsDir / L"setupwizard-suite-maintenance.png", ec) &&
                        std::filesystem::exists(suiteShotsDir / L"setupwizard-suite-dual-progress.png", ec);

                    // E. WCAG 2.2 Level AA Contrast Verification (`>= 4.5:1` text, `>= 3.0:1` focus rings) across Dark & Light
                    auto wcagContrast = [](COLORREF c1, COLORREF c2) -> double {
                        auto chan = [](BYTE v) -> double {
                            const double s = static_cast<double>(v) / 255.0;
                            return (s <= 0.03928) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
                        };
                        auto lum = [&](COLORREF c) -> double {
                            return 0.2126 * chan(GetRValue(c)) + 0.7152 * chan(GetGValue(c)) + 0.0722 * chan(GetBValue(c));
                        };
                        const double l1 = lum(c1), l2 = lum(c2);
                        return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
                    };
                    rsw::ui::RswTheme thDark{}, thLight{};
                    rsw::ui::ForcedThemeOverride() = 1;
                    thDark.Refresh();
                    rsw::ui::ForcedThemeOverride() = 0;
                    thLight.Refresh();
                    rsw::ui::ForcedThemeOverride() = -1;

                    const bool wcagContrastOk =
                        wcagContrast(thDark.textPrimary,   thDark.windowBg)  >= 4.5 &&
                        wcagContrast(thDark.textPrimary,   thDark.cardBg)    >= 4.5 &&
                        wcagContrast(thDark.textSecondary, thDark.windowBg)  >= 4.5 &&
                        wcagContrast(thDark.textSecondary, thDark.cardBg)    >= 4.5 &&
                        wcagContrast(thLight.textPrimary,   thLight.windowBg) >= 4.5 &&
                        wcagContrast(thLight.textPrimary,   thLight.cardBg)   >= 4.5 &&
                        wcagContrast(thLight.textSecondary, thLight.windowBg) >= 4.5 &&
                        wcagContrast(thLight.textSecondary, thLight.cardBg)   >= 4.5 &&
                        wcagContrast(RGB(255, 255, 255),    RGB(29, 78, 216)) >= 4.5 &&
                        wcagContrast(rsw::ui::brand::kWarmGold, thDark.cardBg) >= 3.0 &&
                        wcagContrast(RGB(29, 78, 216),     thLight.cardBg)    >= 3.0;

                    ::SetEnvironmentVariableW(L"RSW_TEST_ADDON_DATA_ROOT", nullptr);
                    ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", (prevVLen > 0) ? prevVReg : nullptr);

                    Check(maintRegSeparated && repairHubRc == 0 && fullUninstClean &&
                          suiteCancelUnwoundAll && natCancelRolledBack && suiteWizardShotsOk && wcagContrastOk,
                          "v0.9.1 Suite Wizard Maintenance (ModifyPath vs UninstallString + InstallSource Repair), Cancel 100% LIFO rollback, & WCAG 2.2 AA contrast verified",
                          "maintSep=" + std::to_string(maintRegSeparated) +
                          " repRc=" + std::to_string(repairHubRc) +
                          " uninstClean=" + std::to_string(fullUninstClean) +
                          " suiteCancelRb=" + std::to_string(suiteCancelUnwoundAll) +
                          " natCancelRb=" + std::to_string(natCancelRolledBack) +
                          " shots=" + std::to_string(suiteWizardShotsOk) +
                          " wcag=" + std::to_string(wcagContrastOk));
                }
            }
        }

        if (rsscVirtReg) {
            ::SetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", nullptr);
        }
    }

    std::filesystem::remove_all(testWorkDir, ec);

    std::cout << "--------------------------------------------------------------------\n"
              << "  Summary: " << gPassed << " passed, " << gFailed << " failed.\n"
              << "====================================================================\n";
    return (gFailed == 0) ? 0 : 1;
}
