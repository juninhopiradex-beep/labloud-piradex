# LABLOUD by Piradex — referência técnica do DSP

Núcleo em C++17 puro (`Source/DSP/`), sem dependência do JUCE; o processador (`Source/PluginProcessor.*`) liga-o aos parâmetros e à latência.

| Ficheiro | Conteúdo |
|---|---|
| `LoudnexDsp.h` | Limiter, Clipper, curvas, medidores (LUFS BS.1770, True Peak), Auto-Dose |
| `ClipLimEngine.h` | Crossover LR4 de 3 bandas, clipper multibanda, oversampler de meia-banda (1×…32×), FFT e analisador de harmónicos, perfis de carácter |
| `Assist.h` | Assistentes determinísticos (perfil do material, Smart Clip Amount, Auto Routing, Smart Distribution, Balance, Aliasing Guard, Loudness Efficiency, Quality Optimizer) |
| `ClipPath.h` | `ClipPath` (clipper + oversampling com latência fixa), `ClipSwitcher` (troca AUTO por crossfade), monitor de espectro |

## Modos

* **Limiter** — lookahead 0,2–5 ms, estimador de pico verdadeiro 8× (48 taps), release com envelope simples/dual/adaptativo.
* **Clipper** — curvas Soft / Cubic / Hard / Custom, oversampling 1×/4×/16×/32×, multibanda opcional, barreira final de pico.
* **Hybrid** — dois estágios em série. `Clip>Limit`: o clipper corta a `Ceiling + (1−r)·6 dB` e o limitador fecha no teto. `Limit>Clip`: o limitador fecha a `Ceiling + r·3 dB` e o clipper termina no teto, seguido da barreira de pico. `r` é o parâmetro **Clip/Limit**.

## Garantias (verificadas em `tests/`)

1. **Teto**: o pico de amostra nunca ultrapassa o teto em nenhuma combinação de modo/rota/M-S/multibanda/oversampling (fuzz: 40 combinações aleatórias, pior caso 1,0005× o teto; esse excesso é o limiar do teste, 0,05 %).
2. **Pico verdadeiro**: com TP ligado, medido por um medidor independente de 64 taps a 8×, o excesso máximo medido foi 1,036× o teto (≈0,31 dB) em fuzz de limiter puro; nos testes do processador o `TruePeakMeter` ficou em −1,00 dBTP para teto −1 dB. O estimador interno tem erro de ~0,2 dB, não é o filtro normativo da BS.1770.
3. **Latência constante**: o valor comunicado ao anfitrião é o mesmo em todos os modos, rotas, M/S e estágios de oversampling (419 amostras = 8,73 ms a 48 kHz; 8,98 ms a 44,1 kHz; 6,68 ms a 192 kHz). Mede-se com um impulso e coincide com o reportado (desvio máximo 0 amostras em 32 combinações).
4. **Tempo real**: 0 alocações (`operator new`) e sem crescimento do heap em `processBlock`, em 11 configurações incluindo o pior caso.
5. **Entradas inválidas**: NaN/Inf/denormais dão saída finita.

## Parâmetros novos

**Limiter**: `relmode` (Single/Dual/Adaptive), `punch`, `bassamt`, `spectral`, `rlink`, `distguard`.
**Clipper**: `postgain`, `clippos`/`clipneg` (clip assimétrico), `dcblock`, `cexp` + `bias` (curva Custom), `dual` + `dualoff`, `transel`, `hchar` (Manual/Clean/Warm/Punch/Aggressive), `mb` + `xo1`/`xo2` + `mbg1..3`, `alias` (Manual/ECO/AUTO/MAX), `os` (1×/4×/16×/32×).
**Sistema**: `route`, `ratio`, `balance` (macro aplicada por `applyBalance()`), `ceilock`, `delta` + `deltasel`, `ms`, `hq`.

## Notas de implementação

* **Curva Custom** — linear até ao joelho `k`; depois `k + (1−k)·g(v)` com `g(v) = v/(1+v^p)^(1/p)`, derivada contínua no joelho, monótona, `p` 1…16. O viés desloca o ponto de operação (`y = f(u+b) − f(b)`) e gera harmónicos pares; precisa do DC blocker. No Clipper usa-se uma tabela de 2048 pontos (erro máximo medido 1e-6 contra a fórmula exata).
* **DC blocker** — passa-alto de 5 Hz; faz overshoot de ~3 % em sinais muito cortados, por isso tem um clamp ao teto e vem desligado por omissão.
* **Multibanda** — 3 bandas LR4; a banda grave passa também pelo passa-tudo do 2.º cruzamento, por isso a soma é plana (desvio ≤ 0,04 dB, 40 Hz–12 kHz). A soma é limitada ao teto.
* **Oversampling** — filtros de meia-banda de 65 taps em cascata (polifásicos), reconstrução exata na banda útil, latência inteira fixa; o caminho é sempre atrasado até 62 amostras (o valor do 32×) para a latência não depender do estágio.
* **Aliasing Guard AUTO** — decide o estágio (1×/4×/16×) a partir da densidade de corte e do conteúdo agudo; sobe depressa e só desce após ~0,8 s calmos. A troca corre num segundo caminho em paralelo (pré-roll de 256 amostras) e faz crossfade de 256 amostras: sem descontinuidade (maior passo igual ao dos caminhos fixos).
* **Mid/Side** — `M=(L+R)/2`, `S=(L−R)/2`; a descodificação pode levantar o pico em L/R, por isso há sempre uma barreira de pico no fim.
* **Delta** — total (saída − entrada alinhada) ou por estágio (Clipper / Limiter). O delta do estágio do clipper usa o ganho linear interpolado por bloco, aproximação do ganho suavizado do clipper.
* **HQ offline** — em renderização não-tempo-real força 32×; a latência não muda.
* **Assistentes** — todos são regras explícitas, deterministas, com a pontuação visível; **não são IA**. Correm no thread de mensagens sobre 8 s de áudio capturado pelo Auto-Dose.

## Estimativa de CPU (compilação Release, 1 núcleo deste servidor, estéreo 48 kHz, blocos de 512)

| Configuração | CPU |
|---|---|
| Limiter (TP, release adaptativo) | 4 % |
| Clipper 1× / 4× / 16× / 32× | 4 % / 5 % / 11 % / 21 % |
| Clipper 16× multibanda | 29 % |
| Hybrid 16× | 13–14 % |
| Tudo ao máximo (Hybrid Limit>Clip, 32×, multibanda, M/S, Delta) | 52 % |

Valores medidos neste servidor; no computador do utilizador serão diferentes. 32× e multibanda a 16×/32× são modos pesados e devem ser apresentados como tal.
