#include "Access.h"
#include "AppPaths.h"
#include "Secrets.h"

using namespace lx;

AccessManager& AccessManager::get() { static AccessManager a; return a; }

namespace
{
    // marca do dia visto: HMAC com um segredo derivado do hash da senha master (só quem tem o Secrets.h gera uma marca válida)
    std::string macFor (const std::string& day)
    {
        const std::string key = std::string (secrets::kPasswords[0].hashHex) + "|labloud-lastseen";
        const crypto::Bytes k (key.begin(), key.end()), m (day.begin(), day.end());
        return crypto::toHex (crypto::hmacSha256 (k, m)).substr (0, 16);
    }
}

AccessManager::AccessManager()
{
    juce::PropertiesFile::Options o; o.storageFormat = juce::PropertiesFile::storeAsXML; o.millisecondsBeforeSaving = 0;
    props = std::make_unique<juce::PropertiesFile> (lxpaths::settingsFile(), o);

    const auto ls = props->getValue ("lastSeen").toStdString(), lm = props->getValue ("lastSeenMac").toStdString();
    if (! ls.empty() && lm == macFor (ls)) lastSeenDay = std::atoi (ls.c_str());

    // sessão lembrada neste computador
    const auto id = props->getValue ("loginId"), tok = props->getValue ("loginToken");
    if (id.isNotEmpty() && tok.isNotEmpty())
        for (int i = 0; i < secrets::kPasswordCount; ++i)
            if (id == secrets::kPasswords[i].id && tok == tokenFor (i) && ! lic::pwExpired (secrets::kPasswords[i], today())) { setRole (i); break; }
}

int AccessManager::today() const
{
    const juce::ScopedLock sl (lock);
    const int now = lic::todayDays2026();
    if (now > lastSeenDay)
    {
        lastSeenDay = now; const auto s = std::to_string (now);
        props->setValue ("lastSeen", juce::String (s)); props->setValue ("lastSeenMac", juce::String (macFor (s)));
        props->saveIfNeeded();
    }
    return lic::effectiveToday (now, lastSeenDay);
}

juce::String AccessManager::tokenFor (int index) const
{
    const std::string s = std::string (juce::SystemStats::getUniqueDeviceID().toRawUTF8()) + "|" + secrets::kPasswords[index].id + "|" + secrets::kPasswords[index].hashHex + "|LABLOUD";
    return juce::String (crypto::sha256Hex (crypto::Bytes (s.begin(), s.end())));
}

void AccessManager::setRole (int index)
{
    currentIndex = index; roleValue.store ((int) secrets::kPasswords[index].role); unlocked.store (true);
}

AccessManager::Match AccessManager::matchPassword (const juce::String& password)
{
    Match m; bool ex = false;
    m.index = lic::matchPassword (secrets::kPasswords, secrets::kPasswordCount, password.toStdString(), AccessManager::get().today(), ex);
    m.expired = ex; return m;
}

AccessManager::LoginResult AccessManager::applyLogin (Match m, bool remember)
{
    if (m.index < 0) return m.expired ? LoginResult::Expired : LoginResult::Wrong;
    const juce::ScopedLock sl (lock);
    setRole (m.index);
    if (remember) { props->setValue ("loginId", juce::String (secrets::kPasswords[m.index].id)); props->setValue ("loginToken", tokenFor (m.index)); }
    else          { props->removeValue ("loginId"); props->removeValue ("loginToken"); }
    props->saveIfNeeded();
    return LoginResult::Ok;
}

void AccessManager::logout()
{
    const juce::ScopedLock sl (lock);
    unlocked.store (false); roleValue.store (0); currentIndex = -1;
    props->removeValue ("loginId"); props->removeValue ("loginToken"); props->saveIfNeeded();
}

void AccessManager::checkExpiry()
{
    int idx; { const juce::ScopedLock sl (lock); idx = currentIndex; }
    if (unlocked.load() && idx >= 0 && lic::pwExpired (secrets::kPasswords[idx], today())) logout();
}

int AccessManager::betaDaysLeft() const
{
    int idx; { const juce::ScopedLock sl (lock); idx = currentIndex; }
    if (! unlocked.load() || role() != lic::Role::Beta || idx < 0) return -1;
    return lic::daysLeft (secrets::kPasswords[idx], today());
}

juce::String AccessManager::roleText() const
{
    int idx; { const juce::ScopedLock sl (lock); idx = currentIndex; }
    if (! unlocked.load() || idx < 0) return {};
    if (role() == lic::Role::Master) return "MESTRE";
    const auto& e = secrets::kPasswords[idx];
    return juce::String (juce::CharPointer_UTF8 ("BETA \xC2\xB7 at\xC3\xA9 ")) + juce::String (e.expDay).paddedLeft ('0', 2) + "/" + juce::String (e.expMonth).paddedLeft ('0', 2) + "/" + juce::String (e.expYear);
}
