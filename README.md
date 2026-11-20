# labloud-piradex
## Licença online (PCC, desde a 0.3.0)

No ecrã de senha, além da master e das senhas beta (offline), aceita-se uma chave de licença `LDX-XXXX-XXXX-XXXX`,
validada online no PCC (Plugin Control Central, Supabase). A chave fica presa ao primeiro computador que a ativar,
funciona 14 dias sem internet e é revalidada no máximo 1 vez por dia; desativada no PCC, deixa de abrir o plugin.
Criar chaves no SQL Editor do PCC: `select * from pcc_new_loudnex_license('Nome','email','País','venda', null);`.
A chave em claro nunca sai do computador (vai `sha256(CHAVE|LOUDNEX)`); a resposta é assinada (Ed25519) e verificada
com a chave pública embebida no build. Código: `Source/pcc/`.
