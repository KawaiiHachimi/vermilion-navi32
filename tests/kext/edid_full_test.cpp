// SPDX-License-Identifier: MIT
// edid_full_test.cpp: host test of src/vermilion-boot/PhxDdc.h readEdidFull / readEdidBlocks / auxEdidBlock(segment)
// (kext b127, debug.phxedid_full), against a fake DP AUX engine + E-DDC sink built from the register layout auxOnce uses.
// b155: dpcdAccess (drm's 32-try rule for native DPCD reads / writes) against the same engine with injected failures.
//   c++ -std=c++17 -Wall -Wextra -Werror -I src/vermilion-boot -o /tmp/edid_full_test tests/kext/edid_full_test.cpp && /tmp/edid_full_test
#include "PhxDdc.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- fake AUX engine `eng` with an E-DDC sink behind it --------------------------------------------------------------
struct Sink {
    unsigned eng = 2;
    std::vector<uint8_t> edid;      // what the monitor holds (segment s = bytes 256 s ..)
    bool segments = true;           // false: the 0x30 write is I2C-NACKed (no E-DDC)
    uint8_t seg = 0, off = 0;
    // engine state
    uint32_t swc = 0;
    bool done = false, building = false;
    std::vector<uint8_t> req, reply;
    size_t replyPos = 0;
    uint32_t stat = 0;
    std::vector<std::string> log;   // one line per AUX transaction
    uint8_t dpcd[0x700] = {};       // b155: native DPCD space for 0x80 / 0x90
    std::vector<int> fails;         // b155: per coming transaction: 't' timeout, 'n' NACK, 'd' DEFER, 's' short read
    uint32_t delayUs = 0;
    uint32_t base() const { return phxddc::kAux0 + phxddc::kAuxStride * eng; }

    uint32_t rd(uint32_t r) {
        const uint32_t b = base();
        if (r == b) return 1;                    // AUX_EN set
        if (r == b + 1) return swc;
        if (r == b + 2) return 1u << 2;          // REG_RW_CNTL_STATUS 1 = granted to SW
        if (r == b + 3) return 0;
        if (r == b + 4) return done ? stat : 0;
        if (r == b + 6) return replyPos < reply.size() ? static_cast<uint32_t>(reply[replyPos++]) << 8 : 0;
        return 0xFFFFFFFFu;                      // anything else (pad mask, ...): unreadable = nothing written
    }
    void wr(uint32_t r, uint32_t v) {
        const uint32_t b = base();
        if (r == b + 3 && (v & 2u)) { done = false; return; }   // SW_DONE_ACK
        if (r == b + 1) { swc = v & ~1u; if (v & 1u) go(); return; }   // AUX_SW_GO self-clears
        if (r == b + 6) {
            const uint8_t byte = static_cast<uint8_t>(v >> 8);
            if ((v & 0x80000000u) && !(v & 1u) && byte && !building) { building = true; req.assign(1, byte); return; }
            if (building) req.push_back(byte);
            return;                              // the read-back writes (0, 0x80000000, 0x80000001)
        }
    }
    void go() {
        building = false;
        const unsigned count = (swc >> 16) & 0x1Fu;
        const uint8_t action = req[0] & 0xF0;
        const uint32_t addr = (static_cast<uint32_t>(req[0] & 0xF) << 16) | (req[1] << 8) | req[2];
        const unsigned len = count > 3 ? req[3] + 1u : 0u;
        std::vector<uint8_t> data(req.begin() + (count > 3 ? 4 : 3), req.end());
        uint8_t rep = 0;
        std::vector<uint8_t> out;
        char line[64];
        const int f = fails.empty() ? 0 : fails.front();
        if (!fails.empty()) fails.erase(fails.begin());
        if (f == 't') {   // AUX_SW_STATUS: done with the RX timeout bit (7)
            log.push_back("TIMEOUT"); reply.clear(); replyPos = 0; stat = 1u | (1u << 7); done = true; return;
        }
        if (action == 0x90 || action == 0x80) {
            std::snprintf(line, sizeof line, "%s %x %u", action == 0x90 ? "DR" : "DW", addr, len);
            if (f == 'n') rep = 1;
            else if (f == 'd') rep = 2;
            else if (action == 0x80) for (unsigned i = 0; i < len && addr + i < sizeof dpcd; i++) dpcd[addr + i] = data[i];
            else for (unsigned i = 0; i < (f == 's' ? len - 1 : len); i++) out.push_back(addr + i < sizeof dpcd ? dpcd[addr + i] : 0);
        } else if (action == 0x40 && addr == 0x30) {
            std::snprintf(line, sizeof line, "W30 %u", data.empty() ? 999u : data[0]);
            if (!segments) rep = 4; else seg = data[0];
        } else if (action == 0x40 && addr == 0x50) {
            std::snprintf(line, sizeof line, "W50 %u", data.empty() ? 999u : data[0]);
            off = data[0];
        } else if (action == 0x50 && addr == 0x50) {
            std::snprintf(line, sizeof line, "R50 %u", len);
            for (unsigned i = 0; i < len; i++) {
                const size_t a = static_cast<size_t>(seg) * 256 + off;
                out.push_back(a < edid.size() ? edid[a] : 0xFF);
                off = static_cast<uint8_t>(off + 1);
            }
        } else if (action == 0x10 && addr == 0x50) {
            std::snprintf(line, sizeof line, "STOP");
            seg = 0;
        } else {
            std::snprintf(line, sizeof line, "?? %02x %x", action, addr);
        }
        log.push_back(line);
        reply.assign(1, static_cast<uint8_t>(rep << 4));
        reply.insert(reply.end(), out.begin(), out.end());
        replyPos = 0;
        stat = 1u | (static_cast<uint32_t>(reply.size()) << 24);
        done = true;
    }
};
static phxddc::Io io(Sink &s) {
    return phxddc::Io{&s, [](void *c, uint32_t r) { return static_cast<Sink *>(c)->rd(r); },
                      [](void *c, uint32_t r, uint32_t v) { static_cast<Sink *>(c)->wr(r, v); },
                      [](void *c, uint32_t us) { static_cast<Sink *>(c)->delayUs += us; }};
}

// A synthetic EDID of 1 + ext blocks, each summing to 0 (block 1 CTA-861, block 2 DisplayID, as the LG's).
static std::vector<uint8_t> makeEdid(unsigned ext) {
    std::vector<uint8_t> e((1 + ext) * 128, 0);
    const uint8_t hdr[8] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
    std::memcpy(e.data(), hdr, 8);
    e[18] = 1; e[19] = 4; e[126] = static_cast<uint8_t>(ext);
    for (unsigned b = 1; b <= ext; b++) {
        e[b * 128] = b == 1 ? 0x02 : 0x70;
        for (unsigned i = 1; i < 127; i++) e[b * 128 + i] = static_cast<uint8_t>(b * 31 + i);
    }
    for (unsigned b = 0; b <= ext; b++) {
        uint8_t s = 0;
        for (unsigned i = 0; i < 127; i++) s = static_cast<uint8_t>(s + e[b * 128 + i]);
        e[b * 128 + 127] = static_cast<uint8_t>(-s);
    }
    return e;
}
static size_t countLines(const Sink &s, const char *l) { size_t n = 0; for (auto &x : s.log) n += x == l; return n; }
static long indexOf(const Sink &s, const char *l, size_t from = 0) {
    for (size_t i = from; i < s.log.size(); i++) if (s.log[i] == l) return static_cast<long>(i);
    return -1;
}

int main() {
    const unsigned kSrc = 0x22;   // AUX engine 2: the LG on DP B (debug.phxedid_auto_src 34, XTX machine 2026-11-19)
    uint8_t out[phxddc::kEdidMaxBlocks * 128];
    unsigned len = 0, ann = 0;

    {   // the LG case: 384 bytes, block 2 behind segment 1
        Sink s; s.edid = makeEdid(2);
        std::memset(out, 0, sizeof out);
        const int rc = phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr);
        CHECK(rc == phxddc::kOk && len == 384 && ann == 3);
        CHECK(std::memcmp(out, s.edid.data(), 384) == 0);
        CHECK(countLines(s, "W30 1") == 1 && countLines(s, "W30 0") == 0);   // one segment write, never segment 0
        const long w30 = indexOf(s, "W30 1");
        CHECK(w30 > 0 && indexOf(s, "W50 0", static_cast<size_t>(w30)) == w30 + 1);   // segment, then offset 0 (Linux order)
        CHECK(s.log.back() == "STOP");
        CHECK(countLines(s, "STOP") == 3);   // one I2C stop per block
    }
    {   // segment 0 is the b78..b126 sequence: the first 256 bytes read exactly as readEdid reads them
        Sink a; a.edid = makeEdid(1);
        Sink b; b.edid = makeEdid(1);
        uint8_t o2[256];
        unsigned l2 = 0;
        CHECK(phxddc::readEdidFull(io(a), kSrc, out, &len, &ann, nullptr) == phxddc::kOk && len == 256 && ann == 2);
        CHECK(phxddc::readEdid(io(b), kSrc, o2, &l2, nullptr) == phxddc::kOk && l2 == 256);
        CHECK(a.log == b.log);
        CHECK(countLines(a, "W30 0") == 0 && countLines(a, "W30 1") == 0);
        CHECK(std::memcmp(out, o2, 256) == 0);
    }
    {   // a sink without E-DDC: the 0x30 write is NACKed, blocks 0-1 kept, the bus stopped
        Sink s; s.edid = makeEdid(2); s.segments = false;
        const int rc = phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr);
        CHECK(rc == phxddc::kOk && len == 256 && ann == 3);
        CHECK(s.log.back() == "STOP");
        CHECK(countLines(s, "R50 16") == 16);   // only blocks 0 and 1 were read
    }
    {   // block 2 with a bad checksum: dropped, blocks 0-1 kept
        Sink s; s.edid = makeEdid(2); s.edid[300] ^= 1;
        CHECK(phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr) == phxddc::kOk && len == 256 && ann == 3);
    }
    {   // 512 bytes: block 3 = segment 1, offset 128
        Sink s; s.edid = makeEdid(3);
        CHECK(phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr) == phxddc::kOk && len == 512 && ann == 4);
        CHECK(std::memcmp(out, s.edid.data(), 512) == 0);
        CHECK(countLines(s, "W30 1") == 2 && countLines(s, "W50 128") == 2);
    }
    {   // more extensions than fit: capped at 4 blocks, announced count still reported
        Sink s; s.edid = makeEdid(5);
        CHECK(phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr) == phxddc::kOk && len == 512 && ann == 6);
        CHECK(countLines(s, "W30 2") == 0);
    }
    {   // a 128-byte EDID: one block, no segment write
        Sink s; s.edid = makeEdid(0);
        CHECK(phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr) == phxddc::kOk && len == 128 && ann == 1);
        CHECK(countLines(s, "STOP") == 1);
    }
    {   // block 0 bad: the error, nothing kept
        Sink s; s.edid = makeEdid(2); s.edid[0] = 0x11;
        CHECK(phxddc::readEdidFull(io(s), kSrc, out, &len, &ann, nullptr) == phxddc::kErrHeader && len == 0 && ann == 0);
        Sink c; c.edid = makeEdid(2); c.edid[20] ^= 1;
        CHECK(phxddc::readEdidFull(io(c), kSrc, out, &len, &ann, nullptr) == phxddc::kErrChecksum && len == 0);
    }
    {   // readEdidBlocks alone: maxBlocks caps the read (the DC_I2C path passes 2)
        const std::vector<uint8_t> e = makeEdid(2);
        unsigned reads = 0;
        auto rb = [&](unsigned b, uint8_t *buf) { reads++; std::memcpy(buf, e.data() + b * 128, 128); return phxddc::kOk; };
        CHECK(phxddc::readEdidBlocks(rb, 2, out, &len, &ann) == phxddc::kOk && len == 256 && ann == 3 && reads == 2);
        CHECK(phxddc::readEdidBlocks(rb, 0, out, &len, &ann) == phxddc::kErrArg);
    }
    {   // a source that is neither AUX nor DC_I2C
        Sink s; s.edid = makeEdid(1);
        CHECK(phxddc::readEdidFull(io(s), 0x30, out, &len, &ann, nullptr) == phxddc::kErrArg && s.log.empty());
    }
    // ---- b155: dpcdAccess = drm_dp_dpcd_access's rule: 32 tries, at once after a timeout, else 500 us later ----
    {
        Sink s; phxddc::DpcdStat st{};
        s.dpcd[0x202] = 0x77; s.dpcd[0x203] = 0x77;
        uint8_t b[2] = {};
        CHECK(phxddc::dpcdAccess(io(s), 2, false, 0x202, b, 2, &st) == phxddc::kOk && b[0] == 0x77 && b[1] == 0x77);
        CHECK(st.calls == 1 && st.ok == 1 && st.retried == 0 && s.log.size() == 1 && s.delayUs == 0);
        // a timeout then an ACK: the second try goes at once (no 500 us)
        s.log.clear(); s.delayUs = 0; s.fails = {'t'};
        uint8_t d0 = 1;
        CHECK(phxddc::dpcdAccess(io(s), 2, true, 0x600, &d0, 1, &st) == phxddc::kOk && s.dpcd[0x600] == 1);
        CHECK(s.log.size() == 2 && s.delayUs == 0 && st.retried == 1 && st.timeouts == 1 && st.lastFirstErr == phxddc::kErrTimeout);
        // a NACK, a DEFER, a short read, then the read lands: 500 us before each retry; the old aux() stopped at the NACK
        s.log.clear(); s.delayUs = 0; s.fails = {'n', 'd', 's'};
        CHECK(phxddc::dpcdAccess(io(s), 2, false, 0x202, b, 2, &st) == phxddc::kOk && b[0] == 0x77);
        CHECK(s.log.size() == 4 && s.delayUs == 1500 && st.nacks == 2 && st.defers == 1 && st.maxTries == 4);
        CHECK(st.lastFirstErr == phxddc::kErrNack);
        s.log.clear(); s.fails = {'n'};
        uint8_t got = 0;
        CHECK(phxddc::aux(io(s), 2, 0x90, 0x202, nullptr, 2, b, 2, &got, nullptr) == phxddc::kErrNack && s.log.size() == 1);
        // every try fails: 32 transactions, the first error returned
        s.log.clear(); s.delayUs = 0; s.fails.assign(40, 't'); s.fails[0] = 'n';
        CHECK(phxddc::dpcdAccess(io(s), 2, false, 0x0, b, 1, &st) == phxddc::kErrNack && s.log.size() == 32);
        CHECK(st.gaveUp == 1 && st.calls == 4 && st.ok == 3 && s.delayUs == 500);   // only after the NACK; timeouts back to back
        // an argument error never reaches the wire
        s.log.clear();
        uint8_t big[17] = {};
        CHECK(phxddc::dpcdAccess(io(s), 2, false, 0x0, big, 17, &st) == phxddc::kErrArg && s.log.empty());
    }
    std::printf("edid_full_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
