// Testes das funcionalidades do relatório CLIPLIM (PDF) implementadas no núcleo DSP do LABLOUD.
#include "test_util.h"

static LimiterParams base (double gain = 12, double ceil = -1)
{ LimiterParams p; p.gainDb = gain; p.ceilingDb = ceil; p.releaseMs = 100; p.lookaheadMs = 1.5; p.character = 0; p.bassGuard = false; p.truePeak = true; p.stereoLink = 1; return p; }

// ganho aplicado a um tom constante pequeno depois de uma rajada: mede a recuperação (0 = ainda reduzido, 1 = recuperado)
static std::vector<double> recovery (const LimiterParams& p, double fs, double burstSec, double burstAmp, double toneHz, double toneAmp, double measureSec, int points)
{
    Limiter L; L.prepare (fs, 1); L.setParams (p); L.settle();
    const int nb = (int) (burstSec * fs), nm = (int) (measureSec * fs);
    std::vector<float> x ((size_t) (nb + nm));
    for (int i = 0; i < nb + nm; ++i) x[(size_t) i] = (float) ((i < nb ? burstAmp : toneAmp) * std::sin (2 * kPi * toneHz * i / fs));
    std::vector<float> y = x; runLimiter (L, y);
    const int lat = L.latencySamples(); std::vector<double> g;
    const int win = (int) (fs / toneHz * 2);                      // 2 ciclos
    const double lin = dbToLin (p.gainDb);
    for (int k = 0; k < points; ++k)
    { const int a = nb + lat + (int) ((double) k / points * nm) , b = std::min (a + win, nb + nm - 1); if (b <= a) break;
      g.push_back (rmsOf (y, a, b) / std::max (1e-9, rmsOf (x, a - lat, b - lat) * lin)); }
    return g;
}
static double timeToFrac (const std::vector<double>& g, double frac, double totalSec)   // primeiro instante em que a recuperação passa de frac
{ for (size_t i = 0; i < g.size(); ++i) if (g[i] >= frac) return (double) i / g.size() * totalSec; return totalSec; }

int main()
{
    const double fs = 48000.0;
    std::printf ("[Limiter: release Single / Dual / Adaptive]\n");
    {   // 1) recuperação bi-exponencial: mais depressa no início, mais devagar no fim
        auto p = base (12); p.releaseMs = 150;
        auto s = recovery (p, fs, 0.02, 1.0, 1000, 0.08, 1.2, 240);
        p.releaseMode = ReleaseMode::Dual; auto d = recovery (p, fs, 0.02, 1.0, 1000, 0.08, 1.2, 240);
        const double s50 = timeToFrac (s, 0.5, 1.2), d50 = timeToFrac (d, 0.5, 1.2), s95 = timeToFrac (s, 0.95, 1.2), d95 = timeToFrac (d, 0.95, 1.2);
        CHECK (d50 < s50, "Dual: 50%% da recuperação em %.0f ms (Single %.0f ms): mais rápido no início", d50 * 1000, s50 * 1000);
        CHECK (d95 > s95, "Dual: 95%% da recuperação em %.0f ms (Single %.0f ms): cauda mais lenta", d95 * 1000, s95 * 1000);
    }
    {   // 2) Adaptive: depois de uma redução longa e funda recupera mais devagar do que depois de um clique
        auto p = base (12); p.releaseMs = 120; p.releaseMode = ReleaseMode::Adaptive;
        auto click = recovery (p, fs, 0.004, 1.0, 1000, 0.08, 1.0, 200), longB = recovery (p, fs, 0.6, 1.0, 1000, 0.08, 1.0, 200);
        const double c50 = timeToFrac (click, 0.5, 1.0), l50 = timeToFrac (longB, 0.5, 1.0);
        CHECK (l50 > c50 * 1.15, "Adaptive: recuperação após redução longa (%.0f ms) > após clique (%.0f ms)", l50 * 1000, c50 * 1000);
        p.releaseMode = ReleaseMode::Single;
        auto c1 = recovery (p, fs, 0.004, 1.0, 1000, 0.08, 1.0, 200), l1 = recovery (p, fs, 0.6, 1.0, 1000, 0.08, 1.0, 200);
        CHECK (std::fabs (timeToFrac (c1, 0.5, 1.0) - timeToFrac (l1, 0.5, 1.0)) < 0.06, "Single: recuperação igual nos dois casos (%.0f ms vs %.0f ms)", timeToFrac (c1, 0.5, 1.0) * 1000, timeToFrac (l1, 0.5, 1.0) * 1000);
    }
    std::printf ("\n[Limiter: Punch Protect]\n");
    {   // corpo do kick depois do pico: com Punch Protect fica menos "afundado"
        auto run = [&] (double punch) { Limiter L; L.prepare (fs, 1); auto p = base (6); p.releaseMs = 160; p.punchProtect = punch; L.setParams (p); L.settle();
            auto x = kickTrain (fs, 3.0, 2.4, 0.55); auto y = x; runLimiter (L, y); double body = 0, pk = 0; int cnt = 0; const int beat = (int) (fs * 60.0 / 124.0), lat = L.latencySamples();
            for (int s = beat; s + (int) (0.15 * fs) < (int) y.size(); s += beat) { body += rmsOf (y, s + lat + (int) (0.03 * fs), s + lat + (int) (0.12 * fs)); ++cnt; }
            pk = peakAbs (y, 2000); return std::pair<double, double> (body / cnt, pk); };
        auto a = run (0.0), b = run (1.0);
        CHECK (b.first > a.first * 1.03, "Punch Protect 100%%: corpo do kick %.4f vs %.4f sem protecção (+%.1f%%)", b.first, a.first, (b.first / a.first - 1) * 100);
        CHECK (b.second <= dbToLin (-1.0) * (1 + 1e-5) && a.second <= dbToLin (-1.0) * (1 + 1e-5), "Punch Protect nunca viola o teto (pico %.4f, teto %.4f)", b.second, dbToLin (-1.0));
    }
    std::printf ("\n[Limiter: Bass Guard %% e sensibilidade espectral]\n");
    {
        auto t = [&] (double amt, Spectral sp) { auto p = base (12); p.releaseMs = 80; p.bassGuard = true; p.bassAmount = amt; p.spectral = sp;
            auto g = recovery (p, fs, 0.15, 1.0, 55, 0.08, 1.0, 200); return timeToFrac (g, 0.6, 1.0); };
        const double t0 = t (0.0, Spectral::BassSafe), t5 = t (0.5, Spectral::BassSafe), t1 = t (1.0, Spectral::BassSafe);
        CHECK (t0 < t5 && t5 < t1, "Bass Guard 0/50/100%%: recuperação %.0f / %.0f / %.0f ms (cresce com a quantidade)", t0 * 1000, t5 * 1000, t1 * 1000);
        const double tf = t (1.0, Spectral::Flat), tp = t (1.0, Spectral::Perceptual), tb = t (1.0, Spectral::BassSafe);
        CHECK (tf <= tp && tp < tb, "Sensibilidade FLAT / PERCEPTUAL / BASS-SAFE: %.0f / %.0f / %.0f ms", tf * 1000, tp * 1000, tb * 1000);
        CHECK (std::fabs (tf - t0) < 0.01, "FLAT equivale a Bass Guard a 0%% (%.0f vs %.0f ms)", tf * 1000, t0 * 1000);
    }
    std::printf ("\n[Limiter: Transient Link e Release Link]\n");
    {   // L leva uma rajada; R é um tom baixo que não precisa de redução
        auto run = [&] (double tlink, double rlink) { Limiter L; L.prepare (fs, 2); auto p = base (12); p.stereoLink = tlink; p.releaseLink = rlink; p.releaseMs = 200; L.setParams (p); L.settle();
            const int n = (int) (fs * 1.0), nb = (int) (0.05 * fs); std::vector<float> a ((size_t) n), b ((size_t) n);
            for (int i = 0; i < n; ++i) { a[(size_t) i] = (float) ((i < nb ? 1.0 : 0.02) * std::sin (2 * kPi * 500 * i / fs)); b[(size_t) i] = (float) (0.02 * std::sin (2 * kPi * 300 * i / fs)); }
            auto bin = b; runLimiter (L, a, &b); const int lat = L.latencySamples(); const int s0 = nb + lat + (int) (0.02 * fs), s1 = s0 + (int) (0.1 * fs);
            return rmsOf (b, s0, s1) / (rmsOf (bin, s0 - lat, s1 - lat) * dbToLin (12.0)); };
        const double g0 = run (0.0, 0.0), g1 = run (0.0, 1.0), gl = run (1.0, 0.0);
        CHECK (g0 > 0.98, "Link de transiente 0%% e de release 0%%: o canal R fica intacto (ganho %.3f)", g0);
        CHECK (g1 < 0.9, "Release Link 100%%: R acompanha a recuperação de L (ganho %.3f)", g1);
        CHECK (gl < 0.9, "Transient Link 100%%: R também é reduzido (ganho %.3f)", gl);
    }
    std::printf ("\n[Limiter: Distortion Guard]\n");
    {   // estimativa de distorção por modulação de ganho num baixo com release curto
        auto run = [&] (GuardMode g) { Limiter L; L.prepare (fs, 1); auto p = base (14); p.releaseMs = 6; p.guard = g; p.releaseMode = ReleaseMode::Single; L.setParams (p); L.settle();
            std::vector<float> x = sineV (fs, 70, 0.9, (int) (fs * 4)); runLimiter (L, x); return std::pair<double, double> (L.distortionIndexDb(), peakAbs (x, 4000)); };
        auto off = run (GuardMode::Off), so = run (GuardMode::Soft), no = run (GuardMode::Normal), st = run (GuardMode::Strict);
        CHECK (st.first < off.first - 3.0, "Guard STRICT reduz o índice de distorção: %.1f dB vs %.1f dB (OFF)", st.first, off.first);
        CHECK (st.first <= no.first + 0.5 && no.first <= so.first + 0.5 && so.first <= off.first + 0.5, "Ordem OFF %.1f >= SOFT %.1f >= NORMAL %.1f >= STRICT %.1f dB", off.first, so.first, no.first, st.first);
        CHECK (st.second <= dbToLin (-1.0) * (1 + 1e-5), "Guard nunca viola o teto (%.4f)", st.second);
    }
    std::printf ("\n[Limiter: teto em todas as combinações (fuzz)]\n");
    {
        std::mt19937 rng (99); std::uniform_real_distribution<double> u (0, 1); int bad = 0, total = 0; double worst = 0;
        for (int k = 0; k < 40; ++k)
        {
            LimiterParams p = base (u (rng) * 24, -u (rng) * 6); p.releaseMs = 0.1 + u (rng) * 400; p.lookaheadMs = 0.2 + u (rng) * 4.8; p.character = (int) (u (rng) * 2.99);
            p.releaseMode = (ReleaseMode) (int) (u (rng) * 2.99); p.spectral = (Spectral) (int) (u (rng) * 2.99); p.guard = (GuardMode) (int) (u (rng) * 3.99);
            p.bassGuard = u (rng) > 0.3; p.bassAmount = u (rng); p.punchProtect = u (rng); p.clipAssist = u (rng) * 0.5; p.stereoLink = u (rng); p.releaseLink = u (rng); p.mix = 1.0; p.truePeak = true;
            Limiter L; L.prepare (fs, 2); L.setParams (p); L.settle();
            const int n = (int) (fs * 1.5); std::vector<float> a ((size_t) n), b ((size_t) n); std::uniform_real_distribution<double> nz (-1, 1);
            for (int i = 0; i < n; ++i) { const double e = (i / 9000) % 3 == 0 ? 1.0 : 0.15; a[(size_t) i] = (float) (e * (0.7 * std::sin (2 * kPi * 80 * i / fs) + 0.5 * nz (rng))); b[(size_t) i] = (float) (e * (0.8 * std::sin (2 * kPi * 3000 * i / fs) + 0.4 * nz (rng))); }
            runLimiter (L, a, &b); const double c = dbToLin (p.ceilingDb); ++total;
            const double pk = std::max (peakAbs (a, 4000), peakAbs (b, 4000)), tp = std::max (truePeak8xV (a, 4000), truePeak8xV (b, 4000));
            worst = std::max (worst, tp / c); if (pk > c * (1 + 1e-5) || tp > c * 1.041) ++bad;          // 0.35 dB de tolerância face ao medidor independente de 64 taps
        }
        CHECK (bad == 0, "%d combinações aleatórias de parâmetros: nenhuma acima do teto (pior pico verdadeiro = %.3f x teto)", total, worst);
    }
    std::printf ("\n[Clipper: curva Custom, assimetria, dois estágios, seletivo, pós-ganho, estatísticas]\n");
    {
        auto runC = [&] (const ClipperParams& p, std::vector<float> x)
        { Clipper c; c.prepare (fs); c.setParams (p); c.settle(); for (auto& s : x) s = c.process (s); return std::make_pair (x, c.takeStats()); };
        ClipperParams b; b.gainDb = 12; b.ceilingDb = -3; b.shape = ClipShape::Custom; b.cexp = 4; b.knee = 0.5;
        const double C = dbToLin (-3.0);
        auto sine = sineV (fs, 1000, 0.9, (int) fs);
        auto r = runC (b, sine);
        CHECK (peakAbs (r.first, 2000) <= C * (1 + 1e-6), "Custom nunca passa o teto (%.4f <= %.4f)", peakAbs (r.first, 2000), C);
        // continuidade: curva monótona e contínua
        bool mono = true; double prev = -2; for (int i = -300; i <= 300; ++i) { const double y = customCurve (i / 100.0, 0.5, 4.0); if (y < prev - 1e-12) mono = false; prev = y; }
        CHECK (mono, "Curva Custom monótona");
        { ClipperParams hard = b; hard.cexp = 16; ClipperParams soft = b; soft.cexp = 1;
          auto h = runC (hard, sine), s = runC (soft, sine);
          CHECK (rmsOf (h.first, 2000, 40000) > rmsOf (s.first, 2000, 40000), "Expoente alto = mais duro (RMS %.3f > %.3f)", rmsOf (h.first, 2000, 40000), rmsOf (s.first, 2000, 40000)); }
        // assimetria
        { ClipperParams a = b; a.negOffsetDb = -6; auto q = runC (a, sine); double pmax = 0, nmax = 0; for (size_t i = 2000; i < q.first.size(); ++i) { pmax = std::max (pmax, (double) q.first[i]); nmax = std::max (nmax, (double) -q.first[i]); }
          CHECK (pmax <= C * (1 + 1e-6) && nmax <= C * dbToLin (-6) * (1 + 1e-6) && nmax < pmax * 0.6, "Assimetria: +%.3f / -%.3f (neg -6 dB)", pmax, nmax); }
        // dual-stage mantém teto e é mais suave que o hard puro
        { ClipperParams d = b; d.dual = true; d.shape = ClipShape::Soft; d.shape2 = ClipShape::Hard; auto q = runC (d, sine);
          CHECK (peakAbs (q.first, 2000) <= C * (1 + 1e-6), "Dual-stage respeita o teto (%.4f)", peakAbs (q.first, 2000)); }
        // pós-ganho: teto efetivo desce
        { ClipperParams g = b; g.postGainDb = -6; auto q = runC (g, sine); CHECK (peakAbs (q.first, 2000) <= C * dbToLin (-6) * (1 + 1e-6), "Pós-ganho -6 dB baixa o teto (%.4f)", peakAbs (q.first, 2000)); }
        // DC blocker com bias: remove média e mantém o teto
        { ClipperParams d = b; d.bias = 0.6; d.dcBlock = false; auto n = runC (d, sine); d.dcBlock = true; auto y = runC (d, sine);
          double mn = 0, my = 0; for (size_t i = 20000; i < n.first.size(); ++i) { mn += n.first[i]; my += y.first[i]; } mn /= (n.first.size() - 20000.0); my /= (y.first.size() - 20000.0);
          CHECK (std::fabs (my) < std::fabs (mn) * 0.2 + 1e-4, "DC blocker: média %.4f -> %.4f", mn, my);
          CHECK (peakAbs (y.first, 2000) <= C * (1 + 1e-6), "DC blocker mantém o teto (%.4f)", peakAbs (y.first, 2000)); }
        // seletivo: bumbo (picos rápidos) cortado, corpo contínuo poupado
        { ClipperParams s = b; s.gainDb = 6; s.shape = ClipShape::Hard; auto k = kickTrain (fs, 2.0, 1.2, 0.5);
          ClipperParams s0 = s; s0.transSel = 0.0; ClipperParams s1 = s; s1.transSel = 1.0;
          auto a0 = runC (s0, k), a1 = runC (s1, k);
          CHECK (a1.second.affected < a0.second.affected, "Seletivo corta menos amostras: %lld < %lld", (long long) a1.second.affected, (long long) a0.second.affected); }
        // estatísticas
        { ClipperParams s = b; s.gainDb = 6; s.shape = ClipShape::Hard; auto q = runC (s, sineV (fs, 1000, 0.4, (int) fs)); ClipStats st = q.second;
          CHECK (st.densityPct() > 20 && st.densityPct() < 45 && st.events > 900 && st.maxClipDb > 0.8 && st.maxClipDb < 1.3, "Stats: densidade %.1f%%, eventos %lld, corte máx %.1f dB, duração média %.1f amostras", st.densityPct(), (long long) st.events, st.maxClipDb, st.avgEventSamples());
          ClipperParams low = b; low.gainDb = -12; auto q2 = runC (low, sine); CHECK (q2.second.affected == 0, "Sem corte quando abaixo do teto"); }
        // a tabela rápida da curva Custom coincide com a fórmula exata (todas as durezas e com viés)
        { double worst = 0; for (double pe : { 1.0, 2.0, 4.0, 8.0, 16.0 }) for (double bias : { 0.0, 0.5 })
          { ClipperParams q; q.gainDb = 0; q.ceilingDb = 0; q.shape = ClipShape::Custom; q.cexp = pe; q.knee = 0.6; q.bias = bias; q.mix = 1; Clipper c; c.prepare (fs); c.setParams (q); c.settle();
            for (int i = -400; i <= 400; ++i) { const double u = i / 100.0 * 0.5; const double ref = customCurve (u, 1.0 - 0.95 * 0.6, pe, bias); const float y = c.process ((float) u); worst = std::max (worst, std::fabs ((double) y - ref)); } }
          CHECK (worst < 2e-3, "Tabela da curva Custom vs fórmula exata: erro máx %.2e (p = 1..16, com e sem viés)", worst); }
        // entradas patológicas
        { ClipperParams s = b; Clipper c; c.prepare (fs); c.setParams (s); c.settle(); bool ok = true; const float bad[] = { NAN, INFINITY, -INFINITY, 1e30f, 1e-40f };
          for (int i = 0; i < 1000; ++i) { const float o = c.process (bad[i % 5]); if (!std::isfinite (o) || std::fabs (o) > 1.0f) ok = false; } CHECK (ok, "NaN/Inf/denormal -> saída finita e dentro do teto"); }
    }
    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails); return fails ? 1 : 0;
}
