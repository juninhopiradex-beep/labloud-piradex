// =============================================================================
//  Piradex — protocolo do PCC (Plugin Control Central), parte pura (sem JUCE, testavel)
//
//  Pedido   POST {PCC}/functions/v1/validate   (JSON)
//     { "product":"<PLUGIN>", "version":"x.y.z", "machineId":"...", "keyHash":"sha256(chave|BURNIX)", "nonce":"<hex>" }
//     A chave em claro NUNCA sai do computador: o servidor compara sha256(chave|plugin) com a sua tabela.
//  Resposta (JSON) assinada com Ed25519 pelo servidor:
//     { "valid":true, "role":"master|beta|venda", "expires":"YYYY-MM-DD" ou "", "message":"...",
//       "product":"<PLUGIN>", "machineId":"...", "nonce":"...", "ts":1790000000, "sig":"<128 hex>" }
//  Mensagem assinada (texto UTF-8):  v1|<valid 0/1>|<role>|<expires>|<product>|<machineId>|<nonce>|<ts>
//  O plugin so confia numa resposta cuja assinatura bata certo com a chave publica embebida no build
//  e cujo nonce/maquina/produto coincidam com o pedido. Offline: usa-se a ultima resposta assinada
//  guardada, ate kGraceDays dias depois do `ts` do servidor.
// =============================================================================
#pragma once
#include "Crypto.h"
#include "Ed25519.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace pq { namespace pcc
{
constexpr int kGraceDays = 14;          // sem rede: a licenca continua valida este tempo depois da ultima validacao
constexpr int kRecheckHours = 24;       // revalidar em segundo plano no maximo 1x por dia

struct Reply
{
    bool valid = false; std::string role, expires, message, product, machineId, nonce, sig; long long ts = 0;
};

inline std::string keyHashOf (const std::string& key, const std::string& product)
{
    std::string k; for (char c : key) if (! std::isspace ((unsigned char) c)) k += c;     // ignora espacos
    const std::string s = k + "|" + product; return crypto::sha256Hex (crypto::Bytes (s.begin(), s.end()));
}

inline std::string signedMessage (const Reply& r)
{
    return "v1|" + std::string (r.valid ? "1" : "0") + "|" + r.role + "|" + r.expires + "|" + r.product + "|" + r.machineId + "|" + r.nonce + "|" + std::to_string (r.ts);
}

// parser JSON plano (objeto com strings/numeros/booleanos) — suficiente para a resposta do PCC
inline bool parseFlatJson (const std::string& s, std::map<std::string, std::string>& out)
{
    out.clear(); std::size_t i = 0; auto ws = [&] { while (i < s.size() && std::isspace ((unsigned char) s[i])) ++i; };
    auto str = [&] (std::string& v) -> bool
    {
        if (i >= s.size() || s[i] != '"') return false; ++i; v.clear();
        while (i < s.size() && s[i] != '"')
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                ++i; const char c = s[i];
                if (c == 'n') v += '\n'; else if (c == 't') v += '\t'; else if (c == 'u') { if (i + 4 >= s.size()) return false; v += '?'; i += 4; } else v += c;
            }
            else v += s[i];
            ++i;
        }
        if (i >= s.size()) return false; ++i; return true;
    };
    ws(); if (i >= s.size() || s[i] != '{') return false; ++i;
    for (;;)
    {
        ws(); if (i < s.size() && s[i] == '}') return true;
        std::string k, v; if (! str (k)) return false; ws(); if (i >= s.size() || s[i] != ':') return false; ++i; ws();
        if (i < s.size() && s[i] == '"') { if (! str (v)) return false; }
        else { while (i < s.size() && s[i] != ',' && s[i] != '}' && ! std::isspace ((unsigned char) s[i])) v += s[i++]; if (v.empty()) return false; }
        out[k] = v; ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
}

inline bool parseReply (const std::string& json, Reply& r)
{
    std::map<std::string, std::string> m; if (! parseFlatJson (json, m)) return false;
    auto g = [&] (const char* k) { auto it = m.find (k); return it == m.end() ? std::string() : it->second; };
    r = {}; r.valid = (g ("valid") == "true" || g ("valid") == "1"); r.role = g ("role"); r.expires = g ("expires"); r.message = g ("message");
    r.product = g ("product"); r.machineId = g ("machineId"); r.nonce = g ("nonce"); r.sig = g ("sig"); r.ts = std::atoll (g ("ts").c_str());
    return ! r.sig.empty() && r.ts > 0;
}

// assinatura valida E resposta ligada ao pedido (produto, maquina e, se dado, nonce)
inline bool verifyReply (const Reply& r, const std::string& pubKeyHex, const std::string& product, const std::string& machineId, const std::string* nonce)
{
    std::vector<std::uint8_t> pk, sig; if (! ed25519::fromHex (pubKeyHex, pk) || pk.size() != 32 || ! ed25519::fromHex (r.sig, sig) || sig.size() != 64) return false;
    if (r.product != product || r.machineId != machineId) return false;
    if (nonce != nullptr && r.nonce != *nonce) return false;
    const std::string msg = signedMessage (r);
    return ed25519::verify (sig.data(), (const std::uint8_t*) msg.data(), msg.size(), pk.data());
}

inline bool parseDate (const std::string& s, int& y, int& m, int& d) { return s.size() == 10 && std::sscanf (s.c_str(), "%d-%d-%d", &y, &m, &d) == 3; }

// A licenca da resposta ainda vale? (valid, nao expirada, dentro da janela offline)
//   todayDays/expiry em dias desde 2026-01-01 (ver lic::daysSince2026); nowUnix em segundos.
inline bool stillGood (const Reply& r, int expiresDaysOrMinus1, int todayDays, long long nowUnix)
{
    if (! r.valid) return false;
    if (expiresDaysOrMinus1 >= 0 && todayDays > expiresDaysOrMinus1) return false;
    if (nowUnix - r.ts > (long long) kGraceDays * 86400) return false;
    if (r.ts - nowUnix > 2 * 86400) return false;           // timestamp do futuro: relogio ou resposta suspeitos
    return true;
}
}} // namespace pq::pcc
