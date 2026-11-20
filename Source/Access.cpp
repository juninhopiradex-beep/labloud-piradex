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
    if (! unlocked.load() && pcc.restore (*props)) setOnline();       // chave de licença (PCC) lembrada neste computador
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
    currentIndex = index; roleValue.store ((int) secrets::kPasswords[index].role); online.store (false); unlocked.store (true);
}

void AccessManager::setOnline()
{
    const auto r = pcc.role();
    currentIndex = -1; online.store (true);
    roleValue.store ((int) (r == "master" ? lx::lic::Role::Master : r == "beta" ? lx::lic::Role::Beta : lx::lic::Role::Beta));
    unlocked.store (true);
}

AccessManager::LoginResult AccessManager::applyOnline (const piradex::pcc::Check& c, bool remember)
{
    if (c.outcome != piradex::pcc::Outcome::Ok) { lastOnline = piradex::pcc::uiText (c, false); return LoginResult::Online; }
    const juce::ScopedLock sl (lock);
    pcc.commit (c, remember, *props);
    props->removeValue ("loginId"); props->removeValue ("loginToken"); props->saveIfNeeded();
    setOnline();
    return LoginResult::Ok;
}

bool AccessManager::claimOnlineRecheck()
{
    const auto now = piradex::pcc::unixNow();
    if (! online.load() || ! piradex::pcc::enabled() || now - lastRecheckTry.load() < 3600) return false;   // sem rede: tenta de hora a hora
    if (now - props->getValue ("pccCheck").getLargeIntValue() < (juce::int64) pq::pcc::kRecheckHours * 3600) return false;
    lastRecheckTry.store (now);
    return true;
}

void AccessManager::revalidateOnline()
{
    if (pcc.revalidate (*props) == piradex::pcc::Session::Recheck::Revoked)
        juce::MessageManager::callAsync ([] { AccessManager::get().logout(); });
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
    unlocked.store (false); online.store (false); pcc.clear (*props); roleValue.store (0); currentIndex = -1;
    props->removeValue ("loginId"); props->removeValue ("loginToken"); props->saveIfNeeded();
}

void AccessManager::checkExpiry()
{
    if (unlocked.load() && online.load()) { if (! pcc.stillGood()) logout(); return; }   // licença online expirada ou 14 dias sem validar
    int idx; { const juce::ScopedLock sl (lock); idx = currentIndex; }
    if (unlocked.load() && idx >= 0 && lic::pwExpired (secrets::kPasswords[idx], today())) logout();
}

int AccessManager::betaDaysLeft() const
{
    if (online.load()) return (unlocked.load() && role() == lx::lic::Role::Beta) ? pcc.daysLeft() : -1;
    int idx; { const juce::ScopedLock sl (lock); idx = currentIndex; }
    if (! unlocked.load() || role() != lic::Role::Beta || idx < 0) return -1;
    return lic::daysLeft (secrets::kPasswords[idx], today());
}

juce::String AccessManager::roleText() const
{
    if (unlocked.load() && online.load())
    {
        if (role() == lx::lic::Role::Master) return "MESTRE";
        int y = 0, mo = 0, d = 0; if (! pq::pcc::parseDate (pcc.expires(), y, mo, d)) return "LICENCIADO";
        return juce::String (juce::CharPointer_UTF8 ("LICEN\xC3\x87""A \xC2\xB7 at\xC3\xA9 ")) + juce::String (d).paddedLeft ('0', 2) + "/" + juce::String (mo).paddedLeft ('0', 2) + "/" + juce::String (y);
    }
    int idx; { const juce::ScopedLock sl (lock); idx = currentIndex; }
    if (! unlocked.load() || idx < 0) return {};
    if (role() == lic::Role::Master) return "MESTRE";
    const auto& e = secrets::kPasswords[idx];
    return juce::String (juce::CharPointer_UTF8 ("BETA \xC2\xB7 at\xC3\xA9 ")) + juce::String (e.expDay).paddedLeft ('0', 2) + "/" + juce::String (e.expMonth).paddedLeft ('0', 2) + "/" + juce::String (e.expYear);
}
