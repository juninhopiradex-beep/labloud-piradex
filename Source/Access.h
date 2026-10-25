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

    bool isUnlocked() const        { return unlocked.load(); }      // seguro na thread de áudio (atómico)
    lx::lic::Role role() const     { return (lx::lic::Role) roleValue.load(); }
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
