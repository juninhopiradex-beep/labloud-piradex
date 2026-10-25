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
| Latência constante | 419 amostras (8,73 ms @48 kHz) em 32 combinações de modo × rota × M/S × oversampling; 8,98 ms @44,1; 7,36 ms @96; 6,68 ms @192 |
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
* **Editor JUCE**: não expõe os controlos novos (só Modo/Forma/Oversampling foram estendidos); os restantes são parâmetros do anfitrião. Falta portar a interface da especificação v7.
* **Verificadores independentes**: o pico verdadeiro foi comparado com um medidor próprio de 64 taps, não com um medidor certificado BS.1770/EBU; os LUFS foram verificados contra sinais de referência simples (tom a −20 dBFS → −20,01 LUFS), não contra os ficheiros de teste oficiais EBU.
* **Concorrentes**: as afirmações do relatório CLIPLIM sobre outros produtos não foram verificadas.
* **Estabilidade a longo prazo**: não foram feitas corridas de horas; o fuzz foi de segundos de áudio por combinação.
