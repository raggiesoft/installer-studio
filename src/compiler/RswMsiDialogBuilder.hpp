// ================================================================================
// FILE: src/compiler/RswMsiDialogBuilder.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How do traditional Windows Installer (`MSI.DLL`) dialogs work when someone
//   double-clicks `Product.msi` directly without `Setup.exe`?
//     Inside every `.msi` database, `msiexec.exe` reads relational SQL tables:
//       - `Dialog`           : Window dimensions (`370 x 270` dialog units) & title
//       - `Control`          : Buttons, text, `RadioButtonGroup`, `SelectionTree`,
//                              `PathEdit`, `DirectoryList`, `CheckBox`, `ProgressBar`
//       - `RadioButton`      : Individual choices inside a `RadioButtonGroup`
//       - `ControlEvent`     : Navigation (`NewDialog`, `SpawnDialog`, `EndDialog`)
//                              and state mutations (`AddLocal`, `SetTargetPath`,
//                              `ReinstallMode`, `Reinstall`, `Remove`)
//       - `ControlCondition` : Dynamic Enable / Disable / Hide / Show rules
//       - `EventMapping`     : Live progress bar & `SelectionTree` description wiring
//
//   What does this header build into `Product.msi`?
//     1. **`MsiDialogPreset = express_custom` (Default)**:
//        Brings our full signature **Express / Compact / Custom + SelectionTree +
//        Folder BrowseDlg + Shortcuts & File Associations + Office-Style Quick
//        Repair vs. Full Reinstall + Safe Uninstall** workflow straight into native
//        `MSI.DLL` SQL tables!
//     2. **`MsiDialogPreset = minimal`**:
//        Streamlined 3-screen `WelcomeDlg -> LicenseAgreementDlg -> InstallDirDlg`.
//     3. **Custom `[MsiDialog.<id>]` & `[MsiControl.<id>]` Authoring**:
//        Compiles author-defined dialogs and controls from `.rsip` or `@include`
//        `.rsii` templates—automatically wiring `Back`/`Next` navigation AND adding
//        bound uppercase properties to `SecureCustomProperties`!
// ================================================================================

#pragma once

#include "../common/RswManifestAst.hpp"
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rsw {

struct MsiUiBuildOptions {
    bool        perMachine          = true;
    bool        hasLicensePage      = false;
    bool        offerLaunchOnFinish = true;
    std::string mainExeFileKey;
    std::string licenseRtfContent;
    std::string welcomeHeadline;
    std::string bannerSubtitle;
    std::string installBtnText      = "&Install";
    std::string launchChkText       = "&Launch application when setup finishes";
    std::string expressDesc;
    std::string compactDesc;
    std::string customDesc;
};

/// Populates all Windows Installer internal UI tables (`TextStyle`, `Dialog`, `Control`,
/// `RadioButton`, `ComboBox`, `ListBox`, `ControlEvent`, `ControlCondition`, `EventMapping`,
/// plus custom `[MsiDialog.*]` & `[MsiControl.*]` definitions!).
template <typename InsertRowFn>
inline bool PopulateMsiInternalUiTables(
    MSIHANDLE hDb,
    const RswManifestDocument& doc,
    const MsiUiBuildOptions& opt,
    InsertRowFn&& insertRow,
    std::set<std::string>& ioSecureCustomProps,
    std::string& outError)
{
    using SqlVal = std::variant<std::monostate, int, std::string>;

    // 1. Standard Wizard 97 Typography (`TextStyle` table) & Complete `UIText` Localization Strings!
    // Populating `UIText` ensures native `MSI.DLL` `SelectionTree` popup menus (`MenuLocal`,
    // `MenuAllLocal`, `MenuAbsent`, etc.) and live Feature Details byte-cost summaries never
    // print `"!MenuLocal is missing from the UIText Table!"` during standalone `.msi` execution!
    insertRow(hDb, "TextStyle", {"TextStyle", "FaceName", "Size", "Color", "StyleBits"},
              std::vector<SqlVal>{std::string("DlgFont8"),      std::string("Segoe UI"), 9,  0, 0}, outError);
    insertRow(hDb, "TextStyle", {"TextStyle", "FaceName", "Size", "Color", "StyleBits"},
              std::vector<SqlVal>{std::string("DlgFontBold8"),  std::string("Segoe UI"), 9,  0, 1}, outError);
    insertRow(hDb, "TextStyle", {"TextStyle", "FaceName", "Size", "Color", "StyleBits"},
              std::vector<SqlVal>{std::string("VerdanaBold13"), std::string("Segoe UI"), 12, 0, 1}, outError);

    static constexpr std::pair<const char*, const char*> kStandardUiTextRows[] = {
        {"AbsentPath",          ""},
        {"bytes",               "bytes"},
        {"Bytes",               "B"},
        {"GB",                  "GB"},
        {"KB",                  "KB"},
        {"MB",                  "MB"},
        {"MenuAbsent",          "Entire feature will be unavailable"},
        {"MenuAdvertise",       "Feature will be installed when required"},
        {"MenuAllCD",           "Entire feature will be installed to run from CD"},
        {"MenuAllLocal",        "Entire feature will be installed on local hard drive"},
        {"MenuAllNetwork",      "Entire feature will be installed to run from network"},
        {"MenuCD",              "Will be installed to run from CD"},
        {"MenuLocal",           "Will be installed on local hard drive"},
        {"MenuNetwork",         "Will be installed to run from network"},
        {"ScriptInProgress",    "Gathering required information..."},
        {"SelAbsentAbsent",     "This feature will not be installed."},
        {"SelAbsentAdvertise",  "This feature will be set to be installed when required."},
        {"SelAbsentCD",         "This feature will be installed to run from CD."},
        {"SelAbsentLocal",      "This feature will be installed on the local hard drive."},
        {"SelAbsentNetwork",    "This feature will be installed to run from the network."},
        {"SelAdvertiseAbsent",  "This feature will become unavailable."},
        {"SelAdvertiseAdvertise","Will be installed when required."},
        {"SelAdvertiseCD",      "This feature will be available to run from CD."},
        {"SelAdvertiseLocal",   "This feature will be installed on your local hard drive."},
        {"SelAdvertiseNetwork", "This feature will be available to run from the network."},
        {"SelCDAbsent",         "This feature will be uninstalled completely, and you won't be able to run it from CD."},
        {"SelCDAdvertise",      "This feature will change from run from CD state to set to be installed when required."},
        {"SelCDCD",             "This feature will remain to be run from CD."},
        {"SelCDLocal",          "This feature will change from run from CD state to be installed on the local hard drive."},
        {"SelChildCostNeg",     "This feature frees up [1] on your hard drive."},
        {"SelChildCostPos",     "This feature requires [1] on your hard drive."},
        {"SelCostPending",      "Compiling cost for this feature..."},
        {"SelLocalAbsent",      "This feature will be completely removed."},
        {"SelLocalAdvertise",   "This feature will be removed from your local hard drive, but will be set to be installed when required."},
        {"SelLocalCD",          "This feature will be removed from your local hard drive, but will be still available to run from CD."},
        {"SelLocalLocal",       "This feature will remain on your local hard drive."},
        {"SelLocalNetwork",     "This feature will be removed from your local hard drive, but will be still available to run from the network."},
        {"SelNetworkAbsent",    "This feature will be uninstalled completely, and you won't be able to run it from the network."},
        {"SelNetworkAdvertise", "This feature will change from run from network state to set to be installed when required."},
        {"SelNetworkLocal",     "This feature will change from run from network state to be installed on the local hard drive."},
        {"SelNetworkNetwork",   "This feature will remain to be run from the network."},
        {"SelParentCostNegNeg", "This feature frees up [1] on your hard drive. It has [2] of [3] subfeatures selected. The subfeatures free up [4] on your hard drive."},
        {"SelParentCostNegPos", "This feature frees up [1] on your hard drive. It has [2] of [3] subfeatures selected. The subfeatures require [4] on your hard drive."},
        {"SelParentCostPosNeg", "This feature requires [1] on your hard drive. It has [2] of [3] subfeatures selected. The subfeatures free up [4] on your hard drive."},
        {"SelParentCostPosPos", "This feature requires [1] on your hard drive. It has [2] of [3] subfeatures selected. The subfeatures require [4] on your hard drive."},
        {"TimeRemaining",       "Time remaining: {[1] minutes }{[2] seconds}"},
        {"VolumeCostAvailable", "Available"},
        {"VolumeCostDifference","Differences"},
        {"VolumeCostRequired",  "Required"},
        {"VolumeCostSize",      "Disk Size"},
        {"VolumeCostVolume",    "Volume"}
    };
    for (const auto& [uiKey, uiVal] : kStandardUiTextRows) {
        insertRow(hDb, "UIText", {"Key", "Text"},
                  std::vector<SqlVal>{std::string(uiKey), std::string(uiVal)}, outError);
    }

    // MSI Control attribute bitmasks
    constexpr int kVisEn      = 3;       // Visible | Enabled
    constexpr int kVisTrans   = 65539;   // Visible | Enabled | Transparent (`0x00010003`)
    constexpr int kBmpFixed   = 1048579; // Visible | Enabled | FixedSize (`0x00100003`)
    constexpr int kSunken     = 7;       // Visible | Enabled | Sunken
    constexpr int kHasBorder  = 3;       // GroupBox / RadioButtonGroup standard
    const int installBtnAttr  = opt.perMachine ? (kVisEn | 0x00800000 /* ElevationShield */) : kVisEn;

    auto addDlg = [&](std::string id, int w, int h, int attr, std::string title,
                      std::string first, std::string defBtn, std::string cancelBtn) {
        insertRow(hDb, "Dialog",
                  {"Dialog", "HCentering", "VCentering", "Width", "Height", "Attributes", "Title", "Control_First", "Control_Default", "Control_Cancel"},
                  std::vector<SqlVal>{id, 50, 50, w, h, attr, title, first, defBtn, cancelBtn}, outError);
    };
    auto addCtrl = [&](std::string dlg, std::string ctrl, std::string type,
                       int x, int y, int w, int h, int attr,
                       SqlVal prop, SqlVal text, SqlVal next) {
        insertRow(hDb, "Control",
                  {"Dialog_", "Control", "Type", "X", "Y", "Width", "Height", "Attributes", "Property", "Text", "Control_Next", "Help"},
                  std::vector<SqlVal>{dlg, ctrl, type, x, y, w, h, attr, prop, text, next, std::monostate{}}, outError);
    };
    auto addEvt = [&](std::string dlg, std::string ctrl, std::string evt, std::string arg, std::string cond, int ord) {
        insertRow(hDb, "ControlEvent",
                  {"Dialog_", "Control_", "Event", "Argument", "Condition", "Ordering"},
                  std::vector<SqlVal>{dlg, ctrl, evt, arg, cond, ord}, outError);
    };
    auto addCond = [&](std::string dlg, std::string ctrl, std::string action, std::string cond) {
        insertRow(hDb, "ControlCondition",
                  {"Dialog_", "Control_", "Action", "Condition"},
                  std::vector<SqlVal>{dlg, ctrl, action, cond}, outError);
    };
    auto addRadio = [&](std::string prop, int order, std::string val, int x, int y, int w, int h, std::string text) {
        insertRow(hDb, "RadioButton",
                  {"Property", "Order", "Value", "X", "Y", "Width", "Height", "Text", "Help"},
                  std::vector<SqlVal>{prop, order, val, x, y, w, h, text, std::monostate{}}, outError);
    };
    auto addBannerChrome = [&](const std::string& dlg, const std::string& title, const std::string& sub) {
        addCtrl(dlg, "BannerBitmap", "Bitmap", 0,  0,  370, 44, kBmpFixed, std::monostate{}, std::string("RswBannerBmp"), std::monostate{});
        addCtrl(dlg, "BannerLine",   "Line",   0,  44, 370, 0,  1,         std::monostate{}, std::monostate{}, std::monostate{});
        addCtrl(dlg, "BottomLine",   "Line",   0,  234,370, 0,  1,         std::monostate{}, std::monostate{}, std::monostate{});
        addCtrl(dlg, "Title",        "Text",   15, 6,  340, 15, kVisTrans, std::monostate{}, "{\\DlgFontBold8}" + title, std::monostate{});
        addCtrl(dlg, "Description",  "Text",   20, 22, 335, 18, kVisTrans, std::monostate{}, sub, std::monostate{});
    };

    const bool expressCustomWorkflow = !EqualsIgnoreCase(doc.GetString("UI", "", "MsiDialogPreset", "express_custom"), "minimal");

    // Seed MSI UI state properties
    auto setUiProp = [&](const std::string& k, const std::string& v) {
        insertRow(hDb, "Property", {"Property", "Value"}, std::vector<SqlVal>{k, v}, outError);
        ioSecureCustomProps.insert(k);
    };
    setUiProp("RSW_SETUP_MODE",   "Express");
    setUiProp("RSW_MAINT_MODE",   "Repair");
    setUiProp("RSW_REPAIR_KIND",  "Quick");
    setUiProp("DESKTOP_SHORTCUT", "1");
    setUiProp("REGISTER_ASSOC",   "1");
    insertRow(hDb, "Property", {"Property", "Value"}, std::vector<SqlVal>{std::string("_BrowseProperty"), std::string("INSTALLDIR")}, outError);

    const std::string postWelcomeDlg = expressCustomWorkflow ? "SetupTypeDlg" : "InstallDirDlg";

    // -------------------------------------------------------------------------
    // 1. `WelcomeDlg` (370 x 270)
    // -------------------------------------------------------------------------
    addDlg("WelcomeDlg", 370, 270, 3, "[ProductName] Setup", "Next", "Next", "Cancel");
    addCtrl("WelcomeDlg", "Bitmap",      "Bitmap",     0,   0,   370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("WelcomeDlg", "BottomLine",  "Line",       0,   234, 370, 0,   1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("WelcomeDlg", "Title",       "Text",       135, 20,  220, 56,  kVisTrans, std::monostate{}, "{\\VerdanaBold13}" + opt.welcomeHeadline, std::monostate{});
    addCtrl("WelcomeDlg", "Description", "Text",       135, 82,  220, 110, kVisTrans, std::monostate{},
            std::string("This wizard installs [ProductName] v[ProductVersion] on your computer.\r\n\r\n"
                        "Click Next to choose Express, Compact, or Custom Setup, or Cancel to exit."),
            std::monostate{});
    addCtrl("WelcomeDlg", "Back",        "PushButton", 180, 243, 56,  17,  2 /* Disabled */, std::monostate{}, std::string("< &Back"), std::string("Next"));
    addCtrl("WelcomeDlg", "Next",        "PushButton", 236, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("WelcomeDlg", "Cancel",      "PushButton", 304, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("Cancel"),  std::string("Back"));
    addEvt("WelcomeDlg", "Next",   "NewDialog",   opt.hasLicensePage ? "LicenseAgreementDlg" : postWelcomeDlg, "1", 1);
    addEvt("WelcomeDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);

    // -------------------------------------------------------------------------
    // 2. `LicenseAgreementDlg` (370 x 270) — Closed 5-control cycle (`AgreementText -> AgreementChoice -> Back -> Next -> Cancel -> AgreementText`)
    // -------------------------------------------------------------------------
    addDlg("LicenseAgreementDlg", 370, 270, 3, "[ProductName] Setup - License Agreement", "AgreementChoice", "Next", "Cancel");
    addBannerChrome("LicenseAgreementDlg", "End-User License Agreement", "Please review the license terms carefully before continuing.");
    addCtrl("LicenseAgreementDlg", "AgreementText",   "ScrollableText", 20, 54,  330, 132, kSunken, std::monostate{}, opt.licenseRtfContent, std::string("AgreementChoice"));
    addCtrl("LicenseAgreementDlg", "AgreementChoice", "CheckBox",       20, 196, 330, 18,  kVisEn,  std::string("IAGREECHECK"), std::string("I have &read and accept the terms in the License Agreement"), std::string("Back"));
    addCtrl("LicenseAgreementDlg", "Back",            "PushButton",     180,243, 56,  17,  kVisEn,  std::monostate{}, std::string("< &Back"), std::string("Next"));
    addCtrl("LicenseAgreementDlg", "Next",            "PushButton",     236,243, 56,  17,  kVisEn,  std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("LicenseAgreementDlg", "Cancel",          "PushButton",     304,243, 56,  17,  kVisEn,  std::monostate{}, std::string("Cancel"),  std::string("AgreementText"));
    addCond("LicenseAgreementDlg", "Next", "Disable", "NOT IAGREECHECK");
    addCond("LicenseAgreementDlg", "Next", "Enable",  "IAGREECHECK");
    addEvt("LicenseAgreementDlg", "Back",   "NewDialog",   "WelcomeDlg",   "1", 1);
    addEvt("LicenseAgreementDlg", "Next",   "NewDialog",   postWelcomeDlg, "IAGREECHECK", 1);
    addEvt("LicenseAgreementDlg", "Cancel", "SpawnDialog", "CancelDlg",    "1", 1);

    // -------------------------------------------------------------------------
    // 3. `BrowseDlg` (Reusable Windows Installer Folder Picker Dialog!)
    // -------------------------------------------------------------------------
    addDlg("BrowseDlg", 370, 270, 3, "[ProductName] Setup - Change Destination Folder", "PathEdit", "OK", "Cancel");
    addBannerChrome("BrowseDlg", "Change Current Destination Folder", "Browse to the destination folder where Setup will install files.");
    addCtrl("BrowseDlg", "ComboLabel", "Text",           20,  54,  60,  14, kVisEn, std::monostate{}, std::string("&Look in:"), std::string("DirCombo"));
    addCtrl("BrowseDlg", "DirCombo",   "DirectoryCombo", 82,  52,  214, 80, kVisEn | 0x00010000 /* Removable/Fixed volumes */, std::string("_BrowseProperty"), std::monostate{}, std::string("Up"));
    addCtrl("BrowseDlg", "Up",         "PushButton",     302, 52,  22,  16, kVisEn, std::monostate{}, std::string("&Up"),  std::string("NewFolder"));
    addCtrl("BrowseDlg", "NewFolder",  "PushButton",     328, 52,  22,  16, kVisEn, std::monostate{}, std::string("&New"), std::string("DirList"));
    addCtrl("BrowseDlg", "DirList",    "DirectoryList",  20,  74,  330, 110,kSunken,std::string("_BrowseProperty"), std::monostate{}, std::string("PathLabel"));
    addCtrl("BrowseDlg", "PathLabel",  "Text",           20,  192, 70,  14, kVisEn, std::monostate{}, std::string("&Folder name:"), std::string("PathEdit"));
    addCtrl("BrowseDlg", "PathEdit",   "PathEdit",       92,  190, 258, 18, kVisEn, std::string("_BrowseProperty"), std::monostate{}, std::string("OK"));
    addCtrl("BrowseDlg", "OK",         "PushButton",     236, 243, 56,  17, kVisEn, std::monostate{}, std::string("OK"),     std::string("Cancel"));
    addCtrl("BrowseDlg", "Cancel",     "PushButton",     304, 243, 56,  17, kVisEn, std::monostate{}, std::string("Cancel"), std::string("ComboLabel"));
    addEvt("BrowseDlg", "Up",        "DirectoryListUp",  "0", "1", 1);
    addEvt("BrowseDlg", "NewFolder", "DirectoryListNew", "0", "1", 1);
    addEvt("BrowseDlg", "OK",        "SetTargetPath",    "[_BrowseProperty]", "1", 1);
    addEvt("BrowseDlg", "OK",        "EndDialog",        "Return", "1", 2);
    addEvt("BrowseDlg", "Cancel",    "Reset",            "0",      "1", 1);
    addEvt("BrowseDlg", "Cancel",    "EndDialog",        "Return", "1", 2);

    if (expressCustomWorkflow) {
        // ---------------------------------------------------------------------
        // 4A. `SetupTypeDlg` — Express vs. Compact vs. Custom Setup Workflow!
        // ---------------------------------------------------------------------
        addDlg("SetupTypeDlg", 370, 270, 3, "[ProductName] Setup - Choose Setup Mode", "ModeGroup", "Next", "Cancel");
        addBannerChrome("SetupTypeDlg", "Choose Setup Mode", "Select the setup option that best suits your needs.");
        addCtrl("SetupTypeDlg", "ModeGroup", "RadioButtonGroup", 20, 54, 330, 152, kHasBorder, std::string("RSW_SETUP_MODE"), std::monostate{}, std::string("Back"));

        addRadio("RSW_SETUP_MODE", 1, "Express", 4, 4,   320, 16, "{\\DlgFontBold8}&Express Setup (Recommended)");
        addRadio("RSW_SETUP_MODE", 2, "Compact", 4, 52,  320, 16, "{\\DlgFontBold8}Co&mpact Core Setup");
        addRadio("RSW_SETUP_MODE", 3, "Custom",  4, 100, 320, 16, "{\\DlgFontBold8}C&ustom Setup (Choose Features, Folder & Shortcuts)");

        addCtrl("SetupTypeDlg", "ExpHint", "Text", 38, 74,  310, 24, kVisTrans, std::monostate{}, opt.expressDesc, std::monostate{});
        addCtrl("SetupTypeDlg", "CmpHint", "Text", 38, 122, 310, 24, kVisTrans, std::monostate{}, opt.compactDesc, std::monostate{});
        addCtrl("SetupTypeDlg", "CusHint", "Text", 38, 170, 310, 28, kVisTrans, std::monostate{}, opt.customDesc,  std::monostate{});

        addCtrl("SetupTypeDlg", "Back",   "PushButton", 180, 243, 56, 17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Next"));
        addCtrl("SetupTypeDlg", "Next",   "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
        addCtrl("SetupTypeDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"),  std::string("ModeGroup"));

        addEvt("SetupTypeDlg", "Back", "NewDialog", opt.hasLicensePage ? "LicenseAgreementDlg" : "WelcomeDlg", "1", 1);
        // Express -> Install all default features, skip straight to VerifyReadyDlg!
        addEvt("SetupTypeDlg", "Next", "AddLocal",  "ALL",            "RSW_SETUP_MODE = \"Express\"", 1);
        addEvt("SetupTypeDlg", "Next", "NewDialog", "VerifyReadyDlg", "RSW_SETUP_MODE = \"Express\"", 2);
        // Compact -> Install core `Main` feature only, skip straight to VerifyReadyDlg!
        addEvt("SetupTypeDlg", "Next", "Remove",    "ALL",            "RSW_SETUP_MODE = \"Compact\"", 3);
        addEvt("SetupTypeDlg", "Next", "AddLocal",  "Main",           "RSW_SETUP_MODE = \"Compact\"", 4);
        addEvt("SetupTypeDlg", "Next", "NewDialog", "VerifyReadyDlg", "RSW_SETUP_MODE = \"Compact\"", 5);
        // Custom -> Proceed through CustomizeDlg -> InstallDirDlg -> ShortcutsDlg -> VerifyReadyDlg!
        addEvt("SetupTypeDlg", "Next", "NewDialog", "CustomizeDlg",   "RSW_SETUP_MODE = \"Custom\"",  6);
        addEvt("SetupTypeDlg", "Cancel", "SpawnDialog", "CancelDlg",  "1", 1);

        // ---------------------------------------------------------------------
        // 4B. `CustomizeDlg` — Native MSI `SelectionTree` Feature Selector!
        // ---------------------------------------------------------------------
        addDlg("CustomizeDlg", 370, 270, 3, "[ProductName] Setup - Custom Features", "Tree", "Next", "Cancel");
        addBannerChrome("CustomizeDlg", "Select Features", "Choose which program features you want installed.");
        addCtrl("CustomizeDlg", "Tree",      "SelectionTree", 20,  56, 198, 150, kSunken, std::string("_BrowseProperty"), std::string("Tree of selections"), std::string("Back"));
        addCtrl("CustomizeDlg", "ItemBox",   "GroupBox",      226, 52, 126, 154, kVisEn,  std::monostate{}, std::string("Feature Details"), std::monostate{});
        addCtrl("CustomizeDlg", "ItemDesc",  "Text",          232, 66, 114, 86,  kVisEn,  std::monostate{}, std::string("Select a feature in the tree to view its description."), std::monostate{});
        addCtrl("CustomizeDlg", "ItemSize",  "Text",          232, 160,114, 40,  kVisEn,  std::monostate{}, std::string(""), std::monostate{});
        addCtrl("CustomizeDlg", "Back",      "PushButton",    180, 243,56,  17,  kVisEn,  std::monostate{}, std::string("< &Back"), std::string("Next"));
        addCtrl("CustomizeDlg", "Next",      "PushButton",    236, 243,56,  17,  kVisEn,  std::monostate{}, std::string("&Next >"), std::string("Cancel"));
        addCtrl("CustomizeDlg", "Cancel",    "PushButton",    304, 243,56,  17,  kVisEn,  std::monostate{}, std::string("Cancel"),  std::string("Tree"));

        insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
                  std::vector<SqlVal>{std::string("CustomizeDlg"), std::string("ItemDesc"), std::string("SelectionDescription"), std::string("Text")}, outError);
        insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
                  std::vector<SqlVal>{std::string("CustomizeDlg"), std::string("ItemSize"), std::string("SelectionSize"),        std::string("Text")}, outError);

        addEvt("CustomizeDlg", "Back",   "NewDialog",   "MaintenanceTypeDlg", "Installed",     1);
        addEvt("CustomizeDlg", "Back",   "NewDialog",   "SetupTypeDlg",       "NOT Installed", 2);
        addEvt("CustomizeDlg", "Next",   "NewDialog",   "VerifyReadyDlg",     "Installed",     1);
        addEvt("CustomizeDlg", "Next",   "NewDialog",   "InstallDirDlg",      "NOT Installed", 2);
        addEvt("CustomizeDlg", "Cancel", "SpawnDialog", "CancelDlg",          "1",             1);

        // ---------------------------------------------------------------------
        // 4C. `InstallDirDlg` — Custom Destination Folder + `BrowseDlg`!
        // ---------------------------------------------------------------------
        addDlg("InstallDirDlg", 370, 270, 3, "[ProductName] Setup - Destination Folder", "FolderEdit", "Next", "Cancel");
        addBannerChrome("InstallDirDlg", "Destination Folder", "Choose the folder where Setup will install [ProductName].");
        addCtrl("InstallDirDlg", "FolderLabel",  "Text",       20,  62,  330, 16, kVisEn, std::monostate{}, std::string("&Destination folder path:"), std::string("FolderEdit"));
        addCtrl("InstallDirDlg", "FolderEdit",   "PathEdit",   20,  82,  258, 18, kVisEn, std::string("INSTALLDIR"), std::string("[INSTALLDIR]"), std::string("ChangeFolder"));
        addCtrl("InstallDirDlg", "ChangeFolder", "PushButton", 284, 81,  66,  19, kVisEn, std::monostate{}, std::string("B&rowse..."), std::string("Back"));
        addCtrl("InstallDirDlg", "SpaceHint",    "Text",       20,  118, 330, 50, kVisEn, std::monostate{},
                std::string("To install into this folder, click Next. To install to a different folder, enter it above or click Browse."), std::monostate{});
        addCtrl("InstallDirDlg", "Back",         "PushButton", 180, 243, 56,  17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Next"));
        addCtrl("InstallDirDlg", "Next",         "PushButton", 236, 243, 56,  17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
        addCtrl("InstallDirDlg", "Cancel",       "PushButton", 304, 243, 56,  17, kVisEn, std::monostate{}, std::string("Cancel"),  std::string("FolderLabel"));
        addEvt("InstallDirDlg", "ChangeFolder", "[_BrowseProperty]", "INSTALLDIR",    "1", 1);
        addEvt("InstallDirDlg", "ChangeFolder", "SpawnDialog",       "BrowseDlg",     "1", 2);
        addEvt("InstallDirDlg", "Back",         "NewDialog",         "CustomizeDlg",  "1", 1);
        addEvt("InstallDirDlg", "Next",         "SetTargetPath",     "INSTALLDIR",    "1", 1);
        addEvt("InstallDirDlg", "Next",         "NewDialog",         "ShortcutsDlg",  "1", 2);
        addEvt("InstallDirDlg", "Cancel",       "SpawnDialog",       "CancelDlg",     "1", 1);

        // ---------------------------------------------------------------------
        // 4D. `ShortcutsDlg` — Desktop Shortcut & File Associations CheckBoxes!
        // ---------------------------------------------------------------------
        addDlg("ShortcutsDlg", 370, 270, 3, "[ProductName] Setup - Shortcuts & Integration", "DesktopChk", "Next", "Cancel");
        addBannerChrome("ShortcutsDlg", "Shortcuts & File Associations", "Choose optional Windows shell integration settings.");
        addCtrl("ShortcutsDlg", "DesktopChk", "CheckBox",   24, 68,  322, 18, kVisEn, std::string("DESKTOP_SHORTCUT"), std::string("&Create a Desktop shortcut for quick access"), std::string("AssocChk"));
        addCtrl("ShortcutsDlg", "AssocChk",   "CheckBox",   24, 98,  322, 18, kVisEn, std::string("REGISTER_ASSOC"),   std::string("&Register supported file extensions with Windows Explorer"), std::string("Back"));
        addCtrl("ShortcutsDlg", "InfoNote",   "Text",       24, 134, 322, 54, kVisEn, std::monostate{},
                std::string("Start Menu shortcuts are always organized cleanly under your Start Menu Apps list and removed automatically on uninstall."), std::monostate{});
        addCtrl("ShortcutsDlg", "Back",       "PushButton", 180,243, 56,  17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Next"));
        addCtrl("ShortcutsDlg", "Next",       "PushButton", 236,243, 56,  17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
        addCtrl("ShortcutsDlg", "Cancel",     "PushButton", 304,243, 56,  17, kVisEn, std::monostate{}, std::string("Cancel"),  std::string("DesktopChk"));
        addEvt("ShortcutsDlg", "Back",   "NewDialog",   "InstallDirDlg",  "1", 1);
        addEvt("ShortcutsDlg", "Next",   "NewDialog",   "VerifyReadyDlg", "1", 1);
        addEvt("ShortcutsDlg", "Cancel", "SpawnDialog", "CancelDlg",      "1", 1);

        // ---------------------------------------------------------------------
        // 4E. `VerifyReadyDlg` — Ready to Apply Changes + UAC Shield!
        // ---------------------------------------------------------------------
        addDlg("VerifyReadyDlg", 370, 270, 3, "[ProductName] Setup - Ready to Install", "Install", "Install", "Cancel");
        addBannerChrome("VerifyReadyDlg", "Ready to Install [ProductName]", opt.bannerSubtitle);
        addCtrl("VerifyReadyDlg", "Summary", "Text",       20, 62,  330, 120, kVisEn, std::monostate{},
                std::string("Setup is ready to begin configuring [ProductName] on your computer:\r\n\r\n"
                            "  • Selected Mode:  [RSW_SETUP_MODE]\r\n"
                            "  • Target Folder:  [INSTALLDIR]\r\n\r\n"
                            "Click Install to begin the transaction, or click Back to review your choices."),
                std::monostate{});
        addCtrl("VerifyReadyDlg", "Back",    "PushButton", 180,243, 56,  17, kVisEn,         std::monostate{}, std::string("< &Back"), std::string("Install"));
        addCtrl("VerifyReadyDlg", "Install", "PushButton", 236,243, 56,  17, installBtnAttr, std::monostate{}, opt.installBtnText,     std::string("Cancel"));
        addCtrl("VerifyReadyDlg", "Cancel",  "PushButton", 304,243, 56,  17, kVisEn,         std::monostate{}, std::string("Cancel"),  std::string("Back"));
        addEvt("VerifyReadyDlg", "Back",    "NewDialog", "CustomizeDlg", "Installed", 1);
        addEvt("VerifyReadyDlg", "Back",    "NewDialog", "ShortcutsDlg", "NOT Installed AND RSW_SETUP_MODE = \"Custom\"",  2);
        addEvt("VerifyReadyDlg", "Back",    "NewDialog", "SetupTypeDlg", "NOT Installed AND RSW_SETUP_MODE <> \"Custom\"", 3);
        addEvt("VerifyReadyDlg", "Install", "EndDialog", "Return",       "1", 1);
        addEvt("VerifyReadyDlg", "Cancel",  "SpawnDialog","CancelDlg",   "1", 1);
    } else {
        // Streamlined `minimal` 3-screen `InstallDirDlg`
        addDlg("InstallDirDlg", 370, 270, 3, "[ProductName] Setup - Ready to Install", "Install", "Install", "Cancel");
        addBannerChrome("InstallDirDlg", "Destination Folder & Ready to Install", opt.bannerSubtitle);
        addCtrl("InstallDirDlg", "FolderLabel",  "Text",       20,  62, 330, 18, kVisEn, std::monostate{}, std::string("Install [ProductName] to:"), std::string("FolderEdit"));
        addCtrl("InstallDirDlg", "FolderEdit",   "PathEdit",   20,  84, 258, 18, kVisEn, std::string("INSTALLDIR"), std::string("[INSTALLDIR]"), std::string("ChangeFolder"));
        addCtrl("InstallDirDlg", "ChangeFolder", "PushButton", 284, 83, 66,  19, kVisEn, std::monostate{}, std::string("B&rowse..."), std::string("Back"));
        addCtrl("InstallDirDlg", "Back",         "PushButton", 180, 243,56,  17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Install"));
        addCtrl("InstallDirDlg", "Install",      "PushButton", 236, 243,56,  17, installBtnAttr, std::monostate{}, opt.installBtnText, std::string("Cancel"));
        addCtrl("InstallDirDlg", "Cancel",       "PushButton", 304, 243,56,  17, kVisEn, std::monostate{}, std::string("Cancel"),  std::string("FolderLabel"));
        addEvt("InstallDirDlg", "ChangeFolder", "[_BrowseProperty]", "INSTALLDIR", "1", 1);
        addEvt("InstallDirDlg", "ChangeFolder", "SpawnDialog",       "BrowseDlg",  "1", 2);
        addEvt("InstallDirDlg", "Back",    "NewDialog",     opt.hasLicensePage ? "LicenseAgreementDlg" : "WelcomeDlg", "1", 1);
        addEvt("InstallDirDlg", "Install", "SetTargetPath", "INSTALLDIR", "1", 1);
        addEvt("InstallDirDlg", "Install", "EndDialog",     "Return",     "1", 2);
        addEvt("InstallDirDlg", "Cancel",  "SpawnDialog",   "CancelDlg",  "1", 1);
    }

    // -------------------------------------------------------------------------
    // 5. Maintenance Hub, Office 2000-Style `RepairModeDlg`, & `VerifyRemoveDlg`!
    // -------------------------------------------------------------------------
    addDlg("MaintenanceWelcomeDlg", 370, 270, 3, "[ProductName] Maintenance", "Next", "Next", "Cancel");
    addCtrl("MaintenanceWelcomeDlg", "Bitmap",      "Bitmap",     0,   0,   370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "BottomLine",  "Line",       0,   234, 370, 0,   1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "Title",       "Text",       135, 20,  220, 54,  kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}[ProductName] Maintenance Wizard"), std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "Description", "Text",       135, 82,  220, 90,  kVisTrans, std::monostate{},
            std::string("Modify installed features, run Quick Check & Repair or Full Reinstall, or cleanly remove [ProductName]."), std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "Next",        "PushButton", 236, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("MaintenanceWelcomeDlg", "Cancel",      "PushButton", 304, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("Cancel"),  std::string("Next"));
    addEvt("MaintenanceWelcomeDlg", "Next",   "NewDialog",   "MaintenanceTypeDlg", "1", 1);
    addEvt("MaintenanceWelcomeDlg", "Cancel", "SpawnDialog", "CancelDlg",          "1", 1);

    addDlg("MaintenanceTypeDlg", 370, 270, 3, "[ProductName] Maintenance Options", "MaintGroup", "Next", "Cancel");
    addBannerChrome("MaintenanceTypeDlg", "Program Maintenance", "Modify, repair, or remove your installation.");
    addCtrl("MaintenanceTypeDlg", "MaintGroup", "RadioButtonGroup", 20, 58, 330, 132, kHasBorder, std::string("RSW_MAINT_MODE"), std::monostate{}, std::string("Back"));
    addRadio("RSW_MAINT_MODE", 1, "Modify", 6, 8,  316, 24, "{\\DlgFontBold8}&Modify — Change which program features are installed");
    addRadio("RSW_MAINT_MODE", 2, "Repair", 6, 48, 316, 24, "{\\DlgFontBold8}&Repair — Verify and restore missing or damaged files");
    addRadio("RSW_MAINT_MODE", 3, "Remove", 6, 88, 316, 24, "{\\DlgFontBold8}Re&move — Uninstall [ProductName] from your computer");
    addCtrl("MaintenanceTypeDlg", "Back",   "PushButton", 180, 243, 56, 17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Next"));
    addCtrl("MaintenanceTypeDlg", "Next",   "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("MaintenanceTypeDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"),  std::string("MaintGroup"));
    addEvt("MaintenanceTypeDlg", "Back", "NewDialog", "MaintenanceWelcomeDlg", "1", 1);
    addEvt("MaintenanceTypeDlg", "Next", "NewDialog", expressCustomWorkflow ? "CustomizeDlg" : "RepairModeDlg", "RSW_MAINT_MODE = \"Modify\"", 1);
    addEvt("MaintenanceTypeDlg", "Next", "NewDialog", "RepairModeDlg",   "RSW_MAINT_MODE = \"Repair\"", 2);
    addEvt("MaintenanceTypeDlg", "Next", "NewDialog", "VerifyRemoveDlg", "RSW_MAINT_MODE = \"Remove\"", 3);
    addEvt("MaintenanceTypeDlg", "Cancel", "SpawnDialog", "CancelDlg",   "1", 1);

    // Office 2000 `DATA1.MSI`-Style `RepairModeDlg` (`pecms` vs `vomus`!)
    addDlg("RepairModeDlg", 370, 270, 3, "[ProductName] - Repair Options", "RepairGroup", "RepairBtn", "Cancel");
    addBannerChrome("RepairModeDlg", "Repair & Verify Installation", "Choose Quick Check & Repair or Full Reinstall.");
    addCtrl("RepairModeDlg", "RepairGroup", "RadioButtonGroup", 20, 58, 330, 128, kHasBorder, std::string("RSW_REPAIR_KIND"), std::monostate{}, std::string("Back"));
    addRadio("RSW_REPAIR_KIND", 1, "Quick", 6, 10, 316, 36, "{\\DlgFontBold8}&Quick Check & Repair (Recommended) — Restores missing or corrupted files");
    addRadio("RSW_REPAIR_KIND", 2, "Full",  6, 62, 316, 36, "{\\DlgFontBold8}&Full Reinstall — Replaces all program files and refreshes shortcuts");
    addCtrl("RepairModeDlg", "Back",      "PushButton", 180, 243, 56, 17, kVisEn,         std::monostate{}, std::string("< &Back"), std::string("RepairBtn"));
    addCtrl("RepairModeDlg", "RepairBtn", "PushButton", 236, 243, 56, 17, installBtnAttr, std::monostate{}, std::string("&Repair"), std::string("Cancel"));
    addCtrl("RepairModeDlg", "Cancel",    "PushButton", 304, 243, 56, 17, kVisEn,         std::monostate{}, std::string("Cancel"),  std::string("RepairGroup"));
    addEvt("RepairModeDlg", "Back",      "NewDialog",       "MaintenanceTypeDlg", "1", 1);
    addEvt("RepairModeDlg", "RepairBtn", "ReinstallMode",   "pecms",  "RSW_REPAIR_KIND = \"Quick\"", 1);
    addEvt("RepairModeDlg", "RepairBtn", "[REINSTALLMODE]", "pecms",  "RSW_REPAIR_KIND = \"Quick\"", 2);
    addEvt("RepairModeDlg", "RepairBtn", "ReinstallMode",   "vomus",  "RSW_REPAIR_KIND = \"Full\"",  3);
    addEvt("RepairModeDlg", "RepairBtn", "[REINSTALLMODE]", "vomus",  "RSW_REPAIR_KIND = \"Full\"",  4);
    addEvt("RepairModeDlg", "RepairBtn", "Reinstall",       "ALL",    "1", 5);
    addEvt("RepairModeDlg", "RepairBtn", "[REINSTALL]",     "ALL",    "1", 6);
    addEvt("RepairModeDlg", "RepairBtn", "EndDialog",       "Return", "1", 7);
    addEvt("RepairModeDlg", "Cancel",    "SpawnDialog",     "CancelDlg", "1", 1);

    // Safe Uninstall Confirmation (`VerifyRemoveDlg` — Closed 5-control cycle without dangling `Note` pointer!)
    // CRITICAL UAC ELEVATION FIX: Set BOTH `Remove = ALL` AND `[REMOVE] = ALL` so when `ExecuteAction`
    // crosses from the non-elevated `InstallUISequence` client to the elevated `msiserver` service,
    // `REMOVE="ALL"` is passed explicitly in the public property bag and completely uninstalls the app!
    addDlg("VerifyRemoveDlg", 370, 270, 3, "[ProductName] - Safe Uninstall", "RemoveBtn", "RemoveBtn", "Cancel");
    addBannerChrome("VerifyRemoveDlg", "Remove [ProductName]", "Your personal documents and projects are never deleted.");
    addCtrl("VerifyRemoveDlg", "Note",      "Text",       20, 60,  330, 44, kVisEn, std::monostate{},
            std::string("Click Remove to uninstall [ProductName] program files from your computer. Optional cleanup choices below are unchecked by default:"), std::monostate{});
    addCtrl("VerifyRemoveDlg", "CacheChk",  "CheckBox",   24, 114, 320, 18, kVisEn, std::string("REMOVE_CACHE"), std::string("&Also remove temporary cache and diagnostic logs"), std::string("PrefsChk"));
    addCtrl("VerifyRemoveDlg", "PrefsChk",  "CheckBox",   24, 140, 320, 18, kVisEn, std::string("REMOVE_PREFS"), std::string("A&lso remove saved window layout and preferences"), std::string("Back"));
    addCtrl("VerifyRemoveDlg", "Back",      "PushButton", 180,243, 56,  17, kVisEn,         std::monostate{}, std::string("< &Back"), std::string("RemoveBtn"));
    addCtrl("VerifyRemoveDlg", "RemoveBtn", "PushButton", 236,243, 56,  17, installBtnAttr, std::monostate{}, std::string("&Remove"), std::string("Cancel"));
    addCtrl("VerifyRemoveDlg", "Cancel",    "PushButton", 304,243, 56,  17, kVisEn,         std::monostate{}, std::string("Cancel"),  std::string("CacheChk"));
    addEvt("VerifyRemoveDlg", "Back",      "NewDialog",   "MaintenanceTypeDlg", "1", 1);
    addEvt("VerifyRemoveDlg", "RemoveBtn", "Remove",      "ALL",    "1", 1);
    addEvt("VerifyRemoveDlg", "RemoveBtn", "[REMOVE]",    "ALL",    "1", 2);
    addEvt("VerifyRemoveDlg", "RemoveBtn", "EndDialog",   "Return", "1", 3);
    addEvt("VerifyRemoveDlg", "Cancel",    "SpawnDialog", "CancelDlg", "1", 1);

    // -------------------------------------------------------------------------
    // 6. `ProgressDlg`, `ExitDialog`, `CancelDlg`, `UserExit`, & `FatalError`
    // -------------------------------------------------------------------------
    // Detailed `MSI.DLL` `ProgressDlg` (`ActionText` + multiline `ActionData` [`File` / `Directory` / `Size`]
    // + `Progress95` `ProgressBar` + live `TimeRemaining` + `EnableRollback` Cancel lock!):
    // CRITICAL `MsiDefs.h` BITMASK GUARDRAIL:
    //   - `msidbControlAttributesNoPrefix`   == `0x00020000` (`131072`: prevents `&` in file paths from turning into underlines).
    //   - `msidbControlAttributesFormatSize` == `0x00080000` (`524288`: converts integers to `"0KB"` — NEVER set on `ActionText`/`ActionData`/`TimeRemaining`!).
    constexpr int kVisNoPrefix     = kVisEn | msidbControlAttributesNoPrefix;
    const bool wantFlatMsiProgress = EqualsIgnoreCase(doc.GetString("UI", "", "MsiProgressStyle", "standard"), "flat");
    const int progressBarAttr      = wantFlatMsiProgress
        ? msidbControlAttributesVisible
        : (msidbControlAttributesVisible | msidbControlAttributesProgress95);

    addDlg("ProgressDlg", 370, 270, msidbDialogAttributesVisible | msidbDialogAttributesMinimize /* Modeless */, "[ProductName] Setup", "Cancel", "Cancel", "Cancel");
    addCtrl("ProgressDlg", "BannerBitmap", "Bitmap", 0,  0,   370, 44, kBmpFixed, std::monostate{}, std::string("RswBannerBmp"), std::monostate{});
    addCtrl("ProgressDlg", "BannerLine",   "Line",   0,  44,  370, 0,  1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ProgressDlg", "BottomLine",   "Line",   0,  234, 370, 0,  1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ProgressDlg", "TitleInstall", "Text",   15, 6,   340, 15, kVisTrans, std::monostate{}, std::string("{\\DlgFontBold8}Configuring [ProductName]"), std::monostate{});
    addCtrl("ProgressDlg", "TitleRemove",  "Text",   15, 6,   340, 15, kVisTrans, std::monostate{}, std::string("{\\DlgFontBold8}Removing [ProductName]"), std::monostate{});
    addCtrl("ProgressDlg", "DescInstall",  "Text",   20, 22,  335, 18, kVisTrans, std::monostate{}, std::string("The program features you selected are being configured."), std::monostate{});
    addCtrl("ProgressDlg", "DescRemove",   "Text",   20, 22,  335, 18, kVisTrans, std::monostate{}, std::string("The program features you selected are currently being uninstalled."), std::monostate{});
    addCond("ProgressDlg", "TitleInstall", "Hide", "REMOVE~=\"ALL\"");
    addCond("ProgressDlg", "DescInstall",  "Hide", "REMOVE~=\"ALL\"");
    addCond("ProgressDlg", "TitleRemove",  "Hide", "NOT (REMOVE~=\"ALL\")");
    addCond("ProgressDlg", "DescRemove",   "Hide", "NOT (REMOVE~=\"ALL\")");

    addCtrl("ProgressDlg", "StatusLabel",   "Text",        20, 58,  42,  14, kVisEn,          std::monostate{}, std::string("{\\DlgFontBold8}Status:"), std::monostate{});
    addCtrl("ProgressDlg", "ActionText",    "Text",        64, 58,  286, 14, kVisNoPrefix,    std::monostate{}, std::string("Preparing installation..."), std::monostate{});
    addCtrl("ProgressDlg", "ActionData",    "Text",        20, 76,  330, 44, kVisNoPrefix,    std::monostate{}, std::string("Initializing..."), std::monostate{});
    addCtrl("ProgressDlg", "ProgressBar",   "ProgressBar", 20, 126, 330, 16, progressBarAttr, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ProgressDlg", "TimeRemaining", "Text",        20, 148, 330, 16, kVisNoPrefix,    std::monostate{}, std::string(""), std::monostate{});
    addCtrl("ProgressDlg", "Cancel",        "PushButton",  304,243, 56,  17, kVisEn,          std::monostate{}, std::string("Cancel"), std::string("Cancel"));
    addEvt("ProgressDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);
    insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
              std::vector<SqlVal>{std::string("ProgressDlg"), std::string("ActionText"),    std::string("ActionText"),     std::string("Text")}, outError);
    insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
              std::vector<SqlVal>{std::string("ProgressDlg"), std::string("ActionData"),    std::string("ActionData"),     std::string("Text")}, outError);
    insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
              std::vector<SqlVal>{std::string("ProgressDlg"), std::string("ProgressBar"),   std::string("SetProgress"),    std::string("Progress")}, outError);
    insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
              std::vector<SqlVal>{std::string("ProgressDlg"), std::string("TimeRemaining"), std::string("TimeRemaining"),  std::string("TimeRemaining")}, outError);
    insertRow(hDb, "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
              std::vector<SqlVal>{std::string("ProgressDlg"), std::string("Cancel"),        std::string("EnableRollback"), std::string("Enable")}, outError);

    const bool hasExitLaunchCheck = (!opt.mainExeFileKey.empty() && opt.offerLaunchOnFinish);
    addDlg("ExitDialog", 370, 270, 3, "[ProductName] Setup", "Finish", "Finish", "Finish");
    addCtrl("ExitDialog", "Bitmap",      "Bitmap",     0,   0,   370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("ExitDialog", "BottomLine",  "Line",       0,   234, 370, 0,   1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ExitDialog", "Title",       "Text",       135, 20,  220, 60,  kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}Completed the [ProductName] Setup Wizard"), std::monostate{});
    addCtrl("ExitDialog", "TitleRemove", "Text",       135, 20,  220, 60,  kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}[ProductName] Has Been Uninstalled"), std::monostate{});
    addCond("ExitDialog", "Title",       "Hide", "REMOVE~=\"ALL\"");
    addCond("ExitDialog", "TitleRemove", "Hide", "NOT (REMOVE~=\"ALL\")");
    addCtrl("ExitDialog", "Description", "Text",       135, 85,  220, 60,  kVisTrans, std::monostate{}, std::string("Click Finish to exit the Setup Wizard."), std::monostate{});
    if (hasExitLaunchCheck) {
        addCtrl("ExitDialog", "LaunchCheck", "CheckBox", 135, 160, 220, 18, kVisTrans, std::string("LAUNCHAPP"), opt.launchChkText, std::string("Finish"));
        addCond("ExitDialog", "LaunchCheck", "Hide", "REMOVE~=\"ALL\"");
        addEvt("ExitDialog",  "Finish", "DoAction", "RswLaunchMainApp", "LAUNCHAPP = \"1\" AND NOT (REMOVE~=\"ALL\")", 1);
    }
    // CRITICAL FIX FOR MSI ERROR 2809: When `LaunchCheck -> Finish` exists, `Finish` MUST point back to `"LaunchCheck"` to close the cycle!
    addCtrl("ExitDialog", "Finish", "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Finish"),
            hasExitLaunchCheck ? SqlVal{std::string("LaunchCheck")} : SqlVal{std::monostate{}});
    addEvt("ExitDialog", "Finish", "EndDialog", "Return", "1", 2);

    addDlg("CancelDlg", 260, 85, 3, "[ProductName] Setup", "No", "No", "No");
    addCtrl("CancelDlg", "Text", "Text",       20,  15, 220, 30, kVisEn, std::monostate{}, std::string("Are you sure you want to cancel [ProductName] installation?"), std::monostate{});
    addCtrl("CancelDlg", "Yes",  "PushButton", 72,  57, 56,  17, kVisEn, std::monostate{}, std::string("&Yes"), std::string("No"));
    addCtrl("CancelDlg", "No",   "PushButton", 132, 57, 56,  17, kVisEn, std::monostate{}, std::string("&No"),  std::string("Yes"));
    addEvt("CancelDlg", "Yes", "EndDialog", "Exit",   "1", 1);
    addEvt("CancelDlg", "No",  "EndDialog", "Return", "1", 1);

    addDlg("UserExit", 370, 270, 3, "[ProductName] Setup", "Finish", "Finish", "Finish");
    addCtrl("UserExit", "Bitmap",      "Bitmap",     0,   0,   370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("UserExit", "BottomLine",  "Line",       0,   234, 370, 0,   1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("UserExit", "Title",       "Text",       135, 20,  220, 60,  kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}[ProductName] Setup Was Interrupted"), std::monostate{});
    addCtrl("UserExit", "Description", "Text",       135, 85,  220, 60,  kVisTrans, std::monostate{}, std::string("Your system has not been modified. Run Setup again later to complete installation."), std::monostate{});
    addCtrl("UserExit", "Finish",      "PushButton", 236, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("&Finish"), std::monostate{});
    addEvt("UserExit", "Finish", "EndDialog", "Exit", "1", 1);

    addDlg("FatalError", 370, 270, 3, "[ProductName] Setup", "Finish", "Finish", "Finish");
    addCtrl("FatalError", "Bitmap",      "Bitmap",     0,   0,   370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("FatalError", "BottomLine",  "Line",       0,   234, 370, 0,   1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("FatalError", "Title",       "Text",       135, 20,  220, 60,  kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}[ProductName] Setup Ended Prematurely"), std::monostate{});
    addCtrl("FatalError", "Description", "Text",       135, 85,  220, 60,  kVisTrans, std::monostate{}, std::string("[ProductName] Setup ended prematurely because of an error. Your system has not been modified."), std::monostate{});
    addCtrl("FatalError", "Finish",      "PushButton", 236, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("&Finish"), std::monostate{});
    addEvt("FatalError", "Finish", "EndDialog", "Exit", "1", 1);

    // -------------------------------------------------------------------------
    // 6B. Administrative Installation Dialogs (`msiexec.exe /a` Network Image!)
    //     Closed `Control_Next` cycles guarantee zero Error 2809 failures!
    // -------------------------------------------------------------------------
    addDlg("AdminWelcomeDlg", 370, 270, 3, "[ProductName] Network Setup", "Next", "Next", "Cancel");
    addCtrl("AdminWelcomeDlg", "Bitmap",      "Bitmap",     0,   0,   370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("AdminWelcomeDlg", "BottomLine",  "Line",       0,   234, 370, 0,   1,         std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("AdminWelcomeDlg", "Title",       "Text",       135, 20,  220, 56,  kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}Welcome to the [ProductName] Network Setup Wizard"), std::monostate{});
    addCtrl("AdminWelcomeDlg", "Description", "Text",       135, 82,  220, 110, kVisTrans, std::monostate{},
            std::string("This wizard creates an uncompressed administrative server image of [ProductName] at a specified network or folder location.\r\n\r\nClick Next to choose the target path, or Cancel to exit."),
            std::monostate{});
    addCtrl("AdminWelcomeDlg", "Back",        "PushButton", 180, 243, 56,  17,  2 /* Disabled */, std::monostate{}, std::string("< &Back"), std::string("Next"));
    addCtrl("AdminWelcomeDlg", "Next",        "PushButton", 236, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("AdminWelcomeDlg", "Cancel",      "PushButton", 304, 243, 56,  17,  kVisEn,    std::monostate{}, std::string("Cancel"),  std::string("Back"));
    addEvt("AdminWelcomeDlg", "Next",   "NewDialog",   "AdminInstallPointDlg", "1", 1);
    addEvt("AdminWelcomeDlg", "Cancel", "SpawnDialog", "CancelDlg",            "1", 1);

    addDlg("AdminInstallPointDlg", 370, 270, 3, "[ProductName] Network Setup - Target Location", "PathEdit", "Install", "Cancel");
    addBannerChrome("AdminInstallPointDlg", "Network Location", "Specify a network share or folder path for the administrative image.");
    addCtrl("AdminInstallPointDlg", "PathLabel",    "Text",       20,  62,  330, 16, kVisEn,         std::monostate{}, std::string("&Network or target folder path:"), std::string("PathEdit"));
    addCtrl("AdminInstallPointDlg", "PathEdit",     "PathEdit",   20,  82,  258, 18, kVisEn,         std::string("TARGETDIR"), std::string("[TARGETDIR]"), std::string("ChangeFolder"));
    addCtrl("AdminInstallPointDlg", "ChangeFolder", "PushButton", 284, 81,  66,  19, kVisEn,         std::monostate{}, std::string("B&rowse..."), std::string("Back"));
    addCtrl("AdminInstallPointDlg", "AdminHint",    "Text",       20,  116, 330, 56, kVisEn,         std::monostate{},
            std::string("Click Install to extract a clean, patchable uncompressed administrative installation image of [ProductName] to the path above."), std::monostate{});
    addCtrl("AdminInstallPointDlg", "Back",         "PushButton", 180, 243, 56,  17, kVisEn,         std::monostate{}, std::string("< &Back"), std::string("Install"));
    addCtrl("AdminInstallPointDlg", "Install",      "PushButton", 236, 243, 56,  17, kVisEn,         std::monostate{}, opt.installBtnText,     std::string("Cancel"));
    addCtrl("AdminInstallPointDlg", "Cancel",       "PushButton", 304, 243, 56,  17, kVisEn,         std::monostate{}, std::string("Cancel"),  std::string("PathLabel"));
    addEvt("AdminInstallPointDlg", "ChangeFolder", "[_BrowseProperty]", "TARGETDIR",        "1", 1);
    addEvt("AdminInstallPointDlg", "ChangeFolder", "SpawnDialog",       "BrowseDlg",        "1", 2);
    addEvt("AdminInstallPointDlg", "Back",         "NewDialog",         "AdminWelcomeDlg",  "1", 1);
    addEvt("AdminInstallPointDlg", "Install",      "SetTargetPath",     "TARGETDIR",        "1", 1);
    addEvt("AdminInstallPointDlg", "Install",      "EndDialog",         "Return",           "1", 2);
    addEvt("AdminInstallPointDlg", "Cancel",       "SpawnDialog",       "CancelDlg",        "1", 1);

    // Populate standard Windows Installer `Error` rows for Multi-Volume Disk Spanning (`Error 1302`)!
    static constexpr std::pair<int, const char*> kStandardMsiErrors[] = {
        {1302, "Please insert the disk: [2]"},
        {1308, "Source file not found: [2]. Verify that the file exists and that you can access it."},
        {1309, "Error reading from file: [3]. System error [2]. Verify that the source media is accessible."},
        {1606, "Could not access network location [2]."}
    };
    for (const auto& [errCode, errMsg] : kStandardMsiErrors) {
        insertRow(hDb, "Error", {"Error", "Message"}, std::vector<SqlVal>{errCode, std::string(errMsg)}, outError);
    }

    // -------------------------------------------------------------------------
    // 7. Compile Author-Defined `[MsiDialog.<id>]` & `[MsiControl.<id>]` Sections!
    // -------------------------------------------------------------------------
    const auto customDlgIds  = doc.GetNamedSectionIds("MsiDialog");
    const auto customCtrlIds = doc.GetNamedSectionIds("MsiControl");

    // Sort custom `[MsiControl.<id>]` entries by `(TabOrder > 0 ? TabOrder : 10000 + origIdx)`
    // so authors can control exact `Tab` focus traversal across custom `370 x 270` DLU dialogs!
    std::vector<std::string> sortedCtrlIds = customCtrlIds;
    std::stable_sort(sortedCtrlIds.begin(), sortedCtrlIds.end(), [&](const std::string& a, const std::string& b) {
        const int ta = doc.GetInt("MsiControl", a, "TabOrder", 0);
        const int tb = doc.GetInt("MsiControl", b, "TabOrder", 0);
        const int ea = (ta > 0) ? ta : 100000;
        const int eb = (tb > 0) ? tb : 100000;
        return ea < eb;
    });

    for (const auto& did : customDlgIds) {
        const std::string title    = doc.GetString("MsiDialog", did, "Title", "[ProductName] Setup");
        const int w                = doc.GetInt("MsiDialog", did, "Width",  370);
        const int h                = doc.GetInt("MsiDialog", did, "Height", 270);
        const bool hasBanner       = doc.GetBool("MsiDialog", did, "Banner", true);
        const std::string hTitle   = doc.GetString("MsiDialog", did, "HeaderTitle", did);
        const std::string hSub     = doc.GetString("MsiDialog", did, "HeaderSubtitle", "");
        const std::string prevDlg  = doc.GetString("MsiDialog", did, "PrevDialog", expressCustomWorkflow ? "SetupTypeDlg" : "WelcomeDlg");
        const std::string nextDlg  = doc.GetString("MsiDialog", did, "NextDialog", expressCustomWorkflow ? "VerifyReadyDlg" : "InstallDirDlg");

        // Find first interactive control belonging to `did` so `Control_First` + Tab chain stay valid!
        std::vector<std::string> myCtrls;
        for (const auto& cid : sortedCtrlIds) {
            if (EqualsIgnoreCase(doc.GetString("MsiControl", cid, "Dialog"), did)) {
                const std::string tp = doc.GetString("MsiControl", cid, "Type", "Text");
                if (!EqualsIgnoreCase(tp, "Text") && !EqualsIgnoreCase(tp, "Line") &&
                    !EqualsIgnoreCase(tp, "Bitmap") && !EqualsIgnoreCase(tp, "GroupBox")) {
                    myCtrls.push_back(cid);
                }
            }
        }
        const std::string firstTab       = myCtrls.empty() ? "Next" : myCtrls.front();
        const std::string cancelNextCtrl = myCtrls.empty() ? "Back" : myCtrls.front();

        addDlg(did, w, h, 3, title, firstTab, "Next", "Cancel");
        if (hasBanner) {
            addBannerChrome(did, hTitle, hSub);
        }
        addCtrl(did, "Back",   "PushButton", w - 190, h - 27, 56, 17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Next"));
        addCtrl(did, "Next",   "PushButton", w - 134, h - 27, 56, 17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
        addCtrl(did, "Cancel", "PushButton", w - 66,  h - 27, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"),  cancelNextCtrl);

        addEvt(did, "Back",   "NewDialog",   prevDlg,     "1", 1);
        addEvt(did, "Next",   "NewDialog",   nextDlg,     "1", 1);
        addEvt(did, "Cancel", "SpawnDialog", "CancelDlg", "1", 1);

        // Automatically splice `prevDlg -> did -> nextDlg` inside `ControlEvent`!
        if (!prevDlg.empty() && !nextDlg.empty()) {
            const std::wstring wUpd = Utf8ToWide(
                "UPDATE `ControlEvent` SET `Argument` = '" + did +
                "' WHERE `Dialog_` = '" + prevDlg + "' AND `Event` = 'NewDialog' AND `Argument` = '" + nextDlg + "'");
            MSIHANDLE hUpdView = 0;
            if (::MsiDatabaseOpenViewW(hDb, wUpd.c_str(), &hUpdView) == ERROR_SUCCESS) {
                ::MsiViewExecute(hUpdView, 0);
                ::MsiCloseHandle(hUpdView);
            }
            const std::wstring wBackUpd = Utf8ToWide(
                "UPDATE `ControlEvent` SET `Argument` = '" + did +
                "' WHERE `Dialog_` = '" + nextDlg + "' AND `Control_` = 'Back' AND `Event` = 'NewDialog' AND `Argument` = '" + prevDlg + "'");
            MSIHANDLE hBackView = 0;
            if (::MsiDatabaseOpenViewW(hDb, wBackUpd.c_str(), &hBackView) == ERROR_SUCCESS) {
                ::MsiViewExecute(hBackView, 0);
                ::MsiCloseHandle(hBackView);
            }
        }
    }

    for (std::size_t idx = 0; idx < sortedCtrlIds.size(); ++idx) {
        const std::string& cid     = sortedCtrlIds[idx];
        const std::string dlg      = doc.GetString("MsiControl", cid, "Dialog");
        const std::string rawType  = doc.GetString("MsiControl", cid, "Type", "Text");
        const bool isPasswordEdit  = EqualsIgnoreCase(rawType, "PasswordEdit");
        const std::string type     = isPasswordEdit ? "Edit" : rawType;
        const int x                = doc.GetInt("MsiControl", cid, "X", 20);
        const int y                = doc.GetInt("MsiControl", cid, "Y", 60);
        const int w                = doc.GetInt("MsiControl", cid, "Width", 330);
        const int h                = doc.GetInt("MsiControl", cid, "Height", 18);
        const std::string prop     = TrimAscii(doc.GetString("MsiControl", cid, "Property"));
        const std::string defV     = doc.GetString("MsiControl", cid, "DefaultValue");
        const std::string txt      = doc.GetString("MsiControl", cid, "Text");
        const std::string items    = doc.GetString("MsiControl", cid, "Items");

        const bool nonTab = EqualsIgnoreCase(type, "Text") || EqualsIgnoreCase(type, "Line") ||
                            EqualsIgnoreCase(type, "Bitmap") || EqualsIgnoreCase(type, "GroupBox");

        // Link tab chain to next interactive control on the same custom dialog (or `"Back"`)
        SqlVal nextCtrl = std::monostate{};
        if (!nonTab) {
            std::string foundNext = "Back";
            for (std::size_t j = idx + 1; j < sortedCtrlIds.size(); ++j) {
                if (EqualsIgnoreCase(doc.GetString("MsiControl", sortedCtrlIds[j], "Dialog"), dlg)) {
                    const std::string tp2 = doc.GetString("MsiControl", sortedCtrlIds[j], "Type", "Text");
                    if (!EqualsIgnoreCase(tp2, "Text") && !EqualsIgnoreCase(tp2, "Line") &&
                        !EqualsIgnoreCase(tp2, "Bitmap") && !EqualsIgnoreCase(tp2, "GroupBox")) {
                        foundNext = sortedCtrlIds[j];
                        break;
                    }
                }
            }
            nextCtrl = foundNext;
        }

        if (!prop.empty()) {
            ioSecureCustomProps.insert(prop);
            if (!defV.empty()) {
                insertRow(hDb, "Property", {"Property", "Value"}, std::vector<SqlVal>{prop, defV}, outError);
            }
        }

        const int attr = EqualsIgnoreCase(type, "Text")
            ? kVisTrans
            : (isPasswordEdit ? (kVisEn | msidbControlAttributesPassword) : kVisEn);
        addCtrl(dlg, cid, type, x, y, w, h, attr,
                prop.empty() ? SqlVal{std::monostate{}} : SqlVal{prop},
                txt.empty()  ? SqlVal{std::monostate{}} : SqlVal{txt},
                nextCtrl);

        // Populate `RadioButton`, `ComboBox`, or `ListBox` child items (`Value:Caption|Value2:Caption2`)!
        if (!prop.empty() && !items.empty()) {
            int ord = 1;
            int ry  = 4;
            std::size_t pos = 0;
            while (pos <= items.size()) {
                auto bar = items.find('|', pos);
                std::string seg = TrimAscii(bar == std::string::npos ? items.substr(pos) : items.substr(pos, bar - pos));
                if (!seg.empty()) {
                    auto col = seg.find(':');
                    std::string val = (col == std::string::npos) ? seg : TrimAscii(seg.substr(0, col));
                    std::string cap = (col == std::string::npos) ? seg : TrimAscii(seg.substr(col + 1));
                    if (EqualsIgnoreCase(type, "RadioButtonGroup")) {
                        addRadio(prop, ord, val, 6, ry, std::max(40, w - 12), 18, cap);
                        ry += 22;
                    } else if (EqualsIgnoreCase(type, "ComboBox")) {
                        insertRow(hDb, "ComboBox", {"Property", "Order", "Value", "Text"},
                                  std::vector<SqlVal>{prop, ord, val, cap}, outError);
                    } else if (EqualsIgnoreCase(type, "ListBox")) {
                        insertRow(hDb, "ListBox", {"Property", "Order", "Value", "Text"},
                                  std::vector<SqlVal>{prop, ord, val, cap}, outError);
                    }
                    ++ord;
                }
                if (bar == std::string::npos) break;
                pos = bar + 1;
            }
        }
    }

    return true;
}

} // namespace rsw
