# Changelog — LABLOUD by Piradex

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
* Editor JUCE com os controlos novos (hoje só Modo/Forma/Oversampling foram estendidos; os restantes são parâmetros acessíveis pelo anfitrião). A especificação de interface está em `docs/gui/loudnex_gui_v7.html`.
* Verificação auditiva (ABX), AAX, macOS e Windows, CI, instaladores.
