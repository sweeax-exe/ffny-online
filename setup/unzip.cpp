// The setup payload's ZIP, unpacked in this process. It used to go through Windows PowerShell's
// Expand-Archive, which Wine only stubs: it "succeeded" without writing a file, and setup then
// failed with "Could not start a setup tool (Windows error 3)". Here every entry is inflated and its
// CRC-32 checked, so a payload either unpacks completely or setup says that it did not.
#include "bootstrap.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace {

[[noreturn]] void broken(const char* what) {
    throw std::runtime_error(std::string("The setup files inside this installer are damaged (") + what +
                             "). Download the setup again.");
}

uint32_t crcTable[256];
void initCrc() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[i] = c;
    }
}
uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = crcTable[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---- Inflate (RFC 1951) -------------------------------------------------------------------------
struct Bits {
    const uint8_t* p; const uint8_t* end;
    uint64_t buf = 0; int count = 0; size_t over = 0;   // bytes "read" past the end (as zeros)
    void refill() {
        while (count <= 56) {
            uint64_t byte = 0;
            if (p < end) byte = *p++; else ++over;
            buf |= byte << count; count += 8;
        }
    }
    uint32_t get(int n) {
        if (n == 0) return 0;
        if (count < n) refill();
        uint32_t v = (uint32_t)(buf & ((1ull << n) - 1));
        buf >>= n; count -= n;
        return v;
    }
    void check() const { if (over * 8 > (size_t)count) broken("data ends early"); }
};

constexpr int FAST = 10;
struct Huffman {
    uint16_t fast[1 << FAST];
    uint16_t count[16];
    uint16_t symbol[320];
};

bool build(Huffman& h, const uint8_t* lengths, int n) {
    std::memset(h.fast, 0, sizeof(h.fast));
    std::memset(h.count, 0, sizeof(h.count));
    for (int i = 0; i < n; ++i) h.count[lengths[i]]++;
    h.count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) { left <<= 1; left -= h.count[len]; if (left < 0) return false; }
    uint16_t offs[16]{};
    for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
    for (int i = 0; i < n; ++i) if (lengths[i]) h.symbol[offs[lengths[i]]++] = (uint16_t)i;
    uint32_t next[16]{}, code = 0;
    for (int len = 1; len < 16; ++len) { code = (code + h.count[len - 1]) << 1; next[len] = code; }
    for (int i = 0; i < n; ++i) {
        int len = lengths[i];
        if (!len) continue;
        uint32_t c = next[len]++;
        if (len > FAST) continue;
        uint32_t rev = 0;
        for (int k = 0; k < len; ++k) rev |= ((c >> k) & 1) << (len - 1 - k);
        for (uint32_t k = rev; k < (1u << FAST); k += 1u << len) h.fast[k] = (uint16_t)((i << 4) | len);
    }
    return true;
}

int decode(Bits& b, const Huffman& h) {
    if (b.count < 16) b.refill();
    uint16_t e = h.fast[b.buf & ((1u << FAST) - 1)];
    if (e) { b.buf >>= (e & 15); b.count -= (e & 15); return e >> 4; }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= (int)b.get(1);
        int n = h.count[len];
        if (code - first < n) return h.symbol[index + (code - first)];
        index += n; first += n; first <<= 1; code <<= 1;
    }
    broken("bad code");
}

const uint16_t LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const uint8_t LEXT[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const uint16_t DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                            4097,6145,8193,12289,16385,24577};
const uint8_t DEXT[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

Huffman fixedLit, fixedDist;
void initFixed() {
    uint8_t l[288];
    for (int i = 0; i < 144; ++i) l[i] = 8;
    for (int i = 144; i < 256; ++i) l[i] = 9;
    for (int i = 256; i < 280; ++i) l[i] = 7;
    for (int i = 280; i < 288; ++i) l[i] = 8;
    build(fixedLit, l, 288);
    uint8_t d[30];
    for (auto& x : d) x = 5;
    build(fixedDist, d, 30);
}

void inflate(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) {
    Bits b{in, in + inSize};
    size_t pos = 0;
    auto lit = std::make_unique<Huffman>(), dist = std::make_unique<Huffman>();
    bool last = false;
    while (!last) {
        last = b.get(1);
        int type = (int)b.get(2);
        if (type == 0) {
            b.get(b.count & 7);                 // to the byte boundary
            uint32_t len = b.get(16), nlen = b.get(16);
            if ((len ^ 0xFFFF) != nlen) broken("bad stored block");
            if (pos + len > outSize) broken("too much data");
            while (len && b.count >= 8) { out[pos++] = (uint8_t)b.get(8); --len; }
            if (len) {
                // The bit buffer is empty and byte aligned: copy straight from the input.
                if ((size_t)(b.end - b.p) < len) broken("data ends early");
                std::memcpy(out + pos, b.p, len); b.p += len; pos += len;
            }
            continue;
        }
        const Huffman* L = &fixedLit;
        const Huffman* D = &fixedDist;
        if (type == 2) {
            int nlit = (int)b.get(5) + 257, ndist = (int)b.get(5) + 1, ncode = (int)b.get(4) + 4;
            if (nlit > 286 || ndist > 30) broken("bad block header");
            static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t lengths[320]{};
            for (int i = 0; i < ncode; ++i) lengths[order[i]] = (uint8_t)b.get(3);
            Huffman codes;
            if (!build(codes, lengths, 19)) broken("bad code lengths");
            std::memset(lengths, 0, sizeof(lengths));
            for (int i = 0; i < nlit + ndist;) {
                int sym = decode(b, codes);
                if (sym < 16) { lengths[i++] = (uint8_t)sym; continue; }
                int repeat, value = 0;
                if (sym == 16) { if (!i) broken("bad repeat"); value = lengths[i - 1]; repeat = 3 + (int)b.get(2); }
                else if (sym == 17) repeat = 3 + (int)b.get(3);
                else repeat = 11 + (int)b.get(7);
                if (i + repeat > nlit + ndist) broken("bad repeat");
                while (repeat--) lengths[i++] = (uint8_t)value;
            }
            if (!lengths[256]) broken("no end code");
            if (!build(*lit, lengths, nlit) || !build(*dist, lengths + nlit, ndist)) broken("bad codes");
            L = lit.get(); D = dist.get();
        } else if (type != 1) broken("bad block type");
        for (;;) {
            int sym = decode(b, *L);
            if (sym < 256) {
                if (pos >= outSize) broken("too much data");
                out[pos++] = (uint8_t)sym;
                continue;
            }
            if (sym == 256) break;
            sym -= 257;
            if (sym >= 29) broken("bad length");
            size_t len = LBASE[sym] + b.get(LEXT[sym]);
            int ds = decode(b, *D);
            if (ds >= 30) broken("bad distance");
            size_t d = DBASE[ds] + b.get(DEXT[ds]);
            if (d > pos) broken("bad distance");
            if (pos + len > outSize) broken("too much data");
            uint8_t* to = out + pos;
            const uint8_t* from = to - d;
            if (d >= len) std::memcpy(to, from, len);
            else for (size_t k = 0; k < len; ++k) to[k] = from[k];
            pos += len;
        }
        b.check();
    }
    b.check();
    if (pos != outSize) broken("wrong size");
}

// ---- ZIP ---------------------------------------------------------------------------------------
uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

struct Entry { std::string name; uint16_t method; uint32_t crc, packed, size, local; };

bool safeName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        return false;
    size_t start = 0;
    std::string body = name.back() == '/' ? name.substr(0, name.size() - 1) : name;
    while (start <= body.size()) {
        size_t slash = body.find('/', start);
        std::string part = body.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

std::wstring longPath(const fs::path& p) {
    auto text = fs::absolute(p).wstring();
    for (auto& c : text) if (c == L'/') c = L'\\';
    return text.rfind(L"\\\\?\\", 0) == 0 ? text : L"\\\\?\\" + text;
}

}  // namespace

void extractZip(const uint8_t* data, size_t size, const fs::path& destination) {
    static std::once_flag once;
    std::call_once(once, [] { initCrc(); initFixed(); });
    if (size < 22) broken("too small");
    size_t eocd = std::string::npos;
    for (size_t i = size - 22 + 1; i-- > 0 && size - i <= 65535 + 22;)
        if (u32(data + i) == 0x06054b50) { eocd = i; break; }
    if (eocd == std::string::npos) broken("no directory");
    uint32_t total = u16(data + eocd + 10), cdSize = u32(data + eocd + 12), cdOffset = u32(data + eocd + 16);
    if (total == 0xFFFF || cdOffset == 0xFFFFFFFFu || (size_t)cdOffset + cdSize > eocd) broken("unsupported directory");
    std::vector<Entry> entries;
    const uint8_t* p = data + cdOffset;
    const uint8_t* end = data + cdOffset + cdSize;
    for (uint32_t i = 0; i < total; ++i) {
        if (end - p < 46 || u32(p) != 0x02014b50) broken("bad directory entry");
        uint16_t flags = u16(p + 8), nameLen = u16(p + 28), extraLen = u16(p + 30), commentLen = u16(p + 32);
        if (end - p < 46 + nameLen + extraLen + commentLen) broken("bad directory entry");
        Entry e{std::string((const char*)p + 46, nameLen), u16(p + 10), u32(p + 16), u32(p + 20), u32(p + 24), u32(p + 42)};
        if (flags & 1) broken("encrypted entry");
        if (!safeName(e.name)) broken("unsafe file name");
        if (e.method != 0 && e.method != 8) broken("unsupported compression");
        if (e.size == 0xFFFFFFFFu || e.packed == 0xFFFFFFFFu || e.local == 0xFFFFFFFFu) broken("unsupported entry size");
        entries.push_back(std::move(e));
        p += 46 + nameLen + extraLen + commentLen;
    }
    // Folders first, in this thread; then the files, several at once (inflating and writing ~4500
    // files one by one is most of the wait).
    std::set<fs::path> folders;
    for (const auto& e : entries) {
        fs::path relative = utf8(e.name);
        folders.insert(e.name.back() == '/' ? destination / relative : (destination / relative).parent_path());
    }
    for (const auto& folder : folders) {
        std::error_code error;
        fs::create_directories(longPath(folder), error);
        if (error) throw std::runtime_error("Could not create the setup's working folder " + narrow(folder.wstring()) +
                                            " (" + error.message() + "). Check disk space.");
    }
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::string failure;
    std::mutex failureLock;
    auto work = [&] {
        std::vector<uint8_t> buffer;
        for (size_t i; !failed && (i = next++) < entries.size();) {
            const auto& e = entries[i];
            if (e.name.back() == '/') continue;
            try {
                if ((size_t)e.local + 30 > size || u32(data + e.local) != 0x04034b50) broken("bad file header");
                size_t start = (size_t)e.local + 30 + u16(data + e.local + 26) + u16(data + e.local + 28);
                if (start + e.packed > size) broken("file runs past the end");
                buffer.resize(e.size);
                if (e.method == 0) {
                    if (e.packed != e.size) broken("bad stored size");
                    if (e.size) std::memcpy(buffer.data(), data + start, e.size);
                } else inflate(data + start, e.packed, buffer.data(), e.size);
                if (crc32(buffer.data(), buffer.size()) != e.crc) broken("checksum mismatch");
                auto target = longPath(destination / fs::path(utf8(e.name)));
                HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    throw std::runtime_error("Could not write the setup's files (Windows error " + std::to_string(GetLastError()) +
                                             "). Check disk space and that security software allows setup.");
                DWORD written = 0;
                bool ok = buffer.empty() || (WriteFile(file, buffer.data(), (DWORD)buffer.size(), &written, nullptr) &&
                                             written == buffer.size());
                DWORD code = GetLastError();
                CloseHandle(file);
                if (!ok) throw std::runtime_error("Could not write the setup's files (Windows error " + std::to_string(code) +
                                                  "). Check disk space.");
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lock(failureLock);
                if (!failed.exchange(true)) failure = ex.what();
            }
        }
    };
    unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned i = 1; i < threads; ++i) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    if (failed) throw std::runtime_error(failure);
}
