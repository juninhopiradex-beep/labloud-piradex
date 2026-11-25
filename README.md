# labloud-piradex
## Licença online (PCC, desde a 0.3.0)

> **Versão atual: 1.0.2** (todos os plugins Piradex passaram a ter a mesma numeração; as versões abaixo de "Histórico" são da numeração antiga).
>
> * **Login:** obrigatório desde a 1.0.1 — senha master, senha beta (válida até 31/12/2026) ou chave de licença validada online no PCC. Sem sessão, o áudio passa intacto.
> * **Instaladores (loja www.beatfreakstudio.com/loja):** Mac Apple Silicon (`…-macOS-AppleSilicon.pkg`), Mac Intel com macOS 10.13 ou mais recente (`…-macOS-Intel-macOS10.13.pkg`), Windows (`…-Windows-Setup.exe`) e AAX para Pro Tools (`…-AAX-*.zip`, ver LEIA-ME-AAX.txt).
> * **Criador e desenvolvedor:** Ricardo Amorim / Juninho Piradex.

No ecrã de senha, além da master e das senhas beta (offline), aceita-se uma chave de licença `LDX-XXXX-XXXX-XXXX`,
validada online no PCC (Plugin Control Central, Supabase). A chave fica presa ao primeiro computador que a ativar,
funciona 14 dias sem internet e é revalidada no máximo 1 vez por dia; desativada no PCC, deixa de abrir o plugin.
Criar chaves no SQL Editor do PCC: `select * from pcc_new_loudnex_license('Nome','email','País','venda', null);`.
A chave em claro nunca sai do computador (vai `sha256(CHAVE|LOUDNEX)`); a resposta é assinada (Ed25519) e verificada
com a chave pública embebida no build. Código: `Source/pcc/`.
