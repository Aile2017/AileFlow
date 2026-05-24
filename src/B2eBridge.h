// B2eBridge.h
// UNICODE/ANSI bridge between SevenZipB2e.cpp (UNICODE UI layer) and the
// B2E engine (kilib ANSI layer compiled without UNICODE).
// This header exposes only Win32 / std types — no kilib types leak through.

#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"

// Format descriptor returned by B2e_GetWritableFormats().
struct B2eFormatInfo {
    std::wstring label;  // e.g. L"7-Zip (.7z)"
    std::wstring ext;    // e.g. L"7z"
};

// Returns all formats whose .b2e script has an encode: section.
// Used by SevenZip::Load() to populate m_writableFormats / m_encoderNames.
std::vector<B2eFormatInfo> B2e_GetWritableFormats();

// Returns true if ext (no dot, case-insensitive, e.g. L"7z") is handled
// by a .b2e script (for listing or extraction).
bool B2e_IsArchiveExt(const wchar_t* ext);

// List archive contents.
// Returns S_OK and fills items on success.
// Returns E_NOTIMPL if the extension has no .b2e handler.
// Returns E_FAIL if the archive could not be opened or listed.
HRESULT B2e_List(const wchar_t* archivePath,
                 std::vector<ArchiveItem>& items);

// Extract entries from an archive.
// indices: positions into allItems (as returned by B2e_List). Empty = extract all.
// allItems: the vector returned by the matching B2e_List call (needed for
//           building the per-file list when doing selective extraction).
// sink: optional progress callback; may be nullptr.
HRESULT B2e_Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const std::vector<ArchiveItem>& allItems,
                    const wchar_t* destDir,
                    IExtractProgressSink* sink);

// Compress srcPaths into outPath.
// The output format is inferred from the outPath extension.
// level: 0 = store (method 1 in the .b2e encode: section);
//        1 = default compression (the method marked * in load: type);
//        2+ = successive methods in the type list.
// sink: optional progress callback; may be nullptr.
HRESULT B2e_Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     int level,
                     IExtractProgressSink* sink);
