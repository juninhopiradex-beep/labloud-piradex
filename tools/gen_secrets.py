#!/usr/bin/env python3
"""
LABLOUD by Piradex — gera as senhas e o ficheiro de segredos do build.

Cria (e NUNCA deve ir para o GitHub):
  Source/Secrets.h                 usado pelo compilador (já está no .gitignore): só salt + hash PBKDF2
  PRIVADO_senhas_LOUDNEX.txt       as senhas em claro: master e beta
  secrets.b64                      conteúdo para o "secret" LABLOUD_SECRETS_B64 do GitHub Actions

Uso:   python3 tools/gen_secrets.py                          # gera tudo novo
       python3 tools/gen_secrets.py --master "MinhaSenha"    # escolhe a senha master
       python3 tools/gen_secrets.py --beta-until 2026-11-01  # data limite da senha beta (inclusive)
       python3 tools/gen_secrets.py --out /tmp/x             # escreve noutra pasta (testes)
"""
import argparse, base64, datetime, hashlib, os, secrets as sec

ALPHA = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
ITER = 60000   # tem de ser igual a lx::lic::kPbkdf2Iterations


def group(prefix, n_groups):
    return prefix + "".join("-" + "".join(sec.choice(ALPHA) for _ in range(4)) for _ in range(n_groups))


def pbkdf2(pw: str, salt: bytes) -> bytes:
    return hashlib.pbkdf2_hmac("sha256", pw.strip().encode(), salt, ITER, 32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--master"); ap.add_argument("--beta")
    ap.add_argument("--beta-until", default="2026-11-01")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    a = ap.parse_args()
    root = os.path.abspath(a.out)
    until = datetime.date.fromisoformat(a.beta_until)

    master = a.master or group("LXM", 4)
    beta = a.beta or group("LXB", 3)
    s_m, s_b = sec.token_bytes(16), sec.token_bytes(16)
    h_m, h_b = pbkdf2(master, s_m), pbkdf2(beta, s_b)

    header = f'''// GERADO por tools/gen_secrets.py — NÃO COMMITAR (está no .gitignore).
#pragma once
#include "Core/Licensing.h"

namespace lx {{ namespace secrets
{{
// Senhas de acesso: só salt + hash PBKDF2-HMAC-SHA256 ({ITER} iterações)
inline const lic::PwEntry kPasswords[] =
{{
    {{ "master", lic::Role::Master, "{s_m.hex()}", "{h_m.hex()}", 0, 0, 0 }},
    {{ "beta-1", lic::Role::Beta,   "{s_b.hex()}", "{h_b.hex()}", {until.year}, {until.month}, {until.day} }},
}};
inline constexpr int kPasswordCount = 2;
}} }} // namespace lx::secrets
'''
    os.makedirs(os.path.join(root, "Source"), exist_ok=True)
    with open(os.path.join(root, "Source", "Secrets.h"), "w") as f:
        f.write(header)
    with open(os.path.join(root, "secrets.b64"), "w") as f:
        f.write(base64.b64encode(header.encode()).decode())
    priv = f'''LABLOUD by Piradex — SENHAS PRIVADAS  (guarda num gestor de senhas; NUNCA no GitHub)
=====================================================================================

Senha MASTER (acesso total, não expira):
    {master}

Senha BETA (abre o plugin até ao fim de {until.strftime("%d/%m/%Y")}):
    {beta}

Para trocar uma senha ou a data: corre de novo  python3 tools/gen_secrets.py  e substitui o
secret LABLOUD_SECRETS_B64 no GitHub (Settings -> Secrets and variables -> Actions) pelo novo secrets.b64.
'''
    with open(os.path.join(root, "PRIVADO_senhas_LOUDNEX.txt"), "w") as f:
        f.write(priv)
    print("Gerado em", root, ": Source/Secrets.h, secrets.b64, PRIVADO_senhas_LOUDNEX.txt")


if __name__ == "__main__":
    main()
