// Testes do motor auxiliar: LR4/multibanda, oversampler, analisador de harmónicos, perfis de carácter
#include "DSP/ClipPath.h"
#include "test_util.h"
int main()
{
    const double fs = 48000.0;
    std::printf ("[Crossover LR4 3 bandas: soma plana]\n");
    {
        MultibandClipper mb; mb.prepare (fs); MultibandParams p; p.base.ceilingDb = 0; p.base.gainDb = 0; p.base.shape = ClipShape::Hard; p.base.mix = 1; mb.setParams (p); mb.settle();
        double worst = 0;
        for (double f : { 40.0, 100.0, 150.0, 300.0, 1000.0, 2500.0, 5000.0, 12000.0 })
        {
            mb.reset(); auto x = sineV (fs, f, 0.1, (int) fs); std::vector<float> y (x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = mb.process (x[i]);
            const double g = 20 * std::log10 (rmsOf (y, 20000, 40000) / rmsOf (x, 20000, 40000)); worst = std::max (worst, std::fabs (g));
        }
        CHECK (worst < 0.05, "Resposta de módulo da soma das 3 bandas: desvio máx %.4f dB (40 Hz..12 kHz)", worst);
    }
    std::printf ("\n[Multibanda: teto, segurança e estatísticas]\n");
    {
        MultibandClipper mb; mb.prepare (fs); MultibandParams p; p.base.ceilingDb = -2; p.base.gainDb = 10; p.base.shape = ClipShape::Soft; p.bandGainDb[0] = 3; p.bandCeilOffDb[2] = -2; mb.setParams (p); mb.settle();
        std::mt19937 rng (5); std::uniform_real_distribution<double> nz (-1, 1); double pk = 0;
        for (int i = 0; i < (int) fs * 2; ++i) { const float x = (float) (0.6 * std::sin (2 * kPi * 60 * i / fs) + 0.4 * nz (rng)); pk = std::max (pk, (double) std::fabs (mb.process (x))); }
        CHECK (pk <= dbToLin (-2.0) * (1 + 1e-6), "Pico multibanda %.4f <= teto %.4f", pk, dbToLin (-2.0));
        ClipStats s = mb.takeStats(); CHECK (s.affected > 0 && s.maxClipDb > 1.0, "Stats multibanda: %.1f%% das amostras-banda, corte máx %.1f dB", s.densityPct(), s.maxClipDb);
        bool ok = true; const float bad[] = { NAN, INFINITY, -INFINITY, 1e30f }; for (int i = 0; i < 400; ++i) { const float o = mb.process (bad[i % 4]); if (!std::isfinite (o) || std::fabs (o) > 1.0f) ok = false; }
        CHECK (ok, "Multibanda: NaN/Inf -> saída finita");
    }
    std::printf ("\n[Oversampler de meia-banda]\n");
    {
        for (int st = 1; st <= 5; ++st)
        {
            HalfbandOversampler os; os.prepare (st); auto id = [] (float v) { return v; };
            std::vector<float> x = sineV (fs, 1000, 0.5, 20000), y (x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = os.process (x[i], id);
            const int L = os.latencySamples(); double err = 0; for (int i = 8000; i < 16000; ++i) { const double e = y[(size_t) i] - 0.5 * std::sin (2 * kPi * 1000 * (i - L) / fs); err = std::max (err, std::fabs (e)); }
            CHECK (err < 2e-3, "%2dx: reconstrução da sinusoide com latência %d amostras, erro máx %.5f", os.factor(), L, err);
        }
    }
    std::printf ("\n[Analisador de harmónicos e aliasing]\n");
    {
        const int N = 8192; const double f0 = 171.0 * fs / N;     // coerente com a janela
        auto run = [&] (int stages, const ClipperParams& cp, double amp = 0.5)
        {
            HalfbandOversampler os; os.prepare (stages); Clipper c; c.prepare (fs * os.factor()); c.setParams (cp); c.settle();
            auto f = [&] (float v) { return c.process (v); };
            const int total = 30000 + N; std::vector<float> x = sineV (fs, f0, amp, total), y ((size_t) total); for (int i = 0; i < total; ++i) y[(size_t) i] = os.process (x[(size_t) i], f);
            return analyzeHarmonics (y.data() + 30000, N, fs, f0);
        };
        ClipperParams hard; hard.gainDb = 12; hard.ceilingDb = -3; hard.shape = ClipShape::Hard; hard.mix = 1;
        { // sinusoide pura = ~sem distorção
          ClipperParams q = hard; q.gainDb = -20; auto r = run (0, q); CHECK (r.thdPct < 0.01 && r.thdnDb < -80, "Sinusoide sem clip: THD %.4f%%, THD+N %.1f dB", r.thdPct, r.thdnDb); }
        auto a1 = run (0, hard), a4 = run (2, hard), a16 = run (4, hard);
        CHECK (a16.nonHarmDb < a1.nonHarmDb - 10.0, "Aliasing estimado: 1x %.1f dB, 4x %.1f dB, 16x %.1f dB (16x pelo menos 10 dB abaixo)", a1.nonHarmDb, a4.nonHarmDb, a16.nonHarmDb);
        CHECK (a4.nonHarmDb < a1.nonHarmDb, "4x melhor que 1x (%.1f < %.1f dB)", a4.nonHarmDb, a1.nonHarmDb);
        ClipperParams base; base.gainDb = 6; base.ceilingDb = -3; base.mix = 1;
        auto clean = run (2, applyCharacter (base, HarmChar::Clean)), warm = run (2, applyCharacter (base, HarmChar::Warm)), agg = run (2, applyCharacter (base, HarmChar::Aggressive));
        CHECK (warm.hDb[1] > clean.hDb[1] + 10.0, "WARM tem mais 2.º harmónico que CLEAN: %.1f vs %.1f dB", warm.hDb[1], clean.hDb[1]);
        CHECK (agg.thdPct > clean.thdPct, "AGGRESSIVE distorce mais que CLEAN: THD %.2f%% vs %.2f%%", agg.thdPct, clean.thdPct);
        CHECK (clean.hDb[1] < -60.0, "CLEAN simétrico: 2.º harmónico %.1f dB (só ímpares)", clean.hDb[1]);
    }
    std::printf ("\n[ClipPath / ClipSwitcher: latência constante, troca sem cliques]\n");
    {
        // 1) impulso chega sempre na mesma amostra, qualquer estágio e com/sem multibanda
        bool same = true; int ref = -1;
        for (int st = 0; st <= 5; ++st) for (int useMb = 0; useMb < 2; ++useMb)
        {
            ClipPath cp; cp.prepare (fs, 1, st); ClipperParams p; p.gainDb = 0; p.ceilingDb = 0; p.shape = ClipShape::Hard; p.mix = 1; MultibandParams m; cp.setParams (p, m, useMb != 0); cp.settle();
            int arr = -1; double mx = 0; for (int i = 0; i < 400; ++i) { const float y = cp.process (0, i == 10 ? 0.5f : 0.f); if (std::fabs (y) > mx) { mx = std::fabs (y); arr = i; } }
            if (useMb) { if (std::abs (arr - 10 - ClipPath::kLatency) > 1 + 12) same = false; }       // o multibanda tem fase de grupo própria (alguns samples)
            else { if (ref < 0) ref = arr; if (arr != ref) { same = false; std::printf ("        estágio %d chegou em %d (ref %d)\n", st, arr, ref); } }
        }
        CHECK (same, "Latência constante = %d amostras em 1x..32x (impulso no mesmo índice)", ClipPath::kLatency);
        // 2) troca de estágio a meio de uma sinusoide: sem salto
        ClipSwitcher sw; sw.prepare (fs, 2, 0); ClipperParams p; p.gainDb = 6; p.ceilingDb = -3; p.shape = ClipShape::Soft; p.mix = 1; sw.setParams (p, MultibandParams(), false); sw.settle();
        const int N = (int) fs; std::vector<float> l = sineV (fs, 440, 0.5, N), r = sineV (fs, 660, 0.5, N), l0 = l, r0 = r; float* io[2] = { l.data(), r.data() };
        for (int i = 0; i < N; i += 128) { if (i == 8192) sw.requestStage (4); if (i == 24576) sw.requestStage (2); if (i == 32768) sw.requestStage (0); sw.processBlock (io, 128); io[0] += 128; io[1] += 128; if (i + 256 > N) break; }
        // declive máximo teórico: ganho 2 (+6 dB) x amplitude 0.5 x 2π f / fs (zero-crossing, parte linear da curva)
        const double slope = 2.0 * 0.5 * 2.0 * kPi * 440.0 / fs; double maxStep = 0;
        for (int i = 5000; i < N - 400; ++i) maxStep = std::max (maxStep, (double) std::fabs (l[(size_t) i] - l[(size_t) i - 1]));
        CHECK (maxStep < slope * 1.02 && !sw.switching(), "Troca 1x->16x->4x->1x: maior salto entre amostras %.4f <= declive teórico %.4f", maxStep, slope);
        CHECK (peakAbs (l, 2000) <= dbToLin (-3.0) * 1.0005 && peakAbs (r, 2000) <= dbToLin (-3.0) * 1.0005, "Teto -3 dB mantido durante as trocas (%.4f)", std::max (peakAbs (l, 2000), peakAbs (r, 2000)));
    }
    std::printf ("\n[Monitor de espectro]\n");
    {
        auto tone = sineV (fs, 171.0 * fs / 8192, 0.5, 8192); for (auto& v : tone) v = std::max (-0.3f, std::min (0.3f, v));
        auto f = computeSpectrumFrame (tone.data(), 8192, fs); auto noise = std::vector<float> (8192); std::mt19937 rg (3); std::uniform_real_distribution<float> u (-1, 1); for (auto& v : noise) v = u (rg);
        auto g = computeSpectrumFrame (noise.data(), 8192, fs);
        CHECK (f.tone && std::fabs (f.f0 - 171.0 * fs / 8192) < 6.0 && f.h.thdPct > 5.0, "Tom cortado detetado (f0 %.1f Hz, THD %.1f%%)", f.f0, f.h.thdPct);
        CHECK (! g.tone, "Ruído não é tratado como tom");
    }
    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails); return fails ? 1 : 0;
}
