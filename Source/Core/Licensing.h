// =============================================================================
//  LABLOUD by Piradex — regras de acesso (puras, sem JUCE, testáveis)
//
//   • Duas senhas: MASTER (não expira) e BETA (válida até ao fim de uma data).
//   • Só se guardam salt + hash PBKDF2-HMAC-SHA256 (nunca a senha em claro).
//   • O ficheiro Source/Secrets.h é gerado por tools/gen_secrets.py e nunca vai para o GitHub.
//   • Nada disto corre na thread de áudio: a verificação é lenta de propósito (PBKDF2).
// =============================================================================
#pragma once
#include "Crypto.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>

namespace lx { namespace lic
{
enum class Role { None = 0, Master, Beta };

struct PwEntry
{
    const char* id;
    Role        role;
    const char* saltHex;
    const char* hashHex;
    int expYear, expMonth, expDay;   // só beta: válida até ao fim deste dia (hora local)
};

constexpr int kPbkdf2Iterations = 60000;

// dias desde 2026-01-01 (algoritmo de Howard Hinnant, calendário gregoriano)
inline int daysSince2026 (int y, int m, int d)
{
    auto days = [] (int yy, int mm, int dd)
    {
        yy -= mm <= 2;
        const int era = (yy >= 0 ? yy : yy - 399) / 400;
        const unsigned yoe = (unsigned) (yy - era * 400);
        const unsigned doy = (unsigned) ((153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + dd - 1);
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int) doe - 719468;
    };
    return days (y, m, d) - days (2026, 1, 1);
}

inline int todayDays2026()
{
    std::time_t t = std::time (nullptr); std::tm lt {};
   #if defined (_WIN32)
    localtime_s (&lt, &t);
   #else
    localtime_r (&t, &lt);
   #endif
    return daysSince2026 (lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// proteção contra recuo do relógio: usa-se sempre o maior entre "hoje" e o último dia visto
inline int effectiveToday (int nowDays, int lastSeenDays) { return std::max (nowDays, lastSeenDays); }

inline bool pwExpired (const PwEntry& e, int today)
{
    if (e.role != Role::Beta) return false;
    return today > daysSince2026 (e.expYear, e.expMonth, e.expDay);
}

inline std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

// índice da entrada que corresponde à senha; -1 se errada. expired=true se acertou mas já caducou.
inline int matchPassword (const PwEntry* entries, int n, const std::string& passwordRaw, int today, bool& expired)
{
    const std::string pw = trim (passwordRaw);
    expired = false; int found = -1;
    for (int i = 0; i < n; ++i)   // percorre todas as entradas, sempre
    {
        const crypto::Bytes expected = crypto::fromHex (entries[i].hashHex);
        const crypto::Bytes got = crypto::pbkdf2Sha256 (pw, crypto::fromHex (entries[i].saltHex), kPbkdf2Iterations, (int) expected.size());
        if (! crypto::constantTimeEq (got, expected)) continue;
        if (pwExpired (entries[i], today)) { expired = true; continue; }
        if (found < 0) found = i;
    }
    return found;
}

inline std::string formatDate (int y, int m, int d)
{
    char b[24]; std::snprintf (b, sizeof b, "%02d/%02d/%04d", d, m, y); return b;
}

// dias que faltam (incluindo hoje) até ao fim do dia de expiração; 0 se já caducou
inline int daysLeft (const PwEntry& e, int today)
{
    const int last = daysSince2026 (e.expYear, e.expMonth, e.expDay);
    return std::max (0, last - today + 1);
}
}} // namespace lx::lic
