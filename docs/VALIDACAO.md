# LABLOUD by Piradex — validação final (Beta 2)

Resultado das corridas finais. Tudo o que está aqui foi **medido** nesta sessão; o que não foi medido está na secção "Não verificado".

## 1. Testes automáticos

| Conjunto | Verificações | Falhas | O que cobre |
|---|---|---|---|
| `test_dsp` | 50 | 0 | núcleo original: limiter, clipper, LUFS, true peak, Auto-Dose |
| `test_pdf` | 29 | 0 | limiter novo (Dual/Adaptive, Punch, Bass, Release Link, Distortion Guard, fuzz de 40 combinações) e clipper novo (Custom, assimetria, dual, seletivo, post-gain, DC blocker, estatísticas, tabela vs fórmula, NaN/Inf) |
| `test_engine` | 20 | 0 | crossover LR4 plano, multibanda, oversampler 2×–32× (reconstrução exata), analisador de harmónicos, aliasing por estágio, perfis, latência constante do `ClipPath`, troca sem cliques, monitor de espectro |
| `test_assist` | 15 | 0 | perfil do material, Smart Clip Amount, Auto Routing, Smart Distribution, Balance, Aliasing Guard (histerese), Efficiency, Quality Optimizer determinístico |
| `test_licensing` | 22 | 0 | senhas, PBKDF2, datas (corre com `Secrets.h` de teste gerado por `tools/gen_secrets.py`) |
| `LoudnexSmoke` (processador JUCE) | 46 | 0 | latência, tetos, estado, laudo, Auto-Dose, gain match, Hybrid, M/S, multibanda, Delta, slots, assistentes, sample rates, fuzz do processador, tempo real, CPU, editor |

Compilação do núcleo com `-Wall -Wextra`: **0 avisos**.

## 2. Sanitizers

* Núcleo (`test_dsp`, `test_pdf`, `test_engine`, `test_assist`) com **AddressSanitizer + UndefinedBehaviorSanitizer** (`-fno-sanitize-recover`): limpo.
* Teste de fumo completo do processador (JUCE incluído) com ASan + UBSan: **46 verificações, 0 falhas, nenhum erro de memória nem comportamento indefinido** (detecção de fugas desligada porque o JUCE mantém singletons até ao fim do processo).
* Não foi corrido ThreadSanitizer (o processador não tem threads próprias além do temporizador de licença).

## 3. Garantias medidas

| Garantia | Resultado |
|---|---|
| Teto de pico de amostra | nunca acima do teto em 40 combinações aleatórias de todos os modos/parâmetros (pior caso 1,0005× o teto) e em 7 configurações Hybrid/M-S/multibanda |
| Pico verdadeiro (TP ligado) | −1,00 dBTP para teto −1 dB a 44,1 / 96 / 192 kHz (medidor interno); limiter puro em fuzz: ≤ 1,036× o teto contra um medidor independente de 64 taps |
| Latência constante | 539 amostras (11,23 ms @48 kHz) desde a 0.5.0 (antes 419: o estimador de pico verdadeiro passou de 48 para 128 taps), igual em todos os modos, rotas, M/S e oversampling |
| Tempo real | **0** alocações (`operator new`) e sem crescimento do heap em `processBlock` em 11 configurações |
| Entradas inválidas | NaN, ±Inf, 1e30, denormais: saída finita e dentro do teto |
| Troca AUTO de oversampling | sem descontinuidade (maior passo igual ao dos caminhos fixos) |
| Multibanda | soma plana, desvio máximo 0,038 dB (40 Hz–12 kHz) |
| Oversampler | reconstrução da sinusoide com erro < 1e-5 em 2×, 4×, 8×, 16×, 32× |
| Aliasing (clip duro, 3 kHz) | 1×: −44,5 dB · 4×: −67,5 dB · 16×: −69,7 dB (relativo à fundamental; estimativa). De 4× para 16× o ganho neste teste é só ~2 dB: o piso de −68/−70 dB é o limite do filtro de 65 taps e da estimativa, por isso 16× e 32× compram pouco aliasing neste material e custam muito CPU |
| Curva Custom (tabela vs fórmula exata) | erro máximo 1e-6 |

## 4. CPU (compilação Release, 1 núcleo **deste servidor**, estéreo 48 kHz, blocos de 512)

| Configuração | CPU |
|---|---|
| Limiter | 4,0 % |
| Limiter M/S | 5,6 % |
| Clipper 1× / 4× / 16× / 32× | 3,8 % / 5,2 % / 12,8 % / 18,9 % |
| Clipper 16× multibanda | 24,7 % |
| Hybrid Clip>Limit 16× / Limit>Clip 16× | 12,8 % / 17,7 % |
| Hybrid AUTO + M/S + multibanda | 23,7 % |
| Tudo no máximo (Limit>Clip, 32×, multibanda, M/S, Delta) | 42,5 % |

Há variação de ±15 % entre corridas neste servidor. As otimizações feitas durante a validação (oversampler polifásico com acumuladores independentes, tabelas em vez de `pow`/`tanh`, estimador de pico verdadeiro em buffer duplo) baixaram o pior caso de 64 % para ~43–52 %. 32× e multibanda a 16×/32× devem ser tratados como modos de exportação/máquina folgada.

## 5. Defeitos encontrados e corrigidos pela validação

1. Viés da curva Custom sem efeito (a curva recebia |x|).
2. DC blocker com overshoot de ~3 % acima do teto.
3. Mid/Side ortonormal baixava mono em 3 dB.
4. Sem TP, o filtro de decimação do oversampler podia passar o teto em ~0,5 %.
5. Desalinhamento de 0,5 amostra por estágio na decimação do oversampler.
6. Medidor de transiência dos assistentes mal calibrado.
7. Vários testes que eram fracos ou erradamente desenhados foram refeitos (e documentados no histórico).

## 6. Não verificado (e porquê)

* **Audição / ABX**: não foi possível ouvir nada. Nenhum dos perfis (Warm, Punch, Aggressive), das curvas, nem o comportamento musical do release adaptativo foi avaliado de ouvido. Os assistentes são heurísticas e a pontuação do Quality Optimizer é uma fórmula explícita, não uma medida de qualidade percebida.
* **Valores de demonstração**: os medidores da especificação de interface (`docs/gui/loudnex_gui_v7.html`) são simulados, exceto o painel de análise, que calcula de verdade a curva.
* **Formatos e plataformas**: apenas Linux/Release com o alvo de teste. VST3/AU/AAX reais, macOS, Windows, Pro Tools (AAX/PACE) e o CI não foram compilados nem testados.
* **Editor JUCE** (resolvido na 0.4.0): todos os parâmetros têm controlo na interface; os últimos que faltavam eram o Alvo LUFS e o Clip Assist (página Assistentes · Delta → Sistema).
* **Verificadores independentes** (feito na 0.4.0, ver secção 7): LUFS e LRA comparados com a libebur128; pico verdadeiro comparado com um interpolador ideal independente. Os ficheiros de teste oficiais EBU (Tech 3341/3342) ainda não foram corridos.
* **Concorrentes**: as afirmações do relatório CLIPLIM sobre outros produtos não foram verificadas.
* **Estabilidade a longo prazo**: não foram feitas corridas de horas; o fuzz foi de segundos de áudio por combinação.

## 7. Validação independente (0.4.0, `tests/test_ebur128.cpp`, corre no GitHub Actions)

Referências: **libebur128** (implementação de referência ITU-R BS.1770 / EBU R128 usada por ffmpeg e Audacity) para LUFS e LRA;
para o pico verdadeiro, um **interpolador ideal** independente (32×, sinc de 8193 taps com janela de Kaiser β = 14, verificado:
seno fs/4 de amplitude 0,5 = −6,021 dBTP exatos). A libebur128 também mede pico verdadeiro, mas o seu interpolador de 4×
lê até **+0,6 dB a mais** em tons perto de fs/4 (o próprio teste mostra-o), por isso não serve de critério para o teto.

| Medidor do LABLOUD vs libebur128 (ruído com 3 níveis, 20 s) | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| LUFS integrado (diferença) | 0,01 LU | 0,00 LU | 0,00 LU |
| LRA (LABLOUD / libebur128) | 12,00 / 12,02 LU | 12,00 / 12,01 LU | 12,10 / 12,04 LU |

| Teto de pico verdadeiro (TP ligado, teto −1,0 dBTP, +6 dB) — interpolador ideal | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Seno fs/4 a 45° (pior caso clássico) | −1,00 | −1,00 | −1,00 |
| Varrimento 20 Hz–20 kHz | −0,99 | −0,99 | −1,00 |
| Programa estéreo com hats **sem filtro** (muito ruído perto de Nyquist) — 0.4.0 (estimador de 48 taps) | −0,70 | −0,69 | −0,71 |
| O mesmo programa — **0.5.0** (estimador de 128 taps, janela de Kaiser) | **−0,91** | **−0,91** | **−0,91** |

**Conclusão (0.5.0):** o medidor de loudness do LABLOUD é equivalente à referência (≤ 0,01 LU). O teto de pico verdadeiro
fica a ≤ 0,09 dB do teto em todo o material testado, incluindo o caso mais difícil (muita energia perto de Nyquist), que na
0.4.0 passava ~0,3 dB. A causa era o filtro do estimador (48 taps, janela Blackman): perdia os picos de conteúdo acima de ~0,4 fs.
Com 128 taps e janela de Kaiser (β = 8) o erro do estimador é 0,000 dB até 0,42 fs. Custo: +120 amostras de latência
(2,5 ms a 48 kHz) e ~+1,8 % de CPU no modo Limiter. A barreira de segurança que já existia não resolvia o problema
(medido: igual com e sem ela), porque usava o mesmo estimador.
