#!/usr/bin/env python3
# ================================================================================
# FILE: desktop-apps/installer-studio/tools/stage-offline-app-seeds.py
# PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
# WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
# LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
#
# PURPOSE: Stages 100% Offline-Ready Seed Bundles (`OfflineSeed\`) for RaggieSoft's
#          Windows Desktop Applications (`launcher`, `book-reader`, `listening-room`,
#          and `backstage`) so every installed app launches, browses its complete
#          bookshelf/discography/dossiers, displays every cover & lyric, and reads
#          all Backstage bonus features with ZERO internet connection required!
# INPUTS:  - `raggiesoft-servers/raggiesoft-assets/{raggiesoft-books,engine-room-records,raggiesoft-backstage,desktop-apps,common}`
#          - `raggiesoft-servers/raggiesoft.com/subdomains/backstage/dist/**/*.html`
# OUTPUTS: - `desktop-apps/installer-studio/offline-seeds/launcher/`
#          - `desktop-apps/installer-studio/offline-seeds/book-reader/`
#          - `desktop-apps/installer-studio/offline-seeds/listening-room/`
#          - `desktop-apps/installer-studio/offline-seeds/backstage/`
# ================================================================================
# PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
#   Why do we build an `OfflineSeed\` folder for each desktop application?
#     Imagine installing `RaggieSoft Book Reader`, `The Listening Room`,
#     `RaggieSoft Backstage`, or `RaggieSoft Launcher` onto a laptop and opening
#     it on an airplane with Wi-Fi completely turned off!
#
#     Without a local seed bundle, the app would try to fetch `catalog.json`,
#     cover images, `.vtt` lyrics, or Backstage dossiers from `https://assets.raggiesoft.com`
#     on first launch and show empty placeholders offline.
#
#     Instead, this script creates an architecture-neutral `OfflineSeed\` folder
#     for each app using the EXACT same relative folder paths as `assets.raggiesoft.com`:
#       - JSON catalogs, Tables of Contents (`toc.json`), Story Arcs, `.vtt`/`.md`
#         lyrics, songwriting notes, CSS themes, and Backstage JSON dossiers are
#         copied losslessly byte-for-byte.
#       - Huge `3000x3000` print-master cover sleeves and `2K` Backstage plates are
#         resized cleanly with high-quality `LANCZOS` resampling (`480x720` / `600x600`
#         covers and `1280px` Retina plates) so every single cover and Backstage
#         illustration looks razor-sharp offline while keeping each core app installer
#         compact (`~12-18 MB` total)!
#       - Meanwhile, installing a Book Micro-Installer or Supporter HQ Album
#         Micro-Installer into `%LOCALAPPDATA%\RaggieSoft\{Books,Music}` automatically
#         takes higher priority (`Tier 1`) over `OfflineSeed\` (`Tier 3`), upgrading
#         books to full-resolution scene illustrations and albums to `320 kbps` HQ audio!
# ================================================================================

from __future__ import annotations

import json
import re
import shutil
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image


# Resolve workspace paths dynamically (`Y:\` in Parallels Windows 11 Pro, `/Users/michael/Developer` on macOS).
# `Path(__file__).resolve()` turns this script's path into an absolute filesystem path,
# and `.parent` steps upward one directory level at a time (`tools` -> `installer-studio` -> `desktop-apps` -> root).
SCRIPT_DIR = Path(__file__).resolve().parent
SETUP_STUDIO_ROOT = SCRIPT_DIR.parent
SEEDS_ROOT = SETUP_STUDIO_ROOT / "offline-seeds"
WORKSPACE_ROOT = SETUP_STUDIO_ROOT.parent.parent
SERVERS_ROOT = WORKSPACE_ROOT / "raggiesoft-servers"
ASSETS_ROOT = SERVERS_ROOT / "raggiesoft-assets"
BACKSTAGE_DIST_ROOT = SERVERS_ROOT / "raggiesoft.com" / "subdomains" / "backstage" / "dist"


def copy_if_newer(src: Path, dst: Path) -> bool:
    """
    Copies `src` to `dst` only if `src` exists and `dst` is missing or older than `src`.
    Comparing `st_mtime` (modification timestamp) and `st_size` (byte length) makes
    repeat builds finish in under half a second without redundant disk writes!
    """
    if not src.is_file():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.is_file() and dst.stat().st_mtime >= src.stat().st_mtime and dst.stat().st_size == src.stat().st_size:
        return False
    shutil.copy2(src, dst)
    return True


def stage_resized_image(
    src: Path,
    dst: Path,
    max_width: int,
    max_height: int,
    jpeg_quality: int = 83,
) -> bool:
    """
    Downscales high-resolution master artwork proportionally to fit within `(max_width, max_height)`
    using `Image.Resampling.LANCZOS` (never upscaling smaller images!) so every offline bookshelf,
    album grid, and Backstage dossier renders instantaneously without bloating the `.msi` / `.exe`.
    """
    if not src.is_file():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)

    # Incremental build guard: skip re-encoding if `dst` already exists and is newer than `src`.
    if dst.is_file() and dst.stat().st_mtime >= src.stat().st_mtime and dst.stat().st_size > 0:
        return False

    ext = src.suffix.lower()
    if ext not in (".jpg", ".jpeg", ".png", ".webp"):
        return copy_if_newer(src, dst)

    # Open the image safely inside a Python `with` context manager so file handles close immediately.
    with Image.open(src) as im:
        w, h = im.size
        # If the source image is already compact (`<= 145 KB` and within target bounds), copy it verbatim.
        if w <= max_width and h <= max_height and src.stat().st_size <= 145_000:
            shutil.copy2(src, dst)
            return True

        # `min(1.0, ...)` preserves aspect ratio while guaranteeing we NEVER upscale small graphics.
        scale = min(1.0, max_width / max(1, w), max_height / max(1, h))
        new_w = max(1, int(round(w * scale)))
        new_h = max(1, int(round(h * scale)))

        if ext in (".jpg", ".jpeg"):
            rgb = im.convert("RGB")
            if (new_w, new_h) != (w, h):
                rgb = rgb.resize((new_w, new_h), Image.Resampling.LANCZOS)
            rgb.save(dst, format="JPEG", quality=jpeg_quality, optimize=True, progressive=True)
        elif ext == ".png":
            rgba = im.convert("RGBA") if im.mode in ("RGBA", "LA", "P") else im.convert("RGB")
            if (new_w, new_h) != (w, h):
                rgba = rgba.resize((new_w, new_h), Image.Resampling.LANCZOS)
            rgba.save(dst, format="PNG", optimize=True)
        else:
            resized = im.resize((new_w, new_h), Image.Resampling.LANCZOS) if (new_w, new_h) != (w, h) else im
            resized.save(dst, quality=jpeg_quality)

    return True


def collect_referenced_asset_paths_from_text(raw_text: str) -> set[str]:
    r"""
    Scans HTML or JSON text using a regular expression (`re.compile`) for any
    `assets.raggiesoft.com` or root-relative `/raggiesoft-*` / `/engine-room-*` image path
    so every inline or hero plate referenced by Story Arcs or Backstage dossiers is staged.
    """
    pattern = re.compile(
        r"(?:https://assets\.raggiesoft\.com/|(?<=[\"\'\(\s])/)"
        r"((?:raggiesoft-books|raggiesoft-backstage|engine-room-records|common)/[^\s\"\'\)\]#<>?]+)",
        re.IGNORECASE,
    )
    hits: set[str] = set()
    for match in pattern.finditer(raw_text):
        rel = match.group(1).strip("/")
        # Skip iPhone/iPad Safari Home Screen WebClip startup splashes (`apple-splash-*.jpg`),
        # which are Safari-only mobile tags never requested by Windows desktop apps.
        if "/pwa-icons/apple-splash-" in rel.lower():
            continue
        hits.add(rel)
    return hits


def stage_launcher_seed(books_list: list[dict], albums_list: list[dict]) -> Path:
    """Stages `offline-seeds/launcher/` with app/book/album catalogs and compact covers."""
    seed = SEEDS_ROOT / "launcher"
    seed.mkdir(parents=True, exist_ok=True)

    # 1. Copy master JSON manifests so RaggieSoft Launcher populates every tab 100% offline.
    for rel in (
        "desktop-apps/windows/apps-manifest.json",
        "desktop-apps/windows/books/book-packs-manifest.json",
        "raggiesoft-books/books/catalog.json",
        "engine-room-records/catalog.json",
        "engine-room-records/json/master-catalog.json",
    ):
        copy_if_newer(ASSETS_ROOT / rel, seed / rel)

    # 2. Stage compact portrait (`2x3`) and widescreen (`og`) book covers.
    for b in books_list:
        if b.get("hide"):
            continue
        slug = b["slug"].strip()
        for rel_cov, mw, mh in (
            (f"raggiesoft-books/images/covers/2x3/{slug}.jpg", 360, 540),
            (f"raggiesoft-books/images/covers/og/{slug}.jpg", 640, 336),
        ):
            stage_resized_image(ASSETS_ROOT / rel_cov, seed / rel_cov, mw, mh, jpeg_quality=80)

    # 3. Stage compact square (`1x1`) and widescreen (`og`) album sleeves.
    for a in albums_list:
        artist = a["artistSlug"].strip()
        album = a["slug"].strip()
        for rel_art, mw, mh in (
            (f"engine-room-records/artists/{artist}/{album}/album-art.jpg", 360, 360),
            (f"engine-room-records/artists/{artist}/{album}/social-preview.jpg", 640, 336),
        ):
            stage_resized_image(ASSETS_ROOT / rel_art, seed / rel_art, mw, mh, jpeg_quality=80)

    return seed


def stage_book_reader_seed(books_list: list[dict], albums_list: list[dict]) -> Path:
    """Stages `offline-seeds/book-reader/` with TOCs, themes, covers, dossiers, and `.vtt` lyrics."""
    seed = SEEDS_ROOT / "book-reader"
    seed.mkdir(parents=True, exist_ok=True)

    # 1. Copy master book & music catalogs plus `reader.css` and `story-arcs.json`.
    for rel in (
        "raggiesoft-books/books/catalog.json",
        "raggiesoft-books/json/story-arcs.json",
        "raggiesoft-books/css/reader.css",
        "engine-room-records/catalog.json",
        "engine-room-records/json/master-catalog.json",
    ):
        copy_if_newer(ASSETS_ROOT / rel, seed / rel)

    # 2. Copy all narrative theme stylesheets (`raggiesoft-books/css/themes/*.css`).
    themes_dir = ASSETS_ROOT / "raggiesoft-books" / "css" / "themes"
    if themes_dir.is_dir():
        for css_file in themes_dir.glob("*.css"):
            copy_if_newer(css_file, seed / css_file.relative_to(ASSETS_ROOT))

    # 3. Copy Backstage JSON dossiers so inline chapter bonus callouts open offline.
    bs_api_dir = ASSETS_ROOT / "raggiesoft-backstage" / "api"
    if bs_api_dir.is_dir():
        for jf in bs_api_dir.rglob("*.json"):
            copy_if_newer(jf, seed / jf.relative_to(ASSETS_ROOT))

    # 4. Copy curated music playlist manifests (`engine-room-records/playlists/**/playlist.json`).
    playlists_dir = ASSETS_ROOT / "engine-room-records" / "playlists"
    if playlists_dir.is_dir():
        for pf in playlists_dir.rglob("*.json"):
            copy_if_newer(pf, seed / pf.relative_to(ASSETS_ROOT))

    # 5. Copy every visible book's `toc.json` and stage crisp `2x3` + `og` covers.
    for b in books_list:
        if b.get("hide"):
            continue
        slug = b["slug"].strip()
        toc_rel = f"raggiesoft-books/books/{slug}/toc.json"
        copy_if_newer(ASSETS_ROOT / toc_rel, seed / toc_rel)

        raw_img = (b.get("image") or "").replace("{{CDN}}/", "").replace("https://assets.raggiesoft.com/", "").lstrip("/")
        if raw_img:
            stage_resized_image(ASSETS_ROOT / raw_img, seed / raw_img, 480, 720, jpeg_quality=84)

        for rel_cov, mw, mh in (
            (f"raggiesoft-books/images/covers/2x3/{slug}.jpg", 480, 720),
            (f"raggiesoft-books/images/covers/og/{slug}.jpg", 960, 504),
        ):
            stage_resized_image(ASSETS_ROOT / rel_cov, seed / rel_cov, mw, mh, jpeg_quality=84)

    # 6. Stage Story Arc hero banners referenced inside `story-arcs.json`.
    arcs_file = ASSETS_ROOT / "raggiesoft-books" / "json" / "story-arcs.json"
    if arcs_file.is_file():
        for ref_rel in collect_referenced_asset_paths_from_text(arcs_file.read_text(encoding="utf-8", errors="ignore")):
            src_img = ASSETS_ROOT / ref_rel
            if src_img.is_file() and src_img.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp"):
                stage_resized_image(src_img, seed / ref_rel, 960, 540, jpeg_quality=83)

    # 7. Stage compact album cover thumbnails + `.vtt` timed lyrics for chapter soundtrack bars.
    for a in albums_list:
        artist = a["artistSlug"].strip()
        album = a["slug"].strip()
        rel_art = f"engine-room-records/artists/{artist}/{album}/album-art.jpg"
        stage_resized_image(ASSETS_ROOT / rel_art, seed / rel_art, 360, 360, jpeg_quality=80)

        lyrics_dir = ASSETS_ROOT / "engine-room-records" / "artists" / artist / album / "lyrics"
        if lyrics_dir.is_dir():
            for vtt in lyrics_dir.glob("*.vtt"):
                copy_if_newer(vtt, seed / vtt.relative_to(ASSETS_ROOT))

    return seed


def stage_listening_room_seed(albums_list: list[dict]) -> Path:
    """Stages `offline-seeds/listening-room/` with discography JSON, sleeves, and `.vtt` lyrics."""
    seed = SEEDS_ROOT / "listening-room"
    seed.mkdir(parents=True, exist_ok=True)

    # 1. Copy global network styles, scripts, and master music catalogs.
    for rel in (
        "common/css/raggiesoft-network.css",
        "common/js/raggiesoft-network.js",
        "engine-room-records/catalog.json",
        "engine-room-records/json/master-catalog.json",
        "engine-room-records/images/pwa-icons/apple-icon-180.png",
    ):
        copy_if_newer(ASSETS_ROOT / rel, seed / rel)

    # 2. Stage Engine Room Records brand logos and playlist artwork.
    logos_dir = ASSETS_ROOT / "engine-room-records" / "images" / "logos"
    if logos_dir.is_dir():
        for lf in logos_dir.rglob("*"):
            if lf.is_file():
                stage_resized_image(lf, seed / lf.relative_to(ASSETS_ROOT), 800, 800, jpeg_quality=85)

    playlists_dir = ASSETS_ROOT / "engine-room-records" / "playlists"
    if playlists_dir.is_dir():
        for pf in playlists_dir.rglob("*"):
            if pf.is_file():
                stage_resized_image(pf, seed / pf.relative_to(ASSETS_ROOT), 640, 640, jpeg_quality=82)

    # 3. Stage per-album `tracks.json`, `album-art.jpg`, `social-preview.jpg`, `lyrics/`, and `suno-prompts/`.
    for a in albums_list:
        artist = a["artistSlug"].strip()
        album = a["slug"].strip()
        album_src = ASSETS_ROOT / "engine-room-records" / "artists" / artist / album
        if not album_src.is_dir():
            continue

        copy_if_newer(album_src / "tracks.json", seed / f"engine-room-records/artists/{artist}/{album}/tracks.json")
        stage_resized_image(
            album_src / "album-art.jpg",
            seed / f"engine-room-records/artists/{artist}/{album}/album-art.jpg",
            600,
            600,
            jpeg_quality=84,
        )
        stage_resized_image(
            album_src / "social-preview.jpg",
            seed / f"engine-room-records/artists/{artist}/{album}/social-preview.jpg",
            960,
            504,
            jpeg_quality=83,
        )

        for sub in ("lyrics", "suno-prompts"):
            sub_dir = album_src / sub
            if sub_dir.is_dir():
                for tf in sub_dir.iterdir():
                    if tf.is_file():
                        copy_if_newer(tf, seed / tf.relative_to(ASSETS_ROOT))

    return seed


def stage_backstage_seed() -> Path:
    """Stages `offline-seeds/backstage/` with API v1 dossiers and Retina behind-the-scenes plates."""
    seed = SEEDS_ROOT / "backstage"
    seed.mkdir(parents=True, exist_ok=True)

    for rel in (
        "common/css/raggiesoft-network.css",
        "common/js/raggiesoft-network.js",
        "raggiesoft-books/json/story-arcs.json",
    ):
        copy_if_newer(ASSETS_ROOT / rel, seed / rel)

    referenced_paths: set[str] = set()

    # 1. Copy all Backstage API JSON dossiers & collect referenced inline/hero images.
    bs_api_dir = ASSETS_ROOT / "raggiesoft-backstage" / "api"
    if bs_api_dir.is_dir():
        for jf in bs_api_dir.rglob("*.json"):
            copy_if_newer(jf, seed / jf.relative_to(ASSETS_ROOT))
            referenced_paths.update(
                collect_referenced_asset_paths_from_text(jf.read_text(encoding="utf-8", errors="ignore"))
            )

    # 2. Scan compiled Backstage SSG HTML pages for every `<img src="https://assets.raggiesoft.com/...">` plate.
    if BACKSTAGE_DIST_ROOT.is_dir():
        for hf in BACKSTAGE_DIST_ROOT.rglob("*.html"):
            referenced_paths.update(
                collect_referenced_asset_paths_from_text(hf.read_text(encoding="utf-8", errors="ignore"))
            )

    # 3. Stage all `raggiesoft-backstage/images/**` (skipping iOS Safari `apple-splash-*` files).
    bs_img_dir = ASSETS_ROOT / "raggiesoft-backstage" / "images"
    if bs_img_dir.is_dir():
        for img_f in bs_img_dir.rglob("*"):
            if not img_f.is_file():
                continue
            rel_str = img_f.relative_to(ASSETS_ROOT).as_posix()
            if "/pwa-icons/apple-splash-" in rel_str.lower():
                continue
            stage_resized_image(img_f, seed / rel_str, 1280, 1280, jpeg_quality=84)

    # 4. Stage any cross-folder book/music plates referenced on Backstage pages or dossiers.
    for rel_str in sorted(referenced_paths):
        src_f = ASSETS_ROOT / rel_str
        if src_f.is_file():
            if src_f.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp"):
                stage_resized_image(src_f, seed / rel_str, 1280, 1280, jpeg_quality=84)
            else:
                copy_if_newer(src_f, seed / rel_str)

    # 5. Write `.offline_backstage_installed` sentinel so Backstage immediately shows offline readiness.
    sentinel = seed / "raggiesoft-backstage" / ".offline_backstage_installed"
    sentinel.parent.mkdir(parents=True, exist_ok=True)
    if not sentinel.is_file():
        sentinel.write_text(datetime.now(timezone.utc).isoformat(), encoding="utf-8")

    return seed


def dir_size_mb(folder: Path) -> float:
    """Computes the total size of all files under `folder` in megabytes (`1024 * 1024` bytes)."""
    total = sum(f.stat().st_size for f in folder.rglob("*") if f.is_file())
    return total / (1024 * 1024)


def main() -> None:
    """Entry point: reads master book & music catalogs and populates all 4 `offline-seeds/` bundles."""
    print("[*] Staging 100% Offline-Ready Seed Bundles (offline-seeds\\) for RaggieSoft Installer Studio...")
    books_cat = json.loads((ASSETS_ROOT / "raggiesoft-books" / "books" / "catalog.json").read_text(encoding="utf-8"))
    albums_cat = json.loads((ASSETS_ROOT / "engine-room-records" / "catalog.json").read_text(encoding="utf-8"))

    seeds = [
        ("launcher", stage_launcher_seed(books_cat, albums_cat)),
        ("book-reader", stage_book_reader_seed(books_cat, albums_cat)),
        ("listening-room", stage_listening_room_seed(albums_cat)),
        ("backstage", stage_backstage_seed()),
    ]
    for name, path in seeds:
        file_count = sum(1 for f in path.rglob("*") if f.is_file())
        print(f"    - OfflineSeed [{name:<14}] -> {file_count:>4} files ({dir_size_mb(path):6.2f} MB) at {path}")
    print("[OK] Finished staging OfflineSeed bundles for all 4 hybrid desktop apps!")


if __name__ == "__main__":
    main()
