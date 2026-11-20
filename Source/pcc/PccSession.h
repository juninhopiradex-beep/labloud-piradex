// =============================================================================
//  Piradex — licenca online pelo PCC (Plugin Control Central, Supabase), igual em todos os plugins.
//
//  Ordem no ecra de login: 1) senhas offline (master #Tecnico01 e betas, PBKDF2, sem internet);
//  2) se nao bater e o texto tiver o formato de uma chave (XXX-XXXX-XXXX-XXXX), pergunta ao PCC.
//  A chave em claro nunca sai do computador: vai sha256(CHAVE|PRODUTO). A resposta vem assinada
//  com Ed25519 e so e aceite se a assinatura bater com a chave publica embebida no build.
//  Sem rede, a ultima resposta assinada vale kGraceDays (14) dias. Revalidacao: no maximo 1x por dia.
//
//  Ligado quando o build define PIRADEX_PCC_URL e PIRADEX_PCC_PUBKEY (ver CMakeLists.txt).
//  check() e revalidate() fazem rede (bloqueante, timeout 8 s): NUNCA nas threads de audio ou de mensagens.
// =============================================================================
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <algorithm>
#include <mutex>
#include <string>
#include "PccProtocol.h"

#ifndef PIRADEX_PCC_URL
 #define PIRADEX_PCC_URL ""
#endif
#ifndef PIRADEX_PCC_PUBKEY
 #define PIRADEX_PCC_PUBKEY ""
#endif
#ifndef PIRADEX_PCC_VERSION
 #define PIRADEX_PCC_VERSION ""
#endif

namespace piradex { namespace pcc
{
using Reply = pq::pcc::Reply;

inline bool enabled()             { return std::string (PIRADEX_PCC_URL).size() > 8 && std::string (PIRADEX_PCC_PUBKEY).size() == 64; }
inline std::string machineId()    { return juce::SystemStats::getUniqueDeviceID().toStdString(); }
inline long long unixNow()        { return juce::Time::currentTimeMillis() / 1000; }
inline int dayOf (long long secs) { return (int) (secs / 86400); }
inline int today()                { return dayOf (unixNow()); }
inline int expiryDay (const std::string& e)       // ultimo dia valido (UTC); -1 = nao expira
{
    int y = 0, m = 0, d = 0; if (! pq::pcc::parseDate (e, y, m, d)) return -1;
    return dayOf (juce::Time (y, m - 1, d, 12, 0, 0, 0, false).toMilliseconds() / 1000);
}

// "XXX-XXXX-XXXX-XXXX" (letras/numeros), com espacos ignorados e minusculas aceites
inline juce::String normaliseKey (const juce::String& s) { return s.removeCharacters (" \t\r\n").toUpperCase(); }
inline bool looksLikeKey (const juce::String& s)
{
    const auto k = normaliseKey (s); if (k.length() != 18) return false;
    for (int i = 0; i < 18; ++i)
    {
        const auto c = k[i];
        if (i == 3 || i == 8 || i == 13) { if (c != '-') return false; }
        else if (! juce::CharacterFunctions::isLetterOrDigit (c)) return false;
    }
    return true;
}

enum class Outcome { Ok, Invalid, Unreachable, BadReply, Disabled };

struct Check { Outcome outcome = Outcome::Disabled; Reply reply; std::string keyHash; juce::String message; };

// mensagens do servidor (PT) -> texto para o ecra, em PT ou EN
inline juce::String uiText (const Check& c, bool english)
{
    auto t = [english] (const char* pt, const char* en) { return juce::String (juce::CharPointer_UTF8 (english ? en : pt)); };
    switch (c.outcome)
    {
        case Outcome::Unreachable: return t ("Sem ligação ao servidor de licenças. Verifique a internet.", "No connection to the licence server. Check the internet.");
        case Outcome::BadReply:    return t ("Resposta do servidor de licenças não reconhecida.", "Unrecognised licence server reply.");
        case Outcome::Invalid:
        {
            const auto m = c.message;
            if (m.startsWith (juce::CharPointer_UTF8 ("Licença expirada")))
                return english ? "This licence expired on " + m.fromFirstOccurrenceOf ("em ", false, false).upToLastOccurrenceOf (".", false, false) + "." : m;
            if (m.contains ("noutro computador")) return t ("Esta licença está ativa noutro computador. Peça a transferência ao suporte.", "This licence is active on another computer. Ask support to transfer it.");
            if (m.contains ("desativada"))        return t ("Licença desativada. Contacte o suporte.", "Licence disabled. Please contact support.");
            return t ("Senha ou licença incorreta.", "Incorrect password or licence.");
        }
        default: return {};
    }
}

class Session
{
public:
    Session (const char* productId, const char* versionString) : product (productId), version (versionString) {}

    bool active() const            { std::lock_guard<std::mutex> g (m); return act; }
    std::string role() const       { std::lock_guard<std::mutex> g (m); return act ? reply.role : std::string(); }
    std::string expires() const    { std::lock_guard<std::mutex> g (m); return act ? reply.expires : std::string(); }
    int daysLeft() const           { std::lock_guard<std::mutex> g (m); const int e = expiryDay (reply.expires); return (! act || e < 0) ? -1 : std::max (0, e - today()); }
    bool stillGood() const         { std::lock_guard<std::mutex> g (m); return act && good (reply); }

    // licenca lembrada neste computador (resposta assinada guardada)
    bool restore (juce::PropertiesFile& p)
    {
        if (! enabled()) return false;
        Reply r; r.valid = p.getValue ("pccValid") == "1"; r.role = p.getValue ("pccRole").toStdString(); r.expires = p.getValue ("pccExpires").toStdString();
        r.nonce = p.getValue ("pccNonce").toStdString(); r.ts = p.getValue ("pccTs").getLargeIntValue(); r.sig = p.getValue ("pccSig").toStdString();
        r.product = product; r.machineId = machineId();
        const auto h = p.getValue ("pccHash").toStdString(); if (h.empty() || r.sig.empty()) return false;
        if (! pq::pcc::verifyReply (r, PIRADEX_PCC_PUBKEY, product, r.machineId, nullptr)) return false;   // adulterado ou de outra maquina
        if (! good (r)) return false;
        std::lock_guard<std::mutex> g (m); reply = r; hash = h; act = true; return true;
    }

    // pergunta ao PCC (bloqueante). Nao muda o estado: o resultado entra com commit() na thread de mensagens.
    Check check (const juce::String& password) const
    {
        Check c; if (! enabled() || ! looksLikeKey (password)) return c;
        c.keyHash = pq::pcc::keyHashOf (normaliseKey (password).toStdString(), product);
        c.outcome = ask (c.keyHash, c.reply);
        if (c.outcome == Outcome::Ok) { c.message = juce::String (juce::CharPointer_UTF8 (c.reply.message.c_str())); if (! good (c.reply)) c.outcome = Outcome::Invalid; }
        return c;
    }

    void commit (const Check& c, bool remember, juce::PropertiesFile& p)
    {
        { std::lock_guard<std::mutex> g (m); reply = c.reply; hash = c.keyHash; act = true; }
        if (remember) store (c.reply, c.keyHash, p); else forget (p);
    }

    enum class Recheck { Kept, Renewed, Revoked };
    // revalidacao em segundo plano (bloqueante; no maximo 1x por dia). Sem rede: mantem (periodo de graca).
    Recheck revalidate (juce::PropertiesFile& p)
    {
        std::string h; { std::lock_guard<std::mutex> g (m); if (! act || ! enabled()) return Recheck::Kept; h = hash; }
        if (unixNow() - p.getValue ("pccCheck").getLargeIntValue() < (juce::int64) pq::pcc::kRecheckHours * 3600) return Recheck::Kept;
        Reply r; if (ask (h, r) != Outcome::Ok) return Recheck::Kept;
        p.setValue ("pccCheck", juce::String ((juce::int64) unixNow()));
        if (! r.valid) { clear (p); return Recheck::Revoked; }                       // revogada ou desativada no PCC
        { std::lock_guard<std::mutex> g (m); reply = r; }
        if (p.getValue ("pccSig").isNotEmpty()) store (r, h, p);                    // renova a validade guardada
        p.saveIfNeeded(); return Recheck::Renewed;
    }

    void clear (juce::PropertiesFile& p) { { std::lock_guard<std::mutex> g (m); act = false; reply = {}; hash.clear(); } forget (p); }

private:
    static bool good (const Reply& r) { return pq::pcc::stillGood (r, expiryDay (r.expires), today(), unixNow()); }

    Outcome ask (const std::string& keyHash, Reply& out) const
    {
        juce::Random rnd; std::string nonce; for (int i = 0; i < 16; ++i) nonce += juce::String::toHexString ((int) rnd.nextInt (256)).paddedLeft ('0', 2).toStdString();
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("product", juce::String (product)); o->setProperty ("version", juce::String (version));
        o->setProperty ("machineId", juce::String (machineId())); o->setProperty ("keyHash", juce::String (keyHash)); o->setProperty ("nonce", juce::String (nonce));
        auto url = juce::URL (juce::String (PIRADEX_PCC_URL).trimCharactersAtEnd ("/") + "/functions/v1/validate").withPOSTData (juce::JSON::toString (juce::var (o.get()), true));
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                 .withExtraHeaders ("Content-Type: application/json\r\n").withConnectionTimeoutMs (8000).withHttpRequestCmd ("POST"));
        if (stream == nullptr) return Outcome::Unreachable;
        Reply r; const auto text = stream->readEntireStreamAsString().toStdString();
        if (! pq::pcc::parseReply (text, r) || ! pq::pcc::verifyReply (r, PIRADEX_PCC_PUBKEY, product, machineId(), &nonce)) return Outcome::BadReply;
        out = r; return Outcome::Ok;
    }
    static void store (const Reply& r, const std::string& h, juce::PropertiesFile& p)
    {
        p.setValue ("pccHash", juce::String (h)); p.setValue ("pccValid", r.valid ? "1" : "0"); p.setValue ("pccRole", juce::String (r.role));
        p.setValue ("pccExpires", juce::String (r.expires)); p.setValue ("pccNonce", juce::String (r.nonce)); p.setValue ("pccTs", juce::String ((juce::int64) r.ts));
        p.setValue ("pccSig", juce::String (r.sig)); p.saveIfNeeded();
    }
    static void forget (juce::PropertiesFile& p)
    {
        for (auto k : { "pccHash", "pccValid", "pccRole", "pccExpires", "pccNonce", "pccTs", "pccSig", "pccCheck" }) p.removeValue (k);
        p.saveIfNeeded();
    }

    std::string product, version;
    mutable std::mutex m; bool act = false; Reply reply; std::string hash;
};
}} // namespace piradex::pcc
