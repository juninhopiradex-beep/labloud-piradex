// Testes numéricos do núcleo DSP do LABLOUD. Cada teste imprime o valor medido.
#include "DSP/LoudnexDsp.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
using namespace lx::dsp;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("FALHOU  "); std::printf (__VA_ARGS__); std::printf ("   (linha %d)\n", __LINE__); } else { std::printf ("ok      "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// ----- utilitários -----------------------------------------------------------
static std::vector<float> sine (double fs, double f, double amp, double phase, int n)
{ std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * kPi * f * i / fs + phase)); return v; }

// medidor independente de pico verdadeiro: 8x com 64 taps (±32), janela Blackman, diferente do estimador do plugin
static double truePeak8x (const std::vector<float>& x, int from = 0)
{
    const int H = 32; double peak = 0;
    for (double v : x) peak = std::max (peak, std::fabs (v));
    std::vector<double> h[8];
    for (int k = 1; k < 8; ++k)
    {
        const double f = k / 8.0; h[k].resize (2 * H);
        double s = 0;
        for (int j = 0; j < 2 * H; ++j)
        {
            const double t = (j - (H - 1)) - f, u = t / H;
            const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            h[k][(size_t) j] = sinc * (0.42 + 0.5 * std::cos (kPi * u) + 0.08 * std::cos (2 * kPi * u)); s += h[k][(size_t) j];
        }
        for (auto& c : h[k]) c /= s;
    }
    for (int n = from + H; n < (int) x.size() - H - 1; ++n)
        for (int k = 1; k < 8; ++k)
        {
            double y = 0; for (int j = 0; j < 2 * H; ++j) y += h[k][(size_t) j] * x[(size_t) (n - (H - 1) + j)];
            peak = std::max (peak, std::fabs (y));
        }
    return peak;
}
static double peakOf (const std::vector<float>& v, int from = 0) { double p = 0; for (size_t i = (size_t) from; i < v.size(); ++i) p = std::max (p, (double) std::fabs (v[i])); return p; }

// material de teste "tipo mix": kick com queda de pitch, sub, hats em ruído, estalo, tudo muito alto
static std::vector<float> program (double fs, double seconds, unsigned seed = 7)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<double> u (-1, 1);
    const int n = (int) (fs * seconds); std::vector<float> v ((size_t) n); const double beat = 60.0 / 124.0; double hp = 0;
    double nh[5] = { 0, 0, 0, 0, 0 };   // histórico do ruído para limitar a banda dos hats ([1 4 6 4 1]/16: sem energia perto de Nyquist)
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, bt = std::fmod (t, beat), et = std::fmod (t, beat / 2);
        double s = 0.9 * std::sin (2 * kPi * (42 * bt + (120.0 / 38) * (1 - std::exp (-38 * bt)))) * std::exp (-bt * 7.2);
        const double to = std::fmod (t + beat / 2, beat);
        s += 0.55 * std::sin (2 * kPi * 55 * to) * std::exp (-to * 5.2);
        for (int k = 4; k > 0; --k) { nh[k] = nh[k - 1]; }
        nh[0] = u (rng);
        const double nz = (nh[0] + 4 * nh[1] + 6 * nh[2] + 4 * nh[3] + nh[4]) / 16.0 * 3.0; s += 0.30 * (nz - hp) * std::exp (-et * 72); hp = nz;
        s += 0.15 * std::sin (2 * kPi * 659.25 * std::fmod (t, beat / 4)) * std::exp (-std::fmod (t, beat / 4) * 26);
        v[(size_t) i] = (float) (0.7 * s);
    }
    return v;
}
static void runLimiter (Limiter& L, std::vector<float>& l, std::vector<float>* r = nullptr, int block = 512)
{
    for (size_t off = 0; off < l.size(); off += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, l.size() - off);
        float* ch[2] = { l.data() + off, r ? r->data() + off : l.data() + off };
        L.process (ch, r ? 2 : 1, n);
    }
}

// ----- testes ----------------------------------------------------------------
static void testTruePeak()
{
    std::printf ("\n[Estimador de pico verdadeiro]\n");
    { // fs/4 a 45 graus: amostras a ±0.7071, pico verdadeiro = 1.0
        TruePeakEstimator tp; double est = 0, smp = 0;
        for (int i = 0; i < 4000; ++i) { const double x = std::sin (kPi / 2 * i + kPi / 4); smp = std::max (smp, std::fabs (x)); const double p = tp.push (x, true); if (i > 50) est = std::max (est, p); }
        const double errDb = 20 * std::log10 (est / 1.0);
        CHECK (std::fabs (smp - 0.70710678) < 1e-6, "pico de amostra = %.4f (0.7071 esperado)", smp);
        CHECK (std::fabs (errDb) < 0.05, "pico verdadeiro estimado = %.4f (erro %+.3f dB, esperado 1.0000)", est, errDb);
    }
    double worst = 0, worstF = 0; const int nF = 40;
    for (int fi = 0; fi < nF; ++fi)
    {
        const double fr = 0.02 + 0.40 * fi / (nF - 1);    // 0.02..0.42 de fs
        double bestErr = 0;
        for (int ph = 0; ph < 16; ++ph)
        {
            TruePeakEstimator tp; double est = 0;
            for (int i = 0; i < 6000; ++i) { const double p = tp.push (std::sin (2 * kPi * fr * i + ph * kPi / 16), true); if (i > 50) est = std::max (est, p); }
            bestErr = std::max (bestErr, std::fabs (20 * std::log10 (est)));
        }
        if (bestErr > worst) { worst = bestErr; worstF = fr; }
    }
    std::printf ("        pior erro em 40 frequências x 16 fases (0.02..0.42 fs): %.3f dB (a %.3f fs)\n", worst, worstF);
    CHECK (worst < 0.35, "erro máximo do estimador < 0.35 dB até 0.42 fs");
}

static void testContainers()
{
    std::printf ("\n[Contentores]\n");
    std::mt19937 rng (3); std::uniform_real_distribution<double> u (0.1, 1.0);
    SlidingMin sm; sm.init (50); bool ok = true; std::vector<double> h;
    for (int L : { 1, 2, 7, 33, 50 })
    {
        sm.setWindow (L); sm.reset(); h.clear();
        for (int i = 0; i < 4000; ++i)
        {
            const double v = u (rng); h.push_back (v); const double got = sm.push (v);
            double ref = 1e9; for (int k = 0; k < L && k <= i; ++k) ref = std::min (ref, h[(size_t) (i - k)]);
            if (std::fabs (got - ref) > 1e-15) ok = false;
        }
    }
    CHECK (ok, "SlidingMin == mínimo por força bruta (L = 1, 2, 7, 33, 50)");
    Boxcar bc; bc.init (64); ok = true;
    for (int L : { 1, 3, 17, 64 })
    {
        bc.setLength (L, 1.0); h.assign ((size_t) L, 1.0);
        for (int i = 0; i < 80000; ++i)
        {
            const double v = u (rng); h.push_back (v); const double got = bc.process (v);
            double ref = 0; for (int k = 0; k < L; ++k) ref += h[h.size() - 1 - (size_t) k]; ref /= L;
            if (std::fabs (got - ref) > 1e-9) ok = false;
        }
    }
    CHECK (ok, "Boxcar == média por força bruta (inclui recálculo da soma)");
    DelayLine d; d.init (100); std::vector<float> o; for (int i = 0; i < 300; ++i) o.push_back (d.process (i == 5 ? 1.f : 0.f, 37));
    CHECK (o[42] == 1.f && o[41] == 0.f && o[43] == 0.f, "DelayLine atrasa exactamente 37 amostras");
}

static void testClipperShapes()
{
    std::printf ("\n[Formas do Clipper]\n");
    for (ClipShape s : { ClipShape::Soft, ClipShape::Cubic, ClipShape::Hard })
    {
        for (double knee : { 0.0, 0.5, 1.0 })
        {
            bool mono = true, bound = true, odd = true; double prev = -1e9;
            for (double u = -20; u <= 20; u += 0.001)
            {
                const double y = clipCurve (u, s, knee);
                if (y < prev - 1e-12) mono = false;
                prev = y;
                if (std::fabs (y) > 1.0 + 1e-12) bound = false;
                if (std::fabs (y + clipCurve (-u, s, knee)) > 1e-12) odd = false;
            }
            const double slope = (clipCurve (1e-4, s, knee) - clipCurve (-1e-4, s, knee)) / 2e-4;
            CHECK (mono && bound && odd && std::fabs (slope - 1.0) < 1e-6, "forma %d joelho %.1f: monótona, |y|<=1, ímpar, declive 1 na origem", (int) s, knee);
        }
    }
    double dmax = 0; const double k = 0.4;
    for (double u : { k - 1e-6, k + 1e-6 }) dmax = std::max (dmax, std::fabs (softCurve (u, k) - softCurve (k, k)));
    CHECK (dmax < 1e-5, "Soft: contínua no joelho (salto %.2e)", dmax);
    CHECK (clipCurve (3.0, ClipShape::Hard, 0) == 1.0 && clipCurve (-3.0, ClipShape::Hard, 0) == -1.0, "Hard = grampo exacto");
    Clipper c; c.prepare (48000); ClipperParams p; p.gainDb = 24; p.ceilingDb = -3; p.shape = ClipShape::Soft; c.setParams (p); c.settle();
    float m = 0; for (int i = 0; i < 48000; ++i) m = std::max (m, std::fabs (c.process ((float) std::sin (2 * kPi * 440.0 * i / 48000))));
    CHECK (m <= dbToLin (-3) + 1e-6, "Clipper com +24 dB não passa o teto de -3 dB (pico %.4f, teto %.4f)", m, dbToLin (-3));
}

static void testLimiterBasics()
{
    std::printf ("\n[Limitador: brickwall, transparência, latência]\n");
    const double fs = 48000;
    for (int chr = 0; chr < 3; ++chr)
    {
        Limiter L; L.prepare (fs, 2); LimiterParams p; p.gainDb = 12; p.ceilingDb = -1; p.character = chr; p.truePeak = false; p.bassGuard = true; L.setParams (p); L.settle();
        auto x = program (fs, 4.0); const double inPk = peakOf (x) * dbToLin (12);
        runLimiter (L, x);
        const double pk = peakOf (x), ceil = dbToLin (-1);
        CHECK (pk <= ceil * (1 + 1e-6) && L.safetyClampCount() == 0, "carácter %d, +12 dB, teto -1: pico %.5f <= %.5f, grampos de segurança = %lld (entrada pós-ganho %.2f)", chr, pk, ceil, (long long) L.safetyClampCount(), inPk);
    }
    { // transparência: sem redução de ganho o sinal sai igual, só atrasado
        Limiter L; L.prepare (fs, 2); LimiterParams p; p.gainDb = 0; p.ceilingDb = -1; p.clipAssist = 0; L.setParams (p);
        auto x = program (fs, 2.0), ref = x; for (auto& v : ref) v *= 0.25f; x = ref;       // pico ~ -12 dB: sem GR
        auto y = x; runLimiter (L, y); const int lat = L.latencySamples(); double maxd = 0;
        for (size_t i = (size_t) lat + 2000; i < x.size(); ++i) maxd = std::max (maxd, (double) std::fabs (y[i] - x[i - (size_t) lat]));
        CHECK (maxd < 1e-6, "sem GR: saída == entrada atrasada %d amostras (erro máx %.2e)", lat, maxd);
    }
    { // latência declarada
        Limiter L; L.prepare (fs, 1); LimiterParams p; p.ceilingDb = 0; L.setParams (p);
        std::vector<float> x (4000, 0.f); x[1000] = 0.1f; runLimiter (L, x);
        int at = -1; for (int i = 0; i < 4000; ++i) if (std::fabs (x[(size_t) i] - 0.1f) < 1e-6f) at = i;
        CHECK (at == 1000 + L.latencySamples(), "impulso sai em %d (esperado 1000 + %d)", at, L.latencySamples());
    }
    { // seno contínuo muito acima do teto: pico estável = teto
        Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 6; p.ceilingDb = -1; p.truePeak = false; p.character = 2; L.setParams (p);
        auto x = sine (fs, 1000, 1.0, 0.3, 48000); runLimiter (L, x);
        const double pk = peakOf (x, 24000); CHECK (std::fabs (20 * std::log10 (pk) + 1.0) < 0.02, "seno 0 dBFS +6 dB: pico estável %.3f dBFS (teto -1.000)", 20 * std::log10 (pk));
    }
    { // lookahead diferente: continua brickwall e latência acompanha
        Limiter L; L.prepare (fs, 2); LimiterParams p; p.gainDb = 15; p.ceilingDb = -2; p.truePeak = false; p.lookaheadMs = 1.5; L.setParams (p); L.settle();
        bool ok = true; int lastLat = -1;
        for (double la : { 0.2, 0.5, 1.5, 3.0, 5.0 })
        { p.lookaheadMs = la; L.setParams (p); L.reset(); L.settle(); auto x = program (fs, 2.0); runLimiter (L, x); if (peakOf (x) > dbToLin (-2) * (1 + 1e-6)) ok = false; lastLat = L.latencySamples(); }
        CHECK (ok, "lookahead de 0.2 a 5 ms: sempre brickwall (latência final %d amostras = %.2f ms)", lastLat, 1000.0 * lastLat / fs);
    }
}

static void testTruePeakLimiting()
{
    std::printf ("\n[Limitador: pico verdadeiro]\n");
    const double fs = 48000, ceil = dbToLin (-1);
    // fs/4 com fase de 45 graus é o pior caso: pico de amostra 3 dB abaixo do pico verdadeiro
    auto run = [&] (bool tp) { Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 6; p.ceilingDb = -1; p.truePeak = tp; p.character = 0; L.setParams (p); L.settle();
                               auto x = sine (fs, fs / 4, 0.7071, kPi / 4, 96000); runLimiter (L, x); return x; };
    auto off = run (false), on = run (true);
    const double tpOff = 20 * std::log10 (truePeak8x (off, 48000)), tpOn = 20 * std::log10 (truePeak8x (on, 48000));
    std::printf ("        fs/4 @45 graus, +6 dB: pico de amostra TP off = %.2f dBFS, pico verdadeiro TP off = %+.2f dBTP, TP on = %+.2f dBTP\n",
                 20 * std::log10 (peakOf (off, 48000)), tpOff, tpOn);
    CHECK (tpOff > 0.0, "sem pico verdadeiro, o sinal ultrapassa o teto entre amostras (%+.2f dBTP > -1)", tpOff);
    CHECK (tpOn <= -1.0 + 0.25, "com pico verdadeiro, medido por filtro independente de 64 taps: %+.2f dBTP (teto -1.0, tolerância 0.25 dB)", tpOn);
    { // material de programa
        Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 12; p.ceilingDb = -1; p.truePeak = true; p.character = 2; L.setParams (p); L.settle();
        auto x = program (fs, 5.0); runLimiter (L, x); const double t = 20 * std::log10 (truePeak8x (x, 24000));
        CHECK (t <= -1.0 + 0.25, "programa +12 dB, carácter Loud: %+.2f dBTP (teto -1.0, tolerância 0.25 dB)", t);
    }
    { // pior caso documentado (NÃO é um critério de aprovação): ruído branco tem energia até Nyquist, onde qualquer estimador FIR de 8x subestima
        std::mt19937 rng (5); std::uniform_real_distribution<double> u (-0.5, 0.5); std::vector<float> x ((size_t) (fs * 4));
        for (auto& v : x) v = (float) u (rng);
        Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 12; p.ceilingDb = -1; p.truePeak = true; p.character = 2; L.setParams (p); L.settle();
        runLimiter (L, x); std::printf ("        informativo: ruído branco +12 dB: %+.2f dBTP (teto -1.0) - limitação conhecida perto de Nyquist\n", 20 * std::log10 (truePeak8x (x, 24000)));
    }
    (void) ceil;
}

static void testStereoAndRelease()
{
    std::printf ("\n[Stereo link, release, robustez]\n");
    const double fs = 48000;
    for (double link : { 1.0, 0.0 })
    {
        Limiter L; L.prepare (fs, 2); LimiterParams p; p.gainDb = 6; p.ceilingDb = -1; p.truePeak = false; p.stereoLink = link; L.setParams (p);
        auto l = sine (fs, 1000, 1.0, 0, 48000), r = sine (fs, 700, 0.05, 0, 48000), r0 = r;
        runLimiter (L, l, &r); const int lat = L.latencySamples(); double ratioSum = 0; int cnt = 0;
        for (size_t i = 24000; i < 47000; ++i) if (std::fabs (r0[i - (size_t) lat]) > 0.02) { ratioSum += r[i] / (r0[i - (size_t) lat] * dbToLin (6)); ++cnt; }
        const double g = ratioSum / cnt;
        if (link == 1.0) CHECK (g < 0.5, "link 100%%: canal baixo também atenuado (ganho médio %.3f)", g);
        else             CHECK (std::fabs (g - 1.0) < 0.01, "link 0%%: canal baixo intacto (ganho médio %.4f)", g);
    }
    { // release: depois de uma explosão, o ganho recupera com a constante de tempo esperada (carácter Punch, sem bass guard)
        Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 12; p.ceilingDb = -1; p.truePeak = false; p.character = 1; p.bassGuard = false; p.releaseMs = 50; L.setParams (p);
        std::vector<float> x; { auto b = sine (fs, 1000, 1.0, 0, 9600); x = b; auto q = sine (fs, 1000, 0.01, 0, 48000); x.insert (x.end(), q.begin(), q.end()); }
        auto ref = x; runLimiter (L, x); const int lat = L.latencySamples();
        auto gAt = [&] (int ms) { const int c = 9600 + (int) (ms * fs / 1000); double num = 0, den = 0;
            for (int i = c; i < c + 48; ++i) { num += std::fabs (x[(size_t) (i + lat)]); den += std::fabs (ref[(size_t) i]) * dbToLin (12); } return num / den; };
        const double g0 = gAt (0); int tReach = -1; for (int ms = 0; ms < 400; ++ms) if (gAt (ms) > 0.99) { tReach = ms; break; }
        const double tau = 0.05 * 0.55, expect = 1000.0 * tau * std::log ((1 - g0) / 0.01);
        std::printf ("        ganho no fim da explosão %.3f; recupera a 0.99 em %d ms (previsto %.0f ms)\n", g0, tReach, expect);
        CHECK (tReach > 0 && std::fabs (tReach - expect) < 0.35 * expect, "release exponencial dentro de ±35%% do previsto");
    }
    { // valores extremos e NaN
        Limiter L; L.prepare (fs, 2); LimiterParams p; p.gainDb = 24; p.ceilingDb = -1; L.setParams (p);
        std::vector<float> l (8192), r (8192); for (size_t i = 0; i < l.size(); ++i) { l[i] = (i % 97 == 0) ? 1.0e6f : -1.0e6f; r[i] = (i == 100) ? NAN : (i == 200 ? INFINITY : 0.5f); }
        runLimiter (L, l, &r); bool fin = true; double m = 0; for (size_t i = 0; i < l.size(); ++i) { if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) fin = false; m = std::max ({ m, (double) std::fabs (l[i]), (double) std::fabs (r[i]) }); }
        CHECK (fin && m <= dbToLin (-1) * (1 + 1e-5), "entradas ±1e6, NaN e Inf: saída finita e abaixo do teto (pico %.4f)", m);
        L.reset(); std::vector<float> z (48000, 0.f); runLimiter (L, z); bool zero = true; for (float v : z) if (v != 0.f) zero = false;
        CHECK (zero, "silêncio -> silêncio exacto (sem denormais nem ruído)");
    }
    { // mix 0 = só o seco (com ganho), atrasado
        Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 6; p.mix = 0.0; L.setParams (p);
        auto x = program (fs, 1.0), ref = x; runLimiter (L, x); const int lat = L.latencySamples(); double e = 0;
        for (size_t i = 12000; i < x.size(); ++i) e = std::max (e, (double) std::fabs (x[i] - ref[i - (size_t) lat] * (float) dbToLin (6)));
        CHECK (e < 1e-4, "mix 0%%: saída = entrada com ganho, atrasada (erro %.2e; inclui a rampa de 5 ms do ganho)", e);
    }
}

static void testLoudness()
{
    std::printf ("\n[LUFS (K-weighting, BS.1770)]\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m; m.prepare (fs, 1); auto x = sine (fs, 997, 1.0, 0, (int) fs); const float* ch[1] = { x.data() }; m.process (ch, 1, (int) fs);
        CHECK (std::fabs (m.momentaryLufs() + 3.01) < 0.1, "%.0f Hz, seno 997 Hz mono 0 dBFS: %.3f LUFS (esperado -3.01)", fs, m.momentaryLufs());
    }
    { LoudnessMeter m; m.prepare (48000, 2); auto x = sine (48000, 997, 0.1, 0, 48000 * 4), y = x; const float* ch[2] = { x.data(), y.data() }; m.process (ch, 2, 48000 * 4);
      CHECK (std::fabs (m.momentaryLufs() + 20.0) < 0.1 && std::fabs (m.shortTermLufs() + 20.0) < 0.1, "estéreo -20 dBFS: momentâneo %.3f, curto prazo %.3f LUFS (esperado -20.0)", m.momentaryLufs(), m.shortTermLufs()); }
    { LoudnessMeter m; m.prepare (48000, 1); std::vector<float> z (48000, 0.f); const float* ch[1] = { z.data() }; m.process (ch, 1, 48000);
      CHECK (m.momentaryLufs() <= -100.0, "silêncio: %.1f LUFS", m.momentaryLufs()); }
}

static void testGatingAndRange()
{
    std::printf ("\n[LUFS integrado (dupla porta), LRA, Bass Guard, Auto-Dose]\n");
    const double fs = 48000;
    auto feed = [&] (LoudnessMeter& m, double ampDb, double seconds) { auto x = sine (fs, 997, dbToLin (ampDb), 0, (int) (fs * seconds)); const float* ch[1] = { x.data() }; m.process (ch, 1, (int) x.size()); };
    { LoudnessMeter m; m.prepare (fs, 1); feed (m, -23.0 + 3.01, 20.0);   // mono: seno de -19.99 dBFS -> -23 LUFS
      CHECK (std::fabs (m.integratedLufs() + 23.0) < 0.15, "tom constante: integrado %.3f LUFS (esperado -23.0)", m.integratedLufs());
      CHECK (m.loudnessRange() < 0.3, "tom constante: LRA %.2f LU (esperado ~0)", m.loudnessRange()); }
    { LoudnessMeter m; m.prepare (fs, 1); feed (m, -20.0, 10.0); feed (m, -120.0, 10.0); feed (m, -20.0, 10.0);   // silêncio no meio nao conta (porta absoluta)
      LoudnessMeter r; r.prepare (fs, 1); feed (r, -20.0, 20.0);
      CHECK (std::fabs (m.integratedLufs() - r.integratedLufs()) < 0.15, "silêncio ignorado: %.3f vs referência %.3f LUFS", m.integratedLufs(), r.integratedLufs()); }
    { LoudnessMeter m; m.prepare (fs, 1); feed (m, -20.0, 15.0); feed (m, -40.0, 15.0);   // parte baixa 20 LU abaixo: fora da porta relativa de -10 LU
      LoudnessMeter r; r.prepare (fs, 1); feed (r, -20.0, 15.0);
      CHECK (std::fabs (m.integratedLufs() - r.integratedLufs()) < 0.3, "porta relativa -10 LU: %.3f vs só a parte alta %.3f LUFS", m.integratedLufs(), r.integratedLufs()); }
    { LoudnessMeter m; m.prepare (fs, 1); feed (m, -20.0, 20.0); feed (m, -28.0, 20.0);   // 8 LU de diferença, dentro de ambas as portas
      const double lra = m.loudnessRange();
      CHECK (std::fabs (lra - 8.0) < 1.0, "dois patamares a 8 LU: LRA %.2f LU (esperado ~8, transição incluída)", lra); }
    { LoudnessMeter m; m.prepare (fs, 1); feed (m, -20.0, 10.0); const long long a = m.gatedBlockCount(); m.reset(); CHECK (a > 80 && m.gatedBlockCount() == 0 && m.integratedLufs() <= -100.0, "10 s geram %lld blocos de 400 ms (passo 100 ms); reset limpa", a); }
    { TruePeakMeter t; t.reset(); auto x = sine (fs, fs / 4, 1.0, kPi / 4, 4800); const float* ch[1] = { x.data() }; t.process (ch, 1, 4800);
      CHECK (std::fabs (t.maxDbTp()) < 0.3, "fs/4 a 45 graus: pico amostrado -3.0 dB, pico verdadeiro medido %.2f dBTP (esperado 0, tolerância 0.3)", t.maxDbTp()); }
    { // Bass Guard: frequência configurável muda o filtro (mais alta -> mais energia grave detetada -> release mais longo)
      auto run = [&] (double f) { Limiter L; L.prepare (fs, 1); LimiterParams p; p.gainDb = 12; p.bassGuard = true; p.bassFreqHz = f; p.releaseMs = 50; L.setParams (p); L.settle();
          auto x = sine (fs, 200, 0.5, 0, 48000), y = x; float* io[1] = { x.data() }; L.process (io, 1, (int) x.size()); double s = 0; for (float v : x) s += std::fabs (v); (void) y; return s; };
      CHECK (std::isfinite (run (60)) && std::isfinite (run (300)) && std::fabs (run (60) - run (300)) > 1e-3, "Bass Guard 60 Hz e 300 Hz dão resultados diferentes num seno de 200 Hz"); }
    { auto a = suggestDose (-18.0, -3.0, -14.0, -1.0), b = suggestDose (-24.0, -6.0, -8.0, -1.0), c = suggestDose (-12.0, -1.0, -14.0, -1.0);
      CHECK (std::fabs (a.gainDb - 4.0) < 1e-9 && a.reachable && a.expectedGrDb < 3.1, "Auto-Dose: -18 -> -14 LUFS pede +4.0 dB (obtido %.1f), GR esperada %.1f dB", a.gainDb, a.expectedGrDb);
      CHECK (b.gainDb > a.gainDb && b.character >= a.character && b.releaseMs <= a.releaseMs, "Auto-Dose: alvo mais agressivo -> mais ganho, carácter >= e release <=");
      CHECK (c.gainDb == 0.0 && c.reachable, "Auto-Dose: já acima do alvo -> ganho 0"); }
}

int main()
{
    testTruePeak(); testContainers(); testClipperShapes(); testLimiterBasics(); testTruePeakLimiting(); testStereoAndRelease(); testLoudness(); testGatingAndRange();
    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails ? 1 : 0;
}
