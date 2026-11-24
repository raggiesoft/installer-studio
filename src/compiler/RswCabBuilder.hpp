// ================================================================================
// FILE: src/compiler/RswCabBuilder.hpp
// PROJECT: RaggieSoft Setup Wizard ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `#payload.cab`, and how does `cabinet.dll` (`fci.h`) build it?
//     Inside a self-contained `.msi` file, all of your application's `.exe`, `.dll`,
//     and asset files are compressed together inside a Microsoft **Cabinet (`.cab`)**
//     archive stored in the MSI database's internal `_Streams` table!
//
//     Windows ships the exact C compression engine (`cabinet.dll` — File Compression
//     Interface, `fci.h`) used by Windows Update, `makecab.exe`, and enterprise
//     installer compilers right inside `C:\Windows\System32\cabinet.dll`!
//
//     How `fci.h` works:
//       - Because `cabinet.dll` was designed in pure C in the 1990s to work on any
//         filesystem or memory buffer, you pass it 13 tiny C callback function
//         pointers (`Open`, `Read`, `Write`, `Seek`, `Close`, `Delete`, `GetOpenInfo`,
//         etc.) and call `FCICreate` -> `FCIAddFile` (once per file, in exact MSI
//         `File.Sequence` order!) -> `FCIFlushCabinet` -> `FCIDestroy`.
//       - Notice our 64-bit pointer safety: `fci.h` passes file handles as `INT_PTR`
//         (64-bit on `x64` and `ARM64`), so storing a real Win32 `HANDLE` from
//         `CreateFileW` via `reinterpret_cast<INT_PTR>(hFile)` is 100% 64-bit safe
//         and supports Unicode (`UTF-16`) Windows paths seamlessly!
// ================================================================================

#pragma once

#include "../common/RswPlatform.hpp"

#include <fcntl.h>
#include <fci.h>
#include <io.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "Cabinet.lib")

namespace rsw {

struct CabInputEntry {
    std::filesystem::path sourcePath; // Real UTF-16 path on the build machine
    std::string           cabFileKey; // Exact primary key in MSI `File.File` column (e.g. `"f_0001"`)
};

namespace detail {

inline FNFCIFILEPLACED(CabFilePlaced) {
    (void)pccab; (void)pszFile; (void)cbFile; (void)fContinuation; (void)pv;
    return 0;
}

inline FNFCIALLOC(CabMemAlloc) {
    return std::malloc(cb);
}

inline FNFCIFREE(CabMemFree) {
    std::free(memory);
}

inline FNFCIOPEN(CabFileOpen) {
    (void)pv;
    const std::wstring wPath = Utf8ToWide(pszFile != nullptr ? pszFile : "");
    DWORD desiredAccess = GENERIC_READ;
    DWORD shareMode     = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD creationDisp  = OPEN_EXISTING;

    if ((oflag & _O_RDWR) != 0) {
        desiredAccess = GENERIC_READ | GENERIC_WRITE;
    } else if ((oflag & _O_WRONLY) != 0) {
        desiredAccess = GENERIC_WRITE;
    }

    if ((oflag & _O_CREAT) != 0) {
        creationDisp = ((oflag & _O_EXCL) != 0) ? CREATE_NEW : CREATE_ALWAYS;
    } else if ((oflag & _O_TRUNC) != 0) {
        creationDisp = TRUNCATE_EXISTING;
    }

    (void)pmode;
    HANDLE h = ::CreateFileW(
        wPath.c_str(),
        desiredAccess,
        shareMode,
        nullptr,
        creationDisp,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = static_cast<int>(::GetLastError());
        return -1;
    }
    return reinterpret_cast<INT_PTR>(h);
}

inline FNFCIREAD(CabFileRead) {
    (void)pv;
    HANDLE h = reinterpret_cast<HANDLE>(hf);
    DWORD bytesRead = 0;
    if (!::ReadFile(h, memory, cb, &bytesRead, nullptr)) {
        if (err) *err = static_cast<int>(::GetLastError());
        return static_cast<UINT>(-1);
    }
    return bytesRead;
}

inline FNFCIWRITE(CabFileWrite) {
    (void)pv;
    HANDLE h = reinterpret_cast<HANDLE>(hf);
    DWORD bytesWritten = 0;
    if (!::WriteFile(h, memory, cb, &bytesWritten, nullptr)) {
        if (err) *err = static_cast<int>(::GetLastError());
        return static_cast<UINT>(-1);
    }
    return bytesWritten;
}

inline FNFCICLOSE(CabFileClose) {
    (void)pv;
    HANDLE h = reinterpret_cast<HANDLE>(hf);
    if (!::CloseHandle(h)) {
        if (err) *err = static_cast<int>(::GetLastError());
        return -1;
    }
    return 0;
}

inline FNFCISEEK(CabFileSeek) {
    (void)pv;
    HANDLE h = reinterpret_cast<HANDLE>(hf);
    DWORD moveMethod = FILE_BEGIN;
    if (seektype == SEEK_CUR) moveMethod = FILE_CURRENT;
    else if (seektype == SEEK_END) moveMethod = FILE_END;

    LARGE_INTEGER distLi{};
    distLi.QuadPart = dist;
    LARGE_INTEGER newPos{};
    if (!::SetFilePointerEx(h, distLi, &newPos, moveMethod)) {
        if (err) *err = static_cast<int>(::GetLastError());
        return -1L;
    }
    return static_cast<long>(newPos.QuadPart);
}

inline FNFCIDELETE(CabFileDelete) {
    (void)pv;
    const std::wstring wPath = Utf8ToWide(pszFile != nullptr ? pszFile : "");
    if (!::DeleteFileW(wPath.c_str())) {
        if (err) *err = static_cast<int>(::GetLastError());
        return -1;
    }
    return 0;
}

inline FNFCIGETTEMPFILE(CabGetTempFile) {
    (void)pv;
    wchar_t tempDir[MAX_PATH]{};
    wchar_t tempFile[MAX_PATH]{};
    if (::GetTempPathW(MAX_PATH, tempDir) == 0 ||
        ::GetTempFileNameW(tempDir, L"rsw", 0, tempFile) == 0) {
        return FALSE;
    }
    // `GetTempFileNameW` creates a 0-byte placeholder file; delete it so FCI can open it with `CREATE_NEW`.
    ::DeleteFileW(tempFile);
    const std::string utf8 = WideToUtf8(tempFile);
    if (static_cast<int>(utf8.size()) + 1 > cbTempName) {
        return FALSE;
    }
    std::memcpy(pszTempName, utf8.c_str(), utf8.size() + 1);
    return TRUE;
}

inline FNFCIGETNEXTCABINET(CabGetNextCabinet) {
    (void)pccab; (void)cbPrevCab; (void)pv;
    return TRUE;
}

inline FNFCIGETOPENINFO(CabGetOpenInfo) {
    (void)pv;
    const std::wstring wPath = Utf8ToWide(pszName != nullptr ? pszName : "");
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (::GetFileAttributesExW(wPath.c_str(), GetFileExInfoStandard, &fad)) {
        FILETIME localFt{};
        WORD dosDate = 0, dosTime = 0;
        ::FileTimeToLocalFileTime(&fad.ftLastWriteTime, &localFt);
        ::FileTimeToDosDateTime(&localFt, &dosDate, &dosTime);
        if (pdate) *pdate = dosDate;
        if (ptime) *ptime = dosTime;
        if (pattribs) {
            *pattribs = (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? _A_RDONLY : 0;
        }
    }
    return CabFileOpen(pszName, _O_RDONLY | _O_BINARY, 0, err, pv);
}

inline FNFCISTATUS(CabStatusCallback) {
    (void)typeStatus; (void)cb1; (void)cb2; (void)pv;
    return 0;
}

} // namespace detail

/// Compresses `entries` (in exact `File.Sequence` order) into a single `.cab` file at `outCabPath`.
inline bool BuildPayloadCabinet(
    const std::vector<CabInputEntry>& entries,
    const std::filesystem::path& outCabPath,
    bool useHighRatioLzx,
    std::string& outError)
{
    std::error_code ec;
    std::filesystem::create_directories(outCabPath.parent_path(), ec);
    std::filesystem::remove(outCabPath, ec);

    const std::string cabDirUtf8  = WideToUtf8(outCabPath.parent_path().wstring()) + "\\";
    const std::string cabNameUtf8 = WideToUtf8(outCabPath.filename().wstring());

    if (cabDirUtf8.size() >= CB_MAX_CAB_PATH || cabNameUtf8.size() >= CB_MAX_CABINET_NAME) {
        outError = "Cabinet output path exceeds FCI CB_MAX_CAB_PATH limit: " + outCabPath.string();
        return false;
    }

    CCAB ccab{};
    ccab.cb             = 0x7FFFFFFFL; // Single undivided embedded cabinet (`~2 GB` ceiling)
    ccab.cbFolderThresh = 0x7FFFFFFFL; // Solid compression across the entire cabinet!
    ccab.iCab           = 1;
    ccab.iDisk          = 1;
    std::memcpy(ccab.szCabPath, cabDirUtf8.c_str(), cabDirUtf8.size() + 1);
    std::memcpy(ccab.szCab, cabNameUtf8.c_str(), cabNameUtf8.size() + 1);

    ERF erf{};
    HFCI hfci = ::FCICreate(
        &erf,
        detail::CabFilePlaced,
        detail::CabMemAlloc,
        detail::CabMemFree,
        detail::CabFileOpen,
        detail::CabFileRead,
        detail::CabFileWrite,
        detail::CabFileClose,
        detail::CabFileSeek,
        detail::CabFileDelete,
        detail::CabGetTempFile,
        &ccab,
        nullptr);

    if (hfci == nullptr) {
        outError = "FCICreate failed (FCI error code " + std::to_string(erf.erfOper) + ").";
        return false;
    }

    // `TCOMPfromLZXWindow(21)` = `tcompTYPE_LZX | tcompLZX_WINDOW_HI` (2 MB LZX sliding dictionary),
    // supported natively by `msi.dll` on all Windows versions with ~25-35% tighter compression than MSZIP!
    const TCOMP compressionType = useHighRatioLzx
        ? TCOMPfromLZXWindow(21)
        : tcompTYPE_MSZIP;

    for (const auto& item : entries) {
        std::string srcUtf8 = WideToUtf8(item.sourcePath.wstring());
        std::string keyCopy = item.cabFileKey;
        if (!::FCIAddFile(
                hfci,
                srcUtf8.data(),
                keyCopy.data(),
                FALSE,
                detail::CabGetNextCabinet,
                detail::CabStatusCallback,
                detail::CabGetOpenInfo,
                compressionType)) {
            const int oper = erf.erfOper;
            ::FCIDestroy(hfci);
            outError = "FCIAddFile failed on '" + srcUtf8 + "' (FCI error " + std::to_string(oper) + ").";
            return false;
        }
    }

    if (!::FCIFlushCabinet(hfci, FALSE, detail::CabGetNextCabinet, detail::CabStatusCallback)) {
        const int oper = erf.erfOper;
        ::FCIDestroy(hfci);
        outError = "FCIFlushCabinet failed (FCI error " + std::to_string(oper) + ").";
        return false;
    }

    if (!::FCIDestroy(hfci)) {
        outError = "FCIDestroy failed.";
        return false;
    }

    return std::filesystem::exists(outCabPath, ec);
}

} // namespace rsw
