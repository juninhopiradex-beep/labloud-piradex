// Testes do sistema de senhas: PBKDF2 contra vectores conhecidos, hash do Python == hash do C++,
// master / beta / expirada / errada, recuo do relógio e datas.
#include "Secrets.h"      // gerado por: python3 tools/gen_secrets.py --out <pasta> --master TestMaster-123 --beta TestBeta-456
#include <cstdio>
#include <string>
using namespace lx;

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::printf ("FALHOU: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } else std::printf ("ok     %s\n", msg); } while (0)

int main()
{
    // vectores PBKDF2-HMAC-SHA256 (conferidos com hashlib do Python)
    CHECK (crypto::toHex (crypto::pbkdf2Sha256 ("password", "salt", 1, 32))    == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", "PBKDF2 c=1");
    CHECK (crypto::toHex (crypto::pbkdf2Sha256 ("password", "salt", 4096, 32)) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a", "PBKDF2 c=4096");
    CHECK (crypto::toHex (crypto::sha256 ("abc", 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc");

    // datas
    CHECK (lic::daysSince2026 (2026, 1, 1) == 0, "2026-01-01 = dia 0");
    CHECK (lic::daysSince2026 (2026, 11, 1) == 304, "2026-11-01 = dia 304");
    CHECK (lic::daysSince2026 (2027, 1, 1) == 365, "2027-01-01 = dia 365");
    CHECK (lic::daysSince2026 (2028, 3, 1) - lic::daysSince2026 (2028, 2, 28) == 2, "2028 é bissexto");

    const auto* P = secrets::kPasswords; const int N = secrets::kPasswordCount;
    const int d1031 = lic::daysSince2026 (2026, 10, 31), d1101 = lic::daysSince2026 (2026, 11, 1), d1102 = lic::daysSince2026 (2026, 11, 2);
    bool exp = false;

    CHECK (lic::matchPassword (P, N, "TestMaster-123", d1031, exp) == 0 && ! exp, "master aceite");
    CHECK (lic::matchPassword (P, N, "TestMaster-123", d1102 + 4000, exp) == 0 && ! exp, "master nunca expira");
    CHECK (lic::matchPassword (P, N, "  TestMaster-123 \n", d1031, exp) == 0, "espaços à volta são ignorados");
    CHECK (lic::matchPassword (P, N, "TestBeta-456", d1031, exp) == 1 && ! exp, "beta aceite antes da data");
    CHECK (lic::matchPassword (P, N, "TestBeta-456", d1101, exp) == 1 && ! exp, "beta aceite no último dia (01/11/2026)");
    CHECK (lic::matchPassword (P, N, "TestBeta-456", d1102, exp) == -1 && exp, "beta recusada e marcada como expirada em 02/11/2026");
    CHECK (lic::matchPassword (P, N, "testmaster-123", d1031, exp) == -1 && ! exp, "maiúsculas/minúsculas contam");
    CHECK (lic::matchPassword (P, N, "errada", d1031, exp) == -1 && ! exp, "senha errada");
    CHECK (lic::matchPassword (P, N, "", d1031, exp) == -1, "senha vazia");

    CHECK (lic::effectiveToday (d1102, d1031) == d1102, "relógio normal");
    CHECK (lic::effectiveToday (d1031, d1102) == d1102, "recuo do relógio não reabre a beta");

    CHECK (lic::daysLeft (P[1], d1031) == 2, "beta: 2 dias (31/10 e 01/11)");
    CHECK (lic::daysLeft (P[1], d1101) == 1, "beta: último dia = 1");
    CHECK (lic::daysLeft (P[1], d1102) == 0, "beta: 0 depois de expirar");
    CHECK (lic::formatDate (2026, 11, 1) == "01/11/2026", "formato de data");

    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails ? 1 : 0;
}
