// =============================================================================
//  LABLOUD by Piradex — acesso: login (Master / Beta com validade)
//  A verificação da senha (PBKDF2) é lenta de propósito: nunca chamar na thread de áudio nem na de mensagens.
// =============================================================================
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include "Core/Licensing.h"
#include "pcc/PccSession.h"

class AccessManager
{
public:
    static AccessManager& get();

    static bool inTrial()          { return false; }   // 1.0.1: sem periodo aberto - login obrigatorio desde ja (master, beta ate 31/12/2026 ou licenca PCC)
    bool isUnlocked() const        { return unlocked.load() || inTrial(); }      // seguro na thread de áudio (atómico)
    lx::lic::Role role() const     { auto r = (lx::lic::Role) roleValue.load(); return (r == lx::lic::Role::None && inTrial()) ? lx::lic::Role::Beta : r; }
    juce::String roleText() const;                                   // "MESTRE" / "BETA · até 01/11/2026"
    int  betaDaysLeft() const;                                       // -1 se não for beta

    struct Match { int index = -1; bool expired = false; };
    static Match matchPassword (const juce::String& password);       // lento: chamar numa thread de fundo
    enum class LoginResult { Ok, Wrong, Expired, Online };    // Online: o PCC recusou ou não respondeu (texto em onlineText())
    LoginResult applyLogin (Match m, bool remember);
    void logout();

    // ---- chave de licença online (PCC): checkOnline numa thread de fundo, applyOnline na thread de mensagens
    static piradex::pcc::Check checkOnline (const juce::String& password) { return get().pcc.check (password); }
    LoginResult applyOnline (const piradex::pcc::Check& c, bool remember);
    juce::String onlineText() const { return lastOnline; }
    bool claimOnlineRecheck();                                       // barato (mensagens): true = lançar revalidateOnline()
    void revalidateOnline();                                         // fundo, no máximo 1x/dia; revogada => logout
    bool onlineLicence() const { return online.load(); }
    void checkExpiry();                                              // chamar periodicamente (thread de mensagens)
    int  today() const;                                              // dia atual, com guarda contra recuo do relógio

private:
    AccessManager();
    void setRole (int index);
    void setOnline();
    juce::String tokenFor (int index) const;
    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::PropertiesFile> props;
    std::atomic<bool> unlocked { false };
    std::atomic<int> roleValue { 0 };
    int currentIndex = -1;
    piradex::pcc::Session pcc { "LOUDNEX", PIRADEX_PCC_VERSION };
    std::atomic<bool> online { false };
    std::atomic<juce::int64> lastRecheckTry { 0 };
    juce::String lastOnline;
    mutable int lastSeenDay = 0;
};
