// =============================================================================
//  LABLOUD by Piradex — criptografia mínima (C++17 puro, testável sem JUCE)
//  SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, cifra de fluxo SHA-256-CTR e Base32.
// =============================================================================
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lx { namespace crypto
{
using Bytes = std::string;   // bytes crus

struct Sha256
{
    uint32_t h[8]; uint8_t buf[64]; uint64_t total = 0; size_t fill = 0;
    Sha256() { reset(); }
    void reset()
    {
        static const uint32_t i[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        std::memcpy (h, i, 32); total = 0; fill = 0;
    }
    static uint32_t rotr (uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block (const uint8_t* p)
    {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = ((uint32_t) p[i * 4] << 24) | ((uint32_t) p[i * 4 + 1] << 16) | ((uint32_t) p[i * 4 + 2] << 8) | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update (const void* data, size_t n)
    {
        const uint8_t* p = (const uint8_t*) data; total += n;
        while (n > 0)
        {
            const size_t take = std::min (n, 64 - fill); std::memcpy (buf + fill, p, take); fill += take; p += take; n -= take;
            if (fill == 64) { block (buf); fill = 0; }
        }
    }
    Bytes finish()
    {
        const uint64_t bits = total * 8; uint8_t pad = 0x80; update (&pad, 1); pad = 0; while (fill != 56) update (&pad, 1);
        uint8_t len[8]; for (int i = 0; i < 8; ++i) len[i] = (uint8_t) (bits >> (56 - 8 * i)); update (len, 8);
        Bytes out (32, '\0'); for (int i = 0; i < 8; ++i) { out[(size_t) i * 4] = (char) (h[i] >> 24); out[(size_t) i * 4 + 1] = (char) (h[i] >> 16); out[(size_t) i * 4 + 2] = (char) (h[i] >> 8); out[(size_t) i * 4 + 3] = (char) h[i]; }
        return out;
    }
};

inline Bytes sha256 (const void* d, size_t n) { Sha256 s; s.update (d, n); return s.finish(); }
inline Bytes sha256 (const Bytes& b) { return sha256 (b.data(), b.size()); }
inline std::string toHex (const Bytes& b) { static const char* x = "0123456789abcdef"; std::string o; for (unsigned char c : b) { o += x[c >> 4]; o += x[c & 15]; } return o; }
inline Bytes fromHex (const std::string& s)
{
    Bytes o; auto v = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0)); };
    for (size_t i = 0; i + 1 < s.size(); i += 2) o += (char) (v (s[i]) * 16 + v (s[i + 1]));
    return o;
}
inline std::string sha256Hex (const Bytes& b) { return toHex (sha256 (b)); }

inline Bytes hmacSha256 (const Bytes& key, const Bytes& msg)
{
    Bytes k = key.size() > 64 ? sha256 (key) : key; k.resize (64, '\0');
    Bytes ip (64, '\0'), op (64, '\0'); for (size_t i = 0; i < 64; ++i) { ip[i] = (char) (k[i] ^ 0x36); op[i] = (char) (k[i] ^ 0x5c); }
    return sha256 (op + sha256 (ip + msg));
}
inline Bytes pbkdf2Sha256 (const Bytes& pw, const Bytes& salt, int iter, int dkLen)
{
    Bytes out;
    for (uint32_t blk = 1; (int) out.size() < dkLen; ++blk)
    {
        Bytes s = salt; s += (char) (blk >> 24); s += (char) (blk >> 16); s += (char) (blk >> 8); s += (char) blk;
        Bytes u = hmacSha256 (pw, s), t = u;
        for (int i = 1; i < iter; ++i) { u = hmacSha256 (pw, u); for (size_t j = 0; j < t.size(); ++j) t[j] = (char) (t[j] ^ u[j]); }
        out += t;
    }
    out.resize ((size_t) dkLen); return out;
}

// cifra de fluxo: keystream_i = SHA256(chave | nonce | i). XOR in-place (cifrar = decifrar)
inline void streamXor (Bytes& data, const Bytes& key, const Bytes& nonce)
{
    const Bytes pre = key + "|" + nonce + "|";
    for (size_t off = 0, ctr = 0; off < data.size(); off += 32, ++ctr)
    {
        Bytes c (8, '\0'); for (int i = 0; i < 8; ++i) c[(size_t) i] = (char) (ctr >> (8 * i));
        const Bytes ks = sha256 (pre + c);
        for (size_t i = 0; i < 32 && off + i < data.size(); ++i) data[off + i] = (char) (data[off + i] ^ ks[i]);
    }
}
inline bool constantTimeEq (const Bytes& a, const Bytes& b)
{
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= (unsigned char) (a[i] ^ b[i]);
    return d == 0;
}
}} // namespace lx::crypto
