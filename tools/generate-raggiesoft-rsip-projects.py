#!/usr/bin/env python3
# ================================================================================
# FILE: desktop-apps/installer-studio/tools/generate-raggiesoft-rsip-projects.py
# PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
# WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
# LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
#
# PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
#   What does this script do?
#     Generates RaggieSoft's complete Windows software, book, and music packaging
#     catalog using our hybrid `.rsip` + `.rsis` + `.rsii` architecture:
#       1. **Shared `.rsii` Include** (`projects/shared/raggiesoft-defaults.rsii`):
#          Common Windows 11 build requirements inherited via `@include`.
#       2. **7 Core Desktop Applications** (`projects/apps/<slug>/<slug>.rsip`):
#          - `installer-studio`     (RaggieSoft Installer Studio — dogfooded!)
#          - `accessibility-studio` (RaggieSoft Accessibility Studio)
#          - `launcher`             (RaggieSoft Launcher + raggiesoft-pkg CLI)
#          - `book-reader`          (RaggieSoft Book Reader + OfflineSeed)
#          - `listening-room`       (The Listening Room + OfflineSeed)
#          - `backstage`            (RaggieSoft Backstage + OfflineSeed)
#          - `storetrainer`         (RaggieSoft StoreTrainer + .stfx ProgId)
#       3. **All Visible Books** (`projects/books/<slug>/<slug>.rsip`):
#          - `640x480` `16:9` OpenGraph CD-Autorun micro-installers (`Scope = user`).
#       4. **All Vault HQ Albums** (`projects/albums/<artist>/<album>/<album>.rsip`):
#          - `640x480` `16:9` OpenGraph CD-Autorun supporter micro-installers.
#       5. **Master Multi-Project Suite** (`RaggieSoft.rsis`):
#          - Groups all 77 `.rsip` projects (just like Visual Studio `.slnx`!) with
#            `Category` tags and `BuildOrder` stages for `instbuild.exe` & `InstallerStudio.exe`!
# ================================================================================

from __future__ import annotations

import json
import os
import re
import shutil
import uuid
from pathlib import Path

# Deterministic RFC 4122 UUIDv5 namespace keeping every generated `UpgradeCode`
# 100% stable across regenerations for seamless Windows Installer Major Upgrades.
RSIP_MIGRATION_NAMESPACE = uuid.UUID("6d0e9b3a-8c2f-4e49-9b4a-7c1d8f4a2026")

SCRIPT_DIR = Path(__file__).resolve().parent
STUDIO_ROOT = SCRIPT_DIR.parent
DESKTOP_APPS_ROOT = STUDIO_ROOT.parent
WORKSPACE_ROOT = DESKTOP_APPS_ROOT.parent
ASSETS_ROOT = WORKSPACE_ROOT / "raggiesoft-servers" / "raggiesoft-assets"
VAULTS_ROOT = WORKSPACE_ROOT / "raggiesoft-servers" / "raggiesoft-vaults"
PROJECTS_ROOT = STUDIO_ROOT / "projects"

FICTITIOUS_DISCLAIMER = (
    "FICTITIOUS PERSONS & ENTITIES DISCLAIMER: All narrative characters, bands "
    "(including The Stardust Engine), biographies, universities (including Commonwealth "
    "Polytechnic Institute / CPI, Virginia Dominion University / VDU, The Forgers, and "
    "The Crucible), corporations (including OmniCorp, Apex Records, Prairie Coachworks, "
    "and Sainte-Claire), transit agencies (BRT and CVTA), in-universe legal filings, and "
    "events are works of fiction. In real life, RaggieSoft, RaggieSoft Media, Engine Room "
    "Records, Ocean View Archives, and RaggieSoft Story Crossroads are unincorporated "
    "creative imprints of Michael P. Ragsdale."
)


def det_guid(seed: str) -> str:
    """Computes a deterministic uppercase `{GUID}` from a stable string seed using SHA-1 UUIDv5."""
    return "{" + str(uuid.uuid5(RSIP_MIGRATION_NAMESPACE, seed)).upper() + "}"


def clean_title(raw: str, max_len: int = 56) -> str:
    """Normalizes display titles for Win32 Wizard headers and MSI `ProductName` columns."""
    s = raw.replace(": ", " - ").replace(":", " -").replace("/", "-").replace("\\", "-")
    for ch in '*?"<>|':
        s = s.replace(ch, "")
    while "  " in s:
        s = s.replace("  ", " ")
    s = s.strip()
    if len(s) > max_len:
        cut = s[:max_len].rsplit(" ", 1)[0].rstrip(" -–,")
        s = cut if len(cut) >= 16 else s[:max_len].rstrip(" -–,")
    return s


def write_rtf_license(dest_rtf: Path, heading: str, summary_body: str) -> None:
    """Writes a valid ANSI RTF agreement including the mandatory Fictitious Disclaimer."""
    def esc_rtf(t: str) -> str:
        out = []
        for ch in t:
            if ch in ("\\", "{", "}"):
                out.append("\\" + ch)
            elif ord(ch) < 128:
                out.append(ch)
            else:
                out.append("-")
        return "".join(out)

    rtf = (
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}}\r\n"
        f"\\fs20\\b {esc_rtf(heading)}\\b0\\par\\par\r\n"
        f"\\fs18 {esc_rtf(summary_body)}\\par\\par\r\n"
        f"{esc_rtf(FICTITIOUS_DISCLAIMER)}\\par}}\r\n"
    )
    dest_rtf.parent.mkdir(parents=True, exist_ok=True)
    dest_rtf.write_text(rtf, encoding="ascii", errors="ignore", newline="")


def to_win_rel_or_abs(target: Path, from_dir: Path) -> str:
    """Computes a portable Windows backslash-delimited relative path from `from_dir` to `target`."""
    try:
        rel = os.path.relpath(target, from_dir)
        return rel.replace("/", "\\")
    except ValueError:
        return str(target).replace("/", "\\")


def write_shared_rsii_include() -> Path:
    """Writes `projects/shared/raggiesoft-defaults.rsii` inherited via `@include` across `.rsip` files."""
    inc_path = PROJECTS_ROOT / "shared" / "raggiesoft-defaults.rsii"
    inc_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "; ==============================================================================",
        "; FILE: projects/shared/raggiesoft-defaults.rsii",
        "; Shared RaggieSoft Installer Include (.rsii) — inherited via @include!",
        "; ==============================================================================",
        "",
        "[Requirements]",
        "MinWindowsBuild       = 22000",
        "BlockDowngrades       = true",
        "",
    ]
    inc_path.write_text("\r\n".join(lines), encoding="utf-8", newline="")
    return inc_path


def generate_core_app_projects(shared_rsii: Path, suite_entries: list[tuple[str, Path, str, int]]) -> int:
    """Generates `.rsip` manifests for all 7 RaggieSoft Windows desktop applications."""
    apps_dir = PROJECTS_ROOT / "apps"
    apps = [
        {
            "slug": "installer-studio",
            "name": "RaggieSoft Installer Studio",
            "tagline": "Install Without Limits - Visual IDE, Compiler & MSI Prism",
            "website": "raggiesoftstudio.com",
            "about_url": "https://raggiesoftstudio.com",
            "version": "0.1.0",
            "upgrade_code": det_guid("installer-studio:upgrade"),
            "default_subdir": r"RaggieSoft\Installer Studio",
            "main_exe": "InstallerStudio.exe",
            "primary_hex": "#2563EB",
            "secondary_hex": "#FBBF24",
            "ico_path": DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/RaggieSoftLauncher/Assets/RaggieSoftLauncher.ico",
            "payloads": [
                ("CoreBinaries", r"..\..\..\bin\$(ARCH)", "", "Main"),
                ("AiSpecs", r"..\..\..\specs", "specs", "Main"),
            ],
            "shortcuts": [
                ("StudioIde", "RaggieSoft Installer Studio", "InstallerStudio.exe", "StartMenu"),
                ("MsiPrism", "RaggieSoft MSI Prism", "msiprism.exe", "StartMenu"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\Installer Studio", "Install Path", "[INSTALLDIR]"),
                ("RsipExt", "HKCR", ".rsip", "@", "RaggieSoft.InstallerStudio.Project"),
                ("RsipProgId", "HKCR", "RaggieSoft.InstallerStudio.Project", "@", "RaggieSoft Installer Project"),
                ("RsisExt", "HKCR", ".rsis", "@", "RaggieSoft.InstallerStudio.Suite"),
                ("RsisProgId", "HKCR", "RaggieSoft.InstallerStudio.Suite", "@", "RaggieSoft Installer Suite"),
            ],
        },
        {
            "slug": "accessibility-studio",
            "name": "RaggieSoft Accessibility Studio",
            "tagline": "Accessibility IDE & Offline Rule Reference",
            "website": "a11y.raggiesoft.com",
            "about_url": "https://a11y.raggiesoft.com",
            "version": "1.0.0",
            "upgrade_code": "{56DD624B-D43B-4B29-80A8-E06BE8EA7BFB}",
            "default_subdir": r"RaggieSoft\Accessibility Studio",
            "main_exe": r"StudioIDE\AccessibilityStudio.exe",
            "primary_hex": "#2563EB",
            "secondary_hex": "#10B981",
            "ico_path": DESKTOP_APPS_ROOT / "accessibility-studio/src/AccessibilityStudio.WinForms/Assets/AccessibilityStudio.ico",
            "payloads": [
                ("StudioIde", r"..\..\..\..\accessibility-studio\build\INSTALLDIR\Release\$(ARCH)\StudioIDE", "StudioIDE", "Main"),
                ("CliScanner", r"..\..\..\..\accessibility-studio\build\INSTALLDIR\Release\$(ARCH)\CLI", "CLI", "Main"),
                ("HelpViewer", r"..\..\..\..\accessibility-studio\build\INSTALLDIR\Release\$(ARCH)\HelpViewer", "HelpViewer", "Main"),
                ("HelpManual", r"..\..\..\..\accessibility-studio\build\INSTALLDIR\Release\x64\HelpModules\raggiesoft.accessibility-studio", r"HelpModules\raggiesoft.accessibility-studio", "Main"),
                ("Samples", r"..\..\..\..\accessibility-studio\build\INSTALLDIR\Release\x64\Samples", "Samples", "Main"),
                ("Legal", r"..\..\..\..\accessibility-studio\build\INSTALLDIR\Release\x64\Legal", "Legal", "Main"),
            ],
            "shortcuts": [
                ("StudioStart", "RaggieSoft Accessibility Studio", r"StudioIDE\AccessibilityStudio.exe", "StartMenu"),
                ("HelpStart", "RaggieSoft Help Viewer", r"HelpViewer\RaggieSoftHelp.exe", "StartMenu"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\Accessibility Studio", "Install Path", "[INSTALLDIR]"),
                ("A11yExt", "HKCR", ".a11yproj", "@", "RaggieSoft.AccessibilityStudio.Workspace"),
                ("A11yProgId", "HKCR", "RaggieSoft.AccessibilityStudio.Workspace", "@", "RaggieSoft Accessibility Studio Workspace"),
            ],
        },
        {
            "slug": "launcher",
            "name": "RaggieSoft Launcher",
            "tagline": "Books, Music & Creative Applications Hub",
            "website": "launcher.raggiesoft.com",
            "about_url": "https://launcher.raggiesoft.com",
            "version": "1.0.0",
            "upgrade_code": det_guid("launcher:upgrade"),
            "default_subdir": r"RaggieSoft\Launcher",
            "main_exe": "RaggieSoftLauncher.exe",
            "primary_hex": "#38BDF8",
            "secondary_hex": "#FBBF24",
            "ico_path": DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/RaggieSoftLauncher/Assets/RaggieSoftLauncher.ico",
            "payloads": [
                ("App", r"..\..\..\..\windows\RaggieSoftApps\RaggieSoftLauncher\bin\Release\net10.0-windows\win-$(ARCH)\publish", "", "Main"),
                ("PkgCli", r"..\..\..\..\windows\RaggieSoftApps\RaggieSoftPkgCli\bin\Release\net10.0\win-$(ARCH)\publish", "CLI", "Main"),
                ("OfflineSeed", r"..\..\..\offline-seeds\launcher", "OfflineSeed", "Main"),
            ],
            "shortcuts": [
                ("StartLink", "RaggieSoft Launcher", "RaggieSoftLauncher.exe", "StartMenu"),
                ("DesktopLink", "RaggieSoft Launcher", "RaggieSoftLauncher.exe", "Desktop"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\Launcher", "Install Path", "[INSTALLDIR]"),
            ],
        },
        {
            "slug": "book-reader",
            "name": "RaggieSoft Book Reader",
            "tagline": "Ocean View Archives & Story Crossroads Library",
            "website": "books.raggiesoft.com",
            "about_url": "https://books.raggiesoft.com",
            "version": "1.0.0",
            "upgrade_code": det_guid("book-reader:upgrade"),
            "default_subdir": r"RaggieSoft\Book Reader",
            "main_exe": "BookReader.exe",
            "primary_hex": "#14B8A6",
            "secondary_hex": "#F59E0B",
            "ico_path": DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/BookReader/Assets/BookReader.ico",
            "payloads": [
                ("App", r"..\..\..\..\windows\RaggieSoftApps\BookReader\bin\Release\net10.0-windows\win-$(ARCH)\publish", "", "Main"),
                ("OfflineSeed", r"..\..\..\offline-seeds\book-reader", "OfflineSeed", "Main"),
            ],
            "shortcuts": [
                ("StartLink", "RaggieSoft Book Reader", "BookReader.exe", "StartMenu"),
                ("DesktopLink", "RaggieSoft Book Reader", "BookReader.exe", "Desktop"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\Book Reader", "Install Path", "[INSTALLDIR]"),
            ],
        },
        {
            "slug": "listening-room",
            "name": "RaggieSoft The Listening Room",
            "tagline": "Engine Room Records Music & Synchronized Lyrics",
            "website": "music.raggiesoft.com",
            "about_url": "https://music.raggiesoft.com",
            "version": "1.0.0",
            "upgrade_code": det_guid("listening-room:upgrade"),
            "default_subdir": r"RaggieSoft\The Listening Room",
            "main_exe": "ListeningRoom.exe",
            "primary_hex": "#06B6D4",
            "secondary_hex": "#EC4899",
            "ico_path": DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/ListeningRoom/Assets/ListeningRoom.ico",
            "payloads": [
                ("App", r"..\..\..\..\windows\RaggieSoftApps\ListeningRoom\bin\Release\net10.0-windows\win-$(ARCH)\publish", "", "Main"),
                ("OfflineSeed", r"..\..\..\offline-seeds\listening-room", "OfflineSeed", "Main"),
            ],
            "shortcuts": [
                ("StartLink", "The Listening Room", "ListeningRoom.exe", "StartMenu"),
                ("DesktopLink", "The Listening Room", "ListeningRoom.exe", "Desktop"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\The Listening Room", "Install Path", "[INSTALLDIR]"),
            ],
        },
        {
            "slug": "backstage",
            "name": "RaggieSoft Backstage",
            "tagline": "Behind-the-Scenes Stories & Creative Companion",
            "website": "backstage.raggiesoft.com",
            "about_url": "https://backstage.raggiesoft.com",
            "version": "1.0.0",
            "upgrade_code": det_guid("backstage:upgrade"),
            "default_subdir": r"RaggieSoft\Backstage",
            "main_exe": "Backstage.exe",
            "primary_hex": "#E11D48",
            "secondary_hex": "#F59E0B",
            "ico_path": DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/Backstage/Assets/Backstage.ico",
            "payloads": [
                ("App", r"..\..\..\..\windows\RaggieSoftApps\Backstage\bin\Release\net10.0-windows\win-$(ARCH)\publish", "", "Main"),
                ("OfflineSeed", r"..\..\..\offline-seeds\backstage", "OfflineSeed", "Main"),
            ],
            "shortcuts": [
                ("StartLink", "RaggieSoft Backstage", "Backstage.exe", "StartMenu"),
                ("DesktopLink", "RaggieSoft Backstage", "Backstage.exe", "Desktop"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\Backstage", "Install Path", "[INSTALLDIR]"),
            ],
        },
        {
            "slug": "storetrainer",
            "name": "RaggieSoft StoreTrainer",
            "tagline": "PC Game & Companion Trainer Organizer",
            "website": "storetrainer.raggiesoft.com",
            "about_url": "https://storetrainer.raggiesoft.com",
            "version": "2.0.0",
            "upgrade_code": det_guid("storetrainer:upgrade"),
            "default_subdir": r"RaggieSoft\StoreTrainer",
            "main_exe": "StoreTrainer.exe",
            "primary_hex": "#0EA5E9",
            "secondary_hex": "#10B981",
            "ico_path": DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/StoreTrainer/Assets/StoreTrainer.ico",
            "payloads": [
                ("App", r"..\..\..\..\windows\RaggieSoftApps\StoreTrainer\bin\Release\net10.0-windows\win-$(ARCH)\publish", "", "Main"),
            ],
            "shortcuts": [
                ("StartLink", "RaggieSoft StoreTrainer", "StoreTrainer.exe", "StartMenu"),
                ("DesktopLink", "RaggieSoft StoreTrainer", "StoreTrainer.exe", "Desktop"),
            ],
            "registry": [
                ("InstallPath", "HKLM", r"Software\RaggieSoft\StoreTrainer", "Install Path", "[INSTALLDIR]"),
                ("StfxExt", "HKCR", ".stfx", "@", "RaggieSoft.StoreTrainer.Profile"),
                ("StfxProgId", "HKCR", "RaggieSoft.StoreTrainer.Profile", "@", "RaggieSoft StoreTrainer Game Profile"),
            ],
        },
    ]

    for idx, a in enumerate(apps, 1):
        proj_dir = apps_dir / a["slug"]
        proj_dir.mkdir(parents=True, exist_ok=True)
        write_rtf_license(
            proj_dir / "License.rtf",
            f"{a['name']} - Software License Agreement",
            "Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft. Software code is licensed under the MIT License; creative narrative/audio references are licensed under CC BY-SA 4.0.",
        )

        ico_rel = to_win_rel_or_abs(a["ico_path"], proj_dir) if a["ico_path"].exists() else ""
        inc_rel = to_win_rel_or_abs(shared_rsii, proj_dir)
        lines = [
            "; ==============================================================================",
            f"; FILE: projects/apps/{a['slug']}/{a['slug']}.rsip",
            f"; APPLICATION: {a['name']} (v{a['version']})",
            "; COMPILER: RaggieSoft Installer Studio (instbuild.exe -> .msi + Setup.exe)",
            "; ==============================================================================",
            "",
            f'@include "{inc_rel}"',
            "",
            "[Package]",
            f"Name                  = {a['name']}",
            f"Tagline               = {a['tagline']}",
            f"WebsiteLabel          = {a['website']}",
            f"Version               = {a['version']}",
            "Manufacturer          = RaggieSoft",
            f"UpgradeCode           = {a['upgrade_code']}",
            "Architecture          = $(ARCH)",
            "Scope                 = machine",
            f"OutputName            = raggiesoft-{a['slug']}-$(ARCH)",
            "OutputDir             = out",
            "BuildBootstrapper     = true",
            f"AboutUrl              = {a['about_url']}",
            "",
            "[Install]",
            f"DefaultSubdir         = {a['default_subdir']}",
            f"StartMenuFolder       = {a['name']}",
            f"MainExecutable        = {a['main_exe']}",
            "",
            "[UI]",
            "WizardLayout          = standard",
            f"AccentPrimaryHex      = {a['primary_hex']}",
            f"AccentSecondaryHex    = {a['secondary_hex']}",
            "LicenseRtf            = License.rtf",
        ]
        if ico_rel:
            lines.append(f"IconFile              = {ico_rel}")
        lines.extend([
            "InstallButtonText     = &Install",
            f"FinishLaunchCheckbox  = &Open {a['name']} when setup finishes",
            "",
        ])

        for pid, src, sub, feat in a["payloads"]:
            lines.append(f"[Payload.{pid}]")
            lines.append(f"Source                = {src}")
            if sub:
                lines.append(f"TargetSubdir          = {sub}")
            lines.append(f"Feature               = {feat}")
            lines.append("")

        for sid, sname, starget, sloc in a["shortcuts"]:
            lines.append(f"[Shortcut.{sid}]")
            lines.append(f"Name                  = {sname}")
            lines.append(f"TargetFile            = {starget}")
            lines.append(f"Location              = {sloc}")
            lines.append("")

        for rid, rroot, rkey, rname, rval in a["registry"]:
            lines.append(f"[Registry.{rid}]")
            lines.append(f"Root                  = {rroot}")
            lines.append(f"Key                   = {rkey}")
            lines.append(f"Name                  = {rname}")
            lines.append(f"Value                 = {rval}")
            lines.append("")

        rsip_path = proj_dir / f"{a['slug']}.rsip"
        rsip_path.write_text("\r\n".join(lines), encoding="utf-8", newline="")
        suite_entries.append((f"App_{a['slug']}", rsip_path, "CoreApps", 10 + idx))

    return len(apps)


def generate_book_micro_projects(shared_rsii: Path, suite_entries: list[tuple[str, Path, str, int]]) -> int:
    """Generates `640x480` `16:9` Autorun `.rsip` micro-installers for every visible book."""
    cat_path = ASSETS_ROOT / "raggiesoft-books" / "books" / "catalog.json"
    raw_books = json.loads(cat_path.read_text(encoding="utf-8"))
    book_ico = DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/BookReader/Assets/BookReader.ico"
    cross_img_pattern = re.compile(
        r"((?:raggiesoft-books|raggiesoft-backstage|engine-room-records)/images/[^\s\"\)\]#>]+)",
        re.IGNORECASE,
    )

    count = 0
    for b in raw_books:
        if b.get("hide"):
            continue
        slug = b["slug"].strip()
        title = clean_title(b["title"].strip())
        full_desc = (b.get("description") or b.get("short_description") or "").strip()
        is_crossroads = slug in ("cedar-run", "waystation") or "crossroads" in full_desc.lower()
        imprint = "RaggieSoft Story Crossroads" if is_crossroads else "Ocean View Archives"

        proj_dir = PROJECTS_ROOT / "books" / slug
        proj_dir.mkdir(parents=True, exist_ok=True)
        write_rtf_license(
            proj_dir / "License.rtf",
            f"{title} - Creative Commons Edition License (CC BY-SA 4.0)",
            "Copyright (c) 2026 Michael P. Ragsdale. Literary prose and illustrations in this offline edition are licensed under CC BY-SA 4.0.",
        )

        og_jpg = ASSETS_ROOT / "raggiesoft-books" / "images" / "covers" / "og" / f"{slug}.jpg"
        p2x3_jpg = ASSETS_ROOT / "raggiesoft-books" / "images" / "covers" / "2x3" / f"{slug}.jpg"
        book_src = ASSETS_ROOT / "raggiesoft-books" / "books" / slug
        themes_src = ASSETS_ROOT / "raggiesoft-books" / "css" / "themes"
        scenes_src = ASSETS_ROOT / "raggiesoft-books" / "images" / "scenes" / slug
        inc_rel = to_win_rel_or_abs(shared_rsii, proj_dir)

        lines = [
            "; ==============================================================================",
            f"; FILE: projects/books/{slug}/{slug}.rsip",
            f"; BOOK EDITION: {title} ({imprint})",
            "; LAYOUT: 640x480 16:9 OpenGraph CD-Autorun Micro-Installer (Per-User)",
            "; ==============================================================================",
            "",
            f'@include "{inc_rel}"',
            "",
            "[Package]",
            f"Name                  = {title}",
            f"Tagline               = {imprint} Offline Illustrated Edition",
            "WebsiteLabel          = books.raggiesoft.com",
            "Version               = 1.0.0",
            "Manufacturer          = RaggieSoft Media",
            f"UpgradeCode           = {det_guid(f'book:{slug}:upgrade')}",
            "Architecture          = $(ARCH)",
            "Scope                 = user",
            f"OutputName            = raggiesoft-book-{slug}-$(ARCH)",
            "OutputDir             = out",
            "BuildBootstrapper     = true",
            "HideFromInstalledApps = true",
            "AboutUrl              = https://books.raggiesoft.com",
            "",
            "[Install]",
            f"DefaultSubdir         = RaggieSoft\\Books\\{slug}",
            "",
            "[UI]",
            "WizardLayout          = autorun16x9",
            f"AuthorOrArtist        = {imprint}",
            "AccentPrimaryHex      = #14B8A6",
            "AccentSecondaryHex    = #F59E0B",
            "LicenseRtf            = License.rtf",
        ]
        if book_ico.exists():
            lines.append(f"IconFile              = {to_win_rel_or_abs(book_ico, proj_dir)}")
        if og_jpg.exists():
            lines.append(f"ArtworkPng            = {to_win_rel_or_abs(og_jpg, proj_dir)}")
        elif p2x3_jpg.exists():
            lines.append(f"ArtworkPng            = {to_win_rel_or_abs(p2x3_jpg, proj_dir)}")
        lines.extend([
            "InstallButtonText     = &Install Book",
            "OfferLaunchOnFinish   = false",
            "",
            "[Payload.Chapters]",
            f"Source                = {to_win_rel_or_abs(book_src, proj_dir)}",
            "",
            "[Payload.Themes]",
            f"Source                = {to_win_rel_or_abs(themes_src, proj_dir)}",
            "TargetSubdir          = themes",
            "",
        ])

        if p2x3_jpg.exists():
            lines.extend([
                "[Payload.Cover2x3]",
                f"Source                = {to_win_rel_or_abs(p2x3_jpg, proj_dir)}",
                r"TargetSubdir          = assets\raggiesoft-books\images\covers\2x3",
                "",
            ])
        if og_jpg.exists():
            lines.extend([
                "[Payload.CoverOg]",
                f"Source                = {to_win_rel_or_abs(og_jpg, proj_dir)}",
                r"TargetSubdir          = assets\raggiesoft-books\images\covers\og",
                "",
            ])
        if scenes_src.is_dir() and any(f.is_file() for f in scenes_src.rglob("*")):
            lines.extend([
                "[Payload.Scenes]",
                f"Source                = {to_win_rel_or_abs(scenes_src, proj_dir)}",
                f"TargetSubdir          = assets\\raggiesoft-books\\images\\scenes\\{slug}",
                "",
            ])

        own_prefix = f"raggiesoft-books/images/scenes/{slug}/".lower()
        extra_imgs: set[str] = set()
        if book_src.is_dir():
            for md_f in book_src.rglob("*.md"):
                txt = md_f.read_text(encoding="utf-8", errors="ignore")
                for m in cross_img_pattern.finditer(txt):
                    rel_i = m.group(1).strip("/")
                    if not rel_i.lower().startswith(own_prefix) and "/images/covers/" not in rel_i.lower():
                        if (ASSETS_ROOT / rel_i).is_file():
                            extra_imgs.add(rel_i)

        for idx, extra_rel in enumerate(sorted(extra_imgs), 1):
            abs_extra = ASSETS_ROOT / extra_rel
            sub_win = "assets\\" + str(Path(extra_rel).parent).replace("/", "\\")
            lines.extend([
                f"[Payload.CrossPlate{idx}]",
                f"Source                = {to_win_rel_or_abs(abs_extra, proj_dir)}",
                f"TargetSubdir          = {sub_win}",
                "",
            ])

        lines.extend([
            "[Registry.PackList]",
            "Root                  = HKCU",
            r"Key                   = Software\RaggieSoft\Book Reader\Books\List",
            f"Name                  = {slug}",
            "Value                 = [INSTALLDIR]",
            "",
            "[Registry.PackDetail]",
            "Root                  = HKCU",
            f"Key                   = Software\\RaggieSoft\\Book Reader\\Books\\{slug}",
            "Name                  = Install Path",
            "Value                 = [INSTALLDIR]",
            "",
        ])

        rsip_path = proj_dir / f"{slug}.rsip"
        rsip_path.write_text("\r\n".join(lines), encoding="utf-8", newline="")
        count += 1
        suite_entries.append((f"Book_{slug}", rsip_path, "Books", 100 + count))

    return count


def generate_album_micro_projects(shared_rsii: Path, suite_entries: list[tuple[str, Path, str, int]]) -> int:
    """Generates `640x480` `16:9` Autorun `.rsip` micro-installers for every HQ Vault album."""
    cat_path = ASSETS_ROOT / "engine-room-records" / "catalog.json"
    raw_albums = json.loads(cat_path.read_text(encoding="utf-8"))
    music_ico = DESKTOP_APPS_ROOT / "windows/RaggieSoftApps/ListeningRoom/Assets/ListeningRoom.ico"

    count = 0
    for a in raw_albums:
        artist_slug = a["artistSlug"].strip()
        album_slug = a["slug"].strip()
        vault_mp3_dir = VAULTS_ROOT / "engine-room-records" / "artists" / artist_slug / album_slug / "vault" / "mp3"
        if not vault_mp3_dir.is_dir() or not any(vault_mp3_dir.glob("*.mp3")):
            continue

        raw_title = a["title"].strip()
        artist_name = a["artist"].strip()
        combined = clean_title(f"{raw_title} - {artist_name}", max_len=256)
        disp_title = combined if len(combined) <= 56 else clean_title(raw_title)

        proj_dir = PROJECTS_ROOT / "albums" / artist_slug / album_slug
        proj_dir.mkdir(parents=True, exist_ok=True)
        write_rtf_license(
            proj_dir / "License.rtf",
            f"{disp_title} - Personal Collector & Supporter Audio License",
            "Copyright (c) 2026 Michael P. Ragsdale (Engine Room Records / RaggieSoft Media). Licensed for personal offline listening across your devices.",
        )

        asset_album = ASSETS_ROOT / "engine-room-records" / "artists" / artist_slug / album_slug
        og_jpg = asset_album / "social-preview.jpg"
        sq_jpg = asset_album / "album-art.jpg"
        tracks_json = asset_album / "tracks.json"
        lyrics_dir = asset_album / "lyrics"
        prompts_dir = asset_album / "suno-prompts"
        inc_rel = to_win_rel_or_abs(shared_rsii, proj_dir)

        lines = [
            "; ==============================================================================",
            f"; FILE: projects/albums/{artist_slug}/{album_slug}/{album_slug}.rsip",
            f"; ALBUM EDITION: {disp_title} ({artist_name})",
            "; LAYOUT: 640x480 16:9 OpenGraph CD-Autorun Micro-Installer (Per-User Vault)",
            "; ==============================================================================",
            "",
            f'@include "{inc_rel}"',
            "",
            "[Package]",
            f"Name                  = {disp_title}",
            f"Tagline               = {artist_name} - Engine Room Records High-Quality Edition",
            "WebsiteLabel          = music.raggiesoft.com",
            "Version               = 1.0.0",
            "Manufacturer          = Engine Room Records",
            f"UpgradeCode           = {det_guid(f'album:{artist_slug}/{album_slug}:upgrade')}",
            "Architecture          = $(ARCH)",
            "Scope                 = user",
            f"OutputName            = raggiesoft-album-{album_slug}-$(ARCH)",
            "OutputDir             = out",
            "BuildBootstrapper     = true",
            "HideFromInstalledApps = true",
            "AboutUrl              = https://music.raggiesoft.com",
            "",
            "[Install]",
            f"DefaultSubdir         = RaggieSoft\\Music\\{artist_slug}\\{album_slug}",
            "",
            "[UI]",
            "WizardLayout          = autorun16x9",
            f"AuthorOrArtist        = {artist_name}",
            "AccentPrimaryHex      = #06B6D4",
            "AccentSecondaryHex    = #EC4899",
            "LicenseRtf            = License.rtf",
        ]
        if music_ico.exists():
            lines.append(f"IconFile              = {to_win_rel_or_abs(music_ico, proj_dir)}")
        if og_jpg.exists():
            lines.append(f"ArtworkPng            = {to_win_rel_or_abs(og_jpg, proj_dir)}")
        elif sq_jpg.exists():
            lines.append(f"ArtworkPng            = {to_win_rel_or_abs(sq_jpg, proj_dir)}")
        lines.extend([
            "InstallButtonText     = &Install Album",
            "OfferLaunchOnFinish   = false",
            "",
            "[Payload.VaultMp3]",
            f"Source                = {to_win_rel_or_abs(vault_mp3_dir, proj_dir)}",
            "TargetSubdir          = hq-mp3",
            "",
        ])

        for p_id, f_path in [("TracksJson", tracks_json), ("AlbumArt", sq_jpg), ("SocialArt", og_jpg)]:
            if f_path.is_file():
                lines.extend([
                    f"[Payload.{p_id}]",
                    f"Source                = {to_win_rel_or_abs(f_path, proj_dir)}",
                    "",
                ])
        if lyrics_dir.is_dir() and any(lyrics_dir.iterdir()):
            lines.extend([
                "[Payload.LyricsVtt]",
                f"Source                = {to_win_rel_or_abs(lyrics_dir, proj_dir)}",
                "TargetSubdir          = lyrics",
                "",
            ])
        if prompts_dir.is_dir() and any(prompts_dir.iterdir()):
            lines.extend([
                "[Payload.SunoPrompts]",
                f"Source                = {to_win_rel_or_abs(prompts_dir, proj_dir)}",
                "TargetSubdir          = suno-prompts",
                "",
            ])

        pack_key = f"{artist_slug}--{album_slug}"
        lines.extend([
            "[Registry.AlbumList]",
            "Root                  = HKCU",
            r"Key                   = Software\RaggieSoft\The Listening Room\Albums\List",
            f"Name                  = {pack_key}",
            "Value                 = [INSTALLDIR]",
            "",
            "[Registry.AlbumDetail]",
            "Root                  = HKCU",
            f"Key                   = Software\\RaggieSoft\\The Listening Room\\Albums\\{pack_key}",
            "Name                  = Install Path",
            "Value                 = [INSTALLDIR]",
            "",
        ])

        rsip_path = proj_dir / f"{album_slug}.rsip"
        rsip_path.write_text("\r\n".join(lines), encoding="utf-8", newline="")
        count += 1
        suite_entries.append((f"Album_{album_slug}", rsip_path, "Albums", 200 + count))

    return count


def write_master_suite(suite_entries: list[tuple[str, Path, str, int]]) -> Path:
    """Writes `RaggieSoft.rsis` grouping all CoreApps, Books, & HQ Albums like Visual Studio `.slnx`."""
    suite_path = STUDIO_ROOT / "RaggieSoft.rsis"
    lines = [
        "; ==============================================================================",
        "; FILE: desktop-apps/installer-studio/RaggieSoft.rsis",
        "; SUITE: RaggieSoft Complete Windows Application, Book & Music Suite",
        "; USAGE:",
        ";   instbuild.exe RaggieSoft.rsis --validate",
        ";   instbuild.exe RaggieSoft.rsis --category=CoreApps",
        ";   instbuild.exe RaggieSoft.rsis --project=installer-studio --with-vpat",
        ";   InstallerStudio.exe RaggieSoft.rsis",
        "; ==============================================================================",
        "",
        "[Suite]",
        "Name                  = RaggieSoft Complete Windows Suite",
        "Description           = 7 Core Desktop Apps, All Ocean View / Story Crossroads Books, and Engine Room Records HQ Albums",
        "Version               = 1.0.0",
        "Architectures         = arm64|x64",
        "",
        "[Variable]",
        "STUDIO_PORTAL         = https://raggiesoftstudio.com",
        "DEV_PORTAL            = https://raggiesoft.dev",
        "",
    ]
    for pid, abs_rsip, cat, order in suite_entries:
        rel_p = to_win_rel_or_abs(abs_rsip, STUDIO_ROOT)
        lines.extend([
            f"[Project.{pid}]",
            f"Path                  = {rel_p}",
            f"Category              = {cat}",
            f"BuildOrder            = {order}",
            "",
        ])
    suite_path.write_text("\r\n".join(lines), encoding="utf-8", newline="")
    return suite_path


def main() -> None:
    """Cleanly generates `projects/` + `RaggieSoft.rsis` across Apps, Books, and HQ Albums."""
    if PROJECTS_ROOT.exists():
        shutil.rmtree(PROJECTS_ROOT)
    suite_entries: list[tuple[str, Path, str, int]] = []
    shared_rsii = write_shared_rsii_include()
    n_apps = generate_core_app_projects(shared_rsii, suite_entries)
    n_books = generate_book_micro_projects(shared_rsii, suite_entries)
    n_albums = generate_album_micro_projects(shared_rsii, suite_entries)
    suite_file = write_master_suite(suite_entries)
    print(
        f"[OK] Generated {n_apps} Core App .rsip projects, "
        f"{n_books} Book Autorun .rsip projects, and "
        f"{n_albums} HQ Album Autorun .rsip projects + master suite {suite_file.name}"
    )


if __name__ == "__main__":
    main()
