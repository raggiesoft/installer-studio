// ================================================================================
// FILE: src/compiler/RswMsmMerger.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is a Merge Module (`.msm`), and how do we merge one into `Product.msi`?
//     Microsoft Visual C++ (`Microsoft_VC143_CRT_x64.msm` / `ARM64.msm`), third-party
//     SDKs, and our own `instbuild.exe --emit-msm` package reusable DLLs, registry
//     keys, and COM registrations as `.msm` Merge Modules.
//
//   Why build a Clean-Room Native 64-Bit Merger (`msi.dll` + `<fdi.h>`) instead of
//   calling legacy `mergemod.dll` COM?
//     Microsoft's SDK `mergemod.dll` is a 32-bit x86 COM DLL that is NOT pre-installed
//     on customer/developer Windows 11 machines and cannot be loaded in-process by a
//     native 64-bit `ARM64` or `x64` compiler!
//     Instead, `System32\msi.dll` + `System32\cabinet.dll` (`<fdi.h>` File Decompression
//     Interface) are built into 100% of every 64-bit Windows installation:
//       1. Open `<module>.msm` read-only via `MsiOpenDatabaseW`.
//       2. Extract `_Streams.MergeModule.CABinet` to a scratch `.cab` and unpack every
//          file via `FDICreate` / `FDICopy` into `scratchDir`, appending them to the
//          parent MSI's `CabInputEntry` queue so all files pack cleanly into ONE unified
//          `#payload.cab`!
//       3. Redirect the module's root directory (`TARGETDIR` / `MergeRedirectFolder`)
//          onto `[MergeModule.<id>] TargetRoot` (e.g. `INSTALLDIR` or `System64Folder`),
//          and copy `Directory`, `Component` (preserving 64-bit flags), `File` (with
//          re-numbered `Sequence`), `Registry`, `CreateFolder`, and `FeatureComponents`
//          (bound to `[MergeModule.<id>] Feature`) straight into `Product.msi`!
// ================================================================================

#pragma once

#include "../common/RswPlatform.hpp"
#include "RswCabBuilder.hpp"

#include <fcntl.h>
#include <fdi.h>
#include <msi.h>
#include <msiquery.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "Cabinet.lib")
#pragma comment(lib, "msi.lib")

namespace rsw {

namespace msm_fdi {

struct FdiExtractContext {
    std::filesystem::path extractDir;
    std::map<std::string, std::filesystem::path> extractedFiles; // `cabFileKey -> extractedPath`
};

inline thread_local FdiExtractContext* gActiveFdiCtx = nullptr;

inline FNALLOC(FdiAlloc) { return std::malloc(cb); }
inline FNFREE(FdiFree)   { std::free(pv); }

inline FNOPEN(FdiOpen) {
    const std::wstring wPath = Utf8ToWide(pszFile ? pszFile : "");
    DWORD access = (oflag & (_O_WRONLY | _O_RDWR)) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    DWORD disp   = (oflag & _O_CREAT) ? CREATE_ALWAYS : OPEN_EXISTING;
    (void)pmode;
    HANDLE h = ::CreateFileW(wPath.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
    return (h == INVALID_HANDLE_VALUE) ? static_cast<INT_PTR>(-1) : reinterpret_cast<INT_PTR>(h);
}

inline FNREAD(FdiRead) {
    DWORD got = 0;
    if (!::ReadFile(reinterpret_cast<HANDLE>(hf), pv, cb, &got, nullptr)) return static_cast<UINT>(-1);
    return got;
}

inline FNWRITE(FdiWrite) {
    DWORD written = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(hf), pv, cb, &written, nullptr)) return static_cast<UINT>(-1);
    return written;
}

inline FNCLOSE(FdiClose) {
    return ::CloseHandle(reinterpret_cast<HANDLE>(hf)) ? 0 : -1;
}

inline FNSEEK(FdiSeek) {
    LARGE_INTEGER liDist{}, pos{};
    liDist.QuadPart = dist;
    if (!::SetFilePointerEx(reinterpret_cast<HANDLE>(hf), liDist, &pos, static_cast<DWORD>(seektype))) return -1L;
    return static_cast<long>(pos.QuadPart);
}

inline FNFDINOTIFY(FdiNotify) {
    auto* ctx = static_cast<FdiExtractContext*>(pfdin->pv);
    if (fdint == fdintCOPY_FILE && ctx && pfdin->psz1) {
        const std::string key = pfdin->psz1;
        std::string safeName = key;
        for (char& c : safeName) {
            if (c == '\\' || c == '/' || c == ':') c = '_';
        }
        const std::filesystem::path outFile = ctx->extractDir / Utf8ToWide(safeName);
        ctx->extractedFiles[key] = outFile;
        const std::string u8 = WideToUtf8(outFile.wstring());
        return FdiOpen(const_cast<LPSTR>(u8.c_str()), _O_BINARY | _O_CREAT | _O_WRONLY, _S_IREAD | _S_IWRITE);
    }
    if (fdint == fdintCLOSE_FILE_INFO) {
        FdiClose(pfdin->hf);
        return TRUE;
    }
    return 0;
}

inline bool ExtractCabToDir(
    const std::filesystem::path& cabFile,
    FdiExtractContext& ioCtx,
    std::string& outError)
{
    std::error_code ec;
    std::filesystem::create_directories(ioCtx.extractDir, ec);

    ERF erf{};
    HFDI hfdi = ::FDICreate(
        &FdiAlloc, &FdiFree, &FdiOpen, &FdiRead, &FdiWrite, &FdiClose, &FdiSeek,
        cpuUNKNOWN, &erf);
    if (!hfdi) {
        outError = "FDICreate failed (" + std::to_string(erf.erfOper) + ").";
        return false;
    }

    std::string cabName = WideToUtf8(cabFile.filename().wstring());
    std::string cabDir  = WideToUtf8(cabFile.parent_path().wstring());
    if (!cabDir.empty() && cabDir.back() != '\\') cabDir.push_back('\\');

    const BOOL ok = ::FDICopy(
        hfdi,
        const_cast<LPSTR>(cabName.c_str()),
        const_cast<LPSTR>(cabDir.c_str()),
        0,
        &FdiNotify,
        nullptr,
        &ioCtx);
    ::FDIDestroy(hfdi);
    if (!ok) {
        outError = "FDICopy failed extracting MergeModule.CABinet (" + std::to_string(erf.erfOper) + ").";
        return false;
    }
    return true;
}

} // namespace msm_fdi

/// Merges a `.msm` Merge Module into an open `Product.msi` (`hTargetDb`) and appends its files to `ioCabInputs`!
inline bool MergeMsmIntoMsiDatabase(
    MSIHANDLE hTargetDb,
    const std::filesystem::path& msmPath,
    const std::string& targetFeatureId,
    const std::string& redirectDirKey,
    const std::filesystem::path& scratchRootDir,
    std::set<std::string>& ioRegisteredDirKeys,
    std::vector<CabInputEntry>& ioCabInputs,
    int& ioNextFileSequence,
    std::size_t& ioTotalComponents,
    std::size_t& ioTotalFiles,
    std::string& outError)
{
    MSIHANDLE hMsm = 0;
    if (::MsiOpenDatabaseW(msmPath.wstring().c_str(), MSIDBOPEN_READONLY, &hMsm) != ERROR_SUCCESS || !hMsm) {
        outError = "Could not open Merge Module '" + WideToUtf8(msmPath.wstring()) + "'.";
        return false;
    }

    auto getRecStr = [](MSIHANDLE hRec, UINT col) -> std::string {
        wchar_t buf[2048]{};
        DWORD cch = static_cast<DWORD>(std::size(buf));
        if (::MsiRecordGetStringW(hRec, col, buf, &cch) != ERROR_SUCCESS) return {};
        return WideToUtf8(buf);
    };

    // 1. Extract `_Streams` -> `MergeModule.CABinet` if present inside the `.msm`
    const std::filesystem::path msmScratch = scratchRootDir / (L"msm_" + std::to_wstring(ioNextFileSequence));
    std::error_code ec;
    std::filesystem::create_directories(msmScratch, ec);
    msm_fdi::FdiExtractContext fdiCtx{msmScratch / L"files", {}};
    {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(hMsm, L"SELECT `Data` FROM `_Streams` WHERE `Name` = 'MergeModule.CABinet'", &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                if (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    const auto tmpCab = msmScratch / L"MergeModule.cab";
                    std::ofstream cabOut(tmpCab, std::ios::binary | std::ios::trunc);
                    char chunk[65536];
                    for (;;) {
                        DWORD cb = sizeof(chunk);
                        if (::MsiRecordReadStream(hRec, 1, chunk, &cb) != ERROR_SUCCESS || cb == 0) break;
                        cabOut.write(chunk, static_cast<std::streamsize>(cb));
                    }
                    cabOut.close();
                    ::MsiCloseHandle(hRec);
                    if (std::filesystem::exists(tmpCab, ec) && std::filesystem::file_size(tmpCab, ec) > 0) {
                        if (!msm_fdi::ExtractCabToDir(tmpCab, fdiCtx, outError)) {
                            ::MsiCloseHandle(hView);
                            ::MsiCloseHandle(hMsm);
                            return false;
                        }
                    }
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    auto mapDirKey = [&](const std::string& d) -> std::string {
        if (d.empty()) return d;
        if (d == "TARGETDIR" || d == "MergeRedirectFolder" || d.rfind("MergeRedirectFolder.", 0) == 0 ||
            d.rfind("INSTALLDIR.", 0) == 0) {
            return redirectDirKey;
        }
        return d;
    };

    auto execInsert = [&](const wchar_t* sql, MSIHANDLE hRec) -> bool {
        MSIHANDLE hIns = 0;
        if (::MsiDatabaseOpenViewW(hTargetDb, sql, &hIns) != ERROR_SUCCESS || !hIns) return false;
        const UINT rc = ::MsiViewExecute(hIns, hRec);
        ::MsiCloseHandle(hIns);
        return rc == ERROR_SUCCESS;
    };

    // 2. Merge `Directory` rows (skipping already-registered system folders like `System64Folder`)
    if (::MsiDatabaseIsTablePersistentW(hMsm, L"Directory") == MSICONDITION_TRUE) {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(hMsm, L"SELECT `Directory`, `Directory_Parent`, `DefaultDir` FROM `Directory`", &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    const std::string rawKey = getRecStr(hRec, 1);
                    const std::string mapped = mapDirKey(rawKey);
                    const std::string parent = mapDirKey(getRecStr(hRec, 2));
                    const std::string defDir = getRecStr(hRec, 3);
                    ::MsiCloseHandle(hRec);

                    if (mapped == redirectDirKey || ioRegisteredDirKeys.count(mapped)) continue;
                    MSIHANDLE r = ::MsiCreateRecord(3);
                    ::MsiRecordSetStringW(r, 1, Utf8ToWide(mapped).c_str());
                    if (!parent.empty()) ::MsiRecordSetStringW(r, 2, Utf8ToWide(parent).c_str());
                    ::MsiRecordSetStringW(r, 3, Utf8ToWide(defDir.empty() ? "." : defDir).c_str());
                    execInsert(L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES (?, ?, ?)", r);
                    ::MsiCloseHandle(r);
                    ioRegisteredDirKeys.insert(mapped);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    // 3. Merge `Component` rows + bind every merged component to `targetFeatureId` in `FeatureComponents`!
    if (::MsiDatabaseIsTablePersistentW(hMsm, L"Component") == MSICONDITION_TRUE) {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(
                hMsm,
                L"SELECT `Component`, `ComponentId`, `Directory_`, `Attributes`, `Condition`, `KeyPath` FROM `Component`",
                &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    const std::string comp    = getRecStr(hRec, 1);
                    const std::string compId  = getRecStr(hRec, 2);
                    const std::string dirKey  = mapDirKey(getRecStr(hRec, 3));
                    const int         attrs   = ::MsiRecordGetInteger(hRec, 4);
                    const std::string cond    = getRecStr(hRec, 5);
                    const std::string keyPath = getRecStr(hRec, 6);
                    ::MsiCloseHandle(hRec);

                    MSIHANDLE r = ::MsiCreateRecord(6);
                    ::MsiRecordSetStringW(r, 1, Utf8ToWide(comp).c_str());
                    if (!compId.empty())  ::MsiRecordSetStringW(r, 2, Utf8ToWide(compId).c_str());
                    ::MsiRecordSetStringW(r, 3, Utf8ToWide(dirKey.empty() ? redirectDirKey : dirKey).c_str());
                    ::MsiRecordSetInteger(r, 4, (attrs == MSI_NULL_INTEGER ? 0 : attrs) | 256 /* ICE80 64-bit safe */);
                    if (!cond.empty())    ::MsiRecordSetStringW(r, 5, Utf8ToWide(cond).c_str());
                    if (!keyPath.empty()) ::MsiRecordSetStringW(r, 6, Utf8ToWide(keyPath).c_str());
                    if (execInsert(L"INSERT INTO `Component` (`Component`, `ComponentId`, `Directory_`, `Attributes`, `Condition`, `KeyPath`) VALUES (?, ?, ?, ?, ?, ?)", r)) {
                        ++ioTotalComponents;
                        MSIHANDLE fc = ::MsiCreateRecord(2);
                        ::MsiRecordSetStringW(fc, 1, Utf8ToWide(targetFeatureId).c_str());
                        ::MsiRecordSetStringW(fc, 2, Utf8ToWide(comp).c_str());
                        execInsert(L"INSERT INTO `FeatureComponents` (`Feature_`, `Component_`) VALUES (?, ?)", fc);
                        ::MsiCloseHandle(fc);
                    }
                    ::MsiCloseHandle(r);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    // 4. Merge `File` rows (re-numbering `Sequence` to match our unified `#payload.cab`!)
    if (::MsiDatabaseIsTablePersistentW(hMsm, L"File") == MSICONDITION_TRUE) {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(
                hMsm,
                L"SELECT `File`, `Component_`, `FileName`, `FileSize`, `Version`, `Language`, `Attributes` FROM `File` ORDER BY `Sequence`",
                &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    const std::string fileKey = getRecStr(hRec, 1);
                    const std::string comp    = getRecStr(hRec, 2);
                    const std::string fName   = getRecStr(hRec, 3);
                    const int         fSize   = ::MsiRecordGetInteger(hRec, 4);
                    const std::string fVer    = getRecStr(hRec, 5);
                    const std::string fLang   = getRecStr(hRec, 6);
                    const int         fAttr   = ::MsiRecordGetInteger(hRec, 7);
                    ::MsiCloseHandle(hRec);

                    auto itFile = fdiCtx.extractedFiles.find(fileKey);
                    if (itFile == fdiCtx.extractedFiles.end()) {
                        // Case-insensitive fallback lookup inside `MergeModule.CABinet`
                        for (const auto& [k, p] : fdiCtx.extractedFiles) {
                            if (EqualsIgnoreCase(k, fileKey)) {
                                itFile = fdiCtx.extractedFiles.find(k);
                                break;
                            }
                        }
                    }
                    if (itFile == fdiCtx.extractedFiles.end()) {
                        ::MsiCloseHandle(hView);
                        ::MsiCloseHandle(hMsm);
                        outError = "Merge Module file '" + fileKey + "' missing from MergeModule.CABinet.";
                        return false;
                    }

                    const int newSeq = ioNextFileSequence++;
                    ioCabInputs.push_back({itFile->second, fileKey});

                    MSIHANDLE r = ::MsiCreateRecord(8);
                    ::MsiRecordSetStringW(r, 1, Utf8ToWide(fileKey).c_str());
                    ::MsiRecordSetStringW(r, 2, Utf8ToWide(comp).c_str());
                    ::MsiRecordSetStringW(r, 3, Utf8ToWide(fName).c_str());
                    ::MsiRecordSetInteger(r, 4, (fSize == MSI_NULL_INTEGER) ? 0 : fSize);
                    if (!fVer.empty())  ::MsiRecordSetStringW(r, 5, Utf8ToWide(fVer).c_str());
                    if (!fLang.empty()) ::MsiRecordSetStringW(r, 6, Utf8ToWide(fLang).c_str());
                    ::MsiRecordSetInteger(r, 7, ((fAttr == MSI_NULL_INTEGER ? 0 : fAttr) & ~8192) | 16384 /* msidbFileAttributesCompressed */);
                    ::MsiRecordSetInteger(r, 8, newSeq);
                    if (execInsert(L"INSERT INTO `File` (`File`, `Component_`, `FileName`, `FileSize`, `Version`, `Language`, `Attributes`, `Sequence`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)", r)) {
                        ++ioTotalFiles;
                    }
                    ::MsiCloseHandle(r);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    // 5. Merge `Registry` rows
    if (::MsiDatabaseIsTablePersistentW(hMsm, L"Registry") == MSICONDITION_TRUE) {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(
                hMsm,
                L"SELECT `Registry`, `Root`, `Key`, `Name`, `Value`, `Component_` FROM `Registry`",
                &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    execInsert(L"INSERT INTO `Registry` (`Registry`, `Root`, `Key`, `Name`, `Value`, `Component_`) VALUES (?, ?, ?, ?, ?, ?)", hRec);
                    ::MsiCloseHandle(hRec);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    // 6. Merge `CreateFolder` rows
    if (::MsiDatabaseIsTablePersistentW(hMsm, L"CreateFolder") == MSICONDITION_TRUE) {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(hMsm, L"SELECT `Directory_`, `Component_` FROM `CreateFolder`", &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    const std::string d = mapDirKey(getRecStr(hRec, 1));
                    const std::string c = getRecStr(hRec, 2);
                    ::MsiCloseHandle(hRec);
                    MSIHANDLE r = ::MsiCreateRecord(2);
                    ::MsiRecordSetStringW(r, 1, Utf8ToWide(d).c_str());
                    ::MsiRecordSetStringW(r, 2, Utf8ToWide(c).c_str());
                    execInsert(L"INSERT INTO `CreateFolder` (`Directory_`, `Component_`) VALUES (?, ?)", r);
                    ::MsiCloseHandle(r);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    ::MsiCloseHandle(hMsm);
    return true;
}

} // namespace rsw
