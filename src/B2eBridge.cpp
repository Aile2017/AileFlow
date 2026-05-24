// B2eBridge.cpp
// Compiled as part of KILIB_B2E_SOURCES (ANSI, /UUNICODE /U_UNICODE, /EHs-c-, /GR-).
// Bridges the UNICODE SevenZip API surface to the ANSI CArcB2e / kilib engine.

#include "stdafx.h"
#include <ctype.h>
#include "ArcB2e.h"
#include "B2eBridge.h"

// ── Extension → .b2e mapping ─────────────────────────────────────────────────

struct B2eTableEntry {
    const char* ext;       // lowercase, no dot
    const char* b2eFile;   // filename under the b2e/ directory
    bool        writable;  // true when the script has an encode: section
    const char* label;     // display label for writable formats (nullptr if not writable)
    const char* cmpExt;    // canonical output extension for writable formats
};

static const B2eTableEntry B2E_TABLE[] = {
    { "7z",   "7z.b2e",                                true,  "7-Zip (.7z)", "7z"  },
    { "zip",  "zip.zipx.b2e",                          true,  "ZIP (.zip)",  "zip" },
    { "zipx", "zip.zipx.b2e",                          false, nullptr,       nullptr },
    { "rar",  "rar.b2e",                               true,  "RAR (.rar)",  "rar" },
    { "lzh",  "lzh.b2e",                               true,  "LZH (.lzh)",  "lzh" },
    { "lha",  "lzh.b2e",                               false, nullptr,       nullptr },
    { "tar",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", true, "TAR (.tar)",  "tar" },
    { "gz",   "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "bz2",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "xz",   "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "zst",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "liz",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "lz4",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "lz5",  "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "br",   "tar.gz.bz2.xz.zst.liz.lz4.lz5.br.b2e", false, nullptr,      nullptr },
    { "cab",  "cab.b2e",                               false, nullptr,       nullptr },
    { "rpm",  "rpm.cpio.b2e",                          false, nullptr,       nullptr },
    { "cpio", "rpm.cpio.b2e",                          false, nullptr,       nullptr },
    { nullptr, nullptr, false, nullptr, nullptr }
};

// ── Utilities ─────────────────────────────────────────────────────────────────

// wchar_t* → ANSI char (CP_ACP).  Returns false on failure.
static bool WToA(const wchar_t* w, char* buf, int bufSize)
{
    return 0 != ::WideCharToMultiByte(CP_ACP, 0, w, -1, buf, bufSize, NULL, NULL);
}

// ANSI char* → wchar_t*.  Returns number of wide chars written (incl. NUL).
static int AToW(const char* a, wchar_t* buf, int bufSize)
{
    return ::MultiByteToWideChar(CP_ACP, 0, a, -1, buf, bufSize);
}

// Convert ANSI extension to lowercase and look it up in B2E_TABLE.
static const B2eTableEntry* FindEntry(const char* path)
{
    // kiPath::ext() returns a pointer to the char after the last '.',
    // or a pointer to '\0' when there is no extension.
    const char* extRaw = kiPath::ext(path);
    if (!extRaw || !extRaw[0]) return nullptr;

    char extLow[64] = {};
    int i = 0;
    for (; extRaw[i] && i < 63; ++i)
        extLow[i] = (char)tolower((unsigned char)extRaw[i]);

    for (const B2eTableEntry* e = B2E_TABLE; e->ext; ++e)
        if (0 == ki_strcmpi(e->ext, extLow))
            return e;
    return nullptr;
}

// Try FindFirstFile; if the file doesn't exist yet, build a minimal struct.
static bool GetWfd(const char* path, WIN32_FIND_DATA* fd)
{
    HANDLE h = ::FindFirstFile(path, fd);
    if (h != INVALID_HANDLE_VALUE) { ::FindClose(h); return true; }

    // File does not exist (e.g. new output archive): fill from the path string.
    ::ZeroMemory(fd, sizeof(*fd));
    const char* fname = kiPath::name(path);
    ki_strcpy(fd->cFileName, fname ? fname : path);
    ki_strcpy(fd->cAlternateFileName, fd->cFileName);
    return false;  // file absent; caller may still proceed
}

// ── Public API ────────────────────────────────────────────────────────────────

std::vector<B2eFormatInfo> B2e_GetWritableFormats()
{
    std::vector<B2eFormatInfo> result;
    for (const B2eTableEntry* e = B2E_TABLE; e->ext; ++e) {
        if (!e->writable || !e->label) continue;
        B2eFormatInfo info;
        wchar_t wbuf[128];
        AToW(e->label,  wbuf, 128); info.label = wbuf;
        AToW(e->cmpExt, wbuf,  32); info.ext   = wbuf;
        result.push_back(info);
    }
    return result;
}

bool B2e_IsArchiveExt(const wchar_t* ext)
{
    // Build "x.<ext>" so kiPath::ext() inside FindEntry can extract the extension.
    char extA[66] = {'x', '.'};
    WToA(ext, extA + 2, 62);
    return FindEntry(extA) != nullptr;
}

HRESULT B2e_List(const wchar_t* archivePath, std::vector<ArchiveItem>& items)
{
    char path[MAX_PATH];
    if (!WToA(archivePath, path, MAX_PATH)) return E_FAIL;

    const B2eTableEntry* entry = FindEntry(path);
    if (!entry) return E_NOTIMPL;

    // Get archive file info (long name + short name for arcname).
    WIN32_FIND_DATA fd;
    if (!GetWfd(path, &fd)) return E_FAIL;  // must exist to list

    // Build directory path (strips filename).
    kiPath dir(path);
    dir.beDirOnly();

    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(entry->b2eFile);
    aflArray aflFiles;
    if (!b2e.list(aname, aflFiles)) return E_FAIL;

    items.clear();
    UINT32 itemIndex = 0;
    for (unsigned int i = 0; i < aflFiles.len(); ++i) {
        const arcfile& af = aflFiles[i];
        if (!af.isfile) continue;  // skip header/separator entries

        ArchiveItem item;

        // szFileName (ANSI) → path (wstring)
        wchar_t wname[FNAME_MAX32 + 2] = {};
        AToW(af.inf.szFileName, wname, FNAME_MAX32 + 1);
        item.path = wname;

        // Leaf name (part after last / or \)
        std::wstring::size_type pos = item.path.find_last_of(L"/\\");
        item.name = (pos != std::wstring::npos) ? item.path.substr(pos + 1) : item.path;

        // Treat entries whose path ends with a separator as directories.
        item.isDir = !item.path.empty() &&
                     (item.path.back() == L'/' || item.path.back() == L'\\');

        // rawline → comment (used by the "Info" ListView column).
        wchar_t wraw[512] = {};
        AToW(af.rawline, wraw, 511);
        item.comment = wraw;

        item.index = itemIndex++;
        items.push_back(std::move(item));
    }
    return S_OK;
}

HRESULT B2e_Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const std::vector<ArchiveItem>& allItems,
                    const wchar_t* destDir,
                    IExtractProgressSink* /*sink*/)
{
    char path[MAX_PATH], dest[MAX_PATH];
    if (!WToA(archivePath, path, MAX_PATH)) return E_FAIL;
    if (!WToA(destDir,     dest, MAX_PATH)) return E_FAIL;

    const B2eTableEntry* entry = FindEntry(path);
    if (!entry) return E_NOTIMPL;

    WIN32_FIND_DATA fd;
    if (!GetWfd(path, &fd)) return E_FAIL;

    kiPath dir(path); dir.beDirOnly();
    kiPath destPath(dest);

    const char* sname = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
    arcname aname(dir, sname, fd.cFileName);

    CArcB2e b2e(entry->b2eFile);
    int result;

    if (indices.empty()) {
        // Full extraction (decode: script).
        result = b2e.melt(aname, destPath, nullptr);
    } else {
        // Selective extraction (decode1: script).
        // Build an aflArray whose arcfile.selected entries tell B2E which files to extract.
        aflArray selected;
        for (UINT32 idx : indices) {
            if (idx >= (UINT32)allItems.size()) continue;
            arcfile af;
            ::ZeroMemory(&af, sizeof(af));
            WToA(allItems[idx].path.c_str(), af.inf.szFileName, FNAME_MAX32);
            af.selected = true;
            selected.add(af);
        }
        result = b2e.melt(aname, destPath, &selected);
    }

    return (result < 0x8000) ? S_OK : E_FAIL;
}

HRESULT B2e_Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     int level,
                     IExtractProgressSink* /*sink*/)
{
    if (srcPaths.empty()) return E_INVALIDARG;

    char out[MAX_PATH];
    if (!WToA(outPath, out, MAX_PATH)) return E_FAIL;

    const B2eTableEntry* entry = FindEntry(out);
    if (!entry || !entry->writable) return E_NOTIMPL;

    // Output dir and filename.
    kiPath outDir(out); outDir.beDirOnly();
    const char* outFilename = kiPath::name(out);

    // Base directory: use the parent of the first source path.
    char src0[MAX_PATH];
    if (!WToA(srcPaths[0].c_str(), src0, MAX_PATH)) return E_FAIL;
    kiPath base(src0); base.beDirOnly();

    // Build wfdArray:
    //   wfd[0]      = output archive (in outDir)  — provides the archive name for (arc)
    //   wfd[1..n]   = source files   (in base)    — listed by (list)/(listr) in the script
    wfdArray wfd;

    WIN32_FIND_DATA fdOut;
    ::ZeroMemory(&fdOut, sizeof(fdOut));
    ki_strcpy(fdOut.cFileName,          outFilename);
    ki_strcpy(fdOut.cAlternateFileName, outFilename);
    wfd.add(fdOut);

    for (const std::wstring& srcW : srcPaths) {
        char srcA[MAX_PATH];
        if (!WToA(srcW.c_str(), srcA, MAX_PATH)) continue;

        WIN32_FIND_DATA fdSrc;
        GetWfd(srcA, &fdSrc);  // fills cFileName; ignores return (might not exist yet)
        wfd.add(fdSrc);
    }

    CArcB2e b2e(entry->b2eFile);
    // level 0 → store (method 1 in the .b2e script); level N → method N+1.
    int result = b2e.compress(base, wfd, outDir, level, /*sfx=*/false);
    return (result < 0x8000) ? S_OK : E_FAIL;
}
