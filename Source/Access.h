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

class AccessManager
{
public:
    static AccessManager& get();

    static bool inTrial()          { return lx::lic::todayDays2026() <= lx::lic::daysSince2026 (2026, 12, 31); } // aberto ate 31/12/2026
    bool isUnlocked() const        { return unlocked.load() || inTrial(); }      // seguro na thread de áudio (atómico)
    lx::lic::Role role() const     { auto r = (lx::lic::Role) roleValue.load(); return (r == lx::lic::Role::None && inTrial()) ? lx::lic::Role::Beta : r; }
    juce::String roleText() const;                                   // "MESTRE" / "BETA · até 01/11/2026"
    int  betaDaysLeft() const;                                       // -1 se não for beta

    struct Match { int index = -1; bool expired = false; };
    static Match matchPassword (const juce::String& password);       // lento: chamar numa thread de fundo
    enum class LoginResult { Ok, Wrong, Expired };
    LoginResult applyLogin (Match m, bool remember);
    void logout();
    void checkExpiry();                                              // chamar periodicamente (thread de mensagens)
    int  today() const;                                              // dia atual, com guarda contra recuo do relógio

private:
    AccessManager();
    void setRole (int index);
    juce::String tokenFor (int index) const;
    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::PropertiesFile> props;
    std::atomic<bool> unlocked { false };
    std::atomic<int> roleValue { 0 };
    int currentIndex = -1;
    mutable int lastSeenDay = 0;
};
