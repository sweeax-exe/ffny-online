// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// FFNY Studio's package: a PyInstaller one-folder app zipped with deflate (Studio repository,
// scripts/package.ps1). Windows has no inflate of its own, so the raw-deflate decoder of stb_image (the
// copy in the toolkit, already used for the texture packs' PNGs; public domain / MIT, thirdparty/stb-LICENSE.txt)
// unpacks each entry in memory, and its CRC-32 is checked before the file is written. The zip has been checked
// against its signed SHA-256 before it comes here; the name checks are there all the same. See studio_app.h.
#define _CRT_SECURE_NO_WARNINGS
#include "studio_app.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../../tools/xboxrecomp/src/nv2a/stb/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace fs = std::filesystem;

namespace studio_zip
{
namespace
{
constexpr uint32_t kMaxEntries = 20000;
constexpr uint64_t kMaxTotal = 4ull << 30, kMaxEntry = 1ull << 30;

uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

uint32_t crc32(const unsigned char *p, size_t n)
{
    static uint32_t table[256];
    static bool made;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool fail(std::string *error, const std::string &text)
{
    if (error) *error = text;
    fprintf(stderr, "[STUDIO] %s\n", text.c_str());
    fflush(stderr);
    return false;
}

bool reserved_device(const std::string &part)
{
    std::string base = part.substr(0, part.find('.'));
    for (auto &c : base) c = (char)toupper((unsigned char)c);
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$"};
    for (auto n : names)
        if (base == n) return true;
    return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) &&
           base[3] >= '0' && base[3] <= '9';
}

// A relative, '/'-separated name below the prefix: no "..", ".", empty parts, drive, backslash, control or
// reserved characters, no part ending in a dot or a space, no device names. Non-ASCII only as UTF-8 (flag 11).
bool safe_name(const std::string &name, bool utf8, const char *prefix)
{
    if (name.empty() || name.size() > 400 || name.compare(0, strlen(prefix), prefix) != 0) return false;
    for (char ch : name) {
        unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c == 0x7F || strchr("\\:*?\"<>|", ch) || (c >= 0x80 && !utf8)) return false;
    }
    std::string body = name.back() == '/' ? name.substr(0, name.size() - 1) : name;
    size_t at = 0;
    while (at <= body.size()) {
        size_t slash = body.find('/', at);
        if (slash == std::string::npos) slash = body.size();
        std::string part = body.substr(at, slash - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' || reserved_device(part))
            return false;
        at = slash + 1;
    }
    return true;
}

struct Entry { std::string name; uint16_t method; uint32_t crc, csize, usize, local; bool dir; };
} // namespace

bool extract(const std::wstring &zip, const std::wstring &dest, const char *prefix, std::string *error)
{
    std::error_code ec;
    fs::path zpath(zip), root(dest);
    uint64_t file_size = fs::file_size(zpath, ec);
    std::ifstream in(zpath, std::ios::binary);
    if (ec || !in || file_size < 22) return fail(error, "The Studio download cannot be read.");
    size_t tail = (size_t)std::min<uint64_t>(file_size, 65536 + 22);
    std::vector<unsigned char> t(tail);
    in.seekg((std::streamoff)(file_size - tail));
    in.read((char *)t.data(), (std::streamsize)tail);
    if (!in) return fail(error, "The Studio download cannot be read.");
    size_t e = tail;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (le32(&t[i]) == 0x06054b50) { e = i; break; }
    if (e == tail) return fail(error, "The Studio download is damaged (no zip directory).");
    uint32_t count = le16(&t[e + 10]), cd_size = le32(&t[e + 12]), cd_off = le32(&t[e + 16]);
    if (count == 0xFFFF || cd_off == 0xFFFFFFFFu) return fail(error, "The Studio download is a zip64 archive; not supported.");
    if ((uint64_t)cd_off + cd_size > file_size || count > kMaxEntries || !count)
        return fail(error, "The Studio download is damaged (zip directory).");
    std::vector<unsigned char> cd(cd_size);
    in.seekg(cd_off);
    in.read((char *)cd.data(), cd_size);
    if (!in) return fail(error, "The Studio download cannot be read.");

    std::vector<Entry> entries;
    uint64_t total = 0;
    size_t p = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50) return fail(error, "The Studio download is damaged (entry).");
        uint16_t flags = le16(&cd[p + 8]), method = le16(&cd[p + 10]), nlen = le16(&cd[p + 28]), xlen = le16(&cd[p + 30]),
                 clen = le16(&cd[p + 32]);
        Entry en;
        en.method = method;
        en.crc = le32(&cd[p + 16]);
        en.csize = le32(&cd[p + 20]);
        en.usize = le32(&cd[p + 24]);
        en.local = le32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) return fail(error, "The Studio download is damaged (name).");
        en.name.assign((const char *)&cd[p + 46], nlen);
        p += 46 + (size_t)nlen + xlen + clen;
        en.dir = !en.name.empty() && en.name.back() == '/';
        if (!safe_name(en.name, (flags & 0x800) != 0, prefix))
            return fail(error, "The Studio download holds a file outside its folder: " + en.name.substr(0, 80));
        if (flags & 1) return fail(error, "The Studio download has an encrypted entry.");
        if (en.dir) { entries.push_back(en); continue; }
        if (method != 0 && method != 8) return fail(error, "The Studio download uses a compression this launcher does not know.");
        if (en.csize == 0xFFFFFFFFu || en.usize == 0xFFFFFFFFu || en.usize > kMaxEntry || (method == 0 && en.csize != en.usize))
            return fail(error, "The Studio download is damaged (sizes).");
        total += en.usize;
        if (total > kMaxTotal) return fail(error, "The Studio download unpacks to too much.");
        entries.push_back(en);
    }

    std::vector<unsigned char> packed, plain;
    for (auto &en : entries) {
        fs::path target = root / fs::u8path(en.name.substr(0, en.dir ? en.name.size() - 1 : en.name.size()));
        if (en.dir) { fs::create_directories(target, ec); continue; }
        unsigned char lh[30];
        in.seekg(en.local);
        in.read((char *)lh, 30);
        if (!in || le32(lh) != 0x04034b50) return fail(error, "The Studio download is damaged (local header).");
        uint64_t data = (uint64_t)en.local + 30 + le16(lh + 26) + le16(lh + 28);
        if (data + en.csize > file_size) return fail(error, "The Studio download is cut short.");
        packed.resize(en.csize);
        in.seekg((std::streamoff)data);
        if (en.csize) in.read((char *)packed.data(), en.csize);
        if (!in) return fail(error, "The Studio download cannot be read.");
        const unsigned char *bytes = packed.data();
        if (en.method == 8) {
            plain.resize(en.usize ? en.usize : 1);
            int got = en.csize ? stbi_zlib_decode_noheader_buffer((char *)plain.data(), (int)plain.size(), (const char *)packed.data(),
                                                                   (int)en.csize)
                               : -1;
            if (got != (int)en.usize) return fail(error, "The Studio download is damaged (" + en.name.substr(0, 80) + ").");
            bytes = plain.data();
        }
        if (crc32(bytes, en.usize) != en.crc) return fail(error, "The Studio download is damaged (checksum of " + en.name.substr(0, 80) + ").");
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) return fail(error, "Cannot write Studio's files (" + en.name.substr(0, 80) + ").");
        out.write((const char *)bytes, (std::streamsize)en.usize);
        if (!out) return fail(error, "Cannot write Studio's files (disk full?).");
    }
    return true;
}
} // namespace studio_zip
