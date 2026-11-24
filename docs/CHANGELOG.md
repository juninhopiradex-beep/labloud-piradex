# Changelog — LABLOUD by Piradex

## 0.5.0 (24/11/2026)

* **Pico verdadeiro mais exato:** o estimador passou de 48 para 128 taps (janela de Kaiser). Em material com muita energia perto de Nyquist (hats sem filtro, ruído), o teto de pico verdadeiro passava ~0,3 dB; agora fica a ≤ 0,09 dB, medido contra um interpolador ideal independente (`tests/test_ebur128.cpp`, tolerância apertada para 0,25 dB em todo o material). O medidor TRUE PEAK da interface fica igualmente mais exato.
* Latência constante passa de 419 para 539 amostras (11,23 ms a 48 kHz), igual em todos os modos (o DAW compensa automaticamente). CPU do Limiter +~1,8 %.

## 0.4.0 (auditoria 24/11/2026)

* **Interface completa:** Alvo LUFS (Off / −23 / −16 / −14 / −9 / −7) e Clip Assist passam a ter controlo na página Assistentes · Delta → Sistema (antes só se mexiam pelo DAW).
* **Validação independente** (`tests/test_ebur128.cpp`, no GitHub Actions): LUFS e LRA contra a libebur128 (diferença ≤ 0,01 LU) e teto de pico verdadeiro contra um interpolador ideal. Resultados e um limite conhecido em material muito brilhante em `docs/VALIDACAO.md` (secção 7).
* Corrigidos acentos mal mostrados no painel Loudness Efficiency ("GR médio").
* Teste de fumo: o teste "bloqueado" é saltado durante o período aberto (até 31/12/2026).
* Nova senha master (a anterior estava escrita no repositório).

## Beta 2 (implementação do relatório CLIPLIM)

### Limiter
* Release **Single / Dual / Adaptive** (bi-exponencial, o lento alarga com a profundidade do corte).
* **Punch Protect** (trava de transiente com decaimento de 80 ms), **Bass Guard** com quantidade, **Release Link**, **Spectral Sensitivity** (Flat / Perceptual / Bass-safe).
* **Distortion Guard** (Off / Soft / Normal / Strict) com índice de distorção por modulação de ganho.
* Estimador de pico verdadeiro de **48 taps** (a versão anterior tinha menos taps e ficava até 1,078× acima do teto no fuzz): erro máximo medido ≈ 0,2 dB.

### Clipper
* Curva **Custom** (dureza 1–16, viés harmónico), **clip assimétrico** (+/−), **dois estágios**, **seletivo por transiente**, **post-gain**, **DC blocker** com clamp ao teto.
* **Multibanda** de 3 bandas LR4 (soma plana, ≤ 0,04 dB).
* **Perfis de carácter**: Clean, Warm, Punch, Aggressive.
* Oversampling **1× / 4× / 16× / 32×** com filtros de meia-banda próprios (polifásicos) e **Aliasing Guard** Manual / ECO / AUTO / MAX; a troca AUTO é um crossfade, sem cliques.
* Estatísticas: densidade de picos, dB cortados, eventos, duração média.

### Modo novo: Hybrid
* **Clip>Limit** e **Limit>Clip**, com o rácio **Clip/Limit**.

### Sistema
* **Mid/Side** com barreira final de pico em L/R, **Delta** (total, clipper, limiter), **Ceiling Lock**, **HQ offline** (32× na exportação), slots **A/B/C/D**, anular/refazer, histórico de GR, monitor de espectro/harmónicos.
* **Assistentes** (determinísticos): Smart Clip Amount, Auto Routing, Smart Distribution, Quality Optimizer, macro Balance, Loudness Efficiency.
* **Latência constante** em todos os modos (419 amostras a 48 kHz).

### Validação
* Núcleo: `test_dsp`, `test_pdf`, `test_engine`, `test_assist` (0 falhas); ASan + UBSan limpos.
* Processador (JUCE): teste de fumo com latência, tetos, Delta, slots, assistentes, sample rates, fuzz de parâmetros, zero alocações em tempo real, CPU.
* Ver `docs/DSP.md`.

### Correções feitas durante a validação
* O viés da curva Custom não tinha efeito (a curva recebia o valor absoluto); corrigido.
* O DC blocker fazia overshoot de ~3 % acima do teto; passou a ter clamp e a vir desligado.
* Mid/Side com escala ortonormal baixava um sinal mono 3 dB; passou a `M=(L+R)/2`.
* Sem True Peak, o filtro de decimação do oversampler podia ultrapassar o teto em ~0,5 %; a barreira final do clipper corre sempre.
* A decimação do oversampler tinha 0,5 amostra de desalinhamento por estágio; corrigido (reconstrução exata).

### Ainda por fazer
* Verificação auditiva (ABX), AAX, macOS e Windows, CI, instaladores.
