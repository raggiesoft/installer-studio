// ================================================================================
// FILE: src/ide/RswIdeMain.cpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `InstallerStudio.exe` ("RaggieSoft Installer Studio")?
//     `InstallerStudio.exe` is our **12-View Tree-First Win32 C++20 Visual IDE**
//     and **5-Wizard AeroWizard Suite** for authoring standalone `.rsip` projects,
//     `@include` `.rsii` dialog/component presets, and multi-project `.rsis` suites!
//     Compiled directly with `cl.exe` + the stock Windows 11 SDK (`/MT` static
//     binary, zero .NET, zero Electron, zero WebView2 bloat), every navigation and
//     architecture view uses genuine Win32 `SysTreeView32` hierarchies with 32-bit
//     premultiplied ARGB vector icons (`StudioTreeIcon`) and `DarkMode_Explorer`
//     theming:
//       - **Left Category Tree (`hNavTree`)**:
//         Groups all 12 views under `Application Information`, `Setup Architecture`,
//         `User Interface`, and `Deployment & Source`.
//       1. **Package & Identity**            : Product metadata, GUIDs, & scope.
//       2. **Application Runtimes & .MSM**   : Hierarchical `SysTreeView32` of
//          Pre-MSI `[Prerequisite.*]` runtimes and 64-bit `[MergeModule.*]` modules.
//       3. **Features (`All Features` Tree)** : Dedicated feature hierarchy editor
//          with `Move Up` / `Move Down` `SelectionTree` ordering, `New Feature`,
//          `Sub-Feature`, `DisallowAbsent` locks, & live Feature Description editor!
//       4. **Files & Folders (Dual Trees)**  : Left `Destination Computer` folder
//          tree (`INSTALLDIR`, `ProgramData`, `AppData`, `StartMenu`, `Desktop`,
//          `System64`, `Fonts` + custom subfolders) + Right `Folder Contents` tree!
//       5. **Components & KeyPaths**         : `Features -> Components -> KeyPaths`
//          tree enforcing `ICE18`/`ICE38`/`ICE64`/`ICE80` with live click-to-inspect!
//       6. **Registry (`RegEdit` Tree)**     : Left `HKEY_CLASSES_ROOT`,
//          `HKEY_CURRENT_USER`, `HKEY_LOCAL_MACHINE\Software\...`, `HKEY_USERS`, &
//          `HKEY_USER_MACHINE_DEFAULT (HKMU)` key tree + Right Values tree +
//          `Filter by Feature` + `.REG` file importer!
//       7. **File Associations**             : `.ext -> ProgId -> Shell Verbs` tree!
//       8. **Shortcuts**                     : `Start Menu` & `Desktop` shortcut tree!
//       9. **C++ Wizard Designer**           : Live GDI+ Designer (`SetupWizard.exe`).
//      10. **MSI.DLL Dialog Designer**       : `370x270` DLU Windows Installer canvas.
//      11. **Signing, .MST & .MSP**          : Dual-Layer Authenticode + `.mst`/`.msp`.
//      12. **.RSIP Source (Lossless AST)**   : 100% comment- & `@include`-preserving
//          monospace manifest editor + `F1` native C++ `RaggieSoftHelp.exe`!
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswCryptoAndPe.hpp"
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
#include "../compiler/RswValidator.hpp"
#include "../compiler/RswVpatEmitter.hpp"
#include "RswIdeAeroWizards.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

enum StudioView : int {
    VIEW_PACKAGE      = 0,
    VIEW_PREREQS      = 1,
    VIEW_FEATURES     = 2,
    VIEW_DEST_FOLDERS = 3,
    VIEW_SETUP_DESIGN = 4,
    VIEW_REGISTRY     = 5,
    VIEW_FILE_ASSOC   = 6,
    VIEW_SHORTCUTS    = 7,
    VIEW_CPP_WIZARD   = 8,
    VIEW_MSI_DIALOGS  = 9,
    VIEW_SIGN_REL     = 10,
    VIEW_SOURCE       = 11,
    VIEW_COUNT        = 12
};

enum IdeId : int {
    // Top Menu Commands
    IDM_FILE_NEW             = 2001,
    IDM_FILE_OPEN            = 2002,
    IDM_FILE_SAVE            = 2003,
    IDM_FILE_EXIT            = 2004,

    IDM_WIZ_PROJECT          = 2005,
    IDM_WIZ_FILE_ASSOC       = 2006,
    IDM_WIZ_COMPONENT        = 2007,
    IDM_WIZ_RELEASE          = 2008,
    IDM_WIZ_INSERT_PRESET    = 2009,
    IDM_WIZ_PREREQ_MSM       = 2017,

    IDM_BUILD_VALIDATE       = 2010,
    IDM_BUILD_MSI            = 2011,
    IDM_BUILD_MSM            = 2012,
    IDM_BUILD_VPAT           = 2013,
    IDM_BUILD_EMIT_AI        = 2014,
    IDM_LAUNCH_MSIPRISM      = 2015,
    IDM_OPEN_OUTDIR          = 2016,

    // Editions & Help Menu (`Community == 100% Premier Feature Parity`!)
    IDM_EDITION_COMMUNITY    = 2020,
    IDM_EDITION_EXPRESS      = 2021,
    IDM_EDITION_PROFESSIONAL = 2022,
    IDM_EDITION_PREMIER      = 2023,
    IDM_HELP_ABOUT           = 2024,
    IDM_HELP_TOPICS          = 2025,

    // Persistent Toolbar & Category Navigation Tree
    IDC_TB_WIZARD            = 2100,
    IDC_TB_NEW               = 2101,
    IDC_TB_OPEN              = 2102,
    IDC_TB_SAVE              = 2103,
    IDC_TB_VALIDATE          = 2104,
    IDC_TB_BUILD_MSI         = 2105,
    IDC_TB_BUILD_MSM         = 2106,
    IDC_TB_EXPORT_VPAT       = 2107,
    IDC_TB_MSIPRISM          = 2108,
    IDC_ARCH_COMBO           = 2109,
    IDC_SUITE_COMBO          = 2110,
    IDC_NAV_TREE             = 2200,

    // View 0: Package & Identity
    IDC_PKG_NAME             = 2210,
    IDC_PKG_TAGLINE          = 2211,
    IDC_PKG_WEBSITE          = 2212,
    IDC_PKG_VERSION          = 2213,
    IDC_PKG_PUBLISHER        = 2214,
    IDC_PKG_UPGRADECODE      = 2215,
    IDC_PKG_NEW_GUID         = 2216,
    IDC_PKG_SUBDIR           = 2217,
    IDC_PKG_MAINEXE          = 2218,
    IDC_PKG_SCOPE            = 2219,
    IDC_PKG_APPLY            = 2220,

    // View 1: Application Runtimes (`[Prerequisite.*]`) & Merge Modules (`[MergeModule.*]`) Tree
    IDC_PR_TREE              = 2370,
    IDC_PR_PRESET_COMBO      = 2371,
    IDC_PR_DIST_COMBO        = 2372,
    IDC_PR_SOURCE_EDIT       = 2373,
    IDC_PR_BROWSE_BTN        = 2374,
    IDC_PR_REBOOT_CODES      = 2375,
    IDC_PR_ADD_BTN           = 2376,
    IDC_PR_ADD_MSM_BTN       = 2377,
    IDC_PR_REMOVE_BTN        = 2378,
    IDC_PR_WIZ_BTN           = 2379,

    // View 2: Dedicated Features Hierarchy (`All Features` Tree + Ordering + Description)
    IDC_FT_TREE              = 2400,
    IDC_FT_UP_BTN            = 2401,
    IDC_FT_DOWN_BTN          = 2402,
    IDC_FT_NEW_ROOT_BTN      = 2403,
    IDC_FT_NEW_SUB_BTN       = 2404,
    IDC_FT_SAVE_BTN          = 2405,
    IDC_FT_DELETE_BTN        = 2406,
    IDC_FT_REQUIRED_CHK      = 2407,
    IDC_FT_ID                = 2408,
    IDC_FT_TITLE             = 2409,
    IDC_FT_LEVEL             = 2410,
    IDC_FT_PARENT            = 2411,
    IDC_FT_DESC              = 2412,

    // View 3: Files & Folders (`Destination Computer` Tree + `Folder Contents` Tree)
    IDC_DEST_TREE            = 2225,
    IDC_PAY_TREE             = 2230,
    IDC_PAY_ADD_FILE         = 2231,
    IDC_PAY_ADD_FOLDER       = 2232,
    IDC_DEST_NEW_SUBDIR      = 2233,
    IDC_PAY_REMOVE           = 2234,
    IDC_DEST_SUBDIR_EDIT     = 2235,

    // View 4: Components & KeyPaths (`Features -> Components -> KeyPaths` Tree)
    IDC_DESIGN_TREE          = 2310,
    IDC_FEAT_ID              = 2311,
    IDC_FEAT_TITLE           = 2312,
    IDC_FEAT_LEVEL           = 2313,
    IDC_FEAT_PARENT          = 2314,
    IDC_FEAT_SAVE_BTN        = 2315,
    IDC_COMP_ID              = 2316,
    IDC_COMP_FEAT            = 2317,
    IDC_COMP_ROOT            = 2318,
    IDC_COMP_SUBDIR          = 2319,
    IDC_COMP_KP_KIND         = 2320,
    IDC_COMP_KP_TARGET       = 2321,
    IDC_COMP_SAVE_BTN        = 2322,
    IDC_COMP_WIZ_BTN         = 2323,
    IDC_COMP_DEL_BTN         = 2324,

    // View 5: `RegEdit`-Style Registry Editor (Hives/Keys Tree + Values Tree + `.REG` Importer)
    IDC_REG_FEAT_FILTER      = 2420,
    IDC_REG_IMPORT_BTN       = 2421,
    IDC_REG_KEY_TREE         = 2422,
    IDC_REG_VAL_TREE         = 2423,
    IDC_REG_SUBKEY_EDIT      = 2424,
    IDC_REG_ADD_KEY_BTN      = 2425,
    IDC_REG_DEL_KEY_BTN      = 2426,
    IDC_REG_ROOT_COMBO       = 2427,
    IDC_REG_KEY              = 2428,
    IDC_REG_NAME             = 2429,
    IDC_REG_TYPE_COMBO       = 2430,
    IDC_REG_VALUE            = 2431,
    IDC_REG_FEAT_EDIT        = 2432,
    IDC_REG_COMP             = 2433,
    IDC_REG_SET_VAL_BTN      = 2434,
    IDC_REG_DEL_VAL_BTN      = 2435,

    // View 6: File Associations & Shell Verbs Tree
    IDC_FA_TREE              = 2330,
    IDC_FA_EXT               = 2331,
    IDC_FA_PROGID            = 2332,
    IDC_FA_DESC              = 2333,
    IDC_FA_TARGET            = 2334,
    IDC_FA_VERBS             = 2335,
    IDC_FA_ADVERTISE         = 2336,
    IDC_FA_ADD_BTN           = 2337,
    IDC_FA_REMOVE_BTN        = 2338,
    IDC_FA_WIZ_BTN           = 2339,

    // View 7: Application Shortcuts Tree (`Start Menu` & `Desktop`)
    IDC_SC_TREE              = 2240,
    IDC_SC_ID_EDIT           = 2241,
    IDC_SC_NAME              = 2242,
    IDC_SC_TARGET            = 2243,
    IDC_SC_LOC_COMBO         = 2244,
    IDC_SC_ARGS              = 2245,
    IDC_SC_FEAT              = 2246,
    IDC_SC_SAVE_BTN          = 2247,
    IDC_SC_DEL_BTN           = 2248,

    // View 8: C++ Wizard Designer (`SetupWizard.exe`)
    IDC_UI_LAYOUT            = 2250,
    IDC_UI_PRIMARY_HEX       = 2251,
    IDC_UI_SECONDARY_HEX     = 2252,
    IDC_UI_INSTALL_BTN       = 2253,
    IDC_UI_ICON_PATH         = 2254,
    IDC_UI_ICON_BROWSE       = 2255,
    IDC_UI_ART_PATH          = 2256,
    IDC_UI_ART_BROWSE        = 2257,
    IDC_UI_APPLY             = 2258,
    IDC_UI_PREVIEW_STEP      = 2259,
    IDC_UI_SETUP_TYPES       = 2280,
    IDC_UI_AUTO_WCAG         = 2281,

    // View 9: Traditional `MSI.DLL` `Dialog` Table Designer (`370 x 270` DLU)
    IDC_MSI_PRESET_COMBO     = 2350,
    IDC_MSI_DLG_SELECT       = 2351,
    IDC_MSI_NEW_DLG_ID       = 2352,
    IDC_MSI_NEW_DLG_TITLE    = 2353,
    IDC_MSI_NEW_DLG_AFTER    = 2354,
    IDC_MSI_ADD_DLG_BTN      = 2355,
    IDC_MSI_CTRL_ID          = 2356,
    IDC_MSI_CTRL_TYPE        = 2357,
    IDC_MSI_CTRL_XYWH        = 2358,
    IDC_MSI_CTRL_PROP        = 2359,
    IDC_MSI_CTRL_TEXT        = 2360,
    IDC_MSI_ADD_CTRL_BTN     = 2361,
    IDC_MSI_INCLUDE_BTN      = 2362,

    // View 10: Authenticode Signing, .MST & .MSP
    IDC_SIG_ENABLED          = 2260,
    IDC_SIG_THUMBPRINT       = 2261,
    IDC_SIG_TIMESTAMP        = 2262,
    IDC_SIG_DLIB             = 2263,
    IDC_SIG_META             = 2264,
    IDC_SIG_APPLY            = 2265,
    IDC_ENT_BASE_MSI         = 2270,
    IDC_ENT_BASE_BROWSE      = 2271,
    IDC_ENT_TARG_MSI         = 2272,
    IDC_ENT_TARG_BROWSE      = 2273,
    IDC_ENT_GEN_MST          = 2274,
    IDC_ENT_GEN_MSP          = 2275,
    IDC_ENT_REL_WIZ_BTN      = 2276,

    // View 11: Lossless `.rsip` Source & Bottom Console
    IDC_SOURCE_EDIT          = 2301,
    IDC_CONSOLE_EDIT         = 2302
};

// ================================================================================
// LOSSLESS `.RSIP` AST MUTATION, FEATURE ORDERING & `.REG` IMPORT HELPERS
// ================================================================================
void RemoveAstSection(rsw::RswManifestDocument& doc, std::string_view prefix, std::string_view itemId) {
    std::vector<rsw::RswAstLine> kept;
    kept.reserve(doc.lines.size());
    for (const auto& ln : doc.lines) {
        if ((ln.kind == rsw::AstLineKind::SectionHeader || ln.kind == rsw::AstLineKind::KeyValue) &&
            rsw::EqualsIgnoreCase(ln.sectionPrefix, prefix) &&
            rsw::EqualsIgnoreCase(ln.sectionItemId, itemId)) {
            continue;
        }
        kept.push_back(ln);
    }
    doc.lines = std::move(kept);
}

void RemoveAstKey(rsw::RswManifestDocument& doc, std::string_view prefix, std::string_view itemId, std::string_view key) {
    std::vector<rsw::RswAstLine> kept;
    kept.reserve(doc.lines.size());
    for (const auto& ln : doc.lines) {
        if (ln.kind == rsw::AstLineKind::KeyValue &&
            rsw::EqualsIgnoreCase(ln.sectionPrefix, prefix) &&
            rsw::EqualsIgnoreCase(ln.sectionItemId, itemId) &&
            rsw::EqualsIgnoreCase(ln.key, key)) {
            continue;
        }
        kept.push_back(ln);
    }
    doc.lines = std::move(kept);
}

/// Reorders `[Feature.<featId>]` up (`delta = -1`) or down (`delta = +1`) among its sibling features
/// inside the lossless `.rsip` AST so `SelectionTree` (`Feature.Display`) updates cleanly!
bool SwapFeatureSectionOrder(rsw::RswManifestDocument& doc, const std::string& featId, int delta) {
    struct Span {
        std::string id;
        std::string parent;
        std::size_t startIdx = 0;
        std::size_t endIdx   = 0; // inclusive
    };
    std::vector<Span> spans;
    for (std::size_t i = 0; i < doc.lines.size(); ++i) {
        const auto& ln = doc.lines[i];
        if (ln.kind == rsw::AstLineKind::SectionHeader &&
            rsw::EqualsIgnoreCase(ln.sectionPrefix, "Feature") &&
            !ln.sectionItemId.empty()) {
            std::size_t end = i;
            while (end + 1 < doc.lines.size() &&
                   doc.lines[end + 1].kind == rsw::AstLineKind::KeyValue &&
                   rsw::EqualsIgnoreCase(doc.lines[end + 1].sectionPrefix, "Feature") &&
                   rsw::EqualsIgnoreCase(doc.lines[end + 1].sectionItemId, ln.sectionItemId)) {
                ++end;
            }
            spans.push_back({ln.sectionItemId, rsw::TrimAscii(doc.GetString("Feature", ln.sectionItemId, "Parent")), i, end});
            i = end;
        }
    }

    int curIdx = -1;
    for (int s = 0; s < static_cast<int>(spans.size()); ++s) {
        if (rsw::EqualsIgnoreCase(spans[s].id, featId)) { curIdx = s; break; }
    }
    if (curIdx < 0) return false;

    int partnerIdx = -1;
    for (int step = curIdx + (delta < 0 ? -1 : 1);
         step >= 0 && step < static_cast<int>(spans.size());
         step += (delta < 0 ? -1 : 1)) {
        if (rsw::EqualsIgnoreCase(spans[step].parent, spans[curIdx].parent)) {
            partnerIdx = step;
            break;
        }
    }
    if (partnerIdx < 0) return false;

    const int firstS  = std::min(curIdx, partnerIdx);
    const int secondS = std::max(curIdx, partnerIdx);
    const auto& a = spans[firstS];
    const auto& b = spans[secondS];

    std::vector<rsw::RswAstLine> rebuilt;
    rebuilt.reserve(doc.lines.size());
    rebuilt.insert(rebuilt.end(), doc.lines.begin(), doc.lines.begin() + static_cast<std::ptrdiff_t>(a.startIdx));
    rebuilt.insert(rebuilt.end(), doc.lines.begin() + static_cast<std::ptrdiff_t>(b.startIdx), doc.lines.begin() + static_cast<std::ptrdiff_t>(b.endIdx + 1));
    rebuilt.insert(rebuilt.end(), doc.lines.begin() + static_cast<std::ptrdiff_t>(a.endIdx + 1), doc.lines.begin() + static_cast<std::ptrdiff_t>(b.startIdx));
    rebuilt.insert(rebuilt.end(), doc.lines.begin() + static_cast<std::ptrdiff_t>(a.startIdx), doc.lines.begin() + static_cast<std::ptrdiff_t>(a.endIdx + 1));
    rebuilt.insert(rebuilt.end(), doc.lines.begin() + static_cast<std::ptrdiff_t>(b.endIdx + 1), doc.lines.end());
    doc.lines = std::move(rebuilt);
    return true;
}

/// Parses standard Windows `.reg` exports (UTF-16LE BOM or UTF-8, `Windows Registry Editor Version 5.00` / `REGEDIT4`)
/// and appends clean `[Registry.*]` sections into the `.rsip` AST!
bool ImportRegFileIntoAst(
    const std::filesystem::path& regPath,
    rsw::RswManifestDocument& doc,
    const std::string& targetFeature,
    int& outImportedCount)
{
    outImportedCount = 0;
    std::vector<std::uint8_t> raw;
    std::string err;
    if (!rsw::ReadBinaryFileBytes(regPath, raw, err) || raw.empty()) return false;

    std::string utf8Text;
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        const std::size_t wcCount = (raw.size() - 2) / 2;
        std::wstring ws(wcCount, L'\0');
        std::memcpy(ws.data(), raw.data() + 2, wcCount * 2);
        utf8Text = rsw::WideToUtf8(ws);
    } else {
        std::size_t off = (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        utf8Text.assign(reinterpret_cast<const char*>(raw.data() + off), raw.size() - off);
    }

    auto unescapeReg = [](std::string_view s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                out.push_back(s[i]);
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    };

    std::string activeHive;
    std::string activeSubkey;
    int nextNum = static_cast<int>(doc.GetNamedSectionIds("Registry").size() + 1);

    std::istringstream iss(utf8Text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = rsw::TrimAscii(line);
        if (t.empty() || t.front() == ';') continue;
        if (rsw::EqualsIgnoreCase(t, "REGEDIT4") || t.rfind("Windows Registry Editor", 0) == 0) continue;

        if (t.front() == '[' && t.back() == ']') {
            std::string fullKey = rsw::TrimAscii(t.substr(1, t.size() - 2));
            if (!fullKey.empty() && fullKey.front() == '-') {
                activeHive.clear();
                continue;
            }
            auto slash = fullKey.find('\\');
            std::string hiveRaw = (slash == std::string::npos) ? fullKey : fullKey.substr(0, slash);
            activeSubkey = (slash == std::string::npos) ? "" : fullKey.substr(slash + 1);
            if (rsw::EqualsIgnoreCase(hiveRaw, "HKEY_LOCAL_MACHINE") || rsw::EqualsIgnoreCase(hiveRaw, "HKLM")) activeHive = "HKLM";
            else if (rsw::EqualsIgnoreCase(hiveRaw, "HKEY_CURRENT_USER") || rsw::EqualsIgnoreCase(hiveRaw, "HKCU")) activeHive = "HKCU";
            else if (rsw::EqualsIgnoreCase(hiveRaw, "HKEY_CLASSES_ROOT") || rsw::EqualsIgnoreCase(hiveRaw, "HKCR")) activeHive = "HKCR";
            else if (rsw::EqualsIgnoreCase(hiveRaw, "HKEY_USERS") || rsw::EqualsIgnoreCase(hiveRaw, "HKU")) activeHive = "HKU";
            else activeHive.clear();
            continue;
        }

        if (activeHive.empty() || activeSubkey.empty()) continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;

        std::string lhs = rsw::TrimAscii(t.substr(0, eq));
        std::string rhs = rsw::TrimAscii(t.substr(eq + 1));
        if (rhs == "-") continue;

        std::string valName = "@";
        if (lhs != "@") {
            if (lhs.size() >= 2 && lhs.front() == '"' && lhs.back() == '"') {
                valName = unescapeReg(std::string_view(lhs).substr(1, lhs.size() - 2));
            } else {
                valName = lhs;
            }
        }

        std::string valType = "string";
        std::string valData;
        if (rhs.size() >= 2 && rhs.front() == '"' && rhs.back() == '"') {
            valData = unescapeReg(std::string_view(rhs).substr(1, rhs.size() - 2));
        } else if (rhs.size() > 6 && rsw::EqualsIgnoreCase(rhs.substr(0, 6), "dword:")) {
            valType = "dword";
            unsigned long dv = std::strtoul(rhs.substr(6).c_str(), nullptr, 16);
            valData = std::to_string(dv);
        } else {
            continue;
        }

        const std::string rid = "RegImport" + std::to_string(nextNum++);
        doc.SetString("Registry", rid, "Root",    activeHive);
        doc.SetString("Registry", rid, "Key",     activeSubkey);
        doc.SetString("Registry", rid, "Name",    valName);
        doc.SetString("Registry", rid, "Type",    valType);
        doc.SetString("Registry", rid, "Value",   valData);
        doc.SetString("Registry", rid, "Feature", targetFeature.empty() ? "Main" : targetFeature);
        ++outImportedCount;
    }
    return outImportedCount > 0;
}

std::string BuildStarterRsipTemplate() {
    const std::string guid = rsw::GenerateRandomMsiGuid();
    std::ostringstream s;
    s << "; ==============================================================================\r\n"
      << "; RaggieSoft Installer Project (.rsip) — Author-Owned White-Label Manifest\r\n"
      << "; Every ';', '#', and '@include' line is preserved 100% intact across saves!\r\n"
      << "; ==============================================================================\r\n\r\n"
      << "[Package]\r\n"
      << "Name              = My Native Application\r\n"
      << "Tagline           = Fast, Accessible Windows Desktop Software\r\n"
      << "Version           = 1.0.0\r\n"
      << "Manufacturer      = Sample Studio\r\n"
      << "UpgradeCode       = " << guid << " ; Permanent UpgradeCode across releases\r\n"
      << "Architecture      = $(ARCH)\r\n"
      << "Scope             = machine\r\n"
      << "BuildBootstrapper = true\r\n\r\n"
      << "[Requirements]\r\n"
      << "MinWindowsBuild   = 22000\r\n\r\n"
      << "[Install]\r\n"
      << "DefaultSubdir     = Sample Studio\\My Native App\r\n"
      << "MainExecutable    = HelloRaggieSoft.exe\r\n\r\n"
      << "[UI]\r\n"
      << "WizardLayout      = aerowizard\r\n"
      << "MsiDialogPreset   = express_custom\r\n"
      << "AccentPrimaryHex  = #2563EB\r\n"
      << "AccentSecondaryHex= #FBBF24\r\n"
      << "InstallButtonText = &Install\r\n\r\n"
      << "[Feature.Main]\r\n"
      << "Title             = Core Application Files\r\n"
      << "Description       = Essential program executables and libraries.\r\n"
      << "Inclusion         = minimal\r\n"
      << "DisallowAbsent    = true\r\n\r\n"
      << "[Payload.App]\r\n"
      << "Source            = ..\\..\\bin\\$(ARCH)\\HelloRaggieSoft.exe\r\n"
      << "TargetRoot        = INSTALLDIR\r\n"
      << "Feature           = Main\r\n\r\n"
      << "[Shortcut.AppMenu]\r\n"
      << "Name              = My Native Application\r\n"
      << "TargetFile        = HelloRaggieSoft.exe\r\n"
      << "Location          = StartMenu\r\n\r\n"
      << "[Registry.InstallPath]\r\n"
      << "Root              = HKLM\r\n"
      << "Key               = Software\\Sample Studio\\My Native App\r\n"
      << "Name              = InstallPath\r\n"
      << "Value             = [INSTALLDIR]\r\n";
    return s.str();
}

std::wstring RunChildCapture(const std::wstring& cmdLine, DWORD& outExitCode) {
    outExitCode = 1;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!::CreatePipe(&hRead, &hWrite, &sa, 0)) return L"[ERROR] CreatePipe failed.\r\n";
    ::SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdLine;
    const BOOL ok = ::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    ::CloseHandle(hWrite);
    if (!ok) {
        ::CloseHandle(hRead);
        return L"[ERROR] Failed launching instbuild.exe.\r\n";
    }

    std::string raw;
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) raw.append(buf, n);
    ::CloseHandle(hRead);

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    ::GetExitCodeProcess(pi.hProcess, &outExitCode);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    std::string crlf;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r')) crlf += '\r';
        crlf += raw[i];
    }
    return rsw::Utf8ToWide(crlf);
}

struct TaggedControl {
    int  viewIndex; // `-1` = Persistent across all views
    HWND hwnd;
};

struct DestTreeNodeMeta {
    std::string targetRoot;   // `""` = `Destination Computer` (All folders)
    std::string targetSubdir; // e.g. `"Plugins"`
};

struct SectionTreeRef {
    std::string prefix; // e.g. `"Prerequisite"`, `"MergeModule"`, `"Payload"`, `"Folder"`, `"Feature"`, `"Component"`
    std::string id;     // e.g. `"DotNet10"`, `"Main"`, `"CoreExe"`
};

struct RegKeyNodeMeta {
    std::string hiveCode; // `"HKCR"`, `"HKCU"`, `"HKLM"`, `"HKU"`, `"HKMU"`, or `""` for root
    std::string keyPath;  // e.g. `"Software\\RaggieSoft\\Installer Studio"`
};

class InstallerStudioIde {
public:
    rsw::ui::RswTheme          theme{};
    HWND                       hWnd        = nullptr;
    HFONT                      hFontBanner = nullptr;
    HFONT                      hFontHead   = nullptr;
    HFONT                      hFontUi     = nullptr;
    HFONT                      hFontBold   = nullptr;
    HFONT                      hFontMono   = nullptr;
    HIMAGELIST                 hTreeIcons  = nullptr;

    std::filesystem::path      ideExeDir;
    std::filesystem::path      currentFilePath;
    rsw::RswSuiteWorkspace     activeSuite{};
    bool                       hasActiveSuite = false;
    rsw::RswManifestDocument   astDoc{};
    rsw::RswIdeEdition         activeEdition = rsw::RswIdeEdition::Community;
    int                        activeView = VIEW_PACKAGE;
    bool                       suppressNavEvent = false;
    std::vector<TaggedControl> taggedControls;
    RECT                       previewCanvasRc{};
    RECT                       msiCanvasRc{};

    // Tree selection metadata tables across all 9 `SysTreeView32` controls
    std::vector<int>                           navNodeViews;
    std::array<HTREEITEM, VIEW_COUNT>          viewNavItems{};

    std::vector<SectionTreeRef>                prNodeRefs;
    SectionTreeRef                             selectedPrRef{};

    std::vector<std::string>                   ftNodeIds;
    std::map<std::string, HTREEITEM>           ftItemMap;
    std::string                                selectedFtId;

    std::vector<DestTreeNodeMeta>              destNodeMetas;
    std::string                                selectedDestRoot;
    std::string                                selectedDestSubdir;
    std::vector<SectionTreeRef>                payNodeRefs;
    SectionTreeRef                             selectedPayRef{};

    std::vector<SectionTreeRef>                designNodeRefs;

    std::vector<RegKeyNodeMeta>                regKeyMetas;
    std::map<std::string, HTREEITEM>           regPathItemMap;
    std::vector<std::pair<std::string, std::string>> extraSessionRegKeys;
    std::string                                selectedRegHive = "HKLM";
    std::string                                selectedRegKeyPath;
    std::string                                activeRegFeatureFilter;
    std::vector<std::string>                   regValSectionIds;
    std::string                                selectedRegSectionId;

    std::vector<std::string>                   faNodeIds;
    std::string                                selectedFaId;

    std::vector<SectionTreeRef>                scNodeRefs;
    std::string                                selectedScId;

    // Persistent Toolbar, Category Sidebar Tree, & Console
    HWND hBtnWizard = nullptr, hBtnNew = nullptr, hBtnOpen = nullptr, hBtnSave = nullptr, hBtnValidate = nullptr;
    HWND hBtnBuildMsi = nullptr, hBtnBuildMsm = nullptr, hBtnExportVpat = nullptr, hBtnMsiPrism = nullptr;
    HWND hArchCombo = nullptr, hSuiteCombo = nullptr;
    HWND hNavTree = nullptr, hConsoleEdit = nullptr;

    // View 0: Package & Identity
    static constexpr int IDC_PKG_UNIV_CHK = 2221;
    static constexpr int IDC_PKG_UNIV_URL = 2222;
    static constexpr int IDC_PKG_EXEC_LVL = 2223;
    HWND hPkgName = nullptr, hPkgTag = nullptr, hPkgWeb = nullptr, hPkgVer = nullptr, hPkgPub = nullptr;
    HWND hPkgGuid = nullptr, hPkgBtnGuid = nullptr, hPkgSub = nullptr, hPkgExe = nullptr, hPkgScope = nullptr;
    HWND hPkgUnivChk = nullptr, hPkgUnivUrl = nullptr, hPkgExecLvl = nullptr, hPkgApply = nullptr;

    // View 1: Application Runtimes (`[Prerequisite.*]`) & Merge Modules (`[MergeModule.*]`) Tree
    HWND hPrTree = nullptr, hPrPreset = nullptr, hPrDist = nullptr, hPrSource = nullptr;
    HWND hPrBrowseBtn = nullptr, hPrRebootCodes = nullptr, hPrAddBtn = nullptr;
    HWND hPrAddMsmBtn = nullptr, hPrRemBtn = nullptr, hPrWizBtn = nullptr;

    // View 2: Dedicated Features Hierarchy (`All Features` Tree + Ordering + Inspector)
    HWND hFtBannerLbl = nullptr, hFtTree = nullptr, hFtUpBtn = nullptr, hFtDownBtn = nullptr;
    HWND hFtNewRootBtn = nullptr, hFtNewSubBtn = nullptr, hFtSaveBtn = nullptr, hFtDelBtn = nullptr, hFtReqChk = nullptr;
    HWND hFtId = nullptr, hFtTitle = nullptr, hFtLevel = nullptr, hFtParent = nullptr, hFtDesc = nullptr;

    // View 3: Files & Folders (`Destination Computer` Tree + `Folder Contents` Tree)
    HWND hDestTree = nullptr, hPayTree = nullptr;
    HWND hPayAddFile = nullptr, hPayAddDir = nullptr, hDestSubEdit = nullptr, hDestNewSubBtn = nullptr, hPayRemove = nullptr;

    // View 4: Components & KeyPaths (`Features -> Components -> KeyPaths` Tree)
    HWND hDesignTree = nullptr;
    HWND hFeatId = nullptr, hFeatTitle = nullptr, hFeatLevel = nullptr, hFeatParent = nullptr, hFeatSaveBtn = nullptr;
    HWND hCompId = nullptr, hCompFeat = nullptr, hCompRoot = nullptr, hCompSubdir = nullptr;
    HWND hCompKpKind = nullptr, hCompKpTarget = nullptr, hCompSaveBtn = nullptr, hCompDelBtn = nullptr, hCompWizBtn = nullptr;

    // View 5: `RegEdit`-Style Registry Editor (Hives/Keys Tree + Values Tree + `.REG` Importer)
    HWND hRegFilterLbl = nullptr, hRegFeatFilter = nullptr, hRegImportBtn = nullptr;
    HWND hRegKeyTree = nullptr, hRegValTree = nullptr;
    HWND hRegSubEdit = nullptr, hRegAddKeyBtn = nullptr, hRegDelKeyBtn = nullptr;
    HWND hRegRootCombo = nullptr, hRegKey = nullptr, hRegName = nullptr, hRegTypeCombo = nullptr;
    HWND hRegVal = nullptr, hRegFeatEdit = nullptr, hRegComp = nullptr, hRegSetValBtn = nullptr, hRegDelValBtn = nullptr;

    // View 6: File Associations & Shell Verbs Tree
    HWND hFaTree = nullptr, hFaExt = nullptr, hFaProgId = nullptr, hFaDesc = nullptr;
    HWND hFaTarget = nullptr, hFaVerbs = nullptr, hFaAdvChk = nullptr;
    HWND hFaAddBtn = nullptr, hFaRemBtn = nullptr, hFaWizBtn = nullptr;

    // View 7: Application Shortcuts Tree (`Start Menu` & `Desktop`)
    HWND hScTree = nullptr, hScIdEdit = nullptr, hScName = nullptr, hScTarget = nullptr;
    HWND hScLocCombo = nullptr, hScArgs = nullptr, hScFeat = nullptr, hScSaveBtn = nullptr, hScDelBtn = nullptr;

    // View 8: C++ Wizard Designer (`SetupWizard.exe`)
    HWND hUiLayout = nullptr, hUiSetupTypes = nullptr, hUiPreviewStep = nullptr;
    HWND hUiPrim = nullptr, hUiAutoWcagBtn = nullptr, hUiSec = nullptr, hUiInstBtn = nullptr;
    HWND hUiIcon = nullptr, hUiIconBtn = nullptr, hUiArt = nullptr, hUiArtBtn = nullptr, hUiApply = nullptr;
    HICON   hPreviewIcon = nullptr;
    HBITMAP hPreviewArt  = nullptr;
    int     previewArtW  = 0;
    int     previewArtH  = 0;

    // View 9: Traditional `MSI.DLL` `Dialog` Table Designer
    HWND hMsiPreset = nullptr, hMsiDlgSelect = nullptr;
    HWND hMsiNewId = nullptr, hMsiNewTitle = nullptr, hMsiNewAfter = nullptr, hMsiAddDlgBtn = nullptr;
    HWND hMsiCtrlId = nullptr, hMsiCtrlType = nullptr, hMsiCtrlXywh = nullptr;
    HWND hMsiCtrlProp = nullptr, hMsiCtrlText = nullptr, hMsiAddCtrlBtn = nullptr, hMsiIncludeBtn = nullptr;

    // View 10: Signing, .MST & .MSP
    HWND hSigEnable = nullptr, hSigThumb = nullptr, hSigTs = nullptr, hSigDlib = nullptr, hSigMeta = nullptr, hSigApply = nullptr;
    HWND hEntBase = nullptr, hEntBaseBtn = nullptr, hEntTarg = nullptr, hEntTargBtn = nullptr;
    HWND hEntGenMst = nullptr, hEntGenMsp = nullptr, hEntRelWizBtn = nullptr;

    // View 11: Full Monospace `.rsip` Source
    HWND hSourceEdit = nullptr;

    ~InstallerStudioIde() {
        DestroyFonts();
        ReleasePreviewAssets();
        if (hTreeIcons) {
            ::ImageList_Destroy(hTreeIcons);
            hTreeIcons = nullptr;
        }
    }

    void DestroyFonts() noexcept {
        for (HFONT* f : {&hFontBanner, &hFontHead, &hFontUi, &hFontBold, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    void ReleasePreviewAssets() noexcept {
        if (hPreviewIcon) { ::DestroyIcon(hPreviewIcon); hPreviewIcon = nullptr; }
        if (hPreviewArt)  { ::DeleteObject(hPreviewArt); hPreviewArt  = nullptr; }
        previewArtW = 0;
        previewArtH = 0;
    }

    std::array<HWND, 9> AllTreeControls() const noexcept {
        return {hNavTree, hPrTree, hFtTree, hDestTree, hPayTree, hDesignTree, hRegKeyTree, hRegValTree, hFaTree};
    }

    void RefreshAllTreesThemeAndIcons() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        HIMAGELIST newIml = rsw::ui::CreateStudioTreeImageList(dpi, theme);
        for (HWND tv : {hNavTree, hPrTree, hFtTree, hDestTree, hPayTree, hDesignTree, hRegKeyTree, hRegValTree, hFaTree, hScTree}) {
            if (!tv) continue;
            TreeView_SetImageList(tv, newIml, TVSIL_NORMAL);
            rsw::ui::ApplyDarkTreeViewTheme(tv, theme, dpi);
            rsw::ui::ApplyDarkScrollbarTheme(tv, theme);
        }
        if (hTreeIcons) {
            ::ImageList_Destroy(hTreeIcons);
        }
        hTreeIcons = newIml;
    }

    HMENU BuildMenuBar() {
        HMENU hBar  = ::CreateMenu();
        HMENU hFile = ::CreatePopupMenu();
        ::AppendMenuW(hFile, MF_STRING, IDM_WIZ_PROJECT, L"New Project &Wizard (Aero Project Assistant)...\tCtrl+W");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_NEW,    L"&New Starter .rsip Project\tCtrl+N");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN,   L"&Open .rsip Project or .rsis Suite...\tCtrl+O");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE,   L"&Save Project (Lossless AST)\tCtrl+S");
        ::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT,   L"E&xit");

        HMENU hWiz = ::CreatePopupMenu();
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_PROJECT,       L"&New Project AeroWizard (Project Assistant)...\tCtrl+W");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_PREREQ_MSM,    L"&Runtime Prerequisite & .MSM Merge Module AeroWizard...");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_COMPONENT,     L"&Component & KeyPath AeroWizard (Microsoft Rules)...");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_FILE_ASSOC,    L"&File Association & Document Icon AeroWizard...");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_RELEASE,       L"&Release, Transform (.MST) & Patch (.MSP) AeroWizard...");
        ::AppendMenuW(hWiz, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_INSERT_PRESET, L"&Insert @include Sample Dialog Preset (.rsii)...");

        HMENU hBuild = ::CreatePopupMenu();
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_VALIDATE,  L"&Validate Manifest + Component Rules\tF7");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_MSI,       L"&Build Enterprise MSI + Setup.exe\tF5");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_MSM,       L"Build &Merge Module (.msm)\tF6");
        ::AppendMenuW(hBuild, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_VPAT,      L"Export Installer &VPAT 2.5 HTML5 Report");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_EMIT_AI,   L"Emit &llms.txt + SKILL.md");
        ::AppendMenuW(hBuild, MF_STRING, IDM_LAUNCH_MSIPRISM, L"Open &MSI Prism Inspector (msiprism.exe)");
        ::AppendMenuW(hBuild, MF_STRING, IDM_OPEN_OUTDIR,     L"Open &Output Folder in Explorer");

        HMENU hEd = ::CreatePopupMenu();
        ::AppendMenuW(hEd, MF_STRING,              IDM_HELP_TOPICS,          L"&Help Topics (Offline C++ Help Viewer)\tF1");
        ::AppendMenuW(hEd, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hEd, MF_STRING | MF_CHECKED, IDM_EDITION_COMMUNITY,    L"&Community Edition (100% Premier Feature Parity)");
        ::AppendMenuW(hEd, MF_STRING,              IDM_EDITION_EXPRESS,      L"&Express Edition (Placeholder Tier)");
        ::AppendMenuW(hEd, MF_STRING,              IDM_EDITION_PROFESSIONAL, L"&Professional Edition (Placeholder Tier)");
        ::AppendMenuW(hEd, MF_STRING,              IDM_EDITION_PREMIER,      L"P&remier Edition (Placeholder Tier)");
        ::AppendMenuW(hEd, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hEd, MF_STRING,              IDM_HELP_ABOUT,           L"&About RaggieSoft Installer Studio...");

        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFile),  L"&File");
        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hWiz),   L"&Wizards (Aero)");
        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hBuild), L"&Build & Compliance");
        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hEd),    L"&Editions & Help");
        return hBar;
    }

    HWND RegisterViewCtrl(int viewIdx, HWND h) {
        if (h) taggedControls.push_back({viewIdx, h});
        return h;
    }

    static HTREEITEM InsertIconTreeItem(
        HWND hTree,
        HTREEITEM hParent,
        const std::wstring& text,
        rsw::ui::StudioTreeIcon icon,
        LPARAM lParam = 0,
        bool boldRoot = false)
    {
        TVINSERTSTRUCTW tvi{};
        tvi.hParent             = hParent;
        tvi.hInsertAfter        = TVI_LAST;
        tvi.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | (boldRoot ? TVIF_STATE : 0);
        tvi.item.pszText        = const_cast<LPWSTR>(text.c_str());
        tvi.item.iImage         = static_cast<int>(icon);
        tvi.item.iSelectedImage = static_cast<int>(icon);
        tvi.item.lParam         = lParam;
        if (boldRoot) {
            tvi.item.stateMask = TVIS_BOLD;
            tvi.item.state     = TVIS_BOLD;
        }
        return TreeView_InsertItem(hTree, &tvi);
    }

    void PopulateCategorySidebarTree() {
        TreeView_DeleteAllItems(hNavTree);
        navNodeViews.clear();
        viewNavItems.fill(nullptr);

        auto addCat = [&](const wchar_t* label) {
            const std::size_t idx = navNodeViews.size();
            navNodeViews.push_back(-1);
            return InsertIconTreeItem(hNavTree, TVI_ROOT, label, rsw::ui::StudioTreeIcon::CategoryFolder, static_cast<LPARAM>(idx), true);
        };
        auto addView = [&](HTREEITEM hCat, int viewIdx, const wchar_t* label, rsw::ui::StudioTreeIcon icon) {
            const std::size_t idx = navNodeViews.size();
            navNodeViews.push_back(viewIdx);
            HTREEITEM hItem = InsertIconTreeItem(hNavTree, hCat, label, icon, static_cast<LPARAM>(idx), false);
            if (viewIdx >= 0 && viewIdx < VIEW_COUNT) {
                viewNavItems[static_cast<std::size_t>(viewIdx)] = hItem;
            }
            return hItem;
        };

        HTREEITEM hAppInfo = addCat(L"Application Information");
        addView(hAppInfo, VIEW_PACKAGE, L"Package & Identity",         rsw::ui::StudioTreeIcon::PackageCube);
        addView(hAppInfo, VIEW_PREREQS, L"Application Runtimes & .MSM",rsw::ui::StudioTreeIcon::VerbBolt);

        HTREEITEM hArch = addCat(L"Setup Architecture");
        addView(hArch, VIEW_FEATURES,     L"Features",               rsw::ui::StudioTreeIcon::FeatureBox);
        addView(hArch, VIEW_DEST_FOLDERS, L"Files & Folders",        rsw::ui::StudioTreeIcon::SubFolder);
        addView(hArch, VIEW_SETUP_DESIGN, L"Components & KeyPaths",  rsw::ui::StudioTreeIcon::ComponentGear);
        addView(hArch, VIEW_REGISTRY,     L"Registry",               rsw::ui::StudioTreeIcon::RegistryHive);
        addView(hArch, VIEW_FILE_ASSOC,   L"File Associations",      rsw::ui::StudioTreeIcon::FileExtDoc);

        HTREEITEM hUi = addCat(L"User Interface");
        addView(hUi, VIEW_SHORTCUTS,   L"Shortcuts",               rsw::ui::StudioTreeIcon::ShortcutLink);
        addView(hUi, VIEW_CPP_WIZARD,  L"C++ Wizard Designer",     rsw::ui::StudioTreeIcon::WizardWindow);
        addView(hUi, VIEW_MSI_DIALOGS, L"MSI.DLL Dialog Designer", rsw::ui::StudioTreeIcon::MsiDialogSql);

        HTREEITEM hDep = addCat(L"Deployment & Source");
        addView(hDep, VIEW_SIGN_REL, L"Signing, .MST & .MSP", rsw::ui::StudioTreeIcon::CertShield);
        addView(hDep, VIEW_SOURCE,   L".RSIP Source (AST)",   rsw::ui::StudioTreeIcon::SourceCode);

        for (HTREEITEM c : {hAppInfo, hArch, hUi, hDep}) {
            TreeView_Expand(hNavTree, c, TVE_EXPAND);
        }
        SelectIdeView(VIEW_PACKAGE);
    }

    void SelectIdeView(int viewIdx) {
        if (viewIdx < 0 || viewIdx >= VIEW_COUNT) return;
        activeView = viewIdx;
        if (viewNavItems[static_cast<std::size_t>(viewIdx)]) {
            suppressNavEvent = true;
            TreeView_SelectItem(hNavTree, viewNavItems[static_cast<std::size_t>(viewIdx)]);
            suppressNavEvent = false;
        }
        UpdateActiveViewVisibility();
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        auto mkBtn = [&](int view, const wchar_t* txt, int id, DWORD style = BS_PUSHBUTTON) {
            HWND h = RegisterViewCtrl(view, ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_TABSTOP | style,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
            const DWORD lowType = style & 0x0Fu;
            if (lowType == BS_PUSHBUTTON || lowType == BS_DEFPUSHBUTTON) {
                const bool primary = (id == IDC_TB_BUILD_MSI || id == IDC_PKG_APPLY ||
                                      id == IDC_FT_SAVE_BTN  || id == IDC_REG_SET_VAL_BTN ||
                                      id == IDC_UI_APPLY     || id == IDC_SIG_APPLY);
                rsw::ui::AttachAccessiblePushButtonSubclass(h, &theme, primary, false);
            }
            return h;
        };
        auto mkLbl = [&](int view, const wchar_t* txt) {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                0, L"STATIC", txt, WS_CHILD, 0, 0, 10, 10, hWnd, nullptr, hInst, nullptr));
        };
        auto mkEd = [&](int view, int id, const wchar_t* initText = L"") {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", initText, WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
        };
        auto mkTree = [&](int view, int id, const wchar_t* a11yName) {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_TREEVIEWW, a11yName,
                WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
        };
        auto mkCombo = [&](int view, int id, std::initializer_list<const wchar_t*> items, int defSel = 0) {
            HWND cb = RegisterViewCtrl(view, ::CreateWindowExW(
                0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 160, 200, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
            for (const wchar_t* s : items) {
                ::SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            ::SendMessageW(cb, CB_SETCURSEL, defSel, 0);
            rsw::ui::ApplyDarkComboBoxTheme(cb, theme);
            return cb;
        };

        // Top Persistent Toolbar (`view = -1`)
        hBtnWizard     = mkBtn(-1, L"Project &Wizard...", IDC_TB_WIZARD);
        hBtnNew        = mkBtn(-1, L"&New",               IDC_TB_NEW);
        hBtnOpen       = mkBtn(-1, L"&Open...",           IDC_TB_OPEN);
        hBtnSave       = mkBtn(-1, L"&Save",              IDC_TB_SAVE);
        hBtnValidate   = mkBtn(-1, L"&Validate (F7)",     IDC_TB_VALIDATE);
        hBtnBuildMsi   = mkBtn(-1, L"&Build MSI+EXE (F5)",IDC_TB_BUILD_MSI);
        hBtnBuildMsm   = mkBtn(-1, L".&MSM",              IDC_TB_BUILD_MSM);
        hBtnExportVpat = mkBtn(-1, L"&VPAT 2.5",          IDC_TB_EXPORT_VPAT);
        hBtnMsiPrism   = mkBtn(-1, L"MSI &Prism",         IDC_TB_MSIPRISM);

        const auto hp = rsw::QueryHostPlatform();
        hArchCombo  = mkCombo(-1, IDC_ARCH_COMBO, {L"arm64", L"x64"}, (hp.NativeArchSlug() == "x64") ? 1 : 0);
        hSuiteCombo = mkCombo(-1, IDC_SUITE_COMBO, {L"[Standalone .rsip Mode]"}, 0);

        // Left Category Navigation Tree (`view = -1`)
        hNavTree = mkTree(-1, IDC_NAV_TREE, L"Installer Studio Project Views");

        // --- VIEW 0: Package & Identity ---
        mkLbl(VIEW_PACKAGE, L"&Product Name:");       hPkgName = mkEd(VIEW_PACKAGE, IDC_PKG_NAME);
        mkLbl(VIEW_PACKAGE, L"Subtitle / &Tagline:"); hPkgTag  = mkEd(VIEW_PACKAGE, IDC_PKG_TAGLINE);
        mkLbl(VIEW_PACKAGE, L"Footer &Website:");     hPkgWeb  = mkEd(VIEW_PACKAGE, IDC_PKG_WEBSITE);
        mkLbl(VIEW_PACKAGE, L"MSI &Version (X.Y.Z):");hPkgVer  = mkEd(VIEW_PACKAGE, IDC_PKG_VERSION);
        mkLbl(VIEW_PACKAGE, L"&Manufacturer:");       hPkgPub  = mkEd(VIEW_PACKAGE, IDC_PKG_PUBLISHER);
        mkLbl(VIEW_PACKAGE, L"&UpgradeCode GUID:");   hPkgGuid = mkEd(VIEW_PACKAGE, IDC_PKG_UPGRADECODE);
        hPkgBtnGuid = mkBtn(VIEW_PACKAGE, L"New &GUID", IDC_PKG_NEW_GUID);
        mkLbl(VIEW_PACKAGE, L"Install &Subfolder:");  hPkgSub     = mkEd(VIEW_PACKAGE, IDC_PKG_SUBDIR);
        mkLbl(VIEW_PACKAGE, L"Main &Executable:");    hPkgExe     = mkEd(VIEW_PACKAGE, IDC_PKG_MAINEXE);
        mkLbl(VIEW_PACKAGE, L"Install S&cope:");      hPkgScope   = mkCombo(VIEW_PACKAGE, IDC_PKG_SCOPE, {L"machine", L"user"}, 0);
        hPkgUnivChk = mkBtn(VIEW_PACKAGE, L"Build &Universal Combined EXE (x64+ARM64 + Web Stub)", IDC_PKG_UNIV_CHK, BS_AUTOCHECKBOX);
        mkLbl(VIEW_PACKAGE, L"Universal Web &URL:");  hPkgUnivUrl = mkEd(VIEW_PACKAGE, IDC_PKG_UNIV_URL);
        mkLbl(VIEW_PACKAGE, L"Setup U&AC Level:");
        hPkgExecLvl = mkCombo(VIEW_PACKAGE, IDC_PKG_EXEC_LVL, {
            L"asInvoker (Elevate at Install — Recommended)",
            L"highestAvailable (Elevate upfront for Admins)",
            L"requireAdministrator (Require Admin at 0.0s)"
        }, 0);
        hPkgApply = mkBtn(VIEW_PACKAGE, L"&Apply Package Identity to .rsip AST (Preserves Comments)", IDC_PKG_APPLY);

        // --- VIEW 1: Application Runtimes (`[Prerequisite.*]`) & Merge Modules (`[MergeModule.*]`) Tree ---
        hPrTree = mkTree(VIEW_PREREQS, IDC_PR_TREE, L"Application Runtimes and Merge Modules Tree");
        mkLbl(VIEW_PREREQS, L"Runtime Preset:");
        hPrPreset = mkCombo(VIEW_PREREQS, IDC_PR_PRESET_COMBO, {
            L"DotNet10Desktop (.NET 10 Desktop)",
            L"DotNet10AspNet (ASP.NET Core 10)",
            L"DotNet9Desktop (.NET 9 Desktop)",
            L"DotNet8Desktop (.NET 8 LTS)",
            L"EdgeWebView2 (WebView2 Evergreen)",
            L"VcRedist2022 (VC++ 2015-2022)",
            L"Custom (.exe / .msi)"
        }, 0);
        mkLbl(VIEW_PREREQS, L"Delivery:");
        hPrDist = mkCombo(VIEW_PREREQS, IDC_PR_DIST_COMBO, {L"download", L"embedded", L"side-by-side"}, 0);
        mkLbl(VIEW_PREREQS, L"URL / File:");
        hPrSource      = mkEd(VIEW_PREREQS, IDC_PR_SOURCE_EDIT, L"https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-$(ARCH).exe");
        hPrBrowseBtn   = mkBtn(VIEW_PREREQS, L"&Pick...", IDC_PR_BROWSE_BTN);
        mkLbl(VIEW_PREREQS, L"Reboot Codes:");
        hPrRebootCodes = mkEd(VIEW_PREREQS, IDC_PR_REBOOT_CODES, L"3010, 1641, 23");
        hPrAddBtn      = mkBtn(VIEW_PREREQS, L"+ Add [&Prerequisite]",       IDC_PR_ADD_BTN);
        hPrAddMsmBtn   = mkBtn(VIEW_PREREQS, L"+ Add .&MSM Merge Module...", IDC_PR_ADD_MSM_BTN);
        hPrRemBtn      = mkBtn(VIEW_PREREQS, L"&Remove Selected",            IDC_PR_REMOVE_BTN);
        hPrWizBtn      = mkBtn(VIEW_PREREQS, L"Runtime && .MSM &AeroWizard...", IDC_PR_WIZ_BTN);

        // --- VIEW 2: Dedicated Features Hierarchy (`All Features` Tree + Up/Down + Description) ---
        hFtBannerLbl = mkLbl(VIEW_FEATURES, L"Specify the logical features of your application. Create features or sub-features, sort display order with Move Up / Down, and edit descriptions below.");
        hFtTree       = mkTree(VIEW_FEATURES, IDC_FT_TREE, L"All Application Features Tree");
        hFtUpBtn      = mkBtn(VIEW_FEATURES, L"\x25B2 &Up",            IDC_FT_UP_BTN);
        hFtDownBtn    = mkBtn(VIEW_FEATURES, L"\x25BC &Down",          IDC_FT_DOWN_BTN);
        hFtNewRootBtn = mkBtn(VIEW_FEATURES, L"+ &New Feature",        IDC_FT_NEW_ROOT_BTN);
        hFtNewSubBtn  = mkBtn(VIEW_FEATURES, L"+ &Sub-Feature",        IDC_FT_NEW_SUB_BTN);
        hFtSaveBtn    = mkBtn(VIEW_FEATURES, L"&Save Feature",         IDC_FT_SAVE_BTN);
        hFtDelBtn     = mkBtn(VIEW_FEATURES, L"&Delete Feature",       IDC_FT_DELETE_BTN);
        hFtReqChk     = mkBtn(VIEW_FEATURES, L"Core &Required (DisallowAbsent)", IDC_FT_REQUIRED_CHK, BS_AUTOCHECKBOX);
        mkLbl(VIEW_FEATURES, L"Feature ID:");          hFtId     = mkEd(VIEW_FEATURES, IDC_FT_ID, L"Main");
        mkLbl(VIEW_FEATURES, L"Display Title:");       hFtTitle  = mkEd(VIEW_FEATURES, IDC_FT_TITLE, L"Core Application");
        mkLbl(VIEW_FEATURES, L"Included &In:");
        hFtLevel = mkCombo(VIEW_FEATURES, IDC_FT_LEVEL, {
            L"Minimal / Compact & All Setups",
            L"Typical / Express & Complete",
            L"Complete Only (Optional in Custom)",
            L"Hidden / Conditional Only"
        }, 0);
        mkLbl(VIEW_FEATURES, L"Parent:");              hFtParent = mkEd(VIEW_FEATURES, IDC_FT_PARENT, L"");
        mkLbl(VIEW_FEATURES, L"Feature Description:"); hFtDesc   = mkEd(VIEW_FEATURES, IDC_FT_DESC, L"Essential application executables and support libraries.");

        // --- VIEW 3: Files & Folders (`Destination Computer` Tree + `Folder Contents` Tree) ---
        hDestTree      = mkTree(VIEW_DEST_FOLDERS, IDC_DEST_TREE, L"Destination Computer Folders Tree");
        hPayTree       = mkTree(VIEW_DEST_FOLDERS, IDC_PAY_TREE,  L"Selected Destination Folder Contents Tree");
        hPayAddFile    = mkBtn(VIEW_DEST_FOLDERS, L"&Add File Here...",   IDC_PAY_ADD_FILE);
        hPayAddDir     = mkBtn(VIEW_DEST_FOLDERS, L"Add &Folder Here...", IDC_PAY_ADD_FOLDER);
        hDestSubEdit   = mkEd(VIEW_DEST_FOLDERS,  IDC_DEST_SUBDIR_EDIT, L"Plugins");
        hDestNewSubBtn = mkBtn(VIEW_DEST_FOLDERS, L"+ &CreateFolder",     IDC_DEST_NEW_SUBDIR);
        hPayRemove     = mkBtn(VIEW_DEST_FOLDERS, L"&Remove Selected",    IDC_PAY_REMOVE);

        // --- VIEW 4: Components & KeyPaths (`Features -> Components -> KeyPaths` Tree) ---
        hDesignTree = mkTree(VIEW_SETUP_DESIGN, IDC_DESIGN_TREE, L"Components and KeyPaths Tree");
        mkLbl(VIEW_SETUP_DESIGN, L"Feature ID:");     hFeatId     = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_ID, L"Main");
        mkLbl(VIEW_SETUP_DESIGN, L"Feature Title:");  hFeatTitle  = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_TITLE, L"Core Application");
        mkLbl(VIEW_SETUP_DESIGN, L"Included In:");
        hFeatLevel = mkCombo(VIEW_SETUP_DESIGN, IDC_FEAT_LEVEL, {
            L"Minimal / Compact & All Setups",
            L"Typical / Express & Complete",
            L"Complete Only (Optional in Custom)",
            L"Hidden / Conditional Only"
        }, 0);
        mkLbl(VIEW_SETUP_DESIGN, L"Parent Feature:"); hFeatParent = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_PARENT, L"");
        hFeatSaveBtn = mkBtn(VIEW_SETUP_DESIGN, L"Save &Feature", IDC_FEAT_SAVE_BTN);

        mkLbl(VIEW_SETUP_DESIGN, L"Component ID:");   hCompId     = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_ID, L"CoreExe");
        mkLbl(VIEW_SETUP_DESIGN, L"Owning Feature:"); hCompFeat   = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_FEAT, L"Main");
        mkLbl(VIEW_SETUP_DESIGN, L"Target Root:");
        hCompRoot = mkCombo(VIEW_SETUP_DESIGN, IDC_COMP_ROOT, {
            L"INSTALLDIR", L"CommonFiles64Folder", L"CommonAppDataFolder", L"AppDataFolder",
            L"LocalAppDataFolder", L"ApplicationProgramsFolder", L"DesktopFolder", L"System64Folder", L"FontsFolder"
        }, 0);
        mkLbl(VIEW_SETUP_DESIGN, L"Target Subdir:");  hCompSubdir = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_SUBDIR, L"");
        mkLbl(VIEW_SETUP_DESIGN, L"KeyPath Kind:");
        hCompKpKind = mkCombo(VIEW_SETUP_DESIGN, IDC_COMP_KP_KIND, {L"auto", L"file", L"registry", L"folder"}, 0);
        mkLbl(VIEW_SETUP_DESIGN, L"KeyPath Target:"); hCompKpTarget = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_KP_TARGET, L"");
        hCompSaveBtn = mkBtn(VIEW_SETUP_DESIGN, L"Save &Component", IDC_COMP_SAVE_BTN);
        hCompDelBtn  = mkBtn(VIEW_SETUP_DESIGN, L"&Delete Component", IDC_COMP_DEL_BTN);
        hCompWizBtn  = mkBtn(VIEW_SETUP_DESIGN, L"&Component & KeyPath AeroWizard...", IDC_COMP_WIZ_BTN);

        // --- VIEW 5: `RegEdit`-Style Registry View (Hives/Keys Tree + Values Tree + `.REG` Importer) ---
        hRegFilterLbl  = mkLbl(VIEW_REGISTRY, L"Filter Keys by &Feature:");
        hRegFeatFilter = mkCombo(VIEW_REGISTRY, IDC_REG_FEAT_FILTER, {L"[All Features]"}, 0);
        hRegImportBtn  = mkBtn(VIEW_REGISTRY, L"&Import .REG File...", IDC_REG_IMPORT_BTN);
        hRegKeyTree    = mkTree(VIEW_REGISTRY, IDC_REG_KEY_TREE, L"Target Computer Registry Hives and Keys");
        hRegValTree    = mkTree(VIEW_REGISTRY, IDC_REG_VAL_TREE, L"Selected Registry Key Values");
        hRegSubEdit    = mkEd(VIEW_REGISTRY, IDC_REG_SUBKEY_EDIT, L"Settings");
        hRegAddKeyBtn  = mkBtn(VIEW_REGISTRY, L"+ Add Sub&key", IDC_REG_ADD_KEY_BTN);
        hRegDelKeyBtn  = mkBtn(VIEW_REGISTRY, L"Delete &Key",   IDC_REG_DEL_KEY_BTN);

        mkLbl(VIEW_REGISTRY, L"Hive:");
        hRegRootCombo = mkCombo(VIEW_REGISTRY, IDC_REG_ROOT_COMBO, {L"HKLM", L"HKCU", L"HKCR", L"HKU", L"HKMU"}, 0);
        mkLbl(VIEW_REGISTRY, L"Key Path:");    hRegKey       = mkEd(VIEW_REGISTRY, IDC_REG_KEY, L"Software\\Sample Studio\\My Native App");
        mkLbl(VIEW_REGISTRY, L"Value Name:");  hRegName      = mkEd(VIEW_REGISTRY, IDC_REG_NAME, L"InstallPath");
        mkLbl(VIEW_REGISTRY, L"Type:");        hRegTypeCombo = mkCombo(VIEW_REGISTRY, IDC_REG_TYPE_COMBO, {L"string", L"expand_string", L"dword"}, 0);
        mkLbl(VIEW_REGISTRY, L"Value Data:");  hRegVal       = mkEd(VIEW_REGISTRY, IDC_REG_VALUE, L"[INSTALLDIR]");
        mkLbl(VIEW_REGISTRY, L"Feature:");     hRegFeatEdit  = mkEd(VIEW_REGISTRY, IDC_REG_FEAT_EDIT, L"Main");
        mkLbl(VIEW_REGISTRY, L"Component:");   hRegComp      = mkEd(VIEW_REGISTRY, IDC_REG_COMP, L"");
        hRegSetValBtn = mkBtn(VIEW_REGISTRY, L"&Set Registry Value", IDC_REG_SET_VAL_BTN);
        hRegDelValBtn = mkBtn(VIEW_REGISTRY, L"&Delete Value",       IDC_REG_DEL_VAL_BTN);

        // --- VIEW 6: File Associations & Shell Verbs Tree ---
        hFaTree = mkTree(VIEW_FILE_ASSOC, IDC_FA_TREE, L"File Associations and Shell Verbs Tree");
        mkLbl(VIEW_FILE_ASSOC, L".Ext:");        hFaExt    = mkEd(VIEW_FILE_ASSOC, IDC_FA_EXT, L".rsdoc");
        mkLbl(VIEW_FILE_ASSOC, L"ProgId:");      hFaProgId = mkEd(VIEW_FILE_ASSOC, IDC_FA_PROGID, L"MyStudio.MyApp.Doc");
        mkLbl(VIEW_FILE_ASSOC, L"Description:"); hFaDesc   = mkEd(VIEW_FILE_ASSOC, IDC_FA_DESC, L"Application Document");
        mkLbl(VIEW_FILE_ASSOC, L"Target Exe:");  hFaTarget = mkEd(VIEW_FILE_ASSOC, IDC_FA_TARGET, L"HelloRaggieSoft.exe");
        mkLbl(VIEW_FILE_ASSOC, L"Verbs:");       hFaVerbs  = mkEd(VIEW_FILE_ASSOC, IDC_FA_VERBS, L"open:&Open:\"%1\"");
        hFaAdvChk = mkBtn(VIEW_FILE_ASSOC, L"&Advertise (ProgId/Verb Tables)", IDC_FA_ADVERTISE, BS_AUTOCHECKBOX);
        hFaAddBtn = mkBtn(VIEW_FILE_ASSOC, L"&Save / Add Association", IDC_FA_ADD_BTN);
        hFaRemBtn = mkBtn(VIEW_FILE_ASSOC, L"&Remove Selected",        IDC_FA_REMOVE_BTN);
        hFaWizBtn = mkBtn(VIEW_FILE_ASSOC, L"File Association &AeroWizard...", IDC_FA_WIZ_BTN);

        // --- VIEW 7: Application Shortcuts Tree (`Start Menu` & `Desktop`) ---
        hScTree = mkTree(VIEW_SHORTCUTS, IDC_SC_TREE, L"Application Shortcuts Tree");
        mkLbl(VIEW_SHORTCUTS, L"Shortcut ID:");   hScIdEdit   = mkEd(VIEW_SHORTCUTS, IDC_SC_ID_EDIT, L"AppMenu");
        mkLbl(VIEW_SHORTCUTS, L"Display Name:");  hScName     = mkEd(VIEW_SHORTCUTS, IDC_SC_NAME,    L"My Native Application");
        mkLbl(VIEW_SHORTCUTS, L"Target Exe:");    hScTarget   = mkEd(VIEW_SHORTCUTS, IDC_SC_TARGET,  L"HelloRaggieSoft.exe");
        mkLbl(VIEW_SHORTCUTS, L"Location:");      hScLocCombo = mkCombo(VIEW_SHORTCUTS, IDC_SC_LOC_COMBO, {L"StartMenu", L"Desktop"}, 0);
        mkLbl(VIEW_SHORTCUTS, L"Arguments:");     hScArgs     = mkEd(VIEW_SHORTCUTS, IDC_SC_ARGS,    L"");
        mkLbl(VIEW_SHORTCUTS, L"Feature:");       hScFeat     = mkEd(VIEW_SHORTCUTS, IDC_SC_FEAT,    L"Main");
        hScSaveBtn = mkBtn(VIEW_SHORTCUTS, L"&Save Shortcut",   IDC_SC_SAVE_BTN);
        hScDelBtn  = mkBtn(VIEW_SHORTCUTS, L"&Delete Shortcut", IDC_SC_DEL_BTN);

        // --- VIEW 8: C++ Wizard Designer (`SetupWizard.exe` — All 10 Wizard Styles + WCAG Auto-Contrast!) ---
        mkLbl(VIEW_CPP_WIZARD, L"Setup.exe Style:");
        hUiLayout = mkCombo(VIEW_CPP_WIZARD, IDC_UI_LAYOUT, {}, 0);
        for (int wi = 0; wi < rsw::kWizardStyleCount; ++wi) {
            ::SendMessageW(hUiLayout, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(rsw::WizardStyleDisplayLabel(static_cast<rsw::WizardStyleKind>(wi))));
        }
        ::SendMessageW(hUiLayout, CB_SETCURSEL, static_cast<WPARAM>(rsw::WizardStyleKind::AeroWizard), 0);

        mkLbl(VIEW_CPP_WIZARD, L"Setup Types:");
        hUiSetupTypes = mkCombo(VIEW_CPP_WIZARD, IDC_UI_SETUP_TYPES, {
            L"express_custom (Express / Compact / Custom)",
            L"win95_classic (Typical / Compact / Complete / Custom)"
        }, 0);

        mkLbl(VIEW_CPP_WIZARD, L"Preview Step:");
        hUiPreviewStep = mkCombo(VIEW_CPP_WIZARD, IDC_UI_PREVIEW_STEP, {
            L"Welcome & Hero Brand Card",
            L"Setup Type (Option Cards)",
            L"Office-Style Repair Mode",
            L"Installing Live Progress & Billboard",
            L"16:9 Autorun Cover & 2x2 Menu",
            L"Select Features (MSI SelectionTree)"
        }, 1);
        mkLbl(VIEW_CPP_WIZARD, L"Primary Accent Hex:");
        hUiPrim        = mkEd(VIEW_CPP_WIZARD, IDC_UI_PRIMARY_HEX);
        hUiAutoWcagBtn = mkBtn(VIEW_CPP_WIZARD, L"Auto-&Fix WCAG", IDC_UI_AUTO_WCAG);
        mkLbl(VIEW_CPP_WIZARD, L"Secondary Accent Hex:"); hUiSec     = mkEd(VIEW_CPP_WIZARD, IDC_UI_SECONDARY_HEX);
        mkLbl(VIEW_CPP_WIZARD, L"Install Button Text:");  hUiInstBtn = mkEd(VIEW_CPP_WIZARD, IDC_UI_INSTALL_BTN);
        mkLbl(VIEW_CPP_WIZARD, L"Icon (.ico) Path:");     hUiIcon    = mkEd(VIEW_CPP_WIZARD, IDC_UI_ICON_PATH);
        hUiIconBtn = mkBtn(VIEW_CPP_WIZARD, L"Pick .&ico...", IDC_UI_ICON_BROWSE);
        mkLbl(VIEW_CPP_WIZARD, L"Artwork (.png/.jpg):");  hUiArt     = mkEd(VIEW_CPP_WIZARD, IDC_UI_ART_PATH);
        hUiArtBtn  = mkBtn(VIEW_CPP_WIZARD, L"Pick &Art...", IDC_UI_ART_BROWSE);
        hUiApply   = mkBtn(VIEW_CPP_WIZARD, L"&Apply C++ Wizard Branding && Refresh Canvas", IDC_UI_APPLY);

        // --- VIEW 9: Traditional `MSI.DLL` `Dialog` Table Designer (`370 x 270` DLU) ---
        mkLbl(VIEW_MSI_DIALOGS, L"MSI Workflow:");
        hMsiPreset = mkCombo(VIEW_MSI_DIALOGS, IDC_MSI_PRESET_COMBO, {
            L"express_custom (Express / Compact / Custom)",
            L"minimal (3-Screen Streamlined)"
        }, 0);
        mkLbl(VIEW_MSI_DIALOGS, L"Inspect Dialog:");
        hMsiDlgSelect = mkCombo(VIEW_MSI_DIALOGS, IDC_MSI_DLG_SELECT, {
            L"WelcomeDlg", L"LicenseAgreementDlg", L"SetupTypeDlg",
            L"CustomizeDlg", L"InstallDirDlg", L"ShortcutsDlg",
            L"RepairModeDlg", L"VerifyReadyDlg", L"ExitDialog"
        }, 2);
        mkLbl(VIEW_MSI_DIALOGS, L"New Dialog ID:");    hMsiNewId    = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_NEW_DLG_ID,    L"ServerConfigDlg");
        mkLbl(VIEW_MSI_DIALOGS, L"Banner Title:");     hMsiNewTitle = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_NEW_DLG_TITLE, L"Connection Settings");
        mkLbl(VIEW_MSI_DIALOGS, L"Splice After:");     hMsiNewAfter = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_NEW_DLG_AFTER, L"SetupTypeDlg");
        hMsiAddDlgBtn = mkBtn(VIEW_MSI_DIALOGS, L"+ Add [Msi&Dialog] to Sequence", IDC_MSI_ADD_DLG_BTN);

        mkLbl(VIEW_MSI_DIALOGS, L"Control ID:");       hMsiCtrlId   = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_ID,   L"EndpointEdit");
        mkLbl(VIEW_MSI_DIALOGS, L"Control Type:");
        hMsiCtrlType = mkCombo(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_TYPE, {
            L"Text", L"Edit", L"PathEdit", L"CheckBox", L"RadioButtonGroup", L"ComboBox", L"GroupBox"
        }, 1);
        mkLbl(VIEW_MSI_DIALOGS, L"X, Y, W, H (DLU):"); hMsiCtrlXywh = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_XYWH, L"20, 74, 330, 18");
        mkLbl(VIEW_MSI_DIALOGS, L"MSI Property:");     hMsiCtrlProp = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_PROP, L"ENDPOINT_URL");
        mkLbl(VIEW_MSI_DIALOGS, L"Text / Items:");     hMsiCtrlText = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_TEXT, L"https://raggiesoftstudio.com");
        hMsiAddCtrlBtn = mkBtn(VIEW_MSI_DIALOGS, L"+ Place [Msi&Control]", IDC_MSI_ADD_CTRL_BTN);
        hMsiIncludeBtn = mkBtn(VIEW_MSI_DIALOGS, L"@&include Sample Preset...", IDC_MSI_INCLUDE_BTN);

        // --- VIEW 10: Authenticode Signing, .MST & .MSP ---
        hSigEnable = mkBtn(VIEW_SIGN_REL, L"&Enable Dual-Layer Authenticode Signing (Product.msi + Setup.exe)", IDC_SIG_ENABLED, BS_AUTOCHECKBOX);
        mkLbl(VIEW_SIGN_REL, L"Cert SHA-1 Thumbprint:"); hSigThumb = mkEd(VIEW_SIGN_REL, IDC_SIG_THUMBPRINT);
        mkLbl(VIEW_SIGN_REL, L"Timestamp Server URL:");  hSigTs    = mkEd(VIEW_SIGN_REL, IDC_SIG_TIMESTAMP);
        mkLbl(VIEW_SIGN_REL, L"Azure Signing Dlib:");    hSigDlib  = mkEd(VIEW_SIGN_REL, IDC_SIG_DLIB);
        mkLbl(VIEW_SIGN_REL, L"Azure Metadata JSON:");   hSigMeta  = mkEd(VIEW_SIGN_REL, IDC_SIG_META);
        hSigApply  = mkBtn(VIEW_SIGN_REL, L"&Save Code Signing Settings", IDC_SIG_APPLY);

        mkLbl(VIEW_SIGN_REL, L"Baseline Released .MSI:"); hEntBase = mkEd(VIEW_SIGN_REL, IDC_ENT_BASE_MSI);
        hEntBaseBtn   = mkBtn(VIEW_SIGN_REL, L"&Browse...", IDC_ENT_BASE_BROWSE);
        mkLbl(VIEW_SIGN_REL, L"Target / Upgraded .MSI:"); hEntTarg = mkEd(VIEW_SIGN_REL, IDC_ENT_TARG_MSI);
        hEntTargBtn   = mkBtn(VIEW_SIGN_REL, L"B&rowse...", IDC_ENT_TARG_BROWSE);
        hEntGenMst    = mkBtn(VIEW_SIGN_REL, L"Emit .&MST Transform", IDC_ENT_GEN_MST);
        hEntGenMsp    = mkBtn(VIEW_SIGN_REL, L"Emit .MS&P Patch",     IDC_ENT_GEN_MSP);
        hEntRelWizBtn = mkBtn(VIEW_SIGN_REL, L"&Release / .MST / .MSP AeroWizard...", IDC_ENT_REL_WIZ_BTN);

        // --- VIEW 11: Lossless `.rsip` Source ---
        hSourceEdit = RegisterViewCtrl(VIEW_SOURCE, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_EDIT)), hInst, nullptr));
        ::SendMessageW(hSourceEdit, EM_SETLIMITTEXT, 0, 0);

        // Bottom Console (`view = -1`)
        hConsoleEdit = RegisterViewCtrl(-1, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONSOLE_EDIT)), hInst, nullptr));
        ::SendMessageW(hConsoleEdit, EM_SETLIMITTEXT, 0, 0);

        rsw::ui::AttachAccessibleCheckboxSubclass(hPkgUnivChk, &theme, false);
        rsw::ui::AttachAccessibleCheckboxSubclass(hFtReqChk,   &theme, false);
        rsw::ui::AttachAccessibleCheckboxSubclass(hFaAdvChk,   &theme, false);
        rsw::ui::AttachAccessibleCheckboxSubclass(hSigEnable,  &theme, false);
        for (HWND sc : {hSourceEdit, hConsoleEdit}) {
            rsw::ui::ApplyDarkScrollbarTheme(sc, theme);
        }

        RefreshAllTreesThemeAndIcons();
        PopulateCategorySidebarTree();
    }

    void LayoutChildren() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        DestroyFonts();
        const int ts = theme.textScalePercent;
        hFontBanner = rsw::ui::CreateScaledFont(L"Segoe UI",      13, FW_BOLD,     dpi, ts);
        hFontHead   = rsw::ui::CreateScaledFont(L"Segoe UI",      10, FW_SEMIBOLD, dpi, ts);
        hFontUi     = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_NORMAL,   dpi, ts);
        hFontBold   = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_SEMIBOLD, dpi, ts);
        hFontMono   = rsw::ui::CreateScaledFont(L"Cascadia Mono", 10, FW_NORMAL,   dpi, ts);

        for (const auto& tc : taggedControls) {
            ::SendMessageW(tc.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        for (HWND h : {hBtnWizard, hBtnBuildMsi, hPkgApply, hFtSaveBtn, hRegSetValBtn, hUiApply, hCompWizBtn, hPrWizBtn, hFaWizBtn, hEntRelWizBtn}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hSourceEdit, hConsoleEdit}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);
        }

        RefreshAllTreesThemeAndIcons();

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(780, rc.right - rc.left);
        const int H = std::max<int>(560, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, ts); };

        const int tbY = S(60);
        int tx = S(12);
        auto placeTb = [&](HWND h, int w) { ::MoveWindow(h, tx, tbY, S(w), S(30), TRUE); tx += S(w + 4); };
        placeTb(hBtnWizard,     118);
        placeTb(hBtnNew,         48);
        placeTb(hBtnOpen,        62);
        placeTb(hBtnSave,        52);
        placeTb(hBtnValidate,    92);
        placeTb(hBtnBuildMsi,   132);
        placeTb(hBtnBuildMsm,    56);
        placeTb(hBtnExportVpat,  76);
        placeTb(hBtnMsiPrism,    80);
        ::MoveWindow(hArchCombo,  tx, tbY + S(2), S(74), S(140), TRUE);
        tx += S(78);
        ::MoveWindow(hSuiteCombo, tx, tbY + S(2), std::max(S(140), W - tx - S(12)), S(280), TRUE);

        const int contentTop = S(98);
        const int consoleH   = S(112);
        const int consoleY   = H - consoleH - S(10);
        const int mainH      = std::max(S(260), consoleY - contentTop - S(8));
        const int navW       = S(236);

        ::MoveWindow(hNavTree,     S(12), contentTop, navW, mainH, TRUE);
        ::MoveWindow(hConsoleEdit, S(12), consoleY,   W - S(24), consoleH, TRUE);

        const int paneX = S(24) + navW;
        const int paneW = std::max(S(460), W - paneX - S(12));

        auto viewCtrls = [&](int viewIdx) {
            std::vector<HWND> vh;
            for (const auto& tc : taggedControls) {
                if (tc.viewIndex == viewIdx) vh.push_back(tc.hwnd);
            }
            return vh;
        };

        // View 0: Package & Identity
        {
            auto v = viewCtrls(VIEW_PACKAGE);
            int y = contentTop + S(2);
            const int lw = S(148), fw = std::min(S(520), paneW - lw - S(16));
            auto row = [&](HWND lbl, HWND ed, int rsv = 0) {
                ::MoveWindow(lbl, paneX, y + S(3), lw, S(20), TRUE);
                ::MoveWindow(ed,  paneX + lw, y, fw - rsv, S(25), TRUE);
                y += S(28);
            };
            if (v.size() >= 23) {
                row(v[0], v[1]); row(v[2], v[3]); row(v[4], v[5]); row(v[6], v[7]); row(v[8], v[9]);
                row(v[10], v[11], S(92));
                ::MoveWindow(hPkgBtnGuid, paneX + lw + fw - S(86), y - S(28), S(86), S(25), TRUE);
                row(v[13], v[14]); row(v[15], v[16]);
                ::MoveWindow(v[17],       paneX,               y + S(3), lw,     S(20),  TRUE);
                ::MoveWindow(hPkgScope,   paneX + lw,          y,        S(112), S(140), TRUE);
                ::MoveWindow(hPkgUnivChk, paneX + lw + S(122), y + S(1), std::max(S(240), paneW - lw - S(132)), S(24), TRUE);
                y += S(29);
                row(v[20], hPkgUnivUrl);
                if (v.size() >= 25) {
                    ::MoveWindow(v[22],       paneX,      y + S(3), lw, S(20),  TRUE);
                    ::MoveWindow(hPkgExecLvl, paneX + lw, y,        fw, S(160), TRUE);
                    y += S(29);
                }
                ::MoveWindow(hPkgApply, paneX, y + S(2), fw + lw, S(32), TRUE);
            }
        }

        // View 1: Application Runtimes & Merge Modules Tree
        {
            auto v = viewCtrls(VIEW_PREREQS);
            const int treeH = mainH - S(116);
            ::MoveWindow(hPrTree, paneX, contentTop, paneW, treeH, TRUE);
            if (v.size() >= 14) {
                const int y1 = contentTop + treeH + S(8);
                ::MoveWindow(v[1], paneX,            y1 + S(4), S(104), S(22), TRUE);
                ::MoveWindow(hPrPreset, paneX + S(106), y1,     S(220), S(180), TRUE);
                ::MoveWindow(v[3], paneX + S(336),   y1 + S(4), S(60),  S(22), TRUE);
                ::MoveWindow(hPrDist, paneX + S(398), y1,       S(118), S(140), TRUE);
                ::MoveWindow(v[8], paneX + S(526),   y1 + S(4), S(94),  S(22), TRUE);
                ::MoveWindow(hPrRebootCodes, paneX + S(622), y1, std::max(S(110), paneW - S(622)), S(26), TRUE);

                const int y2 = y1 + S(34);
                ::MoveWindow(v[5], paneX,          y2 + S(4), S(74), S(22), TRUE);
                ::MoveWindow(hPrSource, paneX + S(76), y2,    std::max(S(200), paneW - S(156)), S(26), TRUE);
                ::MoveWindow(hPrBrowseBtn, paneX + paneW - S(74), y2, S(74), S(26), TRUE);

                const int y3 = y2 + S(34);
                ::MoveWindow(hPrAddBtn,    paneX,          y3, S(162), S(30), TRUE);
                ::MoveWindow(hPrAddMsmBtn, paneX + S(170), y3, S(196), S(30), TRUE);
                ::MoveWindow(hPrRemBtn,    paneX + S(374), y3, S(140), S(30), TRUE);
                ::MoveWindow(hPrWizBtn,    paneX + S(522), y3, std::max(S(210), paneW - S(522)), S(30), TRUE);
            }
        }

        // View 2: Dedicated Features Hierarchy (`All Features` Tree + Up/Down + Inspector)
        {
            auto v = viewCtrls(VIEW_FEATURES);
            ::MoveWindow(hFtBannerLbl, paneX, contentTop, paneW, S(22), TRUE);
            const int treeTop = contentTop + S(26);
            const int treeH   = std::max(S(140), mainH - S(142));
            const int ordW    = S(82);
            const int treeW   = paneW - ordW - S(8);
            ::MoveWindow(hFtTree,    paneX,                  treeTop,           treeW, treeH, TRUE);
            ::MoveWindow(hFtUpBtn,   paneX + treeW + S(8),   treeTop,           ordW,  S(30), TRUE);
            ::MoveWindow(hFtDownBtn, paneX + treeW + S(8),   treeTop + S(36),   ordW,  S(30), TRUE);

            const int actY = treeTop + treeH + S(8);
            ::MoveWindow(hFtNewRootBtn, paneX,          actY, S(126), S(28), TRUE);
            ::MoveWindow(hFtNewSubBtn,  paneX + S(132), actY, S(124), S(28), TRUE);
            ::MoveWindow(hFtSaveBtn,    paneX + S(262), actY, S(122), S(28), TRUE);
            ::MoveWindow(hFtDelBtn,     paneX + S(390), actY, S(126), S(28), TRUE);
            ::MoveWindow(hFtReqChk,     paneX + S(526), actY + S(1), std::max(S(210), paneW - S(526)), S(26), TRUE);

            if (v.size() >= 19) {
                const int r1Y = actY + S(36);
                ::MoveWindow(v[9],      paneX,          r1Y + S(4), S(72),  S(22),  TRUE);
                ::MoveWindow(hFtId,     paneX + S(74),  r1Y,        S(100), S(26),  TRUE);
                ::MoveWindow(v[11],     paneX + S(182), r1Y + S(4), S(82),  S(22),  TRUE);
                ::MoveWindow(hFtTitle,  paneX + S(266), r1Y,        S(198), S(26),  TRUE);
                ::MoveWindow(v[13],     paneX + S(472), r1Y + S(4), S(74),  S(22),  TRUE);
                ::MoveWindow(hFtLevel,  paneX + S(548), r1Y,        S(200), S(160), TRUE);
                ::MoveWindow(v[15],     paneX + S(756), r1Y + S(4), S(48),  S(22),  TRUE);
                ::MoveWindow(hFtParent, paneX + S(806), r1Y,        std::max(S(74), paneW - S(806)), S(26), TRUE);

                const int r2Y = r1Y + S(34);
                ::MoveWindow(v[17],  paneX,          r2Y + S(4), S(128), S(22), TRUE);
                ::MoveWindow(hFtDesc,paneX + S(130), r2Y,        paneW - S(130), S(26), TRUE);
            }
        }

        // View 3: Files & Folders (Dual-Tree: `Destination Computer` left + `Folder Contents` right!)
        {
            const int treeW = S(276);
            const int listX = paneX + treeW + S(8);
            const int listW = std::max(S(240), paneW - treeW - S(8));
            const int barY  = contentTop + mainH - S(34);
            ::MoveWindow(hDestTree, paneX, contentTop, treeW, mainH - S(42), TRUE);
            ::MoveWindow(hPayTree,  listX, contentTop, listW, mainH - S(42), TRUE);

            ::MoveWindow(hDestSubEdit,   paneX,            barY + S(2), S(140), S(28), TRUE);
            ::MoveWindow(hDestNewSubBtn, paneX + S(146),   barY,        S(130), S(32), TRUE);
            ::MoveWindow(hPayAddFile,    listX,            barY,        S(136), S(32), TRUE);
            ::MoveWindow(hPayAddDir,     listX + S(142),   barY,        S(146), S(32), TRUE);
            ::MoveWindow(hPayRemove,     listX + S(294),   barY,        S(146), S(32), TRUE);
        }

        // View 4: Components & KeyPaths (`Features -> Components -> KeyPaths` tree + live inspector!)
        {
            auto v = viewCtrls(VIEW_SETUP_DESIGN);
            const int treeW = std::max(S(320), paneW - S(400));
            const int formX = paneX + treeW + S(12);
            const int formW = paneW - treeW - S(12);
            ::MoveWindow(hDesignTree, paneX, contentTop, treeW, mainH, TRUE);
            if (v.size() >= 23) {
                int y = contentTop + S(2);
                const int lw = S(116), fw = std::max(S(140), formW - lw);
                auto row = [&](HWND lbl, HWND ctl, int h = 24) {
                    ::MoveWindow(lbl, formX, y + S(3), lw, S(20), TRUE);
                    ::MoveWindow(ctl, formX + lw, y, fw, S(h), TRUE);
                    y += S(27);
                };
                row(v[1], hFeatId);
                row(v[3], hFeatTitle);
                row(v[5], hFeatLevel, 160);
                row(v[7], hFeatParent);
                ::MoveWindow(hFeatSaveBtn, formX, y, formW, S(28), TRUE);
                y += S(36);

                row(v[10], hCompId);
                row(v[12], hCompFeat);
                row(v[14], hCompRoot, 160);
                row(v[16], hCompSubdir);
                row(v[18], hCompKpKind, 140);
                row(v[20], hCompKpTarget);
                const int halfW = (formW - S(8)) / 2;
                ::MoveWindow(hCompSaveBtn, formX,             y + S(2), halfW, S(28), TRUE);
                ::MoveWindow(hCompDelBtn,  formX + halfW + S(8), y + S(2), halfW, S(28), TRUE);
                y += S(34);
                ::MoveWindow(hCompWizBtn,  formX, y, formW, S(28), TRUE);
            }
        }

        // View 5: `RegEdit`-Style Registry View (Left Hives/Keys Tree + Right Values Tree + `.REG` Importer)
        {
            auto v = viewCtrls(VIEW_REGISTRY);
            ::MoveWindow(hRegFilterLbl,  paneX,          contentTop + S(4), S(148), S(22), TRUE);
            ::MoveWindow(hRegFeatFilter, paneX + S(152), contentTop + S(1), S(210), S(180), TRUE);
            ::MoveWindow(hRegImportBtn,  paneX + paneW - S(168), contentTop, S(168), S(28), TRUE);

            const int treesTop = contentTop + S(34);
            const int treesH   = std::max(S(130), mainH - S(144));
            const int keyTreeW = (paneW * 44) / 100;
            const int valTreeX = paneX + keyTreeW + S(8);
            const int valTreeW = paneW - keyTreeW - S(8);
            ::MoveWindow(hRegKeyTree, paneX,    treesTop, keyTreeW, treesH, TRUE);
            ::MoveWindow(hRegValTree, valTreeX, treesTop, valTreeW, treesH, TRUE);

            const int kBarY = treesTop + treesH + S(6);
            ::MoveWindow(hRegSubEdit,   paneX,                     kBarY + S(1), std::max(S(96), keyTreeW - S(224)), S(26), TRUE);
            ::MoveWindow(hRegAddKeyBtn, paneX + keyTreeW - S(218), kBarY,        S(110), S(28), TRUE);
            ::MoveWindow(hRegDelKeyBtn, paneX + keyTreeW - S(102), kBarY,        S(102), S(28), TRUE);

            if (v.size() >= 22) {
                // Value editor rows
                ::MoveWindow(v[8],          valTreeX,          kBarY + S(4), S(38),  S(22), TRUE);
                ::MoveWindow(hRegRootCombo, valTreeX + S(40),  kBarY + S(1), S(82),  S(160), TRUE);
                ::MoveWindow(v[10],         valTreeX + S(128), kBarY + S(4), S(62),  S(22), TRUE);
                ::MoveWindow(hRegKey,       valTreeX + S(192), kBarY + S(1), std::max(S(120), valTreeW - S(192)), S(26), TRUE);

                const int r1Y = kBarY + S(34);
                ::MoveWindow(v[12],         paneX,          r1Y + S(4), S(80),  S(22), TRUE);
                ::MoveWindow(hRegName,      paneX + S(82),  r1Y,        S(150), S(26), TRUE);
                ::MoveWindow(v[14],         paneX + S(240), r1Y + S(4), S(38),  S(22), TRUE);
                ::MoveWindow(hRegTypeCombo, paneX + S(280), r1Y,        S(118), S(140), TRUE);
                ::MoveWindow(v[16],         paneX + S(406), r1Y + S(4), S(74),  S(22), TRUE);
                ::MoveWindow(hRegVal,       paneX + S(482), r1Y,        std::max(S(140), paneW - S(482)), S(26), TRUE);

                const int r2Y = r1Y + S(34);
                ::MoveWindow(v[18],         paneX,          r2Y + S(4), S(56),  S(22), TRUE);
                ::MoveWindow(hRegFeatEdit,  paneX + S(58),  r2Y,        S(116), S(26), TRUE);
                ::MoveWindow(v[20],         paneX + S(182), r2Y + S(4), S(78),  S(22), TRUE);
                ::MoveWindow(hRegComp,      paneX + S(262), r2Y,        S(136), S(26), TRUE);
                ::MoveWindow(hRegSetValBtn, paneX + S(408), r2Y - S(1), S(162), S(28), TRUE);
                ::MoveWindow(hRegDelValBtn, paneX + S(578), r2Y - S(1), S(124), S(28), TRUE);
            }
        }

        // View 6: File Associations & Shell Verbs Tree
        {
            auto v = viewCtrls(VIEW_FILE_ASSOC);
            const int treeH = mainH - S(116);
            ::MoveWindow(hFaTree, paneX, contentTop, paneW, treeH, TRUE);
            if (v.size() >= 15) {
                const int y1 = contentTop + treeH + S(8);
                ::MoveWindow(v[1], paneX,          y1 + S(4), S(38),  S(22), TRUE);
                ::MoveWindow(hFaExt, paneX + S(40), y1,        S(72),  S(26), TRUE);
                ::MoveWindow(v[3], paneX + S(120), y1 + S(4), S(50),  S(22), TRUE);
                ::MoveWindow(hFaProgId, paneX + S(172), y1,    S(200), S(26), TRUE);
                ::MoveWindow(v[5], paneX + S(380), y1 + S(4), S(78),  S(22), TRUE);
                ::MoveWindow(hFaDesc, paneX + S(460), y1,      std::max(S(140), paneW - S(460)), S(26), TRUE);

                const int y2 = y1 + S(34);
                ::MoveWindow(v[7], paneX,          y2 + S(4), S(74),  S(22), TRUE);
                ::MoveWindow(hFaTarget, paneX + S(76), y2,     S(150), S(26), TRUE);
                ::MoveWindow(v[9], paneX + S(234), y2 + S(4), S(46),  S(22), TRUE);
                ::MoveWindow(hFaVerbs, paneX + S(282), y2,     S(220), S(26), TRUE);
                ::MoveWindow(hFaAdvChk, paneX + S(512), y2,    std::max(S(180), paneW - S(512)), S(26), TRUE);

                const int y3 = y2 + S(34);
                ::MoveWindow(hFaAddBtn, paneX,          y3, S(172), S(30), TRUE);
                ::MoveWindow(hFaRemBtn, paneX + S(180), y3, S(148), S(30), TRUE);
                ::MoveWindow(hFaWizBtn, paneX + S(336), y3, S(248), S(30), TRUE);
            }
        }

        // View 7: Application Shortcuts Tree (`Start Menu` & `Desktop`)
        {
            auto v = viewCtrls(VIEW_SHORTCUTS);
            const int treeH = mainH - S(82);
            ::MoveWindow(hScTree, paneX, contentTop, paneW, treeH, TRUE);
            if (v.size() >= 15) {
                const int y1 = contentTop + treeH + S(8);
                ::MoveWindow(v[1],        paneX,          y1 + S(4), S(78),  S(22), TRUE);
                ::MoveWindow(hScIdEdit,   paneX + S(80),  y1,        S(110), S(26), TRUE);
                ::MoveWindow(v[3],        paneX + S(198), y1 + S(4), S(90),  S(22), TRUE);
                ::MoveWindow(hScName,     paneX + S(290), y1,        S(210), S(26), TRUE);
                ::MoveWindow(v[5],        paneX + S(508), y1 + S(4), S(74),  S(22), TRUE);
                ::MoveWindow(hScTarget,   paneX + S(584), y1,        std::max(S(140), paneW - S(584)), S(26), TRUE);

                const int y2 = y1 + S(34);
                ::MoveWindow(v[7],        paneX,          y2 + S(4), S(62),  S(22), TRUE);
                ::MoveWindow(hScLocCombo, paneX + S(64),  y2,        S(116), S(140), TRUE);
                ::MoveWindow(v[9],        paneX + S(188), y2 + S(4), S(74),  S(22), TRUE);
                ::MoveWindow(hScArgs,     paneX + S(264), y2,        S(150), S(26), TRUE);
                ::MoveWindow(v[11],       paneX + S(422), y2 + S(4), S(54),  S(22), TRUE);
                ::MoveWindow(hScFeat,     paneX + S(478), y2,        S(104), S(26), TRUE);
                ::MoveWindow(hScSaveBtn,  paneX + S(592), y2 - S(1), S(128), S(28), TRUE);
                ::MoveWindow(hScDelBtn,   paneX + S(728), y2 - S(1), S(132), S(28), TRUE);
            }
        }

        // View 8: C++ Wizard Designer (`SetupWizard.exe` — 10 Styles + Setup Types + WCAG Auto-Fix!)
        {
            auto v = viewCtrls(VIEW_CPP_WIZARD);
            const int formW = S(388);
            int y = contentTop + S(2);
            const int lw = S(136);
            if (v.size() >= 20) {
                ::MoveWindow(v[0],          paneX,      y + S(4), lw,          S(22),  TRUE);
                ::MoveWindow(hUiLayout,     paneX + lw, y,        formW - lw,  S(260), TRUE);
                y += S(30);
                ::MoveWindow(v[2],          paneX,      y + S(4), lw,          S(22),  TRUE);
                ::MoveWindow(hUiSetupTypes, paneX + lw, y,        formW - lw,  S(140), TRUE);
                y += S(30);
                ::MoveWindow(v[4],          paneX,      y + S(4), lw,          S(22),  TRUE);
                ::MoveWindow(hUiPreviewStep,paneX + lw, y,        formW - lw,  S(180), TRUE);
                y += S(30);
                auto row = [&](HWND lbl, HWND ed, int rsv = 0) {
                    ::MoveWindow(lbl, paneX, y + S(4), lw, S(22), TRUE);
                    ::MoveWindow(ed,  paneX + lw, y, formW - lw - rsv, S(26), TRUE);
                    y += S(30);
                };
                row(v[6],  hUiPrim, S(124));
                ::MoveWindow(hUiAutoWcagBtn, paneX + formW - S(118), y - S(30), S(118), S(26), TRUE);
                row(v[9],  hUiSec);
                row(v[11], hUiInstBtn);
                row(v[13], hUiIcon, S(86));
                ::MoveWindow(hUiIconBtn, paneX + formW - S(80), y - S(30), S(80), S(26), TRUE);
                row(v[16], hUiArt, S(86));
                ::MoveWindow(hUiArtBtn,  paneX + formW - S(80), y - S(30), S(80), S(26), TRUE);
                ::MoveWindow(hUiApply,   paneX, y + S(4), formW, S(34), TRUE);
            }
            previewCanvasRc = RECT{paneX + formW + S(18), contentTop, paneX + paneW, contentTop + std::min(mainH, S(440))};
        }

        // View 9: Traditional `MSI.DLL` `Dialog` Table Designer (`370 x 270` DLU!)
        {
            auto v = viewCtrls(VIEW_MSI_DIALOGS);
            const int formW = S(386);
            int y = contentTop + S(2);
            const int lw = S(126);
            if (v.size() >= 22) {
                auto row = [&](HWND lbl, HWND ctl, int h = 25) {
                    ::MoveWindow(lbl, paneX, y + S(3), lw, S(20), TRUE);
                    ::MoveWindow(ctl, paneX + lw, y, formW - lw, S(h), TRUE);
                    y += S(28);
                };
                row(v[0], hMsiPreset, 120);
                row(v[2], hMsiDlgSelect, 180);
                row(v[4], hMsiNewId);
                row(v[6], hMsiNewTitle);
                row(v[8], hMsiNewAfter);
                ::MoveWindow(hMsiAddDlgBtn, paneX, y, formW, S(28), TRUE);
                y += S(34);

                row(v[11], hMsiCtrlId);
                row(v[13], hMsiCtrlType, 160);
                row(v[15], hMsiCtrlXywh);
                row(v[17], hMsiCtrlProp);
                row(v[19], hMsiCtrlText);
                ::MoveWindow(hMsiAddCtrlBtn, paneX, y + S(2), (formW - S(8)) / 2, S(30), TRUE);
                ::MoveWindow(hMsiIncludeBtn, paneX + (formW + S(8)) / 2, y + S(2), (formW - S(8)) / 2, S(30), TRUE);
            }
            msiCanvasRc = RECT{paneX + formW + S(18), contentTop, paneX + paneW, contentTop + std::min(mainH, S(430))};
        }

        // View 10: Signing, .MST & .MSP
        {
            auto v = viewCtrls(VIEW_SIGN_REL);
            int y = contentTop + S(4);
            const int lw = S(180), fw = std::min(S(460), paneW - lw - S(100));
            if (v.size() >= 17) {
                ::MoveWindow(hSigEnable, paneX, y, lw + fw, S(26), TRUE);
                y += S(30);
                auto row = [&](HWND lbl, HWND ed) {
                    ::MoveWindow(lbl, paneX, y + S(3), lw, S(20), TRUE);
                    ::MoveWindow(ed,  paneX + lw, y, fw, S(25), TRUE);
                    y += S(29);
                };
                row(v[1], hSigThumb); row(v[3], hSigTs); row(v[5], hSigDlib); row(v[7], hSigMeta);
                ::MoveWindow(hSigApply, paneX, y + S(2), S(240), S(30), TRUE);
                y += S(42);

                ::MoveWindow(v[10], paneX, y + S(3), lw, S(20), TRUE);
                ::MoveWindow(hEntBase, paneX + lw, y, fw, S(25), TRUE);
                ::MoveWindow(hEntBaseBtn, paneX + lw + fw + S(6), y, S(84), S(25), TRUE);
                y += S(31);
                ::MoveWindow(v[13], paneX, y + S(3), lw, S(20), TRUE);
                ::MoveWindow(hEntTarg, paneX + lw, y, fw, S(25), TRUE);
                ::MoveWindow(hEntTargBtn, paneX + lw + fw + S(6), y, S(84), S(25), TRUE);
                y += S(36);

                ::MoveWindow(hEntGenMst,    paneX,          y, S(168), S(32), TRUE);
                ::MoveWindow(hEntGenMsp,    paneX + S(176), y, S(168), S(32), TRUE);
                ::MoveWindow(hEntRelWizBtn, paneX + S(352), y, S(280), S(32), TRUE);
            }
        }

        // View 11: Full Monospace `.rsip` Source
        ::MoveWindow(hSourceEdit, paneX, contentTop, paneW, mainH, TRUE);

        UpdateActiveViewVisibility();
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void UpdateActiveViewVisibility() {
        for (const auto& tc : taggedControls) {
            if (tc.viewIndex == -1) {
                ::ShowWindow(tc.hwnd, SW_SHOW);
            } else {
                ::ShowWindow(tc.hwnd, (tc.viewIndex == activeView) ? SW_SHOW : SW_HIDE);
            }
        }
    }

    std::string ReadSourceEditorUtf8() const {
        const int cch = ::GetWindowTextLengthW(hSourceEdit);
        if (cch <= 0) return {};
        std::wstring ws(static_cast<std::size_t>(cch) + 1, L'\0');
        ::GetWindowTextW(hSourceEdit, ws.data(), cch + 1);
        ws.resize(static_cast<std::size_t>(cch));
        return rsw::WideToUtf8(ws);
    }

    void SyncSourceEditorFromAst() {
        astDoc.useCrlf = true;
        ::SetWindowTextW(hSourceEdit, rsw::Utf8ToWide(astDoc.Serialize()).c_str());
    }

    rsw::RswManifestDocument ResolvedEffectiveDoc() const {
        rsw::RswManifestDocument effectiveDoc = astDoc;
        std::string incErr;
        rsw::ResolveManifestIncludesAndVars(
            astDoc,
            hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
            effectiveDoc,
            incErr);
        return effectiveDoc;
    }

    // ----------------------------------------------------------------------------
    // POPULATE ALL HIERARCHICAL `SysTreeView32` TREES FROM EFFECTIVE AST
    // ----------------------------------------------------------------------------
    void RebuildPrerequisitesTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hPrTree);
        prNodeRefs.clear();

        auto addPrNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, std::string pfx, std::string id, bool bold = false) {
            const std::size_t idx = prNodeRefs.size();
            prNodeRefs.push_back({std::move(pfx), std::move(id)});
            return InsertIconTreeItem(hPrTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
        };

        const auto resolvedPrereqs = rsw::ResolveManifestPrerequisites(doc, SelectedArch());
        const auto msmIds          = doc.GetNamedSectionIds("MergeModule");

        HTREEITEM hPrRoot = addPrNode(
            TVI_ROOT,
            L"Pre-MSI Runtime Prerequisites (" + std::to_wstring(resolvedPrereqs.size()) + L")",
            rsw::ui::StudioTreeIcon::VerbBolt, "", "", true);
        if (resolvedPrereqs.empty()) {
            addPrNode(hPrRoot, L"(No pre-MSI runtimes configured — select a preset below to add .NET, WebView2, VC++, or Custom)",
                      rsw::ui::StudioTreeIcon::FileLeaf, "", "");
        } else {
            for (const auto& pr : resolvedPrereqs) {
                const std::wstring head = rsw::Utf8ToWide(pr.id + " — " + pr.displayName + "  [" + pr.distribution + "]");
                HTREEITEM hItem = addPrNode(hPrRoot, head, rsw::ui::StudioTreeIcon::VerbBolt, "Prerequisite", pr.id);
                const std::string srcTxt = !pr.buildSourcePath.empty() ? rsw::WideToUtf8(pr.buildSourcePath.wstring()) : pr.downloadUrl;
                addPrNode(hItem, L"Source / URL: " + rsw::Utf8ToWide(srcTxt), rsw::ui::StudioTreeIcon::FileLeaf, "Prerequisite", pr.id);
                addPrNode(hItem, L"Reboot Exit Codes: " + rsw::Utf8ToWide(rsw::FormatExitCodeList(pr.rebootCodes)),
                          rsw::ui::StudioTreeIcon::CertShield, "Prerequisite", pr.id);
                TreeView_Expand(hPrTree, hItem, TVE_EXPAND);
            }
        }
        TreeView_Expand(hPrTree, hPrRoot, TVE_EXPAND);

        HTREEITEM hMsmRoot = addPrNode(
            TVI_ROOT,
            L"Windows Installer Merge Modules (.MSM) (" + std::to_wstring(msmIds.size()) + L")",
            rsw::ui::StudioTreeIcon::MergeModuleMsm, "", "", true);
        if (msmIds.empty()) {
            addPrNode(hMsmRoot, L"(No .MSM merge modules attached)", rsw::ui::StudioTreeIcon::FileLeaf, "", "");
        } else {
            for (const auto& mid : msmIds) {
                const std::string rt   = doc.GetString("MergeModule", mid, "TargetRoot", "INSTALLDIR");
                const std::string feat = doc.GetString("MergeModule", mid, "Feature", "Main");
                const std::string src  = doc.GetString("MergeModule", mid, "Source");
                HTREEITEM hM = addPrNode(
                    hMsmRoot,
                    rsw::Utf8ToWide(mid + " -> [" + rt + "]  (Feature: " + feat + ")"),
                    rsw::ui::StudioTreeIcon::MergeModuleMsm, "MergeModule", mid);
                addPrNode(hM, L"Module File: " + rsw::Utf8ToWide(src), rsw::ui::StudioTreeIcon::FileLeaf, "MergeModule", mid);
                TreeView_Expand(hPrTree, hM, TVE_EXPAND);
            }
        }
        TreeView_Expand(hPrTree, hMsmRoot, TVE_EXPAND);
    }

    static rsw::FeatureInclusionTier QueryFeatureTierFromDoc(const rsw::RswManifestDocument& doc, const std::string& fid) {
        const std::string inc = rsw::TrimAscii(doc.GetString("Feature", fid, "Inclusion"));
        const std::string lvl = rsw::TrimAscii(doc.GetString("Feature", fid, "Level"));
        return rsw::ResolveFeatureInclusionTier(inc, doc.GetInt("Feature", fid, "Level", 1), !lvl.empty());
    }

    void PopulateFeatureInspectorFields(const rsw::RswManifestDocument& doc, const std::string& fid) {
        if (fid.empty()) return;
        selectedFtId = fid;
        const std::string title = doc.GetString("Feature", fid, "Title", fid);
        const auto tier         = QueryFeatureTierFromDoc(doc, fid);
        const std::string par   = doc.GetString("Feature", fid, "Parent");
        const std::string desc  = doc.GetString("Feature", fid, "Description");
        const bool req          = doc.GetBool("Feature", fid, "DisallowAbsent", false);

        ::SetWindowTextW(hFtId,     rsw::Utf8ToWide(fid).c_str());
        ::SetWindowTextW(hFtTitle,  rsw::Utf8ToWide(title).c_str());
        ::SendMessageW(hFtLevel, CB_SETCURSEL, static_cast<WPARAM>(tier), 0);
        ::SetWindowTextW(hFtParent, rsw::Utf8ToWide(par).c_str());
        ::SetWindowTextW(hFtDesc,   rsw::Utf8ToWide(desc).c_str());
        ::SendMessageW(hFtReqChk, BM_SETCHECK, req ? BST_CHECKED : BST_UNCHECKED, 0);
        ::InvalidateRect(hFtReqChk, nullptr, FALSE);
    }

    void RebuildFeaturesTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hFtTree);
        ftNodeIds.clear();
        ftItemMap.clear();

        auto addFtNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, const std::string& fid, bool bold = false) {
            const std::size_t idx = ftNodeIds.size();
            ftNodeIds.push_back(fid);
            HTREEITEM h = InsertIconTreeItem(hFtTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
            if (!fid.empty()) ftItemMap[rsw::AsciiLower(fid)] = h;
            return h;
        };

        const std::string prodName = doc.GetString("Package", "", "Name", "Application");
        HTREEITEM hAll = addFtNode(TVI_ROOT, L"All Features (" + rsw::Utf8ToWide(prodName) + L")",
                                   rsw::ui::StudioTreeIcon::ComputerRoot, "", true);

        auto featIds = doc.GetNamedSectionIds("Feature");
        if (featIds.empty()) {
            addFtNode(hAll, L"Main — Core Application Files  [Minimal & All Setups]", rsw::ui::StudioTreeIcon::FeatureBox, "Main");
        } else {
            // Multi-pass insertion so child features attach cleanly under parents regardless of declaration order
            std::set<std::string> inserted;
            for (int pass = 0; pass < 6 && inserted.size() < featIds.size(); ++pass) {
                for (const auto& fid : featIds) {
                    const std::string low = rsw::AsciiLower(fid);
                    if (inserted.count(low)) continue;
                    const std::string par = rsw::TrimAscii(doc.GetString("Feature", fid, "Parent"));
                    HTREEITEM hParent = hAll;
                    if (!par.empty()) {
                        auto pIt = ftItemMap.find(rsw::AsciiLower(par));
                        if (pIt == ftItemMap.end() && pass < 5) continue;
                        if (pIt != ftItemMap.end()) hParent = pIt->second;
                    }
                    const std::string title = doc.GetString("Feature", fid, "Title", fid);
                    const auto tier         = QueryFeatureTierFromDoc(doc, fid);
                    const bool req          = doc.GetBool("Feature", fid, "DisallowAbsent", false);
                    std::string label = fid + " — " + title + "  [" + std::string(rsw::FeatureInclusionBadge(tier)) +
                                        (req ? " • Core Required]" : "]");
                    addFtNode(hParent, rsw::Utf8ToWide(label),
                              req ? rsw::ui::StudioTreeIcon::CertShield : rsw::ui::StudioTreeIcon::FeatureBox,
                              fid);
                    inserted.insert(low);
                }
            }
        }

        TreeView_Expand(hFtTree, hAll, TVE_EXPAND);
        for (const auto& [k, hItem] : ftItemMap) {
            (void)k;
            TreeView_Expand(hFtTree, hItem, TVE_EXPAND);
        }

        if (selectedFtId.empty() && !featIds.empty()) {
            selectedFtId = featIds.front();
        }
        auto selIt = ftItemMap.find(rsw::AsciiLower(selectedFtId));
        if (selIt != ftItemMap.end()) {
            TreeView_SelectItem(hFtTree, selIt->second);
            PopulateFeatureInspectorFields(doc, selectedFtId);
        } else if (!featIds.empty()) {
            selectedFtId = featIds.front();
            PopulateFeatureInspectorFields(doc, selectedFtId);
        }
    }

    void RebuildDestinationTreeAndPayloadTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hDestTree);
        destNodeMetas.clear();

        auto insertNode = [&](HTREEITEM hParent, const std::wstring& label, rsw::ui::StudioTreeIcon ico, std::string root, std::string sub, bool bold = false) {
            const std::size_t idx = destNodeMetas.size();
            destNodeMetas.push_back({std::move(root), std::move(sub)});
            return InsertIconTreeItem(hDestTree, hParent, label, ico, static_cast<LPARAM>(idx), bold);
        };

        HTREEITEM hTargetPcRoot = insertNode(TVI_ROOT, L"Destination Computer (All Folders)", rsw::ui::StudioTreeIcon::ComputerRoot, "", "", true);
        struct StdRoot { const char* id; const wchar_t* label; };
        const StdRoot stdRoots[] = {
            {"INSTALLDIR",                L"[INSTALLDIR] Application Folder"},
            {"CommonFiles64Folder",       L"[CommonFiles64Folder] Common Files"},
            {"CommonAppDataFolder",       L"[CommonAppDataFolder] ProgramData"},
            {"AppDataFolder",             L"[AppDataFolder] Roaming AppData"},
            {"LocalAppDataFolder",        L"[LocalAppDataFolder] LocalAppData"},
            {"ApplicationProgramsFolder", L"[ApplicationProgramsFolder] Start Menu"},
            {"DesktopFolder",             L"[DesktopFolder] Desktop"},
            {"System64Folder",            L"[System64Folder] System32"},
            {"FontsFolder",               L"[FontsFolder] Windows Fonts"}
        };

        std::map<std::string, HTREEITEM> rootMap;
        for (const auto& sr : stdRoots) {
            HTREEITEM hR = insertNode(hTargetPcRoot, sr.label, rsw::ui::StudioTreeIcon::SubFolder, sr.id, "");
            rootMap[rsw::AsciiLower(sr.id)] = hR;
        }

        std::set<std::string> seenSubs;
        auto addSubItem = [&](const std::string& rt, const std::string& sb) {
            if (sb.empty()) return;
            const std::string key = rsw::AsciiLower(rt + "::" + sb);
            if (!seenSubs.insert(key).second) return;
            auto it = rootMap.find(rsw::AsciiLower(rt));
            HTREEITEM parentItem = (it != rootMap.end()) ? it->second : rootMap["installdir"];
            insertNode(parentItem, rsw::Utf8ToWide(sb), rsw::ui::StudioTreeIcon::CategoryFolder, rt, sb);
            TreeView_Expand(hDestTree, parentItem, TVE_EXPAND);
        };
        for (const auto& fid : doc.GetNamedSectionIds("Folder")) {
            addSubItem(doc.GetString("Folder", fid, "TargetRoot", "INSTALLDIR"), doc.GetString("Folder", fid, "TargetSubdir"));
        }
        for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
            addSubItem(doc.GetString("Payload", pid, "TargetRoot", "INSTALLDIR"), doc.GetString("Payload", pid, "TargetSubdir"));
        }
        for (const auto& cid : doc.GetNamedSectionIds("Component")) {
            addSubItem(doc.GetString("Component", cid, "TargetRoot", "INSTALLDIR"), doc.GetString("Component", cid, "TargetSubdir"));
        }
        TreeView_Expand(hDestTree, hTargetPcRoot, TVE_EXPAND);

        RefreshDestinationContentsTree(doc);
    }

    void RefreshDestinationContentsTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hPayTree);
        payNodeRefs.clear();

        auto addPayNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, std::string pfx, std::string id, bool bold = false) {
            const std::size_t idx = payNodeRefs.size();
            payNodeRefs.push_back({std::move(pfx), std::move(id)});
            return InsertIconTreeItem(hPayTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
        };

        const std::string scopeLabel = selectedDestRoot.empty()
            ? "All Destination Folders"
            : (selectedDestSubdir.empty() ? ("[" + selectedDestRoot + "]") : ("[" + selectedDestRoot + "]\\" + selectedDestSubdir));
        HTREEITEM hRoot = addPayNode(TVI_ROOT, rsw::Utf8ToWide("Staged Contents: " + scopeLabel),
                                     rsw::ui::StudioTreeIcon::SubFolder, "", "", true);

        auto matchesFilter = [&](const std::string& rt, const std::string& sb) -> bool {
            if (selectedDestRoot.empty()) return true;
            if (!rsw::EqualsIgnoreCase(rt, selectedDestRoot)) return false;
            if (selectedDestSubdir.empty()) return true;
            return rsw::EqualsIgnoreCase(sb, selectedDestSubdir);
        };

        for (const auto& fid : doc.GetNamedSectionIds("Folder")) {
            const std::string rt = doc.GetString("Folder", fid, "TargetRoot", "INSTALLDIR");
            const std::string sb = doc.GetString("Folder", fid, "TargetSubdir");
            if (!matchesFilter(rt, sb)) continue;
            const std::string feat = doc.GetString("Folder", fid, "Feature", "Main");
            HTREEITEM hF = addPayNode(
                hRoot,
                rsw::Utf8ToWide("[Folder." + fid + "]  ->  [" + rt + "]\\" + sb),
                rsw::ui::StudioTreeIcon::CategoryFolder, "Folder", fid);
            addPayNode(hF, rsw::Utf8ToWide("Feature: " + feat + "  •  CreateFolder + ICE64 RemoveFolder"),
                       rsw::ui::StudioTreeIcon::ComponentGear, "Folder", fid);
            TreeView_Expand(hPayTree, hF, TVE_EXPAND);
        }

        for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
            const std::string comp = doc.GetString("Payload", pid, "Component");
            const std::string rt   = doc.GetString("Payload", pid, "TargetRoot",
                                       comp.empty() ? "INSTALLDIR" : doc.GetString("Component", comp, "TargetRoot", "INSTALLDIR"));
            const std::string sb   = doc.GetString("Payload", pid, "TargetSubdir",
                                       comp.empty() ? "" : doc.GetString("Component", comp, "TargetSubdir"));
            if (!matchesFilter(rt, sb)) continue;
            const std::string dest = sb.empty() ? ("[" + rt + "]") : ("[" + rt + "]\\" + sb);
            const std::string feat = doc.GetString("Payload", pid, "Feature", "Main");
            const std::string src  = doc.GetString("Payload", pid, "Source");
            const std::string kp   = comp.empty()
                ? (rsw::IsPerUserProfileRootDir(rt) ? "HKCU Registry KeyPath (ICE38)" : "Deterministic UUIDv5 File KeyPath")
                : ("Explicit [" + comp + "] (" + doc.GetString("Component", comp, "KeyPathKind", "auto") + ")");

            HTREEITEM hP = addPayNode(
                hRoot,
                rsw::Utf8ToWide("[Payload." + pid + "]  " + dest + "  <-  " + src),
                rsw::ui::StudioTreeIcon::FileLeaf, "Payload", pid);
            addPayNode(hP, rsw::Utf8ToWide("Feature: " + feat + "  •  " + kp),
                       rsw::ui::StudioTreeIcon::ComponentGear, "Payload", pid);
            TreeView_Expand(hPayTree, hP, TVE_EXPAND);
        }

        TreeView_Expand(hPayTree, hRoot, TVE_EXPAND);
    }

    void RebuildSetupDesignTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hDesignTree);
        designNodeRefs.clear();

        auto addTv = [&](HTREEITEM hParent, const std::string& utf8Label, rsw::ui::StudioTreeIcon ico, std::string pfx, std::string id, bool bold = false) {
            const std::size_t idx = designNodeRefs.size();
            designNodeRefs.push_back({std::move(pfx), std::move(id)});
            return InsertIconTreeItem(hDesignTree, hParent, rsw::Utf8ToWide(utf8Label), ico, static_cast<LPARAM>(idx), bold);
        };

        HTREEITEM hTop = addTv(TVI_ROOT, "Setup Design (Features -> Components -> KeyPaths)", rsw::ui::StudioTreeIcon::ComputerRoot, "", "", true);
        std::map<std::string, HTREEITEM> featNodes;
        auto featIds = doc.GetNamedSectionIds("Feature");
        if (featIds.empty()) {
            featNodes["main"] = addTv(hTop, "Feature: Main (Minimal & All Setups)", rsw::ui::StudioTreeIcon::FeatureBox, "Feature", "Main");
        } else {
            for (const auto& fid : featIds) {
                const std::string par = doc.GetString("Feature", fid, "Parent");
                HTREEITEM hPar = hTop;
                if (!par.empty() && featNodes.count(rsw::AsciiLower(par))) {
                    hPar = featNodes[rsw::AsciiLower(par)];
                }
                const auto tier = QueryFeatureTierFromDoc(doc, fid);
                const std::string lbl = "Feature: " + fid + " — " + doc.GetString("Feature", fid, "Title", fid) +
                                        " (" + std::string(rsw::FeatureInclusionBadge(tier)) + ")";
                featNodes[rsw::AsciiLower(fid)] = addTv(hPar, lbl, rsw::ui::StudioTreeIcon::FeatureBox, "Feature", fid);
            }
        }

        auto getFeatNode = [&](const std::string& fid) -> HTREEITEM {
            auto it = featNodes.find(rsw::AsciiLower(fid));
            if (it != featNodes.end()) return it->second;
            HTREEITEM hNew = addTv(hTop, "Feature: " + fid, rsw::ui::StudioTreeIcon::FeatureBox, "Feature", fid);
            featNodes[rsw::AsciiLower(fid)] = hNew;
            return hNew;
        };

        std::map<std::string, HTREEITEM> compNodes;
        for (const auto& cid : doc.GetNamedSectionIds("Component")) {
            const std::string feat = doc.GetString("Component", cid, "Feature", "Main");
            const std::string rt   = doc.GetString("Component", cid, "TargetRoot", "INSTALLDIR");
            const std::string kp   = doc.GetString("Component", cid, "KeyPathKind", "auto");
            const std::string kpt  = doc.GetString("Component", cid, "KeyPathTarget");
            std::string lbl = "Component: " + cid + "  [" + rt + "]  •  KeyPath: " + kp;
            if (!kpt.empty()) lbl += " (" + kpt + ")";
            HTREEITEM hC = addTv(getFeatNode(feat), lbl, rsw::ui::StudioTreeIcon::ComponentGear, "Component", cid);
            compNodes[rsw::AsciiLower(cid)] = hC;
        }

        for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
            const std::string comp = doc.GetString("Payload", pid, "Component");
            const std::string src  = doc.GetString("Payload", pid, "Source");
            if (!comp.empty() && compNodes.count(rsw::AsciiLower(comp))) {
                addTv(compNodes[rsw::AsciiLower(comp)], "Payload." + pid + " -> " + src, rsw::ui::StudioTreeIcon::FileLeaf, "Payload", pid);
            } else {
                const std::string feat = doc.GetString("Payload", pid, "Feature", "Main");
                addTv(getFeatNode(feat), "[Auto UUIDv5 Component] Payload." + pid + " -> " + src, rsw::ui::StudioTreeIcon::FileLeaf, "Payload", pid);
            }
        }

        TreeView_Expand(hDesignTree, hTop, TVE_EXPAND);
        for (const auto& [k, hNode] : featNodes) { (void)k; TreeView_Expand(hDesignTree, hNode, TVE_EXPAND); }
        for (const auto& [k, hNode] : compNodes) { (void)k; TreeView_Expand(hDesignTree, hNode, TVE_EXPAND); }
    }

    void RefreshRegistryFeatureFilterCombo(const rsw::RswManifestDocument& doc) {
        ::SendMessageW(hRegFeatFilter, CB_RESETCONTENT, 0, 0);
        ::SendMessageW(hRegFeatFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"[All Features]"));
        int selIdx = 0;
        int idx = 1;
        for (const auto& fid : doc.GetNamedSectionIds("Feature")) {
            std::wstring w = rsw::Utf8ToWide(fid);
            ::SendMessageW(hRegFeatFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
            if (!activeRegFeatureFilter.empty() && rsw::EqualsIgnoreCase(fid, activeRegFeatureFilter)) {
                selIdx = idx;
            }
            ++idx;
        }
        if (selIdx == 0) activeRegFeatureFilter.clear();
        ::SendMessageW(hRegFeatFilter, CB_SETCURSEL, selIdx, 0);
    }

    void RebuildRegistryTrees(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hRegKeyTree);
        regKeyMetas.clear();
        regPathItemMap.clear();

        auto addKeyNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, const std::string& hive, const std::string& path, bool bold = false) {
            const std::size_t idx = regKeyMetas.size();
            regKeyMetas.push_back({hive, path});
            HTREEITEM h = InsertIconTreeItem(hRegKeyTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
            if (!hive.empty()) {
                regPathItemMap[rsw::AsciiLower(hive + "::" + path)] = h;
            }
            return h;
        };

        HTREEITEM hComputer = addKeyNode(TVI_ROOT, L"Target Computer Registry", rsw::ui::StudioTreeIcon::ComputerRoot, "", "", true);
        struct HiveDef { const char* code; const wchar_t* label; };
        const HiveDef hives[] = {
            {"HKCR", L"HKEY_CLASSES_ROOT"},
            {"HKCU", L"HKEY_CURRENT_USER"},
            {"HKLM", L"HKEY_LOCAL_MACHINE"},
            {"HKU",  L"HKEY_USERS"},
            {"HKMU", L"HKEY_USER_MACHINE_DEFAULT (HKMU)"}
        };
        std::map<std::string, HTREEITEM> hiveRoots;
        for (const auto& hv : hives) {
            hiveRoots[rsw::AsciiLower(hv.code)] = addKeyNode(hComputer, hv.label, rsw::ui::StudioTreeIcon::RegistryHive, hv.code, "");
        }

        auto ensureKeyChain = [&](const std::string& rawHive, const std::string& fullKey) {
            std::string hiveUpper = "HKLM";
            for (const auto& hv : hives) {
                if (rsw::EqualsIgnoreCase(rawHive, hv.code)) { hiveUpper = hv.code; break; }
            }
            HTREEITEM curParent = hiveRoots[rsw::AsciiLower(hiveUpper)];
            std::string accum;
            std::size_t pos = 0;
            while (pos < fullKey.size()) {
                auto slash = fullKey.find_first_of("\\/", pos);
                std::string seg = (slash == std::string::npos) ? fullKey.substr(pos) : fullKey.substr(pos, slash - pos);
                if (!seg.empty()) {
                    accum = accum.empty() ? seg : (accum + "\\" + seg);
                    const std::string mapKey = rsw::AsciiLower(hiveUpper + "::" + accum);
                    auto it = regPathItemMap.find(mapKey);
                    if (it == regPathItemMap.end()) {
                        HTREEITEM hSub = addKeyNode(curParent, rsw::Utf8ToWide(seg), rsw::ui::StudioTreeIcon::RegistryKey, hiveUpper, accum);
                        TreeView_Expand(hRegKeyTree, curParent, TVE_EXPAND);
                        curParent = hSub;
                    } else {
                        TreeView_Expand(hRegKeyTree, curParent, TVE_EXPAND);
                        curParent = it->second;
                    }
                }
                if (slash == std::string::npos) break;
                pos = slash + 1;
            }
        };

        const bool defaultMachine = !rsw::EqualsIgnoreCase(doc.GetString("Package", "", "Scope", "machine"), "user");
        const std::string defHive = defaultMachine ? "HKLM" : "HKCU";
        std::string firstPopulatedHive;
        std::string firstPopulatedKey;

        for (const auto& rid : doc.GetNamedSectionIds("Registry")) {
            const std::string feat = doc.GetString("Registry", rid, "Feature", "Main");
            if (!activeRegFeatureFilter.empty() && !rsw::EqualsIgnoreCase(feat, activeRegFeatureFilter)) continue;
            const std::string hv = doc.GetString("Registry", rid, "Root", defHive);
            const std::string ky = doc.GetString("Registry", rid, "Key");
            ensureKeyChain(hv, ky);
            if (firstPopulatedKey.empty() && !ky.empty()) {
                firstPopulatedHive = hv;
                firstPopulatedKey  = ky;
            }
        }
        for (const auto& [hv, ky] : extraSessionRegKeys) {
            ensureKeyChain(hv, ky);
        }

        TreeView_Expand(hRegKeyTree, hComputer, TVE_EXPAND);

        if (selectedRegKeyPath.empty() && !firstPopulatedKey.empty()) {
            selectedRegHive    = firstPopulatedHive;
            selectedRegKeyPath = firstPopulatedKey;
        }
        const std::string selLookup = rsw::AsciiLower(selectedRegHive + "::" + selectedRegKeyPath);
        auto selIt = regPathItemMap.find(selLookup);
        if (selIt != regPathItemMap.end()) {
            TreeView_SelectItem(hRegKeyTree, selIt->second);
        }
        RefreshRegistryValuesTree(doc);
    }

    bool SelectRegistryKeyInTree(const std::string& hive, const std::string& keyPath) {
        const std::string lookup = rsw::AsciiLower(hive + "::" + keyPath);
        auto it = regPathItemMap.find(lookup);
        if (it == regPathItemMap.end()) return false;
        selectedRegHive    = hive;
        selectedRegKeyPath = keyPath;
        TreeView_SelectItem(hRegKeyTree, it->second);
        TreeView_EnsureVisible(hRegKeyTree, it->second);
        RefreshRegistryValuesTree(ResolvedEffectiveDoc());
        return true;
    }

    void SyncRegRootComboSelection(const std::string& hive) {
        const char* codes[] = {"HKLM", "HKCU", "HKCR", "HKU", "HKMU"};
        for (int i = 0; i < 5; ++i) {
            if (rsw::EqualsIgnoreCase(hive, codes[i])) {
                ::SendMessageW(hRegRootCombo, CB_SETCURSEL, i, 0);
                return;
            }
        }
        ::SendMessageW(hRegRootCombo, CB_SETCURSEL, 0, 0);
    }

    void RefreshRegistryValuesTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hRegValTree);
        regValSectionIds.clear();

        auto addValNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, std::string rid, bool bold = false) {
            const std::size_t idx = regValSectionIds.size();
            regValSectionIds.push_back(std::move(rid));
            return InsertIconTreeItem(hRegValTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
        };

        SyncRegRootComboSelection(selectedRegHive);
        if (!selectedRegKeyPath.empty()) {
            ::SetWindowTextW(hRegKey, rsw::Utf8ToWide(selectedRegKeyPath).c_str());
        }

        const std::string hdr = selectedRegKeyPath.empty()
            ? (selectedRegHive + "  (Select a subkey folder on the left or click + Add Subkey)")
            : (selectedRegHive + "\\" + selectedRegKeyPath);
        HTREEITEM hRoot = addValNode(TVI_ROOT, rsw::Utf8ToWide(hdr), rsw::ui::StudioTreeIcon::RegistryKey, "", true);

        const bool defaultMachine = !rsw::EqualsIgnoreCase(doc.GetString("Package", "", "Scope", "machine"), "user");
        const std::string defHive = defaultMachine ? "HKLM" : "HKCU";

        int count = 0;
        for (const auto& rid : doc.GetNamedSectionIds("Registry")) {
            const std::string feat = doc.GetString("Registry", rid, "Feature", "Main");
            if (!activeRegFeatureFilter.empty() && !rsw::EqualsIgnoreCase(feat, activeRegFeatureFilter)) continue;
            const std::string hv = doc.GetString("Registry", rid, "Root", defHive);
            const std::string ky = doc.GetString("Registry", rid, "Key");
            if (!rsw::EqualsIgnoreCase(hv, selectedRegHive)) continue;
            if (!selectedRegKeyPath.empty() && !rsw::EqualsIgnoreCase(ky, selectedRegKeyPath)) continue;
            if (selectedRegKeyPath.empty() && !ky.empty()) continue;

            const std::string nm  = doc.GetString("Registry", rid, "Name", "@");
            const std::string tp  = doc.GetString("Registry", rid, "Type", "string");
            const std::string val = doc.GetString("Registry", rid, "Value");
            const bool isNum      = rsw::EqualsIgnoreCase(tp, "dword");
            const std::string dispName = (nm == "@" || nm.empty()) ? "(Default)" : nm;
            const std::string typeTag  = isNum ? "REG_DWORD" : (rsw::EqualsIgnoreCase(tp, "expand_string") ? "REG_EXPAND_SZ" : "REG_SZ");
            const std::string line     = dispName + "   (" + typeTag + ")   =   " + val + "   [Feature: " + feat + "]";

            addValNode(hRoot, rsw::Utf8ToWide(line),
                       isNum ? rsw::ui::StudioTreeIcon::RegistryValueNum : rsw::ui::StudioTreeIcon::RegistryValueSz,
                       rid);
            ++count;
        }

        if (count == 0) {
            addValNode(hRoot, L"(No registry values in this key — enter Value Name & Value Data below and click Set Registry Value)",
                       rsw::ui::StudioTreeIcon::FileLeaf, "");
        }
        TreeView_Expand(hRegValTree, hRoot, TVE_EXPAND);
    }

    void RebuildFileAssociationsTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hFaTree);
        faNodeIds.clear();

        auto addFaNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, std::string aid, bool bold = false) {
            const std::size_t idx = faNodeIds.size();
            faNodeIds.push_back(std::move(aid));
            return InsertIconTreeItem(hFaTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
        };

        const auto ids = doc.GetNamedSectionIds("FileAssociation");
        HTREEITEM hRoot = addFaNode(
            TVI_ROOT,
            L"Registered Document Extensions & Explorer Verbs (" + std::to_wstring(ids.size()) + L")",
            rsw::ui::StudioTreeIcon::ComputerRoot, "", true);

        if (ids.empty()) {
            addFaNode(hRoot, L"(No document file associations configured)", rsw::ui::StudioTreeIcon::FileLeaf, "");
        } else {
            for (const auto& aid : ids) {
                const std::string ext  = doc.GetString("FileAssociation", aid, "Extension");
                const std::string prog = doc.GetString("FileAssociation", aid, "ProgId");
                const std::string desc = doc.GetString("FileAssociation", aid, "Description");
                const std::string exe  = doc.GetString("FileAssociation", aid, "TargetFile");
                const std::string vrbs = doc.GetString("FileAssociation", aid, "Verbs", "open:&Open:\"%1\"");
                const bool adv         = doc.GetBool("FileAssociation", aid, "Advertise", false);

                HTREEITEM hExt = addFaNode(
                    hRoot,
                    rsw::Utf8ToWide(ext + " — " + desc + "  (" + prog + ")"),
                    rsw::ui::StudioTreeIcon::FileExtDoc, aid);
                addFaNode(hExt,
                          rsw::Utf8ToWide("Handler: [INSTALLDIR]" + exe + "  •  " + (adv ? "Advertised MSI Tables" : "Clean HKMU\\Software\\Classes")),
                          rsw::ui::StudioTreeIcon::ShortcutLink, aid);

                HTREEITEM hVerbs = addFaNode(hExt, L"Shell Context-Menu Verbs", rsw::ui::StudioTreeIcon::CategoryFolder, aid);
                std::size_t pos = 0;
                while (pos <= vrbs.size()) {
                    auto bar = vrbs.find('|', pos);
                    std::string seg = rsw::TrimAscii(bar == std::string::npos ? vrbs.substr(pos) : vrbs.substr(pos, bar - pos));
                    if (!seg.empty()) {
                        addFaNode(hVerbs, rsw::Utf8ToWide(seg), rsw::ui::StudioTreeIcon::VerbBolt, aid);
                    }
                    if (bar == std::string::npos) break;
                    pos = bar + 1;
                }
                TreeView_Expand(hFaTree, hVerbs, TVE_EXPAND);
                TreeView_Expand(hFaTree, hExt, TVE_EXPAND);
            }
        }
        TreeView_Expand(hFaTree, hRoot, TVE_EXPAND);
    }

    void RebuildShortcutsTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hScTree);
        scNodeRefs.clear();

        auto addScNode = [&](HTREEITEM hPar, const std::wstring& txt, rsw::ui::StudioTreeIcon ico, std::string locOrPfx, std::string id, bool bold = false) {
            const std::size_t idx = scNodeRefs.size();
            scNodeRefs.push_back({std::move(locOrPfx), std::move(id)});
            return InsertIconTreeItem(hScTree, hPar, txt, ico, static_cast<LPARAM>(idx), bold);
        };

        HTREEITEM hRoot    = addScNode(TVI_ROOT, L"Application Shortcuts", rsw::ui::StudioTreeIcon::ComputerRoot, "", "", true);
        HTREEITEM hStart   = addScNode(hRoot, L"Start Menu ([ApplicationProgramsFolder])", rsw::ui::StudioTreeIcon::SubFolder, "Location", "StartMenu", true);
        HTREEITEM hDesktop = addScNode(hRoot, L"Desktop ([DesktopFolder])",                rsw::ui::StudioTreeIcon::SubFolder, "Location", "Desktop", true);

        for (const auto& sid : doc.GetNamedSectionIds("Shortcut")) {
            const std::string loc  = doc.GetString("Shortcut", sid, "Location", "StartMenu");
            const std::string nm   = doc.GetString("Shortcut", sid, "Name", sid);
            const std::string exe  = doc.GetString("Shortcut", sid, "TargetFile");
            const std::string feat = doc.GetString("Shortcut", sid, "Feature", "Main");
            const bool isDesk      = rsw::EqualsIgnoreCase(loc, "Desktop");
            const std::string lbl  = nm + "  ->  [INSTALLDIR]" + exe + "   [Shortcut." + sid + " • Feature: " + feat + "]";
            addScNode(isDesk ? hDesktop : hStart, rsw::Utf8ToWide(lbl), rsw::ui::StudioTreeIcon::ShortcutLink, "Shortcut", sid);
        }

        TreeView_Expand(hScTree, hRoot, TVE_EXPAND);
        TreeView_Expand(hScTree, hStart, TVE_EXPAND);
        TreeView_Expand(hScTree, hDesktop, TVE_EXPAND);
    }

    void SyncAllViewsFromAst() {
        const auto effectiveDoc = ResolvedEffectiveDoc();

        ::SetWindowTextW(hPkgName, rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Name")).c_str());
        ::SetWindowTextW(hPkgTag,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Tagline")).c_str());
        ::SetWindowTextW(hPkgWeb,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "WebsiteLabel")).c_str());
        ::SetWindowTextW(hPkgVer,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Version")).c_str());
        ::SetWindowTextW(hPkgPub,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Manufacturer")).c_str());
        ::SetWindowTextW(hPkgGuid, rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "UpgradeCode")).c_str());
        ::SetWindowTextW(hPkgSub,  rsw::Utf8ToWide(effectiveDoc.GetString("Install", "", "DefaultSubdir")).c_str());
        ::SetWindowTextW(hPkgExe,  rsw::Utf8ToWide(effectiveDoc.GetString("Install", "", "MainExecutable")).c_str());
        const bool isUser = rsw::EqualsIgnoreCase(effectiveDoc.GetString("Package", "", "Scope", "machine"), "user");
        ::SendMessageW(hPkgScope, CB_SETCURSEL, isUser ? 1 : 0, 0);
        const bool isUniv = effectiveDoc.GetBool("Package", "", "BuildUniversal", false);
        ::SendMessageW(hPkgUnivChk, BM_SETCHECK, isUniv ? BST_CHECKED : BST_UNCHECKED, 0);
        ::SetWindowTextW(hPkgUnivUrl, rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "UniversalDownloadBaseUrl")).c_str());
        const auto execLvl = rsw::NormalizeRequestedExecutionLevel(
            effectiveDoc.GetString("Package", "", "RequestedExecutionLevel", "asInvoker"));
        const int execIdx = (execLvl == "requireAdministrator") ? 2 : ((execLvl == "highestAvailable") ? 1 : 0);
        ::SendMessageW(hPkgExecLvl, CB_SETCURSEL, execIdx, 0);

        RebuildPrerequisitesTree(effectiveDoc);
        RebuildFeaturesTree(effectiveDoc);
        RebuildDestinationTreeAndPayloadTree(effectiveDoc);
        RebuildSetupDesignTree(effectiveDoc);
        RefreshRegistryFeatureFilterCombo(effectiveDoc);
        RebuildRegistryTrees(effectiveDoc);
        RebuildFileAssociationsTree(effectiveDoc);
        RebuildShortcutsTree(effectiveDoc);

        const std::string mainExe = effectiveDoc.GetString("Install", "", "MainExecutable");
        if (!mainExe.empty()) {
            ::SetWindowTextW(hFaTarget, rsw::Utf8ToWide(mainExe).c_str());
        }

        // Populate View 8: C++ Wizard Designer (`v0.5.0` 10 Wizard Styles + Setup Types!)
        const auto wk = rsw::ParseWizardStyleKind(effectiveDoc.GetString("UI", "", "WizardLayout", "standard"));
        ::SendMessageW(hUiLayout, CB_SETCURSEL, static_cast<WPARAM>(wk), 0);
        const bool win95Types = rsw::EqualsIgnoreCase(effectiveDoc.GetString("UI", "", "SetupTypePreset", "express_custom"), "win95_classic");
        ::SendMessageW(hUiSetupTypes, CB_SETCURSEL, win95Types ? 1 : 0, 0);
        if (wk == rsw::WizardStyleKind::Autorun16x9) {
            ::SendMessageW(hUiPreviewStep, CB_SETCURSEL, 4, 0);
        } else if (::SendMessageW(hUiPreviewStep, CB_GETCURSEL, 0, 0) == 4) {
            ::SendMessageW(hUiPreviewStep, CB_SETCURSEL, 1, 0);
        }
        ::SetWindowTextW(hUiPrim,    rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "AccentPrimaryHex", "#2563EB")).c_str());
        ::SetWindowTextW(hUiSec,     rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24")).c_str());
        ::SetWindowTextW(hUiInstBtn, rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "InstallButtonText", "&Install")).c_str());
        ::SetWindowTextW(hUiIcon,    rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "IconFile")).c_str());
        ::SetWindowTextW(hUiArt,     rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "ArtworkPng")).c_str());
        RefreshPreviewAssetsFromDoc(effectiveDoc);

        // Populate View 9: MSI.DLL Dialog Designer
        const bool msiMin = rsw::EqualsIgnoreCase(effectiveDoc.GetString("UI", "", "MsiDialogPreset", "express_custom"), "minimal");
        ::SendMessageW(hMsiPreset, CB_SETCURSEL, msiMin ? 1 : 0, 0);

        wchar_t prevMsiDlg[128]{};
        ::GetWindowTextW(hMsiDlgSelect, prevMsiDlg, 128);
        ::SendMessageW(hMsiDlgSelect, CB_RESETCONTENT, 0, 0);
        const wchar_t* stdDlgs[] = {
            L"WelcomeDlg", L"LicenseAgreementDlg", L"SetupTypeDlg",
            L"CustomizeDlg", L"InstallDirDlg", L"ShortcutsDlg",
            L"RepairModeDlg", L"VerifyReadyDlg", L"ExitDialog"
        };
        int selDlgIdx = 2;
        int curIdx = 0;
        for (const wchar_t* sd : stdDlgs) {
            ::SendMessageW(hMsiDlgSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(sd));
            if (std::wcscmp(sd, prevMsiDlg) == 0) selDlgIdx = curIdx;
            ++curIdx;
        }
        for (const auto& cd : effectiveDoc.GetNamedSectionIds("MsiDialog")) {
            std::wstring wcd = rsw::Utf8ToWide(cd);
            ::SendMessageW(hMsiDlgSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wcd.c_str()));
            if (wcd == prevMsiDlg) selDlgIdx = curIdx;
            ++curIdx;
        }
        ::SendMessageW(hMsiDlgSelect, CB_SETCURSEL, selDlgIdx, 0);

        // Populate View 10: Signing
        ::SendMessageW(hSigEnable, BM_SETCHECK, effectiveDoc.GetBool("Signing", "", "Enabled", false) ? BST_CHECKED : BST_UNCHECKED, 0);
        ::SetWindowTextW(hSigThumb, rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "Thumbprint")).c_str());
        ::SetWindowTextW(hSigTs,    rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "TimestampUrl", "http://timestamp.acs.microsoft.com")).c_str());
        ::SetWindowTextW(hSigDlib,  rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "DlibPath")).c_str());
        ::SetWindowTextW(hSigMeta,  rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "MetadataJson")).c_str());
    }

    void RefreshPreviewAssetsFromDoc(const rsw::RswManifestDocument& doc) {
        ReleasePreviewAssets();
        const std::filesystem::path baseDir = currentFilePath.empty()
            ? std::filesystem::current_path()
            : currentFilePath.parent_path();
        const std::string arch = SelectedArch();
        auto resolveP = [&](std::string_view raw) -> std::filesystem::path {
            if (raw.empty()) return {};
            const std::string exp = rsw::ExpandArchToken(raw, arch);
            std::filesystem::path p(rsw::Utf8ToWide(exp));
            return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
        };

        std::error_code ec;
        const auto icoPath = resolveP(doc.GetString("UI", "", "IconFile"));
        if (!icoPath.empty() && std::filesystem::exists(icoPath, ec)) {
            hPreviewIcon = static_cast<HICON>(::LoadImageW(
                nullptr, icoPath.wstring().c_str(), IMAGE_ICON, 64, 64, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
        }
        const auto artPath = resolveP(doc.GetString("UI", "", "ArtworkPng"));
        if (!artPath.empty() && std::filesystem::exists(artPath, ec)) {
            std::vector<std::uint8_t> bytes;
            std::string ignoreErr;
            if (rsw::ReadBinaryFileBytes(artPath, bytes, ignoreErr) && !bytes.empty()) {
                hPreviewArt = rsw::ui::DecodeImageBytesToHBitmap(bytes.data(), bytes.size(), previewArtW, previewArtH);
            }
        }
    }

    void LoadProjectText(const std::string& utf8, const std::filesystem::path& path, bool keepSuite = false) {
        if (!keepSuite) {
            hasActiveSuite = false;
            activeSuite = {};
            ::SendMessageW(hSuiteCombo, CB_RESETCONTENT, 0, 0);
            ::SendMessageW(hSuiteCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"[Standalone .rsip Mode]"));
            ::SendMessageW(hSuiteCombo, CB_SETCURSEL, 0, 0);
        }
        currentFilePath = path;
        selectedDestRoot.clear();
        selectedDestSubdir.clear();
        selectedFtId.clear();
        selectedRegHive = "HKLM";
        selectedRegKeyPath.clear();
        selectedRegSectionId.clear();
        extraSessionRegKeys.clear();
        activeRegFeatureFilter.clear();

        astDoc = rsw::ParseManifestText(utf8, path);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        UpdateWindowCaptionWithEdition();
    }

    void UpdateWindowCaptionWithEdition() {
        std::wstring title = L"RaggieSoft Installer Studio v" + std::wstring(rsw::kVersionStringWide) +
                             L" [" + std::wstring(rsw::EditionDisplayName(activeEdition)) + L"] — ";
        if (hasActiveSuite) {
            title += rsw::Utf8ToWide(activeSuite.name) + L" / ";
        }
        title += currentFilePath.empty() ? L"[Untitled.rsip]" : currentFilePath.filename().wstring();
        ::SetWindowTextW(hWnd, title.c_str());
    }

    /// Launches the zero-dependency native C++20 `RaggieSoftHelp.exe` viewer mapped to the active IDE view (`F1`)!
    void LaunchContextHelp() {
        const wchar_t* topicId = L"welcome-overview";
        switch (activeView) {
            case VIEW_PACKAGE:      topicId = L"welcome-overview";          break;
            case VIEW_PREREQS:      topicId = L"runtimes-and-merge-modules";break;
            case VIEW_FEATURES:     topicId = L"selection-tree-and-dialogs";break;
            case VIEW_DEST_FOLDERS: topicId = L"components-and-keypaths";   break;
            case VIEW_SETUP_DESIGN: topicId = L"components-and-keypaths";   break;
            case VIEW_REGISTRY:     topicId = L"ide-and-aerowizards";       break;
            case VIEW_FILE_ASSOC:   topicId = L"ide-and-aerowizards";       break;
            case VIEW_SHORTCUTS:    topicId = L"ide-and-aerowizards";       break;
            case VIEW_CPP_WIZARD:   topicId = L"selection-tree-and-dialogs";break;
            case VIEW_MSI_DIALOGS:  topicId = L"selection-tree-and-dialogs";break;
            case VIEW_SIGN_REL:     topicId = L"msiprism-and-patches";      break;
            case VIEW_SOURCE:       topicId = L"cli-and-ai-agents";         break;
            default: break;
        }
        const auto helpExe = ideExeDir / L"RaggieSoftHelp.exe";
        std::error_code ec;
        if (!std::filesystem::exists(helpExe, ec)) {
            AppendConsole(L"[WARN] RaggieSoftHelp.exe was not found next to InstallerStudio.exe.\r\n");
            return;
        }
        const std::wstring params = L"--module raggiesoft.installer-studio --topic " + std::wstring(topicId);
        ::ShellExecuteW(hWnd, L"open", helpExe.wstring().c_str(), params.c_str(), ideExeDir.wstring().c_str(), SW_SHOWNORMAL);
    }

    void OpenProjectOrSuiteFile(const std::filesystem::path& path) {
        rsw::RswManifestDocument d;
        std::string err;
        if (!rsw::LoadManifestFile(path, d, err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return;
        }
        if (rsw::IsSuiteFilePath(path)) {
            rsw::RswSuiteWorkspace sw;
            if (!rsw::ParseSuiteWorkspace(d, sw, err)) {
                AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
                return;
            }
            activeSuite = std::move(sw);
            hasActiveSuite = true;
            ::SendMessageW(hSuiteCombo, CB_RESETCONTENT, 0, 0);
            for (const auto& pe : activeSuite.projects) {
                std::wstring item = rsw::Utf8ToWide("[" + pe.category + "] " + pe.id);
                ::SendMessageW(hSuiteCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
            }
            ::SendMessageW(hSuiteCombo, CB_SETCURSEL, 0, 0);
            AppendConsole(L"[SUITE] Opened '" + rsw::Utf8ToWide(activeSuite.name) + L"' (" +
                          std::to_wstring(activeSuite.projects.size()) + L" member .rsip projects).\r\n");
            if (!activeSuite.projects.empty()) {
                rsw::RswManifestDocument firstProj;
                if (rsw::LoadManifestFile(activeSuite.projects[0].resolvedPath, firstProj, err)) {
                    LoadProjectText(firstProj.Serialize(), activeSuite.projects[0].resolvedPath, true);
                }
            }
            return;
        }
        LoadProjectText(d.Serialize(), path, false);
    }

    static std::string ReadCtlUtf8(HWND h) {
        wchar_t buf[1024]{};
        ::GetWindowTextW(h, buf, static_cast<int>(std::size(buf)));
        return rsw::TrimAscii(rsw::WideToUtf8(buf));
    }

    void AppendConsole(const std::wstring& msg) {
        const int len = ::GetWindowTextLengthW(hConsoleEdit);
        ::SendMessageW(hConsoleEdit, EM_SETSEL, len, len);
        ::SendMessageW(hConsoleEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(msg.c_str()));
    }

    void ApplyPackageViewToAst() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        astDoc.SetString("Package", "", "Name",           ReadCtlUtf8(hPkgName));
        astDoc.SetString("Package", "", "Tagline",        ReadCtlUtf8(hPkgTag));
        const std::string web = ReadCtlUtf8(hPkgWeb);
        if (!web.empty()) astDoc.SetString("Package", "", "WebsiteLabel", web);
        astDoc.SetString("Package", "", "Version",        ReadCtlUtf8(hPkgVer));
        astDoc.SetString("Package", "", "Manufacturer",   ReadCtlUtf8(hPkgPub));
        astDoc.SetString("Package", "", "UpgradeCode",    ReadCtlUtf8(hPkgGuid));
        astDoc.SetString("Package", "", "Scope",          (::SendMessageW(hPkgScope, CB_GETCURSEL, 0, 0) == 1) ? "user" : "machine");
        const bool univOn = (::SendMessageW(hPkgUnivChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        astDoc.SetString("Package", "", "BuildUniversal", univOn ? "true" : "false");
        const std::string univUrl = ReadCtlUtf8(hPkgUnivUrl);
        if (!univUrl.empty() || univOn) {
            astDoc.SetString("Package", "", "UniversalDownloadBaseUrl", univUrl);
        }
        const LRESULT execSel = ::SendMessageW(hPkgExecLvl, CB_GETCURSEL, 0, 0);
        const char* execTok   = (execSel == 2) ? "requireAdministrator" : ((execSel == 1) ? "highestAvailable" : "asInvoker");
        if (execSel > 0 || astDoc.HasKey("Package", "", "RequestedExecutionLevel")) {
            astDoc.SetString("Package", "", "RequestedExecutionLevel", execTok);
        }
        astDoc.SetString("Install", "", "DefaultSubdir",  ReadCtlUtf8(hPkgSub));
        astDoc.SetString("Install", "", "MainExecutable", ReadCtlUtf8(hPkgExe));
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Updated [Package] & [Install] fields while preserving 100% of comments & @includes.\r\n");
    }

    void ApplyUiViewToAst() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const int layIdx = std::clamp(static_cast<int>(::SendMessageW(hUiLayout, CB_GETCURSEL, 0, 0)), 0, rsw::kWizardStyleCount - 1);
        const auto wk    = static_cast<rsw::WizardStyleKind>(layIdx);
        astDoc.SetString("UI", "", "WizardLayout",      rsw::WizardStyleToken(wk));
        const bool win95Types = (::SendMessageW(hUiSetupTypes, CB_GETCURSEL, 0, 0) == 1);
        astDoc.SetString("UI", "", "SetupTypePreset",   win95Types ? "win95_classic" : "express_custom");
        astDoc.SetString("UI", "", "AccentPrimaryHex",  ReadCtlUtf8(hUiPrim));
        astDoc.SetString("UI", "", "AccentSecondaryHex",ReadCtlUtf8(hUiSec));
        astDoc.SetString("UI", "", "InstallButtonText", ReadCtlUtf8(hUiInstBtn));
        const std::string ico = ReadCtlUtf8(hUiIcon);
        if (!ico.empty()) astDoc.SetString("UI", "", "IconFile", ico);
        const std::string art = ReadCtlUtf8(hUiArt);
        if (!art.empty()) astDoc.SetString("UI", "", "ArtworkPng", art);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        ::InvalidateRect(hWnd, nullptr, TRUE);
        AppendConsole(L"[AST] Updated [UI] C++ SetupWizard branding & refreshed live preview.\r\n");
    }

    void ApplySigningViewToAst() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const bool en = (::SendMessageW(hSigEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
        astDoc.SetString("Signing", "", "Enabled",      en ? "true" : "false");
        astDoc.SetString("Signing", "", "Thumbprint",   ReadCtlUtf8(hSigThumb));
        astDoc.SetString("Signing", "", "TimestampUrl", ReadCtlUtf8(hSigTs));
        astDoc.SetString("Signing", "", "DlibPath",     ReadCtlUtf8(hSigDlib));
        astDoc.SetString("Signing", "", "MetadataJson", ReadCtlUtf8(hSigMeta));
        SyncSourceEditorFromAst();
        AppendConsole(L"[AST] Updated [Signing] Authenticode settings.\r\n");
    }

    bool PickPathDialog(bool folderMode, const wchar_t* filter, std::wstring& outPath) {
        return rsw::ide_wizards::BrowsePathModal(hWnd, folderMode, filter, outPath);
    }

    void AddPayloadToSelectedDestFolder(bool pickDirectory) {
        std::wstring chosen;
        if (!PickPathDialog(pickDirectory, L"All Files (*.*)\0*.*\0", chosen)) return;
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const int idx = static_cast<int>(astDoc.GetNamedSectionIds("Payload").size() + 1);
        const std::string id = (pickDirectory ? "Dir" : "File") + std::to_string(idx);
        const std::string rt = selectedDestRoot.empty() ? "INSTALLDIR" : selectedDestRoot;
        astDoc.SetString("Payload", id, "Source",     rsw::WideToUtf8(chosen));
        astDoc.SetString("Payload", id, "TargetRoot", rt);
        if (!selectedDestSubdir.empty()) {
            astDoc.SetString("Payload", id, "TargetSubdir", selectedDestSubdir);
        }
        astDoc.SetString("Payload", id, "Feature", "Main");
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Added [Payload." + rsw::Utf8ToWide(id) + L"] into [" + rsw::Utf8ToWide(rt) + L"].\r\n");
    }

    void CreateExplicitDestinationSubfolder() {
        const std::string sub = ReadCtlUtf8(hDestSubEdit);
        if (sub.empty()) return;
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const std::string rt = selectedDestRoot.empty() ? "INSTALLDIR" : selectedDestRoot;
        const std::string fullSub = selectedDestSubdir.empty() ? sub : (selectedDestSubdir + "\\" + sub);
        const std::string fid = "Dir" + std::to_string(astDoc.GetNamedSectionIds("Folder").size() + 1);
        astDoc.SetString("Folder", fid, "TargetRoot",        rt);
        astDoc.SetString("Folder", fid, "TargetSubdir",      fullSub);
        astDoc.SetString("Folder", fid, "Feature",           "Main");
        astDoc.SetString("Folder", fid, "RemoveOnUninstall", "true");
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Added [Folder." + rsw::Utf8ToWide(fid) + L"] (CreateFolder + ICE64 RemoveFolder).\r\n");
    }

    void RemoveSelectedDestItem() {
        if (selectedPayRef.prefix.empty() || selectedPayRef.id.empty()) return;
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        RemoveAstSection(astDoc, selectedPayRef.prefix, selectedPayRef.id);
        const std::string msg = selectedPayRef.prefix + "." + selectedPayRef.id;
        selectedPayRef = {};
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Removed [" + rsw::Utf8ToWide(msg) + L"].\r\n");
    }

    bool SaveCurrentProject() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        astDoc.useCrlf = true;
        if (currentFilePath.empty()) {
            wchar_t buf[MAX_PATH * 2] = L"project.rsip";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hWnd;
            ofn.lpstrFilter = L"RaggieSoft Installer Project (*.rsip)\0*.rsip\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile   = buf;
            ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
            ofn.lpstrDefExt = L"rsip";
            ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (!::GetSaveFileNameW(&ofn)) return false;
            currentFilePath   = buf;
            astDoc.sourcePath = currentFilePath;
        }
        std::string err;
        if (!rsw::SaveManifestFile(currentFilePath, astDoc, err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return false;
        }
        AppendConsole(L"[OK] Saved lossless .rsip manifest: " + currentFilePath.wstring() + L"\r\n");
        return true;
    }

    std::string SelectedArch() const {
        return (::SendMessageW(hArchCombo, CB_GETCURSEL, 0, 0) == 1) ? "x64" : "arm64";
    }

    void RunValidation() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        rsw::RswManifestDocument resolvedDoc;
        std::string incErr;
        ::SetWindowTextW(hConsoleEdit, L"");
        if (!rsw::ResolveManifestIncludesAndVars(
                astDoc,
                hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
                resolvedDoc,
                incErr)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(incErr) + L"\r\n");
            return;
        }
        const auto rep = rsw::ValidateManifestDocument(resolvedDoc, SelectedArch());
        if (rep.IsOk()) {
            AppendConsole(L"[PASS] 0 errors — Manifest & Microsoft Component Rules (Single-Directory, ICE18, ICE38, ICE80) verified for '" +
                          rsw::Utf8ToWide(rep.resolvedArchitecture) + L"'.\r\n");
        } else {
            for (const auto& d : rep.diagnostics) {
                AppendConsole(L"[" + rsw::Utf8ToWide(d.code) + L" Line " + std::to_wstring(d.lineNumber) + L"] " +
                              rsw::Utf8ToWide(d.message) + L"\r\n");
            }
        }
    }

    void InvokeInstBuild(bool mergeModule) {
        if (!SaveCurrentProject()) return;
        const auto cliExe = ideExeDir / L"instbuild.exe";
        if (!std::filesystem::exists(cliExe)) {
            AppendConsole(L"[ERROR] instbuild.exe not found next to InstallerStudio.exe.\r\n");
            return;
        }
        const std::wstring arch = rsw::Utf8ToWide(SelectedArch());
        const auto effDoc = ResolvedEffectiveDoc();
        const bool buildUniv = !mergeModule && effDoc.GetBool("Package", "", "BuildUniversal", false);
        std::wstring cmd = L"\"" + cliExe.wstring() + L"\" ";
        if (mergeModule) {
            const auto msmOut = currentFilePath.parent_path() / L"out" /
                                (currentFilePath.stem().wstring() + L"-" + arch + L".msm");
            cmd += L"--emit-msm \"" + currentFilePath.wstring() + L"\" \"" + msmOut.wstring() + L"\" --arch=" + arch;
        } else if (buildUniv) {
            cmd += L"\"" + currentFilePath.wstring() + L"\" --universal";
        } else {
            cmd += L"\"" + currentFilePath.wstring() + L"\" --arch=" + arch;
        }
        ::SetWindowTextW(hConsoleEdit, (L"> " + cmd + L"\r\n").c_str());
        ::UpdateWindow(hConsoleEdit);
        DWORD code = 1;
        AppendConsole(RunChildCapture(cmd, code));
    }

    void ExportVpatHtml() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        rsw::RswManifestDocument resolvedDoc;
        std::string err;
        if (!rsw::ResolveManifestIncludesAndVars(
                astDoc,
                hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
                resolvedDoc,
                err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return;
        }
        const auto outDir = currentFilePath.empty() ? (ideExeDir / L"vpat") : (currentFilePath.parent_path() / L"out");
        std::filesystem::path htmlPath;
        if (!rsw::EmitProjectInstallerVpatHtml(resolvedDoc, outDir, htmlPath, err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return;
        }
        AppendConsole(L"[OK] Exported WCAG 2.2 AA & Section 508 VPAT 2.5 Report: " + htmlPath.wstring() + L"\r\n");
    }

    void InsertIncludePresetDialog() {
        const std::filesystem::path tplRoot = ideExeDir.parent_path().parent_path() / L"templates" / L"dialogs";
        wchar_t buf[MAX_PATH * 2]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize     = sizeof(ofn);
        ofn.hwndOwner       = hWnd;
        ofn.lpstrInitialDir = tplRoot.wstring().c_str();
        ofn.lpstrFilter     = L"RaggieSoft Include Presets (*.rsii)\0*.rsii\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile       = buf;
        ofn.nMaxFile        = static_cast<DWORD>(std::size(buf));
        ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!::GetOpenFileNameW(&ofn)) return;

        std::string raw = ReadSourceEditorUtf8();
        const std::string incLine = "@include \"" + rsw::WideToUtf8(buf) + "\"\r\n\r\n";
        raw = incLine + raw;
        astDoc = rsw::ParseManifestText(raw, currentFilePath);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        ::InvalidateRect(hWnd, nullptr, FALSE);
        AppendConsole(L"[AST] Prepended @include \"" + std::wstring(buf) + L"\".\r\n");
    }

    // ----------------------------------------------------------------------------
    // LIVE CANVASES: VIEW 8 (`SetupWizard.exe` C++20) & VIEW 9 (`MSI.DLL` 370x270)
    // ----------------------------------------------------------------------------
    void PaintLiveWizardPreview(HDC hdcOut, const RECT& destRc, UINT dpi) {
        const int pvW = destRc.right - destRc.left;
        const int pvH = destRc.bottom - destRc.top;
        if (pvW < 180 || pvH < 180) return;

        const UINT pvDpi = static_cast<UINT>(std::clamp(pvW * 96 / 580, 68, static_cast<int>(dpi)));
        auto PS = [&](int px) { return rsw::ui::ScalePx(px, pvDpi, 100); };

        const COLORREF prim = rsw::ui::ParseHexColor(ReadCtlUtf8(hUiPrim), RGB(37, 99, 235));
        const COLORREF sec  = rsw::ui::ParseHexColor(ReadCtlUtf8(hUiSec),  RGB(251, 191, 36));
        rsw::ui::RswTheme pvTheme{};
        pvTheme.Refresh(prim, sec);
        pvTheme.textScalePercent = 100;

        HDC pvDc       = ::CreateCompatibleDC(hdcOut);
        HBITMAP pvBmp  = ::CreateCompatibleBitmap(hdcOut, pvW, pvH);
        HGDIOBJ oldBmp = ::SelectObject(pvDc, pvBmp);

        RECT localRc{0, 0, pvW, pvH};
        ::FillRect(pvDc, &localRc, pvTheme.hbrWindow);
        ::SetBkMode(pvDc, TRANSPARENT);

        Gdiplus::Graphics pvG(pvDc);
        pvG.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        pvG.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        pvG.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        const std::wstring prodName = rsw::Utf8ToWide(ReadCtlUtf8(hPkgName).empty() ? "My Native Application" : ReadCtlUtf8(hPkgName));
        const std::wstring pubName  = rsw::Utf8ToWide(ReadCtlUtf8(hPkgPub).empty()  ? "RaggieSoft" : ReadCtlUtf8(hPkgPub));
        const std::wstring tagline  = rsw::Utf8ToWide(ReadCtlUtf8(hPkgTag).empty()  ? "Fast, Accessible Windows Software" : ReadCtlUtf8(hPkgTag));
        const std::wstring webLbl   = rsw::Utf8ToWide(ReadCtlUtf8(hPkgWeb));
        const std::wstring ctaLbl   = rsw::Utf8ToWide(ReadCtlUtf8(hUiInstBtn).empty() ? "&Install" : ReadCtlUtf8(hUiInstBtn));

        const int layIdx      = std::clamp(static_cast<int>(::SendMessageW(hUiLayout, CB_GETCURSEL, 0, 0)), 0, rsw::kWizardStyleCount - 1);
        const auto wk         = static_cast<rsw::WizardStyleKind>(layIdx);
        const bool win95Types = (::SendMessageW(hUiSetupTypes, CB_GETCURSEL, 0, 0) == 1);
        const bool aeroMode   = (wk == rsw::WizardStyleKind::AeroWizard);
        const bool micaMode   = (wk == rsw::WizardStyleKind::FluentMica);
        const bool retroFull  = rsw::UsesFullScreenRetroBackdrop(wk);
        const bool classicHdr = rsw::UsesClassic3dDialogSheet(wk) || (wk == rsw::WizardStyleKind::Wizard97);

        rsw::ApplyWizardStyleThemeOverrides(wk, pvTheme);
        ::FillRect(pvDc, &localRc, pvTheme.hbrWindow);

        const LRESULT stepSel = ::SendMessageW(hUiPreviewStep, CB_GETCURSEL, 0, 0);
        const int stepIdx     = (stepSel == CB_ERR) ? 1 : static_cast<int>(stepSel);

        // If `FullScreenBlue` or `FullScreenTeal` (Microsoft ACME `#008080` Teal!), paint the full-screen
        // retro backdrop across the outer canvas AND render the floating `#C0C0C0` 3D dialog into its own
        // `(0, 0)` offscreen bitmap so GDI+ and GDI `DrawTextW` share 100% identical origin coordinates!
        RECT dlgRc = localRc;
        if (retroFull) {
            rsw::BillboardSlideSpec pvBb{
                L"Dual-Layer Authenticode + Universal EXE",
                L"Zero-dependency Win32 C++20 packaging for x64 & ARM64.",
                rsw::ui::GlassGlyph::ShieldCheck
            };
            rsw::ui::DrawRetroFullScreenBackdrop(
                pvDc, pvG, localRc, pvDpi, pvTheme,
                wk == rsw::WizardStyleKind::FullScreenTeal,
                prodName, (stepIdx == 3) ? &pvBb : nullptr,
                true, 680, L"Copying program files...");
            dlgRc = RECT{PS(18), PS(46), pvW - PS(52), pvH - PS(46)};
            RECT shRc{dlgRc.left + PS(5), dlgRc.top + PS(5), dlgRc.right + PS(5), dlgRc.bottom + PS(5)};
            HBRUSH hSh = ::CreateSolidBrush(RGB(0, 0, 0));
            ::FillRect(pvDc, &shRc, hSh);
            ::DeleteObject(hSh);
        } else if (wk == rsw::WizardStyleKind::OneClick || wk == rsw::WizardStyleKind::PassiveToast) {
            HBRUSH hDesk = ::CreateSolidBrush(theme.isDarkMode ? RGB(8, 13, 26) : RGB(219, 228, 240));
            ::FillRect(pvDc, &localRc, hDesk);
            ::DeleteObject(hDesk);
            const int padY = (wk == rsw::WizardStyleKind::PassiveToast) ? PS(68) : PS(44);
            dlgRc = RECT{PS(24), padY, pvW - PS(24), pvH - padY};
        }

        const int dL    = dlgRc.left;
        const int dT    = dlgRc.top;
        const int dW    = std::max<int>(64, dlgRc.right - dlgRc.left);
        const int dH    = std::max<int>(64, dlgRc.bottom - dlgRc.top);
        const int footH = PS(42);
        const int relFootY = dH - footH;

        HDC dlgDc         = ::CreateCompatibleDC(hdcOut);
        HBITMAP dlgBmp    = ::CreateCompatibleBitmap(hdcOut, dW, dH);
        HGDIOBJ oldDlgBmp = ::SelectObject(dlgDc, dlgBmp);
        RECT dlgZeroRc{0, 0, dW, dH};
        ::FillRect(dlgDc, &dlgZeroRc, pvTheme.hbrWindow);
        ::SetBkMode(dlgDc, TRANSPARENT);

        {
            Gdiplus::Graphics dlgG(dlgDc);
            dlgG.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            dlgG.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            dlgG.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            if (rsw::UsesClassic3dDialogSheet(wk)) {
                rsw::ui::DrawClassicEtchedHorizontalRule(dlgDc, PS(10), dW - PS(10), relFootY, pvTheme);
            } else {
                rsw::ui::DrawGlassFooterBar(dlgDc, dlgG, dW, dH, footH, dW - PS(198), pvDpi, pvTheme, pubName, webLbl);
            }

            if (wk == rsw::WizardStyleKind::Autorun16x9 || stepIdx == 4) {
                const int stageH = std::max(PS(120), relFootY - PS(92));
                RECT stageRc{0, 0, dW, stageH};
                rsw::ui::FillHorizontalGradient(dlgDc, stageRc, pvTheme.bannerNavyLeft, pvTheme.bannerNavyRight);
                if (hPreviewArt) {
                    rsw::ui::DrawAlphaBitmapFit(dlgDc, hPreviewArt, previewArtW, previewArtH, stageRc);
                } else {
                    rsw::ui::DrawBrandSquircleBadge(dlgG, static_cast<float>(PS(20)), static_cast<float>(PS(22)), static_cast<float>(PS(42)), pvTheme, pubName);
                    HFONT fHead = rsw::ui::CreateScaledFont(L"Segoe UI", 14, FW_BOLD, pvDpi, 100);
                    HGDIOBJ oF  = ::SelectObject(dlgDc, fHead);
                    ::SetTextColor(dlgDc, pvTheme.bannerTitleText);
                    RECT tRc{PS(20), PS(72), dW - PS(20), PS(116)};
                    ::DrawTextW(dlgDc, prodName.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    ::SelectObject(dlgDc, oF);
                    ::DeleteObject(fHead);
                }
                rsw::ui::DrawMultiStopAccentBar(dlgG, 0.0f, static_cast<float>(stageH), static_cast<float>(dW), static_cast<float>(PS(3)), pvTheme);

                const int cw = (dW - PS(34)) / 2;
                const int ch = PS(34);
                const int ry1 = stageH + PS(8);
                const int ry2 = ry1 + ch + PS(5);
                RECT c0{PS(12),      ry1, PS(12) + cw,     ry1 + ch};
                RECT c1{PS(22) + cw, ry1, PS(22) + cw * 2, ry1 + ch};
                RECT c2{PS(12),      ry2, PS(12) + cw,     ry2 + ch};
                RECT c3{PS(22) + cw, ry2, PS(22) + cw * 2, ry2 + ch};
                rsw::ui::DrawAutorunChoiceCell(dlgDc, dlgG, c0, pvDpi, pvTheme, L"&Install Edition",  L"Add to local library", true, true, false);
                rsw::ui::DrawAutorunChoiceCell(dlgDc, dlgG, c1, pvDpi, pvTheme, L"View &ReadMe",      L"Edition notes & credits", false, false, false);
                rsw::ui::DrawAutorunChoiceCell(dlgDc, dlgG, c2, pvDpi, pvTheme, L"C&hange Folder...", L"Pick custom library path", false, false, false);
                rsw::ui::DrawAutorunChoiceCell(dlgDc, dlgG, c3, pvDpi, pvTheme, L"E&xit Installer",   L"Close without changes", false, false, false);
            } else if (classicHdr && stepIdx == 0) {
                RECT railRc{PS(10), PS(10), PS(142), relFootY - PS(8)};
                rsw::ui::DrawClassicLeftSidebarRail(
                    dlgDc, dlgG, railRc, pvDpi, pvTheme,
                    wk == rsw::WizardStyleKind::FullScreenTeal,
                    hPreviewIcon, hPreviewArt, previewArtW, previewArtH, pubName);
                HFONT fH = rsw::ui::CreateScaledFont(L"Segoe UI", 12, FW_BOLD,   pvDpi, 100);
                HFONT fB = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL, pvDpi, 100);
                HGDIOBJ oF = ::SelectObject(dlgDc, fH);
                ::SetTextColor(dlgDc, pvTheme.textPrimary);
                const std::wstring wHead = L"Welcome to " + prodName + L" Setup";
                RECT hRc{PS(154), PS(16), dW - PS(14), PS(74)};
                ::DrawTextW(dlgDc, wHead.c_str(), -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(dlgDc, fB);
                ::SetTextColor(dlgDc, pvTheme.textSecondary);
                RECT pRc{PS(154), PS(78), dW - PS(14), relFootY - PS(12)};
                ::DrawTextW(dlgDc, L"Classic 1995 / Wizard 97 3D beveled chrome with full UI Automation & Windows High Contrast compliance.", -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(dlgDc, oF);
                ::DeleteObject(fH);
                ::DeleteObject(fB);
            } else {
                rsw::ui::GlassGlyph stepGlyph = rsw::ui::GlassGlyph::Sliders;
                COLORREF stepCol              = rsw::ui::brand::kVioletTune;
                const wchar_t* kicker         = L"Choose how you would like to install";
                if (stepIdx == 0) { stepGlyph = rsw::ui::GlassGlyph::Sparkle;   stepCol = rsw::ui::brand::kRaggieBlue; kicker = L"Welcome to the Setup Wizard"; }
                if (stepIdx == 2) { stepGlyph = rsw::ui::GlassGlyph::Reinstall; stepCol = rsw::ui::brand::kRaggieBlue; kicker = L"Repair & Verify Installation"; }
                if (stepIdx == 3 || wk == rsw::WizardStyleKind::PassiveToast) {
                    stepGlyph = rsw::ui::GlassGlyph::Gear; stepCol = rsw::ui::brand::kRaggieBlue; kicker = L"Applying System Changes";
                }
                if (stepIdx == 5) { stepGlyph = rsw::ui::GlassGlyph::Blocks;    stepCol = rsw::ui::brand::kVioletTune; kicker = L"Select Program Features"; }

                const bool win95Rail = rsw::UsesClassic3dDialogSheet(wk);
                if (micaMode) {
                    rsw::ui::DrawFluentMicaStepRail(dlgDc, dlgG, PS(136), 0, relFootY, (stepIdx == 0) ? 0 : ((stepIdx == 3) ? 5 : 2), pvDpi, pvTheme, prodName, hPreviewIcon);
                } else if (win95Rail) {
                    RECT railRc{PS(10), PS(10), PS(142), relFootY - PS(8)};
                    rsw::ui::DrawClassicLeftSidebarRail(
                        dlgDc, dlgG, railRc, pvDpi, pvTheme,
                        wk == rsw::WizardStyleKind::FullScreenTeal,
                        hPreviewIcon, hPreviewArt, previewArtW, previewArtH, pubName);
                    HFONT fH95 = rsw::ui::CreateScaledFont(L"Segoe UI", 11, FW_BOLD, pvDpi, 100);
                    HGDIOBJ oF95 = ::SelectObject(dlgDc, fH95);
                    ::SetTextColor(dlgDc, pvTheme.textPrimary);
                    RECT h95Rc{PS(154), PS(12), dW - PS(12), PS(38)};
                    ::DrawTextW(dlgDc, kicker, -1, &h95Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    ::SelectObject(dlgDc, oF95);
                    ::DeleteObject(fH95);
                } else if (classicHdr) {
                    rsw::ui::DrawWizard97TopHeader(dlgDc, dW, pvDpi, pvTheme, false, kicker, prodName + L" Setup", hPreviewIcon);
                } else if (aeroMode) {
                    rsw::ui::DrawAeroWizardTopHeader(dlgDc, dlgG, dW, pvDpi, pvTheme, prodName + L" Setup", kicker, hPreviewIcon, stepGlyph, stepCol, pubName);
                } else {
                    rsw::ui::DrawGlassTopHeader(dlgDc, dlgG, dW, pvDpi, pvTheme, pubName, rsw::ui::UpperWide(kicker), prodName, hPreviewIcon, stepGlyph, stepCol);
                }

                const int bTop = micaMode ? PS(28) : (win95Rail ? PS(42) : (classicHdr ? PS(74) : PS(96)));
                const int cx   = micaMode ? PS(148) : (win95Rail ? PS(154) : PS(14));
                const int cw   = dW - cx - PS(14);

                if (wk == rsw::WizardStyleKind::PassiveToast) {
                    Gdiplus::SolidBrush trackBr(rsw::ui::ToGpColor(pvTheme.borderSubtle));
                    Gdiplus::SolidBrush fillBr(rsw::ui::ToGpColor(rsw::ui::brand::kEmeraldOk));
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx), static_cast<float>(bTop + PS(18)), static_cast<float>(cw), static_cast<float>(PS(18)), static_cast<float>(PS(9)), &trackBr);
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx), static_cast<float>(bTop + PS(18)), static_cast<float>(cw * 72 / 100), static_cast<float>(PS(18)), static_cast<float>(PS(9)), &fillBr);
                } else if (wk == rsw::WizardStyleKind::OneClick || stepIdx == 0) {
                    RECT heroRc{cx, bTop, cx + cw, bTop + PS(64)};
                    rsw::ui::DrawHeroBrandCard(dlgDc, dlgG, heroRc, pvDpi, pvTheme, pubName, tagline, webLbl,
                                               (wk == rsw::WizardStyleKind::OneClick) ? L"ONE-CLICK EXPRESS" : L"OFFICIAL INSTALLER", hPreviewIcon);
                    HFONT fB = rsw::ui::CreateScaledFont(L"Segoe UI", 9, FW_NORMAL, pvDpi, 100);
                    HGDIOBJ oF = ::SelectObject(dlgDc, fB);
                    ::SetTextColor(dlgDc, pvTheme.textSecondary);
                    RECT pRc{cx + PS(4), bTop + PS(74), cx + cw, relFootY - PS(8)};
                    ::DrawTextW(dlgDc, (wk == rsw::WizardStyleKind::OneClick)
                        ? L"Compact Express Splash — click Install for instant setup or Customize... to expand full wizard."
                        : L"100% WCAG 2.2 AA Accessible Win32 C++20 Wizard — 10 selectable era-spanning styles, High Contrast & Dark Mode.",
                        -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                    ::SelectObject(dlgDc, oF);
                    ::DeleteObject(fB);
                } else if (stepIdx == 1) {
                    const auto previewOpts = rsw::BuildEffectiveSetupTypeOptions(
                        win95Types ? "win95_classic" : "express_custom", {},
                        L"One-click setup into the default folder with recommended shortcuts.",
                        L"Installs essential application files with minimal disk footprint.",
                        L"Choose features, destination folder, and shortcuts step by step.");
                    const int nOpt = static_cast<int>(previewOpts.size());
                    const int gap  = (nOpt >= 4) ? PS(4) : PS(7);
                    const int ch   = std::max(PS(40), std::min(PS(62), (relFootY - bTop - gap * nOpt) / std::max(1, nOpt)));
                    for (int oi = 0; oi < nOpt; ++oi) {
                        const auto& po = previewOpts[static_cast<std::size_t>(oi)];
                        RECT rCard{cx, bTop + (ch + gap) * oi, cx + cw, bTop + (ch + gap) * oi + ch};
                        rsw::ui::DrawAccessibleOptionCard(
                            dlgDc, dlgG, rCard, pvDpi, pvTheme,
                            po.glyph, po.rimRgb, po.accentRgb, po.titleMnemonic, po.description,
                            oi == 0, oi == 0, false);
                    }
                } else if (stepIdx == 2) {
                    RECT r0{cx, bTop + PS(6),  cx + cw, bTop + PS(76)};
                    RECT r1{cx, bTop + PS(86), cx + cw, bTop + PS(156)};
                    rsw::ui::DrawAccessibleOptionCard(dlgDc, dlgG, r0, pvDpi, pvTheme, rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk,  rsw::ui::brand::kWarmGold, L"&Quick Check & Repair (Recommended)", L"Verifies installed files and restores only missing or damaged files.", true, true, false);
                    rsw::ui::DrawAccessibleOptionCard(dlgDc, dlgG, r1, pvDpi, pvTheme, rsw::ui::GlassGlyph::Reinstall,   rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kSkyCyan,  L"&Full Reinstall (Refresh All Files)", L"Replaces every program file and shortcut from the setup package.", false, false, false);
                } else if (stepIdx == 5) {
                    const int treeW = (cw * 58) / 100;
                    const int paneH = std::max(PS(80), relFootY - bTop - PS(10));
                    Gdiplus::SolidBrush boxBr(rsw::ui::ToGpColor(pvTheme.cardBg));
                    Gdiplus::Pen boxPen(rsw::ui::ToGpColor(pvTheme.borderSubtle), 1.0f);
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx), static_cast<float>(bTop), static_cast<float>(treeW), static_cast<float>(paneH), static_cast<float>(PS(6)), &boxBr, &boxPen);
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx + treeW + PS(10)), static_cast<float>(bTop), static_cast<float>(cw - treeW - PS(10)), static_cast<float>(paneH), static_cast<float>(PS(8)), &boxBr, &boxPen);

                    const struct { rsw::MsiTreeState st; int indent; const wchar_t* label; } previewRows[3] = {
                        {rsw::MsiTreeState::EntireSubtree, 0, L"Core IDE & Compiler"},
                        {rsw::MsiTreeState::Local,         1, L"Dialog & AI Templates"},
                        {rsw::MsiTreeState::Absent,        1, L"Optional Sample Pack"}
                    };
                    HFONT fRow = rsw::ui::CreateScaledFont(L"Segoe UI", 9, FW_NORMAL, pvDpi, 100);
                    HFONT fHd  = rsw::ui::CreateScaledFont(L"Segoe UI", 9, FW_BOLD,   pvDpi, 100);
                    HGDIOBJ oF = ::SelectObject(dlgDc, fRow);
                    ::SetTextColor(dlgDc, pvTheme.textPrimary);
                    for (int r = 0; r < 3; ++r) {
                        const int rx = cx + PS(8 + previewRows[r].indent * 16);
                        const int ry = bTop + PS(10 + r * 26);
                        rsw::PaintSingleMsiStateCell(dlgG, static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(PS(30)), static_cast<float>(PS(18)), previewRows[r].st, pvTheme);
                        RECT lblRc{rx + PS(36), ry, cx + treeW - PS(6), ry + PS(20)};
                        ::DrawTextW(dlgDc, previewRows[r].label, -1, &lblRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    }
                    ::SelectObject(dlgDc, fHd);
                    RECT dtRc{cx + treeW + PS(16), bTop + PS(10), cx + cw - PS(8), bTop + PS(32)};
                    ::DrawTextW(dlgDc, L"Feature Details", -1, &dtRc, DT_LEFT | DT_SINGLELINE);
                    ::SelectObject(dlgDc, fRow);
                    ::SetTextColor(dlgDc, pvTheme.textSecondary);
                    RECT dbRc{cx + treeW + PS(16), bTop + PS(34), cx + cw - PS(8), bTop + paneH - PS(8)};
                    ::DrawTextW(dlgDc, L"Authentic MSI SelectionTree split-icons + live byte cost.", -1, &dbRc, DT_LEFT | DT_WORDBREAK);
                    ::SelectObject(dlgDc, oF);
                    ::DeleteObject(fRow);
                    ::DeleteObject(fHd);
                } else {
                    RECT heroRc{cx, bTop, cx + cw, bTop + PS(60)};
                    rsw::ui::DrawHeroBrandCard(dlgDc, dlgG, heroRc, pvDpi, pvTheme, pubName, tagline, webLbl, L"CONFIGURING SYSTEM", hPreviewIcon);
                    Gdiplus::SolidBrush boxBr(rsw::ui::ToGpColor(pvTheme.cardBg));
                    Gdiplus::Pen boxPen(rsw::ui::ToGpColor(pvTheme.borderSubtle), 1.0f);
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx), static_cast<float>(bTop + PS(70)), static_cast<float>(cw), static_cast<float>(PS(92)), static_cast<float>(PS(10)), &boxBr, &boxPen);
                    Gdiplus::SolidBrush trackBr(rsw::ui::ToGpColor(pvTheme.borderSubtle));
                    Gdiplus::SolidBrush fillBr(rsw::ui::ToGpColor(rsw::ui::brand::kEmeraldOk));
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx + PS(16)), static_cast<float>(bTop + PS(124)), static_cast<float>(cw - PS(32)), static_cast<float>(PS(18)), static_cast<float>(PS(9)), &trackBr);
                    rsw::ui::FillAndStrokeRoundRect(dlgG, static_cast<float>(cx + PS(16)), static_cast<float>(bTop + PS(124)), static_cast<float>((cw - PS(32)) * 68 / 100), static_cast<float>(PS(18)), static_cast<float>(PS(9)), &fillBr);
                }
            }

            // Footer navigation buttons inside `dlgZeroRc`
            RECT backRc{dW - PS(258), relFootY + PS(7), dW - PS(182), relFootY + footH - PS(7)};
            RECT ctaRc {dW - PS(176), relFootY + PS(7), dW - PS(86),  relFootY + footH - PS(7)};
            RECT canRc {dW - PS(80),  relFootY + PS(7), dW - PS(12),  relFootY + footH - PS(7)};
            if (wk != rsw::WizardStyleKind::PassiveToast && stepIdx != 0) {
                rsw::ui::DrawAccessiblePushButton(dlgDc, dlgG, backRc, pvDpi, pvTheme, L"< &Back", false, true, true, false, false, false, false, nullptr);
            }
            if (wk != rsw::WizardStyleKind::PassiveToast) {
                rsw::ui::DrawAccessiblePushButton(dlgDc, dlgG, ctaRc, pvDpi, pvTheme, ctaLbl, true, true, true, false, false, false, false, nullptr);
            }
            rsw::ui::DrawAccessiblePushButton(dlgDc, dlgG, canRc, pvDpi, pvTheme, L"Cancel", false, true, true, false, false, false, false, nullptr);
            if (retroFull || wk == rsw::WizardStyleKind::Wizard95) {
                rsw::ui::DrawClassic3dBevelFrame(dlgDc, dlgZeroRc, false, pvTheme);
            }
        }

        ::BitBlt(pvDc, dL, dT, dW, dH, dlgDc, 0, 0, SRCCOPY);
        ::SelectObject(dlgDc, oldDlgBmp);
        ::DeleteObject(dlgBmp);
        ::DeleteDC(dlgDc);

        // Live WCAG 2.2 AA Brand Contrast Badge at top-right of the Designer Canvas!
        double vsWhite = 0.0, vsDark = 0.0;
        const bool wcagOk = rsw::wcag::MeetsDualThemeAccentContrast(prim, vsWhite, vsDark);
        wchar_t wcagBuf[96]{};
        std::swprintf(wcagBuf, std::size(wcagBuf),
                      wcagOk ? L"WCAG 2.2 AA PASS \x2022 %.1f:1" : L"WCAG WARN \x2022 %.1f:1 (Auto-Fix)",
                      std::min(vsWhite, vsDark * 1.5));
        const int bdgW = PS(182);
        const int bdgH = PS(22);
        Gdiplus::SolidBrush bdgBr(wcagOk ? Gdiplus::Color(235, 6, 95, 70) : Gdiplus::Color(240, 146, 64, 14));
        Gdiplus::Pen bdgPen(Gdiplus::Color(220, 255, 255, 255), 1.0f);
        rsw::ui::FillAndStrokeRoundRect(pvG, static_cast<float>(pvW - bdgW - PS(8)), static_cast<float>(PS(6)),
                                        static_cast<float>(bdgW), static_cast<float>(bdgH), static_cast<float>(PS(11)), &bdgBr, &bdgPen);
        HFONT fWcg = rsw::ui::CreateScaledFont(L"Segoe UI", 8, FW_BOLD, pvDpi, 100);
        HGDIOBJ oFw = ::SelectObject(pvDc, fWcg);
        ::SetTextColor(pvDc, RGB(255, 255, 255));
        RECT wcgRc{pvW - bdgW - PS(8), PS(6), pvW - PS(8), PS(6) + bdgH};
        ::DrawTextW(pvDc, wcagBuf, -1, &wcgRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SelectObject(pvDc, oFw);
        ::DeleteObject(fWcg);

        Gdiplus::Pen outerFrame(rsw::ui::ToGpColor(pvTheme.accentPrimary, 210), 1.5f);
        pvG.DrawRectangle(&outerFrame, 0.5f, 0.5f, static_cast<float>(pvW) - 1.0f, static_cast<float>(pvH) - 1.0f);

        ::BitBlt(hdcOut, destRc.left, destRc.top, pvW, pvH, pvDc, 0, 0, SRCCOPY);
        ::SelectObject(pvDc, oldBmp);
        ::DeleteObject(pvBmp);
        ::DeleteDC(pvDc);
    }

    /// Renders a live `370 x 270` DLU Windows Installer (`MSI.DLL`) Wizard 97 Form Designer Canvas!
    void PaintMsiDialogDesignerCanvas(HDC hdcOut, const RECT& destRc, UINT dpi) {
        const int maxW = destRc.right - destRc.left;
        const int maxH = destRc.bottom - destRc.top;
        if (maxW < 200 || maxH < 160) return;

        int cw = maxW;
        int ch = cw * 270 / 370;
        if (ch > maxH) {
            ch = maxH;
            cw = ch * 370 / 270;
        }
        auto DX = [&](int dluX) { return dluX * cw / 370; };
        auto DY = [&](int dluY) { return dluY * ch / 270; };

        HDC dc         = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp    = ::CreateCompatibleBitmap(hdcOut, cw, ch);
        HGDIOBJ oldBmp = ::SelectObject(dc, bmp);

        HBRUSH hGray  = ::CreateSolidBrush(RGB(240, 240, 240));
        HBRUSH hWhite = ::CreateSolidBrush(RGB(255, 255, 255));
        RECT fullRc{0, 0, cw, ch};
        ::FillRect(dc, &fullRc, hGray);

        wchar_t dlgNameBuf[128]{};
        ::GetWindowTextW(hMsiDlgSelect, dlgNameBuf, 128);
        const std::string dlgId = rsw::WideToUtf8(dlgNameBuf);
        const auto doc = ResolvedEffectiveDoc();

        const bool fullBleedWelcome = (dlgId == "WelcomeDlg" || dlgId == "ExitDialog");
        if (fullBleedWelcome) {
            RECT sideRc{0, 0, DX(122), DY(234)};
            rsw::ui::FillHorizontalGradient(dc, sideRc, RGB(11, 19, 43), RGB(37, 99, 235));
            RECT rightWhite{DX(122), 0, cw, DY(234)};
            ::FillRect(dc, &rightWhite, hWhite);
        } else {
            RECT banRc{0, 0, cw, DY(44)};
            ::FillRect(dc, &banRc, hWhite);
            HPEN lPen = ::CreatePen(PS_SOLID, 1, RGB(180, 188, 198));
            HGDIOBJ oP = ::SelectObject(dc, lPen);
            ::MoveToEx(dc, 0, DY(44), nullptr); ::LineTo(dc, cw, DY(44));
            ::SelectObject(dc, oP);
            ::DeleteObject(lPen);
        }

        HPEN botPen = ::CreatePen(PS_SOLID, 1, RGB(180, 188, 198));
        HGDIOBJ oP2 = ::SelectObject(dc, botPen);
        ::MoveToEx(dc, 0, DY(234), nullptr); ::LineTo(dc, cw, DY(234));
        ::SelectObject(dc, oP2);
        ::DeleteObject(botPen);

        HFONT fBold = rsw::ui::CreateScaledFont(L"Segoe UI", 9, FW_BOLD,   dpi, 100);
        HFONT fNorm = rsw::ui::CreateScaledFont(L"Segoe UI", 8, FW_NORMAL, dpi, 100);
        ::SetBkMode(dc, TRANSPARENT);
        HGDIOBJ oF = ::SelectObject(dc, fBold);
        ::SetTextColor(dc, RGB(15, 23, 42));

        auto drawBoxCtrl = [&](int x, int y, int w, int h, const std::wstring& cap, COLORREF borderCol, bool sunkenFill) {
            RECT r{DX(x), DY(y), DX(x + w), DY(y + h)};
            if (sunkenFill) ::FillRect(dc, &r, hWhite);
            HPEN p = ::CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ op = ::SelectObject(dc, p);
            HGDIOBJ ob = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
            ::Rectangle(dc, r.left, r.top, r.right, r.bottom);
            ::SelectObject(dc, ob);
            ::SelectObject(dc, op);
            ::DeleteObject(p);
            RECT tr{r.left + 6, r.top + 2, r.right - 4, r.bottom - 2};
            ::DrawTextW(dc, cap.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        };

        ::SelectObject(dc, fNorm);
        std::wstring badge = L"MSI.DLL Dialog: " + std::wstring(dlgNameBuf) + L" (370x270 DLU)";
        RECT bdgRc{DX(10), DY(238), DX(176), DY(264)};
        ::SetTextColor(dc, RGB(71, 85, 105));
        ::DrawTextW(dc, badge.c_str(), -1, &bdgRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SetTextColor(dc, RGB(15, 23, 42));
        drawBoxCtrl(180, 243, 54, 17, L"< Back",  RGB(148, 163, 184), false);
        drawBoxCtrl(238, 243, 54, 17, (dlgId == "VerifyReadyDlg") ? L"Install" : L"Next >", RGB(37, 99, 235), false);
        drawBoxCtrl(304, 243, 54, 17, L"Cancel",  RGB(148, 163, 184), false);

        if (dlgId == "WelcomeDlg" || dlgId == "ExitDialog") {
            ::SelectObject(dc, fBold);
            RECT t{DX(135), DY(22), DX(355), DY(70)};
            ::DrawTextW(dc, (dlgId == "WelcomeDlg") ? L"Welcome to [ProductName] Setup" : L"Completed [ProductName] Setup", -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            ::SelectObject(dc, fNorm);
            RECT d{DX(135), DY(80), DX(355), DY(160)};
            ::DrawTextW(dc, L"Native Windows Installer relational Dialog table preview.\r\nSupports silent /qn GPO deployment & full interactive UI.", -1, &d, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else {
            std::wstring hTitle = rsw::Utf8ToWide(doc.GetString("MsiDialog", dlgId, "HeaderTitle", dlgId));
            std::wstring hSub   = rsw::Utf8ToWide(doc.GetString("MsiDialog", dlgId, "HeaderSubtitle", "Windows Installer 370x270 Wizard 97 SQL Dialog"));
            if (dlgId == "SetupTypeDlg")   { hTitle = L"Choose Setup Mode"; hSub = L"Express, Compact Core, or Custom Setup."; }
            if (dlgId == "CustomizeDlg")   { hTitle = L"Select Features";   hSub = L"Native MSI SelectionTree feature selector."; }
            if (dlgId == "InstallDirDlg")  { hTitle = L"Destination Folder";hSub = L"PathEdit + SpawnDialog(BrowseDlg)."; }
            if (dlgId == "ShortcutsDlg")   { hTitle = L"Shortcuts & File Associations"; hSub = L"DESKTOP_SHORTCUT & REGISTER_ASSOC checkboxes."; }
            if (dlgId == "RepairModeDlg")  { hTitle = L"Repair & Verify Installation";  hSub = L"Quick Check (pecms) vs Full Reinstall (vomus)."; }
            if (dlgId == "VerifyReadyDlg") { hTitle = L"Ready to Install [ProductName]";hSub = L"Summary & UAC elevation trigger."; }

            ::SelectObject(dc, fBold);
            RECT ht{DX(15), DY(6), DX(350), DY(22)};
            ::DrawTextW(dc, hTitle.c_str(), -1, &ht, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(dc, fNorm);
            RECT hs{DX(20), DY(23), DX(350), DY(42)};
            ::DrawTextW(dc, hSub.c_str(), -1, &hs, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            if (dlgId == "SetupTypeDlg") {
                drawBoxCtrl(20, 58,  330, 42, L"(o) &Express Setup (Recommended)  [RSW_SETUP_MODE]", RGB(37, 99, 235), true);
                drawBoxCtrl(20, 106, 330, 42, L"( ) Co&mpact Core Setup  (AddLocal=Main)",           RGB(148, 163, 184), true);
                drawBoxCtrl(20, 154, 330, 42, L"( ) C&ustom Setup -> CustomizeDlg -> InstallDirDlg", RGB(148, 163, 184), true);
            } else if (dlgId == "CustomizeDlg") {
                drawBoxCtrl(20,  56, 198, 150, L"[SelectionTree: Main & Sub-Features]", RGB(100, 116, 139), true);
                drawBoxCtrl(226, 56, 126, 150, L"[Feature Description & Cost]",         RGB(148, 163, 184), false);
            } else if (dlgId == "InstallDirDlg") {
                drawBoxCtrl(20,  82, 258, 18, L"[INSTALLDIR]", RGB(100, 116, 139), true);
                drawBoxCtrl(284, 81, 66,  19, L"B&rowse...",   RGB(37, 99, 235),   false);
            } else if (dlgId == "ShortcutsDlg") {
                drawBoxCtrl(24, 68, 322, 20, L"[x] &Create a Desktop shortcut  [DESKTOP_SHORTCUT]", RGB(37, 99, 235), true);
                drawBoxCtrl(24, 98, 322, 20, L"[x] &Register file extensions   [REGISTER_ASSOC]",   RGB(37, 99, 235), true);
            } else if (dlgId == "RepairModeDlg") {
                drawBoxCtrl(20, 62,  330, 50, L"(o) &Quick Check & Repair (ReinstallMode=pecms)", RGB(16, 185, 129), true);
                drawBoxCtrl(20, 120, 330, 50, L"( ) &Full Reinstall (ReinstallMode=vomus)",       RGB(148, 163, 184), true);
            } else if (dlgId == "LicenseAgreementDlg") {
                drawBoxCtrl(20, 54,  330, 132, L"[ScrollableText: License.rtf]", RGB(100, 116, 139), true);
                drawBoxCtrl(20, 196, 330, 18,  L"[x] I have &read and accept the terms  [IAGREECHECK]", RGB(37, 99, 235), false);
            }
        }

        for (const auto& cid : doc.GetNamedSectionIds("MsiControl")) {
            if (!rsw::EqualsIgnoreCase(doc.GetString("MsiControl", cid, "Dialog"), dlgId)) continue;
            const int x  = doc.GetInt("MsiControl", cid, "X", 20);
            const int y  = doc.GetInt("MsiControl", cid, "Y", 60);
            const int w  = doc.GetInt("MsiControl", cid, "Width", 330);
            const int h  = doc.GetInt("MsiControl", cid, "Height", 18);
            const std::string tp = doc.GetString("MsiControl", cid, "Type", "Text");
            const std::string pr = doc.GetString("MsiControl", cid, "Property");
            const std::string tx = doc.GetString("MsiControl", cid, "Text", cid);
            std::string cap = "[" + tp + "] " + tx;
            if (!pr.empty()) cap += "  {" + pr + "}";
            drawBoxCtrl(x, y, w, h, rsw::Utf8ToWide(cap), RGB(124, 58, 237), rsw::EqualsIgnoreCase(tp, "Edit") || rsw::EqualsIgnoreCase(tp, "PathEdit"));
        }

        HPEN rim = ::CreatePen(PS_SOLID, 2, theme.accentPrimary);
        HGDIOBJ oRim = ::SelectObject(dc, rim);
        HGDIOBJ oNb  = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(dc, 0, 0, cw, ch);
        ::SelectObject(dc, oNb);
        ::SelectObject(dc, oRim);
        ::DeleteObject(rim);

        ::SelectObject(dc, oF);
        ::DeleteObject(fBold);
        ::DeleteObject(fNorm);
        ::DeleteObject(hGray);
        ::DeleteObject(hWhite);

        ::BitBlt(hdcOut, destRc.left, destRc.top, cw, ch, dc, 0, 0, SRCCOPY);
        ::SelectObject(dc, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(dc);
    }

    void PaintIdeSurface(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC hdc        = ::CreateCompatibleDC(hdcOut);
        HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(hdc, memBmp);

        ::FillRect(hdc, &rc, theme.hbrWindow);
        RECT bannerRc{0, 0, W, S(50)};
        rsw::ui::FillHorizontalGradient(hdc, bannerRc, theme.bannerNavyLeft, theme.bannerNavyRight);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        rsw::ui::DrawBrandSquircleBadge(g, static_cast<float>(S(14)), static_cast<float>(S(9)), static_cast<float>(S(32)), theme, L"RaggieSoft");
        rsw::ui::DrawMultiStopAccentBar(g, 0.0f, static_cast<float>(S(50)), static_cast<float>(W), static_cast<float>(S(4)), theme);

        ::SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldF = ::SelectObject(hdc, hFontBanner);
        ::SetTextColor(hdc, theme.bannerTitleText);
        RECT tRc{S(54), S(8), W - S(236), S(44)};
        ::DrawTextW(hdc, L"RaggieSoft Installer Studio  •  Install Without Limits (Tree-First IDE • Features & Registry • AeroWizards • Dual Designer)",
                    -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        const int pillW = S(214);
        RECT pillRc{W - pillW - S(14), S(11), W - S(14), S(39)};
        Gdiplus::SolidBrush pillBr(rsw::ui::ToGpColor(theme.accentPrimary, 215));
        Gdiplus::Pen pillPen(Gdiplus::Color(180, 255, 255, 255), 1.0f);
        rsw::ui::FillAndStrokeRoundRect(
            g, static_cast<float>(pillRc.left), static_cast<float>(pillRc.top),
            static_cast<float>(pillW), static_cast<float>(S(28)), static_cast<float>(S(14)), &pillBr, &pillPen);
        ::SelectObject(hdc, hFontBold);
        ::SetTextColor(hdc, RGB(255, 255, 255));
        const std::wstring edBadge = (activeEdition == rsw::RswIdeEdition::Community)
            ? L"COMMUNITY • PREMIER PARITY"
            : rsw::ui::UpperWide(std::wstring(rsw::EditionDisplayName(activeEdition)));
        ::DrawTextW(hdc, edBadge.c_str(), -1, &pillRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (activeView == VIEW_CPP_WIZARD && previewCanvasRc.right > previewCanvasRc.left + S(160)) {
            PaintLiveWizardPreview(hdc, previewCanvasRc, dpi);
        } else if (activeView == VIEW_MSI_DIALOGS && msiCanvasRc.right > msiCanvasRc.left + S(160)) {
            PaintMsiDialogDesignerCanvas(hdc, msiCanvasRc, dpi);
        }

        ::SelectObject(hdc, oldF);
        ::BitBlt(hdcOut, 0, 0, W, H, hdc, 0, 0, SRCCOPY);
        ::SelectObject(hdc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(hdc);
    }

    // ----------------------------------------------------------------------------
    // `WM_NOTIFY` HANDLERS FOR ALL 10 `SysTreeView32` TREES!
    // ----------------------------------------------------------------------------
    void HandleTreeSelectionChanged(const NMTREEVIEWW* nmtv) {
        if (!nmtv) return;
        const UINT_PTR idFrom = nmtv->hdr.idFrom;
        const std::size_t idx = static_cast<std::size_t>(nmtv->itemNew.lParam);

        if (idFrom == IDC_NAV_TREE) {
            if (suppressNavEvent || idx >= navNodeViews.size()) return;
            const int targetView = navNodeViews[idx];
            if (targetView >= 0 && targetView < VIEW_COUNT) {
                activeView = targetView;
                UpdateActiveViewVisibility();
                ::InvalidateRect(hWnd, nullptr, TRUE);
            } else {
                // Category folder clicked — jump cleanly to its first child view!
                HTREEITEM hChild = TreeView_GetChild(hNavTree, nmtv->itemNew.hItem);
                if (hChild) TreeView_SelectItem(hNavTree, hChild);
            }
            return;
        }

        const auto doc = ResolvedEffectiveDoc();

        if (idFrom == IDC_PR_TREE && idx < prNodeRefs.size()) {
            selectedPrRef = prNodeRefs[idx];
            if (selectedPrRef.prefix == "Prerequisite") {
                const std::string src = doc.GetString("Prerequisite", selectedPrRef.id, "SourceFile",
                                          doc.GetString("Prerequisite", selectedPrRef.id, "Url"));
                const std::string reb = doc.GetString("Prerequisite", selectedPrRef.id, "RebootExitCodes", "3010, 1641, 23");
                if (!src.empty()) ::SetWindowTextW(hPrSource, rsw::Utf8ToWide(src).c_str());
                ::SetWindowTextW(hPrRebootCodes, rsw::Utf8ToWide(reb).c_str());
            } else if (selectedPrRef.prefix == "MergeModule") {
                ::SetWindowTextW(hPrSource, rsw::Utf8ToWide(doc.GetString("MergeModule", selectedPrRef.id, "Source")).c_str());
            }
            return;
        }

        if (idFrom == IDC_FT_TREE && idx < ftNodeIds.size()) {
            if (!ftNodeIds[idx].empty()) {
                PopulateFeatureInspectorFields(doc, ftNodeIds[idx]);
            }
            return;
        }

        if (idFrom == IDC_DEST_TREE && idx < destNodeMetas.size()) {
            selectedDestRoot   = destNodeMetas[idx].targetRoot;
            selectedDestSubdir = destNodeMetas[idx].targetSubdir;
            astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
            RefreshDestinationContentsTree(ResolvedEffectiveDoc());
            return;
        }

        if (idFrom == IDC_PAY_TREE && idx < payNodeRefs.size()) {
            selectedPayRef = payNodeRefs[idx];
            return;
        }

        if (idFrom == IDC_DESIGN_TREE && idx < designNodeRefs.size()) {
            const auto& ref = designNodeRefs[idx];
            if (ref.prefix == "Feature" && !ref.id.empty()) {
                ::SetWindowTextW(hFeatId,     rsw::Utf8ToWide(ref.id).c_str());
                ::SetWindowTextW(hFeatTitle,  rsw::Utf8ToWide(doc.GetString("Feature", ref.id, "Title", ref.id)).c_str());
                ::SendMessageW(hFeatLevel, CB_SETCURSEL, static_cast<WPARAM>(QueryFeatureTierFromDoc(doc, ref.id)), 0);
                ::SetWindowTextW(hFeatParent, rsw::Utf8ToWide(doc.GetString("Feature", ref.id, "Parent")).c_str());
            } else if (ref.prefix == "Component" && !ref.id.empty()) {
                ::SetWindowTextW(hCompId,       rsw::Utf8ToWide(ref.id).c_str());
                ::SetWindowTextW(hCompFeat,     rsw::Utf8ToWide(doc.GetString("Component", ref.id, "Feature", "Main")).c_str());
                ::SetWindowTextW(hCompSubdir,   rsw::Utf8ToWide(doc.GetString("Component", ref.id, "TargetSubdir")).c_str());
                ::SetWindowTextW(hCompKpTarget, rsw::Utf8ToWide(doc.GetString("Component", ref.id, "KeyPathTarget")).c_str());
                const std::string rt = doc.GetString("Component", ref.id, "TargetRoot", "INSTALLDIR");
                for (int r = 0; r < 9; ++r) {
                    if (rsw::EqualsIgnoreCase(rt, rsw::ide_wizards::kRootIds[r])) {
                        ::SendMessageW(hCompRoot, CB_SETCURSEL, r, 0);
                        break;
                    }
                }
                const std::string kp = doc.GetString("Component", ref.id, "KeyPathKind", "auto");
                const char* kpKinds[] = {"auto", "file", "registry", "folder"};
                for (int k = 0; k < 4; ++k) {
                    if (rsw::EqualsIgnoreCase(kp, kpKinds[k])) {
                        ::SendMessageW(hCompKpKind, CB_SETCURSEL, k, 0);
                        break;
                    }
                }
            }
            return;
        }

        if (idFrom == IDC_REG_KEY_TREE && idx < regKeyMetas.size()) {
            const auto& meta = regKeyMetas[idx];
            if (!meta.hiveCode.empty()) {
                selectedRegHive    = meta.hiveCode;
                selectedRegKeyPath = meta.keyPath;
                selectedRegSectionId.clear();
                RefreshRegistryValuesTree(doc);
            }
            return;
        }

        if (idFrom == IDC_REG_VAL_TREE && idx < regValSectionIds.size()) {
            const std::string& rid = regValSectionIds[idx];
            if (!rid.empty()) {
                selectedRegSectionId = rid;
                SyncRegRootComboSelection(doc.GetString("Registry", rid, "Root", selectedRegHive));
                ::SetWindowTextW(hRegKey,      rsw::Utf8ToWide(doc.GetString("Registry", rid, "Key")).c_str());
                ::SetWindowTextW(hRegName,     rsw::Utf8ToWide(doc.GetString("Registry", rid, "Name", "@")).c_str());
                ::SetWindowTextW(hRegVal,      rsw::Utf8ToWide(doc.GetString("Registry", rid, "Value")).c_str());
                ::SetWindowTextW(hRegFeatEdit, rsw::Utf8ToWide(doc.GetString("Registry", rid, "Feature", "Main")).c_str());
                ::SetWindowTextW(hRegComp,     rsw::Utf8ToWide(doc.GetString("Registry", rid, "Component")).c_str());
                const std::string tp = doc.GetString("Registry", rid, "Type", "string");
                int tpIdx = 0;
                if (rsw::EqualsIgnoreCase(tp, "expand_string")) tpIdx = 1;
                else if (rsw::EqualsIgnoreCase(tp, "dword"))    tpIdx = 2;
                ::SendMessageW(hRegTypeCombo, CB_SETCURSEL, tpIdx, 0);
            }
            return;
        }

        if (idFrom == IDC_FA_TREE && idx < faNodeIds.size()) {
            const std::string& aid = faNodeIds[idx];
            if (!aid.empty()) {
                selectedFaId = aid;
                ::SetWindowTextW(hFaExt,    rsw::Utf8ToWide(doc.GetString("FileAssociation", aid, "Extension")).c_str());
                ::SetWindowTextW(hFaProgId, rsw::Utf8ToWide(doc.GetString("FileAssociation", aid, "ProgId")).c_str());
                ::SetWindowTextW(hFaDesc,   rsw::Utf8ToWide(doc.GetString("FileAssociation", aid, "Description")).c_str());
                ::SetWindowTextW(hFaTarget, rsw::Utf8ToWide(doc.GetString("FileAssociation", aid, "TargetFile")).c_str());
                ::SetWindowTextW(hFaVerbs,  rsw::Utf8ToWide(doc.GetString("FileAssociation", aid, "Verbs", "open:&Open:\"%1\"")).c_str());
                const bool adv = doc.GetBool("FileAssociation", aid, "Advertise", false);
                ::SendMessageW(hFaAdvChk, BM_SETCHECK, adv ? BST_CHECKED : BST_UNCHECKED, 0);
                ::InvalidateRect(hFaAdvChk, nullptr, FALSE);
            }
            return;
        }

        if (idFrom == IDC_SC_TREE && idx < scNodeRefs.size()) {
            const auto& ref = scNodeRefs[idx];
            if (ref.prefix == "Location") {
                ::SendMessageW(hScLocCombo, CB_SETCURSEL, rsw::EqualsIgnoreCase(ref.id, "Desktop") ? 1 : 0, 0);
            } else if (ref.prefix == "Shortcut" && !ref.id.empty()) {
                selectedScId = ref.id;
                ::SetWindowTextW(hScIdEdit, rsw::Utf8ToWide(ref.id).c_str());
                ::SetWindowTextW(hScName,   rsw::Utf8ToWide(doc.GetString("Shortcut", ref.id, "Name")).c_str());
                ::SetWindowTextW(hScTarget, rsw::Utf8ToWide(doc.GetString("Shortcut", ref.id, "TargetFile")).c_str());
                ::SetWindowTextW(hScArgs,   rsw::Utf8ToWide(doc.GetString("Shortcut", ref.id, "Arguments")).c_str());
                ::SetWindowTextW(hScFeat,   rsw::Utf8ToWide(doc.GetString("Shortcut", ref.id, "Feature", "Main")).c_str());
                const bool isDesk = rsw::EqualsIgnoreCase(doc.GetString("Shortcut", ref.id, "Location", "StartMenu"), "Desktop");
                ::SendMessageW(hScLocCombo, CB_SETCURSEL, isDesk ? 1 : 0, 0);
            }
            return;
        }
    }

    // ----------------------------------------------------------------------------
    // MAIN IDE WINDOW PROCEDURE
    // ----------------------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        InstallerStudioIde* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<InstallerStudioIde*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
        } else {
            self = reinterpret_cast<InstallerStudioIde*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE:
                self->CreateControls();
                rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                self->LayoutChildren();
                return 0;
            case WM_SIZE:
                self->LayoutChildren();
                return 0;
            case WM_DPICHANGED: {
                const auto* r = reinterpret_cast<const RECT*>(lParam);
                if (r) ::SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutChildren();
                return 0;
            }
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh();
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    for (HWND sc : {self->hSourceEdit, self->hConsoleEdit}) {
                        rsw::ui::ApplyDarkScrollbarTheme(sc, self->theme);
                    }
                    for (HWND cb : {self->hArchCombo, self->hSuiteCombo, self->hPkgScope, self->hPkgExecLvl,
                                    self->hFtLevel, self->hFeatLevel, self->hCompRoot, self->hCompKpKind,
                                    self->hPrPreset, self->hPrDist, self->hRegFeatFilter, self->hRegRootCombo, self->hRegTypeCombo,
                                    self->hScLocCombo, self->hUiLayout, self->hUiSetupTypes, self->hUiPreviewStep,
                                    self->hMsiPreset, self->hMsiDlgSelect, self->hMsiCtrlType}) {
                        rsw::ui::ApplyDarkComboBoxTheme(cb, self->theme);
                    }
                    self->LayoutChildren();
                    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    return 0;
                }
                break;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintIdeSurface(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.windowBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrWindow);
            }
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.cardBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrCard);
            }
            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (nm && nm->code == TVN_SELCHANGEDW) {
                    self->HandleTreeSelectionChanged(reinterpret_cast<const NMTREEVIEWW*>(lParam));
                    return 0;
                }
                break;
            }
            case WM_COMMAND: {
                const int id   = LOWORD(wParam);
                const int code = HIWORD(wParam);

                if ((id == IDC_UI_PREVIEW_STEP || id == IDC_UI_SETUP_TYPES || id == IDC_MSI_DLG_SELECT) && code == CBN_SELCHANGE) {
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if ((id == IDC_UI_PRIMARY_HEX || id == IDC_UI_SECONDARY_HEX) && code == EN_CHANGE && self->activeView == VIEW_CPP_WIZARD) {
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (id == IDC_REG_FEAT_FILTER && code == CBN_SELCHANGE) {
                    const LRESULT sel = ::SendMessageW(self->hRegFeatFilter, CB_GETCURSEL, 0, 0);
                    if (sel <= 0) {
                        self->activeRegFeatureFilter.clear();
                    } else {
                        wchar_t fBuf[128]{};
                        ::SendMessageW(self->hRegFeatFilter, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(fBuf));
                        self->activeRegFeatureFilter = rsw::WideToUtf8(fBuf);
                    }
                    self->RebuildRegistryTrees(self->ResolvedEffectiveDoc());
                    return 0;
                }
                if (id == IDC_MSI_PRESET_COMBO && code == CBN_SELCHANGE) {
                    self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                    const bool minMode = (::SendMessageW(self->hMsiPreset, CB_GETCURSEL, 0, 0) == 1);
                    self->astDoc.SetString("UI", "", "MsiDialogPreset", minMode ? "minimal" : "express_custom");
                    self->SyncSourceEditorFromAst();
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (id == IDC_UI_LAYOUT && code == CBN_SELCHANGE) {
                    const int lay = std::clamp(static_cast<int>(::SendMessageW(self->hUiLayout, CB_GETCURSEL, 0, 0)), 0, rsw::kWizardStyleCount - 1);
                    const auto wk = static_cast<rsw::WizardStyleKind>(lay);
                    ::SendMessageW(self->hUiPreviewStep, CB_SETCURSEL, (wk == rsw::WizardStyleKind::Autorun16x9) ? 4 : 1, 0);
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (id == IDC_SUITE_COMBO && code == CBN_SELCHANGE && self->hasActiveSuite) {
                    const LRESULT sel = ::SendMessageW(self->hSuiteCombo, CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR && static_cast<std::size_t>(sel) < self->activeSuite.projects.size()) {
                        const auto& target = self->activeSuite.projects[static_cast<std::size_t>(sel)];
                        rsw::RswManifestDocument d;
                        std::string err;
                        if (rsw::LoadManifestFile(target.resolvedPath, d, err)) {
                            self->LoadProjectText(d.Serialize(), target.resolvedPath, true);
                        }
                    }
                    return 0;
                }
                if (id == IDC_PR_PRESET_COMBO && code == CBN_SELCHANGE) {
                    const LRESULT sel = ::SendMessageW(self->hPrPreset, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < 6) {
                        const auto& pr = rsw::kCuratedRuntimePresets[static_cast<std::size_t>(sel)];
                        ::SetWindowTextW(self->hPrSource, rsw::Utf8ToWide(pr.urlArm64).c_str());
                        ::SetWindowTextW(self->hPrRebootCodes, rsw::Utf8ToWide(pr.rebootCodes).c_str());
                    }
                    return 0;
                }

                switch (id) {
                    case IDM_HELP_TOPICS:
                        self->LaunchContextHelp();
                        return 0;

                    // --- Editions & About Menu (`Community == 100% Premier Parity`) ---
                    case IDM_EDITION_COMMUNITY:
                    case IDM_EDITION_EXPRESS:
                    case IDM_EDITION_PROFESSIONAL:
                    case IDM_EDITION_PREMIER: {
                        self->activeEdition = static_cast<rsw::RswIdeEdition>(id - IDM_EDITION_COMMUNITY);
                        HMENU hMenu = ::GetMenu(hWnd);
                        if (hMenu) {
                            ::CheckMenuRadioItem(hMenu, IDM_EDITION_COMMUNITY, IDM_EDITION_PREMIER, id, MF_BYCOMMAND);
                        }
                        self->UpdateWindowCaptionWithEdition();
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        self->AppendConsole(L"[EDITION] Switched active tier badge to " +
                                            std::wstring(rsw::EditionDisplayName(self->activeEdition)) +
                                            L" — " + std::wstring(rsw::EditionSummary(self->activeEdition)) + L"\r\n");
                        return 0;
                    }
                    case IDM_HELP_ABOUT: {
                        const std::wstring head = L"RaggieSoft Installer Studio v" + std::wstring(rsw::kVersionStringWide) +
                                                  L" (" + std::wstring(rsw::EditionDisplayName(self->activeEdition)) + L")";
                        const std::wstring body =
                            std::wstring(rsw::EditionSummary(self->activeEdition)) +
                            L"\n\n• Zero-dependency static /MT Win32 C++20 suite (x64 & ARM64)"
                            L"\n• 12-View Tree-First Visual IDE: Category Nav, Features Tree, RegEdit Registry Tree & Dual Folder Trees"
                            L"\n• Enterprise .MSI, .MSM Merge Modules, .MST Transforms, .MSP Patches & LZMS Setup.exe"
                            L"\n\nFictitious Persons & Entities Disclaimer: All narrative characters, bands, universities, and corporations across RaggieSoft creative works are fictitious.";
                        ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), L"About RaggieSoft Installer Studio",
                                     head.c_str(), body.c_str(), TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
                        return 0;
                    }

                    // --- 5 Guided Native Win32 AeroWizards (`PSH_AEROWIZARD`)! ---
                    case IDM_WIZ_PREREQ_MSM:
                    case IDC_PR_WIZ_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        if (rsw::ide_wizards::RunPrerequisiteMsmAeroWizard(hWnd, self->astDoc)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[AEROWIZARD] Added Runtime Prerequisite / .MSM Merge Module to .rsip AST.\r\n");
                        }
                        return 0;
                    }
                    case IDM_WIZ_PROJECT:
                    case IDC_TB_WIZARD: {
                        std::string genText;
                        bool autoVal = false;
                        if (rsw::ide_wizards::RunNewProjectAeroWizard(hWnd, genText, autoVal)) {
                            self->LoadProjectText(genText, {});
                            self->AppendConsole(L"[AEROWIZARD] Generated new .rsip project via Project Assistant.\r\n");
                            if (autoVal) self->RunValidation();
                        }
                        return 0;
                    }
                    case IDM_WIZ_FILE_ASSOC:
                    case IDC_FA_WIZ_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        if (rsw::ide_wizards::RunFileAssociationAeroWizard(hWnd, self->astDoc)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[AEROWIZARD] Added [FileAssociation.*] to .rsip AST.\r\n");
                        }
                        return 0;
                    }
                    case IDM_WIZ_COMPONENT:
                    case IDC_COMP_WIZ_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        if (rsw::ide_wizards::RunComponentKeyPathAeroWizard(hWnd, self->astDoc)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[AEROWIZARD] Added [Component.*] & KeyPath to .rsip AST.\r\n");
                        }
                        return 0;
                    }
                    case IDM_WIZ_RELEASE:
                    case IDC_ENT_REL_WIZ_BTN: {
                        rsw::ide_wizards::ReleaseWizardResult res{};
                        if (rsw::ide_wizards::RunReleaseMstMspAeroWizard(hWnd, res)) {
                            if (res.modeIdx == 2) {
                                self->InvokeInstBuild(false);
                                self->ExportVpatHtml();
                            } else {
                                const std::filesystem::path baseP = rsw::Utf8ToWide(res.baselineMsi);
                                const std::filesystem::path targP = rsw::Utf8ToWide(res.targetMsi);
                                const bool wantMst = (res.modeIdx == 0);
                                const auto outP = targP.parent_path() / (targP.stem().wstring() + (wantMst ? L".mst" : L".msp"));
                                std::string err;
                                const bool ok = wantMst
                                    ? rsw::GenerateMsiTransform(baseP, targP, outP, err)
                                    : rsw::GenerateMsiPatchPackage(baseP, targP, outP, err);
                                self->AppendConsole(ok ? (L"[AEROWIZARD] Emitted " + outP.wstring() + L"\r\n")
                                                       : (L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n"));
                                if (ok && res.openInPrism) {
                                    const auto prismExe = self->ideExeDir / L"msiprism.exe";
                                    std::wstring arg = L"\"" + outP.wstring() + L"\"";
                                    ::ShellExecuteW(hWnd, L"open", prismExe.wstring().c_str(), arg.c_str(), nullptr, SW_SHOWNORMAL);
                                }
                            }
                        }
                        return 0;
                    }
                    case IDM_WIZ_INSERT_PRESET:
                    case IDC_MSI_INCLUDE_BTN:
                        self->InsertIncludePresetDialog();
                        return 0;

                    // --- File & Standard Views ---
                    case IDM_FILE_NEW:
                    case IDC_TB_NEW:
                        self->LoadProjectText(BuildStarterRsipTemplate(), {});
                        return 0;
                    case IDM_FILE_OPEN:
                    case IDC_TB_OPEN: {
                        std::wstring p;
                        if (self->PickPathDialog(
                                false,
                                L"Installer Studio Projects & Suites (*.rsip;*.rsis;*.rsii)\0*.rsip;*.rsis;*.rsii;*.rssp;*.rsw\0All Files\0*.*\0",
                                p)) {
                            self->OpenProjectOrSuiteFile(p);
                        }
                        return 0;
                    }
                    case IDM_FILE_SAVE:
                    case IDC_TB_SAVE:
                        self->SaveCurrentProject();
                        return 0;
                    case IDM_FILE_EXIT:
                        ::DestroyWindow(hWnd);
                        return 0;

                    case IDC_PKG_NEW_GUID:
                        ::SetWindowTextW(self->hPkgGuid, rsw::Utf8ToWide(rsw::GenerateRandomMsiGuid()).c_str());
                        return 0;
                    case IDC_PKG_APPLY:
                        self->ApplyPackageViewToAst();
                        return 0;

                    // --- View 1: Runtime Prerequisites & Merge Modules (.MSM) Tree ---
                    case IDC_PR_BROWSE_BTN: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"Runtimes & Merge Modules (*.exe;*.msi;*.msm)\0*.exe;*.msi;*.msm\0All Files (*.*)\0*.*\0", p)) {
                            ::SetWindowTextW(self->hPrSource, p.c_str());
                        }
                        return 0;
                    }
                    case IDC_PR_ADD_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const char* presetIds[] = {
                            "DotNet10Desktop", "DotNet10AspNet", "DotNet9Desktop",
                            "DotNet8Desktop", "EdgeWebView2", "VcRedist2022", "Custom"
                        };
                        const char* distNames[] = {"download", "embedded", "side-by-side"};
                        const int pIdx = std::clamp(static_cast<int>(::SendMessageW(self->hPrPreset, CB_GETCURSEL, 0, 0)), 0, 6);
                        const int dIdx = std::clamp(static_cast<int>(::SendMessageW(self->hPrDist, CB_GETCURSEL, 0, 0)), 0, 2);
                        const std::string preset = presetIds[pIdx];
                        const std::string dist   = distNames[dIdx];
                        const std::string pid    = (preset == "Custom")
                            ? ("Runtime" + std::to_string(self->astDoc.GetNamedSectionIds("Prerequisite").size() + 1))
                            : preset;
                        const std::string src = ReadCtlUtf8(self->hPrSource);
                        const std::string reb = ReadCtlUtf8(self->hPrRebootCodes);

                        self->astDoc.SetString("Prerequisite", pid, "Preset",       preset);
                        self->astDoc.SetString("Prerequisite", pid, "Distribution", dist);
                        if (dist == "embedded" || (dist == "side-by-side" && src.rfind("http", 0) != 0)) {
                            self->astDoc.SetString("Prerequisite", pid, "SourceFile", src);
                        } else if (preset == "Custom" && !src.empty()) {
                            self->astDoc.SetString("Prerequisite", pid, "Url", src);
                        }
                        if (!reb.empty()) {
                            self->astDoc.SetString("Prerequisite", pid, "RebootExitCodes", reb);
                        }
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Added [Prerequisite." + rsw::Utf8ToWide(pid) + L"] (" + rsw::Utf8ToWide(dist) + L").\r\n");
                        return 0;
                    }
                    case IDC_PR_ADD_MSM_BTN: {
                        std::wstring msmPath;
                        if (!self->PickPathDialog(false, L"Windows Installer Merge Modules (*.msm)\0*.msm\0All Files (*.*)\0*.*\0", msmPath)) {
                            return 0;
                        }
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const std::string mid = "Module" + std::to_string(self->astDoc.GetNamedSectionIds("MergeModule").size() + 1);
                        self->astDoc.SetString("MergeModule", mid, "Source",     rsw::WideToUtf8(msmPath));
                        self->astDoc.SetString("MergeModule", mid, "Feature",    "Main");
                        self->astDoc.SetString("MergeModule", mid, "TargetRoot", "INSTALLDIR");
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Added [MergeModule." + rsw::Utf8ToWide(mid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_PR_REMOVE_BTN: {
                        if (self->selectedPrRef.prefix.empty() || self->selectedPrRef.id.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        RemoveAstSection(self->astDoc, self->selectedPrRef.prefix, self->selectedPrRef.id);
                        const std::string removed = self->selectedPrRef.prefix + "." + self->selectedPrRef.id;
                        self->selectedPrRef = {};
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Removed [" + rsw::Utf8ToWide(removed) + L"].\r\n");
                        return 0;
                    }

                    // --- View 2: Dedicated Features Hierarchy (`All Features` Tree + Up/Down + Save/Delete) ---
                    case IDC_FT_NEW_ROOT_BTN:
                    case IDC_FT_NEW_SUB_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const std::string newId = "Feature" + std::to_string(self->astDoc.GetNamedSectionIds("Feature").size() + 1);
                        self->astDoc.SetString("Feature", newId, "Title",       "Optional Program Feature");
                        self->astDoc.SetString("Feature", newId, "Description", "Additional components installed when this feature is selected.");
                        self->astDoc.SetString("Feature", newId, "Inclusion",   "typical");
                        if (id == IDC_FT_NEW_SUB_BTN && !self->selectedFtId.empty()) {
                            self->astDoc.SetString("Feature", newId, "Parent", self->selectedFtId);
                        }
                        self->selectedFtId = newId;
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Created [Feature." + rsw::Utf8ToWide(newId) + L"] (Typical & Complete).\r\n");
                        return 0;
                    }
                    case IDC_FT_SAVE_BTN: {
                        const std::string fid = ReadCtlUtf8(self->hFtId);
                        if (fid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const int tierIdx = std::clamp(static_cast<int>(::SendMessageW(self->hFtLevel, CB_GETCURSEL, 0, 0)), 0, 3);
                        const auto tier   = static_cast<rsw::FeatureInclusionTier>(tierIdx);
                        self->astDoc.SetString("Feature", fid, "Title",     ReadCtlUtf8(self->hFtTitle));
                        self->astDoc.SetString("Feature", fid, "Inclusion", std::string(rsw::FeatureInclusionToken(tier)));
                        RemoveAstKey(self->astDoc, "Feature", fid, "Level");
                        const std::string desc = ReadCtlUtf8(self->hFtDesc);
                        if (!desc.empty()) self->astDoc.SetString("Feature", fid, "Description", desc);
                        else               RemoveAstKey(self->astDoc, "Feature", fid, "Description");

                        const std::string par = ReadCtlUtf8(self->hFtParent);
                        if (!par.empty() && !rsw::EqualsIgnoreCase(par, fid)) {
                            self->astDoc.SetString("Feature", fid, "Parent", par);
                        } else {
                            RemoveAstKey(self->astDoc, "Feature", fid, "Parent");
                        }

                        const bool req = (::SendMessageW(self->hFtReqChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        if (req) self->astDoc.SetString("Feature", fid, "DisallowAbsent", "true");
                        else     RemoveAstKey(self->astDoc, "Feature", fid, "DisallowAbsent");

                        self->selectedFtId = fid;
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Feature." + rsw::Utf8ToWide(fid) + L"] (" +
                                            rsw::Utf8ToWide(rsw::FeatureInclusionBadge(tier)) + L").\r\n");
                        return 0;
                    }
                    case IDC_FT_DELETE_BTN: {
                        const std::string fid = ReadCtlUtf8(self->hFtId);
                        if (fid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        RemoveAstSection(self->astDoc, "Feature", fid);
                        self->selectedFtId.clear();
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Deleted [Feature." + rsw::Utf8ToWide(fid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_FT_UP_BTN:
                    case IDC_FT_DOWN_BTN: {
                        const std::string fid = self->selectedFtId.empty() ? ReadCtlUtf8(self->hFtId) : self->selectedFtId;
                        if (fid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        if (SwapFeatureSectionOrder(self->astDoc, fid, (id == IDC_FT_UP_BTN) ? -1 : 1)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[AST] Reordered [Feature." + rsw::Utf8ToWide(fid) + L"] in SelectionTree sequence.\r\n");
                        }
                        return 0;
                    }

                    // --- View 3: Files & Folders (Dual Trees) ---
                    case IDC_PAY_ADD_FILE:
                        self->AddPayloadToSelectedDestFolder(false);
                        return 0;
                    case IDC_PAY_ADD_FOLDER:
                        self->AddPayloadToSelectedDestFolder(true);
                        return 0;
                    case IDC_DEST_NEW_SUBDIR:
                        self->CreateExplicitDestinationSubfolder();
                        return 0;
                    case IDC_PAY_REMOVE:
                        self->RemoveSelectedDestItem();
                        return 0;

                    // --- View 4: Components & KeyPaths ---
                    case IDC_FEAT_SAVE_BTN: {
                        const std::string fid = ReadCtlUtf8(self->hFeatId);
                        if (fid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const int tierIdx = std::clamp(static_cast<int>(::SendMessageW(self->hFeatLevel, CB_GETCURSEL, 0, 0)), 0, 3);
                        const auto tier   = static_cast<rsw::FeatureInclusionTier>(tierIdx);
                        self->astDoc.SetString("Feature", fid, "Title",     ReadCtlUtf8(self->hFeatTitle));
                        self->astDoc.SetString("Feature", fid, "Inclusion", std::string(rsw::FeatureInclusionToken(tier)));
                        RemoveAstKey(self->astDoc, "Feature", fid, "Level");
                        const std::string par = ReadCtlUtf8(self->hFeatParent);
                        if (!par.empty() && !rsw::EqualsIgnoreCase(par, fid)) {
                            self->astDoc.SetString("Feature", fid, "Parent", par);
                        } else {
                            RemoveAstKey(self->astDoc, "Feature", fid, "Parent");
                        }
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Feature." + rsw::Utf8ToWide(fid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_COMP_SAVE_BTN: {
                        const std::string cid = ReadCtlUtf8(self->hCompId);
                        if (cid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const int rtIdx = std::clamp(static_cast<int>(::SendMessageW(self->hCompRoot, CB_GETCURSEL, 0, 0)), 0, 8);
                        const int kpIdx = std::clamp(static_cast<int>(::SendMessageW(self->hCompKpKind, CB_GETCURSEL, 0, 0)), 0, 3);
                        const char* kpStrs[] = {"auto", "file", "registry", "folder"};
                        self->astDoc.SetString("Component", cid, "Feature",     ReadCtlUtf8(self->hCompFeat));
                        self->astDoc.SetString("Component", cid, "TargetRoot",  rsw::ide_wizards::kRootIds[rtIdx]);
                        const std::string sub = ReadCtlUtf8(self->hCompSubdir);
                        if (!sub.empty()) self->astDoc.SetString("Component", cid, "TargetSubdir", sub);
                        else              RemoveAstKey(self->astDoc, "Component", cid, "TargetSubdir");
                        self->astDoc.SetString("Component", cid, "Guid",        "*");
                        self->astDoc.SetString("Component", cid, "KeyPathKind", kpStrs[kpIdx]);
                        const std::string kpt = ReadCtlUtf8(self->hCompKpTarget);
                        if (!kpt.empty()) self->astDoc.SetString("Component", cid, "KeyPathTarget", kpt);
                        else              RemoveAstKey(self->astDoc, "Component", cid, "KeyPathTarget");
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Component." + rsw::Utf8ToWide(cid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_COMP_DEL_BTN: {
                        const std::string cid = ReadCtlUtf8(self->hCompId);
                        if (cid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        RemoveAstSection(self->astDoc, "Component", cid);
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Deleted [Component." + rsw::Utf8ToWide(cid) + L"].\r\n");
                        return 0;
                    }

                    // --- View 5: `RegEdit`-Style Registry Editor ---
                    case IDC_REG_ADD_KEY_BTN: {
                        const std::string sub = ReadCtlUtf8(self->hRegSubEdit);
                        if (sub.empty()) return 0;
                        const std::string fullKey = self->selectedRegKeyPath.empty() ? sub : (self->selectedRegKeyPath + "\\" + sub);
                        self->extraSessionRegKeys.push_back({self->selectedRegHive, fullKey});
                        self->selectedRegKeyPath = fullKey;
                        ::SetWindowTextW(self->hRegKey, rsw::Utf8ToWide(fullKey).c_str());
                        self->RebuildRegistryTrees(self->ResolvedEffectiveDoc());
                        return 0;
                    }
                    case IDC_REG_DEL_KEY_BTN: {
                        if (self->selectedRegKeyPath.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const std::string pfx = self->selectedRegKeyPath + "\\";
                        for (const auto& rid : self->astDoc.GetNamedSectionIds("Registry")) {
                            const std::string hv = self->astDoc.GetString("Registry", rid, "Root", "HKLM");
                            const std::string ky = self->astDoc.GetString("Registry", rid, "Key");
                            if (rsw::EqualsIgnoreCase(hv, self->selectedRegHive) &&
                                (rsw::EqualsIgnoreCase(ky, self->selectedRegKeyPath) ||
                                 (ky.size() > pfx.size() && rsw::EqualsIgnoreCase(ky.substr(0, pfx.size()), pfx)))) {
                                RemoveAstSection(self->astDoc, "Registry", rid);
                            }
                        }
                        self->selectedRegKeyPath.clear();
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Removed registry key subtree under " + rsw::Utf8ToWide(self->selectedRegHive) + L".\r\n");
                        return 0;
                    }
                    case IDC_REG_SET_VAL_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const char* hiveCodes[] = {"HKLM", "HKCU", "HKCR", "HKU", "HKMU"};
                        const char* typeNames[] = {"string", "expand_string", "dword"};
                        const int hIdx = std::clamp(static_cast<int>(::SendMessageW(self->hRegRootCombo, CB_GETCURSEL, 0, 0)), 0, 4);
                        const int tIdx = std::clamp(static_cast<int>(::SendMessageW(self->hRegTypeCombo, CB_GETCURSEL, 0, 0)), 0, 2);

                        const std::string hv  = hiveCodes[hIdx];
                        const std::string ky  = ReadCtlUtf8(self->hRegKey);
                        const std::string nm  = ReadCtlUtf8(self->hRegName).empty() ? "@" : ReadCtlUtf8(self->hRegName);
                        const std::string val = ReadCtlUtf8(self->hRegVal);
                        const std::string ft  = ReadCtlUtf8(self->hRegFeatEdit).empty() ? "Main" : ReadCtlUtf8(self->hRegFeatEdit);
                        const std::string cmp = ReadCtlUtf8(self->hRegComp);
                        if (ky.empty()) return 0;

                        std::string rid = self->selectedRegSectionId;
                        if (rid.empty()) {
                            // Check if `(hv, ky, nm)` already exists so we update in-place cleanly!
                            for (const auto& existing : self->astDoc.GetNamedSectionIds("Registry")) {
                                if (rsw::EqualsIgnoreCase(self->astDoc.GetString("Registry", existing, "Root", "HKLM"), hv) &&
                                    rsw::EqualsIgnoreCase(self->astDoc.GetString("Registry", existing, "Key"), ky) &&
                                    rsw::EqualsIgnoreCase(self->astDoc.GetString("Registry", existing, "Name", "@"), nm)) {
                                    rid = existing;
                                    break;
                                }
                            }
                            if (rid.empty()) {
                                rid = "Reg" + std::to_string(self->astDoc.GetNamedSectionIds("Registry").size() + 1);
                            }
                        }

                        self->astDoc.SetString("Registry", rid, "Root",    hv);
                        self->astDoc.SetString("Registry", rid, "Key",     ky);
                        self->astDoc.SetString("Registry", rid, "Name",    nm);
                        self->astDoc.SetString("Registry", rid, "Type",    typeNames[tIdx]);
                        self->astDoc.SetString("Registry", rid, "Value",   val);
                        self->astDoc.SetString("Registry", rid, "Feature", ft);
                        if (!cmp.empty()) self->astDoc.SetString("Registry", rid, "Component", cmp);
                        else              RemoveAstKey(self->astDoc, "Registry", rid, "Component");

                        self->selectedRegHive    = hv;
                        self->selectedRegKeyPath = ky;
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Registry." + rsw::Utf8ToWide(rid) + L"] -> " + rsw::Utf8ToWide(hv + "\\" + ky + " :: " + nm) + L".\r\n");
                        return 0;
                    }
                    case IDC_REG_DEL_VAL_BTN: {
                        if (self->selectedRegSectionId.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        RemoveAstSection(self->astDoc, "Registry", self->selectedRegSectionId);
                        self->selectedRegSectionId.clear();
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Deleted selected registry value.\r\n");
                        return 0;
                    }
                    case IDC_REG_IMPORT_BTN: {
                        std::wstring regFile;
                        if (!self->PickPathDialog(false, L"Windows Registry Scripts (*.reg)\0*.reg\0All Files (*.*)\0*.*\0", regFile)) {
                            return 0;
                        }
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        int imported = 0;
                        const std::string targetFeat = self->activeRegFeatureFilter.empty() ? "Main" : self->activeRegFeatureFilter;
                        if (ImportRegFileIntoAst(regFile, self->astDoc, targetFeat, imported)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[REGISTRY] Imported " + std::to_wstring(imported) + L" registry value(s) from " + regFile + L".\r\n");
                        } else {
                            self->AppendConsole(L"[WARN] No standard registry values found in " + regFile + L".\r\n");
                        }
                        return 0;
                    }

                    // --- View 6: File Associations Tree ---
                    case IDC_FA_ADD_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        std::string aid = self->selectedFaId;
                        const std::string ext = ReadCtlUtf8(self->hFaExt);
                        if (aid.empty() || !rsw::EqualsIgnoreCase(self->astDoc.GetString("FileAssociation", aid, "Extension"), ext)) {
                            aid = "Assoc" + std::to_string(self->astDoc.GetNamedSectionIds("FileAssociation").size() + 1);
                        }
                        self->astDoc.SetString("FileAssociation", aid, "Extension",   ext);
                        self->astDoc.SetString("FileAssociation", aid, "ProgId",      ReadCtlUtf8(self->hFaProgId));
                        self->astDoc.SetString("FileAssociation", aid, "Description", ReadCtlUtf8(self->hFaDesc));
                        self->astDoc.SetString("FileAssociation", aid, "TargetFile",  ReadCtlUtf8(self->hFaTarget));
                        self->astDoc.SetString("FileAssociation", aid, "Verbs",       ReadCtlUtf8(self->hFaVerbs));
                        self->astDoc.SetString("FileAssociation", aid, "Advertise",
                                               (::SendMessageW(self->hFaAdvChk, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "true" : "false");
                        self->selectedFaId = aid;
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [FileAssociation." + rsw::Utf8ToWide(aid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_FA_REMOVE_BTN: {
                        if (self->selectedFaId.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        RemoveAstSection(self->astDoc, "FileAssociation", self->selectedFaId);
                        self->selectedFaId.clear();
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Removed selected [FileAssociation.*].\r\n");
                        return 0;
                    }

                    // --- View 7: Application Shortcuts Tree ---
                    case IDC_SC_SAVE_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        std::string sid = ReadCtlUtf8(self->hScIdEdit);
                        if (sid.empty()) sid = "Link" + std::to_string(self->astDoc.GetNamedSectionIds("Shortcut").size() + 1);
                        const bool isDesk = (::SendMessageW(self->hScLocCombo, CB_GETCURSEL, 0, 0) == 1);
                        self->astDoc.SetString("Shortcut", sid, "Name",       ReadCtlUtf8(self->hScName));
                        self->astDoc.SetString("Shortcut", sid, "TargetFile", ReadCtlUtf8(self->hScTarget));
                        self->astDoc.SetString("Shortcut", sid, "Location",   isDesk ? "Desktop" : "StartMenu");
                        const std::string args = ReadCtlUtf8(self->hScArgs);
                        if (!args.empty()) self->astDoc.SetString("Shortcut", sid, "Arguments", args);
                        else               RemoveAstKey(self->astDoc, "Shortcut", sid, "Arguments");
                        const std::string ft = ReadCtlUtf8(self->hScFeat);
                        if (!ft.empty()) self->astDoc.SetString("Shortcut", sid, "Feature", ft);
                        self->selectedScId = sid;
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Shortcut." + rsw::Utf8ToWide(sid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_SC_DEL_BTN: {
                        const std::string sid = self->selectedScId.empty() ? ReadCtlUtf8(self->hScIdEdit) : self->selectedScId;
                        if (sid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        RemoveAstSection(self->astDoc, "Shortcut", sid);
                        self->selectedScId.clear();
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Deleted [Shortcut." + rsw::Utf8ToWide(sid) + L"].\r\n");
                        return 0;
                    }

                    // --- View 8: C++ Wizard Designer ---
                    case IDC_UI_AUTO_WCAG: {
                        const COLORREF cur  = rsw::ui::ParseHexColor(ReadCtlUtf8(self->hUiPrim), RGB(37, 99, 235));
                        const COLORREF safe = rsw::wcag::SnapAccentToAccessibleBand(cur);
                        const std::string hex = rsw::wcag::FormatHexColor(safe);
                        ::SetWindowTextW(self->hUiPrim, rsw::Utf8ToWide(hex).c_str());
                        double vsW = 0.0, vsD = 0.0;
                        rsw::wcag::MeetsDualThemeAccentContrast(safe, vsW, vsD);
                        wchar_t msgBuf[256]{};
                        std::swprintf(msgBuf, std::size(msgBuf),
                                      L"[WCAG 2.2 AA] Snapped Primary Accent to %ls (%.2f:1 vs #FFFFFF text, %.2f:1 vs #15203A Dark Card).\r\n",
                                      rsw::Utf8ToWide(hex).c_str(), vsW, vsD);
                        self->AppendConsole(msgBuf);
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    case IDC_UI_ICON_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"Icon Files (*.ico)\0*.ico\0", p)) ::SetWindowTextW(self->hUiIcon, p.c_str());
                        return 0;
                    }
                    case IDC_UI_ART_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"Artwork Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0", p)) ::SetWindowTextW(self->hUiArt, p.c_str());
                        return 0;
                    }
                    case IDC_UI_APPLY:
                        self->ApplyUiViewToAst();
                        return 0;

                    // --- View 9: MSI.DLL Dialog Designer ---
                    case IDC_MSI_ADD_DLG_BTN: {
                        const std::string did = ReadCtlUtf8(self->hMsiNewId);
                        if (did.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        self->astDoc.SetString("MsiDialog", did, "Title",       "[ProductName] Setup");
                        self->astDoc.SetString("MsiDialog", did, "Width",       "370");
                        self->astDoc.SetString("MsiDialog", did, "Height",      "270");
                        self->astDoc.SetString("MsiDialog", did, "Banner",      "true");
                        self->astDoc.SetString("MsiDialog", did, "HeaderTitle", ReadCtlUtf8(self->hMsiNewTitle));
                        self->astDoc.SetString("MsiDialog", did, "PrevDialog",  ReadCtlUtf8(self->hMsiNewAfter));
                        self->astDoc.SetString("MsiDialog", did, "NextDialog",  "VerifyReadyDlg");
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        const LRESULT idx = ::SendMessageW(self->hMsiDlgSelect, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(rsw::Utf8ToWide(did).c_str()));
                        if (idx != CB_ERR) ::SendMessageW(self->hMsiDlgSelect, CB_SETCURSEL, idx, 0);
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        self->AppendConsole(L"[MSI DIALOG] Added [MsiDialog." + rsw::Utf8ToWide(did) + L"] spliced into Dialog sequence.\r\n");
                        return 0;
                    }
                    case IDC_MSI_ADD_CTRL_BTN: {
                        const std::string cid = ReadCtlUtf8(self->hMsiCtrlId);
                        wchar_t dlgBuf[128]{};
                        ::GetWindowTextW(self->hMsiDlgSelect, dlgBuf, 128);
                        const std::string dlg = rsw::WideToUtf8(dlgBuf);
                        if (cid.empty() || dlg.empty()) return 0;

                        const char* tpNames[] = {"Text", "Edit", "PathEdit", "CheckBox", "RadioButtonGroup", "ComboBox", "GroupBox"};
                        const int tpIdx = std::clamp(static_cast<int>(::SendMessageW(self->hMsiCtrlType, CB_GETCURSEL, 0, 0)), 0, 6);

                        int vals[4] = {20, 74, 330, 18};
                        std::string xywh = ReadCtlUtf8(self->hMsiCtrlXywh);
                        for (char& ch : xywh) if (ch == ',') ch = ' ';
                        std::istringstream iss(xywh);
                        iss >> vals[0] >> vals[1] >> vals[2] >> vals[3];

                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        self->astDoc.SetString("MsiControl", cid, "Dialog", dlg);
                        self->astDoc.SetString("MsiControl", cid, "Type",   tpNames[tpIdx]);
                        self->astDoc.SetString("MsiControl", cid, "X",      std::to_string(vals[0]));
                        self->astDoc.SetString("MsiControl", cid, "Y",      std::to_string(vals[1]));
                        self->astDoc.SetString("MsiControl", cid, "Width",  std::to_string(vals[2]));
                        self->astDoc.SetString("MsiControl", cid, "Height", std::to_string(vals[3]));
                        const std::string prop = ReadCtlUtf8(self->hMsiCtrlProp);
                        if (!prop.empty()) self->astDoc.SetString("MsiControl", cid, "Property", prop);
                        const std::string txt = ReadCtlUtf8(self->hMsiCtrlText);
                        if (!txt.empty()) {
                            if (tpIdx == 4 || tpIdx == 5) self->astDoc.SetString("MsiControl", cid, "Items", txt);
                            else if (tpIdx == 1)          self->astDoc.SetString("MsiControl", cid, "DefaultValue", txt);
                            else                          self->astDoc.SetString("MsiControl", cid, "Text", txt);
                        }
                        self->SyncSourceEditorFromAst();
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        self->AppendConsole(L"[MSI CONTROL] Placed [MsiControl." + rsw::Utf8ToWide(cid) + L"] onto " + std::wstring(dlgBuf) + L".\r\n");
                        return 0;
                    }

                    // --- View 10: Signing, .MST & .MSP ---
                    case IDC_SIG_APPLY:
                        self->ApplySigningViewToAst();
                        return 0;
                    case IDC_ENT_BASE_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"MSI Databases (*.msi)\0*.msi\0", p)) ::SetWindowTextW(self->hEntBase, p.c_str());
                        return 0;
                    }
                    case IDC_ENT_TARG_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"MSI Databases (*.msi)\0*.msi\0", p)) ::SetWindowTextW(self->hEntTarg, p.c_str());
                        return 0;
                    }
                    case IDC_ENT_GEN_MST:
                    case IDC_ENT_GEN_MSP: {
                        const bool wantMst = (id == IDC_ENT_GEN_MST);
                        const std::filesystem::path baseP = rsw::Utf8ToWide(ReadCtlUtf8(self->hEntBase));
                        const std::filesystem::path targP = rsw::Utf8ToWide(ReadCtlUtf8(self->hEntTarg));
                        const auto outP = targP.parent_path() / (targP.stem().wstring() + (wantMst ? L".mst" : L".msp"));
                        std::string err;
                        const bool ok = wantMst
                            ? rsw::GenerateMsiTransform(baseP, targP, outP, err)
                            : rsw::GenerateMsiPatchPackage(baseP, targP, outP, err);
                        self->AppendConsole(ok ? (L"[OK] Emitted " + outP.wstring() + L"\r\n")
                                               : (L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n"));
                        return 0;
                    }

                    // --- Build & Compliance Commands ---
                    case IDM_BUILD_VALIDATE:
                    case IDC_TB_VALIDATE:
                        self->RunValidation();
                        return 0;
                    case IDM_BUILD_MSI:
                    case IDC_TB_BUILD_MSI:
                        self->InvokeInstBuild(false);
                        return 0;
                    case IDM_BUILD_MSM:
                    case IDC_TB_BUILD_MSM:
                        self->InvokeInstBuild(true);
                        return 0;
                    case IDM_BUILD_VPAT:
                    case IDC_TB_EXPORT_VPAT:
                        self->ExportVpatHtml();
                        return 0;
                    case IDM_BUILD_EMIT_AI: {
                        const auto outDir = self->currentFilePath.empty() ? (self->ideExeDir / L"specs") : (self->currentFilePath.parent_path() / L"specs");
                        std::string err;
                        if (rsw::EmitAiSpecifications(outDir, err)) self->AppendConsole(L"[OK] Emitted llms.txt & SKILL.md.\r\n");
                        return 0;
                    }
                    case IDM_LAUNCH_MSIPRISM:
                    case IDC_TB_MSIPRISM: {
                        const auto prismExe = self->ideExeDir / L"msiprism.exe";
                        ::ShellExecuteW(hWnd, L"open", prismExe.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return 0;
                    }
                    case IDM_OPEN_OUTDIR: {
                        const auto outDir = self->currentFilePath.empty() ? self->ideExeDir : (self->currentFilePath.parent_path() / L"out");
                        ::ShellExecuteW(hWnd, L"open", outDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return 0;
                    }
                }
                break;
            }
            case WM_HELP:
                self->LaunchContextHelp();
                return TRUE;
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
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        ::LocalFree(argv);
    }

    // Headless CI smoke test verifying AST round-trip, `@include` resolution, feature reordering, & VPAT export!
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--headless-smoke-test") {
            if (i + 1 >= args.size()) return 2;
            rsw::RswManifestDocument doc;
            std::string err;
            if (!rsw::LoadManifestFile(args[i + 1], doc, err)) return 1;
            const std::string origVer = doc.GetString("Package", "", "Version");
            doc.SetString("Package", "", "Version", "1.2.3");
            doc.SetString("UI", "", "AccentPrimaryHex", "#0EA5E9");
            const std::string roundTrip = doc.Serialize();
            if (roundTrip.find(';') == std::string::npos) return 3;
            doc.SetString("Package", "", "Version", origVer);

            rsw::RswManifestDocument resolved;
            if (!rsw::ResolveManifestIncludesAndVars(doc, {}, resolved, err)) return 6;

            std::error_code ec;
            const auto tmpVpatDir = std::filesystem::temp_directory_path(ec) / L"rsip-ide-vpat-smoke";
            std::filesystem::path vpatHtml;
            if (!rsw::EmitProjectInstallerVpatHtml(resolved, tmpVpatDir, vpatHtml, err) ||
                !std::filesystem::exists(vpatHtml)) {
                return 5;
            }
            std::filesystem::remove_all(tmpVpatDir, ec);

            const auto rep = rsw::ValidateManifestDocument(resolved, "arm64");
            return rep.IsOk() ? 0 : 4;
        }
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gdiPlusSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES};
    ::InitCommonControlsEx(&icc);

    InstallerStudioIde ide{};
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--export-screenshots") {
            rsw::ui::ForcedThemeOverride() = 1;
            break;
        }
    }
    ide.theme.Refresh();

    wchar_t exeBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
    ide.ideExeDir = std::filesystem::path(exeBuf).parent_path();

    constexpr const wchar_t* kCls = L"RaggieSoftInstallerStudioIdeWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &InstallerStudioIde::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = ::GetDpiForSystem();
    const int winW = rsw::ui::ScalePx(1340, dpi, ide.theme.textScalePercent);
    const int winH = rsw::ui::ScalePx(800,  dpi, ide.theme.textScalePercent);
    const int posX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
    const int posY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - winH) / 2);

    HMENU hMenu = ide.BuildMenuBar();
    HWND hWnd = ::CreateWindowExW(
        0, kCls, L"RaggieSoft Installer Studio",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, winW, winH,
        nullptr, hMenu, hInstance, &ide);
    if (!hWnd) {
        ::CoUninitialize();
        return 1;
    }

    std::filesystem::path exportShotDir;
    std::filesystem::path initialProjectArg;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[i + 1];
            rsw::ui::ForcedThemeOverride() = 1;
            ++i;
        } else if (std::filesystem::exists(args[i])) {
            initialProjectArg = args[i];
        }
    }

    if (!initialProjectArg.empty()) {
        ide.OpenProjectOrSuiteFile(initialProjectArg);
    } else {
        ide.LoadProjectText(BuildStarterRsipTemplate(), {});
    }
    ide.AppendConsole(L"RaggieSoft Installer Studio v" + std::wstring(rsw::kVersionStringWide) +
                      L" ready. Press F1 for the Offline C++ Help Viewer, Ctrl+W for the Aero Project Assistant, or F5 to build.\r\n");

    ACCEL accels[] = {
        {FVIRTKEY,            VK_F1, IDM_HELP_TOPICS},
        {FCONTROL | FVIRTKEY, 'W',   IDM_WIZ_PROJECT},
        {FCONTROL | FVIRTKEY, 'N',   IDM_FILE_NEW},
        {FCONTROL | FVIRTKEY, 'O',   IDM_FILE_OPEN},
        {FCONTROL | FVIRTKEY, 'S',   IDM_FILE_SAVE},
        {FVIRTKEY,            VK_F7, IDM_BUILD_VALIDATE},
        {FVIRTKEY,            VK_F5, IDM_BUILD_MSI},
        {FVIRTKEY,            VK_F6, IDM_BUILD_MSM}
    };
    HACCEL hAccel = ::CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);

    if (!exportShotDir.empty()) {
        auto snapView = [&](int viewIdx, const wchar_t* fileName) {
            ide.SelectIdeView(viewIdx);
            return rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / fileName);
        };
        const bool ok1 = snapView(VIEW_FEATURES,     L"ide-features-tree.png");
        ide.SelectIdeView(VIEW_REGISTRY);
        ide.SelectRegistryKeyInTree("HKLM", "Software\\RaggieSoft\\Installer Studio");
        const bool ok2 = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"ide-registry-tree.png");
        const bool ok3 = snapView(VIEW_DEST_FOLDERS, L"ide-destination-folders.png");
        const bool ok4 = snapView(VIEW_PREREQS,      L"ide-runtimes-msm.png");
        ::SendMessageW(ide.hUiLayout, CB_SETCURSEL, static_cast<WPARAM>(rsw::WizardStyleKind::FullScreenTeal), 0);
        ::SendMessageW(ide.hUiSetupTypes, CB_SETCURSEL, 1, 0); // `win95_classic` (`Typical/Compact/Complete/Custom`)
        ::SendMessageW(ide.hUiPreviewStep, CB_SETCURSEL, 1, 0);
        const bool ok5 = snapView(VIEW_CPP_WIZARD,   L"ide-wizard-designer.png");
        if (hAccel) ::DestroyAcceleratorTable(hAccel);
        ::DestroyWindow(hWnd);
        ::CoUninitialize();
        return (ok1 && ok2 && ok3 && ok4 && ok5) ? 0 : 2;
    }

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::TranslateAcceleratorW(hWnd, hAccel, &msg) && !::IsDialogMessageW(hWnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (hAccel) ::DestroyAcceleratorTable(hAccel);
    ::CoUninitialize();
    return 0;
}
