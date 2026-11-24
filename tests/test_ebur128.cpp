// =============================================================================
//  LABLOUD — validação INDEPENDENTE dos medidores e do teto de pico verdadeiro
//
//  1) LUFS integrado e LRA do medidor do LABLOUD contra a libebur128 (implementação de referência ITU-R BS.1770 /
//     EBU R128, usada por ffmpeg, Audacity, etc.), em vários sinais e taxas de amostragem.
//  2) Teto de pico verdadeiro do limiter (TP ligado, teto -1,0 dBTP) medido por um interpolador IDEAL independente:
//     32x com sinc de 8193 taps (janela de Kaiser, beta 14) — muito mais exato do que os 4x / 49 taps da norma.
//     A libebur128 também é mostrada, mas lê até +0,6 dB a mais em tons perto de fs/4 (limite conhecido do
//     interpolador de 4x da BS.1770); por isso o critério usa o interpolador ideal.
//  Compilar:  g++ -O2 -std=c++20 -ISource -Itests tests/test_ebur128.cpp -lebur128 -o test_ebur128
//  (Ubuntu: sudo apt-get install libebur128-dev)
// =============================================================================
#include "test_util.h"
#include <ebur128.h>
#include <cmath>
#include <string>

static double ebuTruePeakDb (const std::vector<float>& l, const std::vector<float>& r, double fs, int from)
{
    ebur128_state* st = ebur128_init (2, (unsigned long) fs, EBUR128_MODE_TRUE_PEAK);
    std::vector<float> inter; inter.reserve ((l.size() - (size_t) from) * 2);
    for (size_t i = (size_t) from; i < l.size(); ++i) { inter.push_back (l[i]); inter.push_back (r[i]); }
    ebur128_add_frames_float (st, inter.data(), inter.size() / 2);
    double a = 0, b = 0; ebur128_true_peak (st, 0, &a); ebur128_true_peak (st, 1, &b); ebur128_destroy (&st);
    return 20.0 * std::log10 (std::max (a, b) + 1e-12);
}

static void ebuLoudness (const std::vector<float>& l, const std::vector<float>& r, double fs, double& lufs, double& lra)
{
    ebur128_state* st = ebur128_init (2, (unsigned long) fs, EBUR128_MODE_I | EBUR128_MODE_LRA);
    std::vector<float> inter; inter.reserve (l.size() * 2);
    for (size_t i = 0; i < l.size(); ++i) { inter.push_back (l[i]); inter.push_back (r[i]); }
    ebur128_add_frames_float (st, inter.data(), inter.size() / 2);
    ebur128_loudness_global (st, &lufs); ebur128_loudness_range (st, &lra); ebur128_destroy (&st);
}

// interpolador ideal: 32 fases, 256 taps por fase (8193 no total), Kaiser beta 14
static double idealTruePeakDb (const std::vector<float>& x, int from)
{
    constexpr int U = 32, H = 128; const double beta = 14.0;
    auto i0 = [] (double v) { double s = 1, t = 1; for (int k = 1; k < 50; ++k) { t *= (v / (2 * k)) * (v / (2 * k)); s += t; } return s; };
    std::vector<std::vector<double>> h (U, std::vector<double> (2 * H));
    for (int p = 0; p < U; ++p)
    {
        double sum = 0;
        for (int j = 0; j < 2 * H; ++j)
        {
            const double t = (j - (H - 1)) - (double) p / U, u = t / H;
            const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double w = std::fabs (u) <= 1.0 ? i0 (beta * std::sqrt (1 - u * u)) / i0 (beta) : 0.0;
            h[(size_t) p][(size_t) j] = sinc * w; sum += h[(size_t) p][(size_t) j];
        }
        for (auto& c : h[(size_t) p]) c /= sum;
    }
    double peak = 0;
    for (int n = std::max (from, H); n < (int) x.size() - H - 1; ++n)
        for (int p = 0; p < U; ++p)
        {
            double y = 0; const float* xp = x.data() + n - (H - 1);
            for (int j = 0; j < 2 * H; ++j) y += h[(size_t) p][(size_t) j] * xp[j];
            peak = std::max (peak, std::fabs (y));
        }
    return 20.0 * std::log10 (peak + 1e-12);
}

int main()
{
    std::printf ("[1. Medidor de loudness do LABLOUD contra a libebur128]\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        std::mt19937 rng (3); std::normal_distribution<double> nd (0, 1);
        const int n = (int) fs * 20; std::vector<float> l ((size_t) n), r ((size_t) n); double hl = 0, hr = 0;
        for (int i = 0; i < n; ++i)
        {   // ruído rosa aproximado com nível que muda (para o LRA ter algo para medir)
            const double env = i < n / 3 ? 0.05 : (i < 2 * n / 3 ? 0.2 : 0.1);
            hl = 0.98 * hl + 0.02 * nd (rng); hr = 0.98 * hr + 0.02 * nd (rng);
            l[(size_t) i] = (float) (env * (hl * 6 + 0.3 * nd (rng))); r[(size_t) i] = (float) (env * (hr * 6 + 0.3 * nd (rng)));
        }
        LoudnessMeter m; m.prepare (fs, 2);
        for (int off = 0; off < n; off += 512) { const int k = std::min (512, n - off); const float* io[2] = { l.data() + off, r.data() + off }; m.process (io, 2, k); }
        double refI = 0, refL = 0; ebuLoudness (l, r, fs, refI, refL);
        CHECK (std::fabs (m.integratedLufs() - refI) < 0.1, "%5.1f kHz LUFS integrado: LABLOUD %.2f, libebur128 %.2f (diferença %.2f LU)", fs / 1000, m.integratedLufs(), refI, m.integratedLufs() - refI);
        CHECK (std::fabs (m.loudnessRange() - refL) < 0.5, "%5.1f kHz LRA: LABLOUD %.2f LU, libebur128 %.2f LU", fs / 1000, m.loudnessRange(), refL);
    }

    std::printf ("\n[2. Teto de pico verdadeiro do limiter (TP ligado, teto -1,0 dBTP, +6 dB de ganho)]\n");
    double worst = -100;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        struct Sig { const char* name; std::vector<float> l, r; };
        std::vector<Sig> sigs;
        {   const int n = (int) fs * 3; std::vector<float> a ((size_t) n);
            for (int i = 0; i < n; ++i) a[(size_t) i] = (float) (0.9 * std::sin (kPi / 2 * i + kPi / 4));
            sigs.push_back ({ "fs/4 a 45 graus", a, a }); }
        {   std::mt19937 rng (11); std::uniform_real_distribution<double> u (-1, 1);
            const int n = (int) fs * 4; std::vector<float> l ((size_t) n), r ((size_t) n); double hl = 0, hr = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / fs, bt = std::fmod (t, 60.0 / 124.0);
                const double kick = 0.9 * std::sin (2 * kPi * (45 * bt + 2.6 * (1 - std::exp (-35 * bt)))) * std::exp (-bt * 7);
                const double bass = 0.35 * std::sin (2 * kPi * 55 * t), na = u (rng), nb = u (rng);
                hl += 0.3 * (na - hl); hr += 0.3 * (nb - hr);
                l[(size_t) i] = (float) (kick + bass + 0.15 * (na - hl)); r[(size_t) i] = (float) (kick + bass + 0.15 * (nb - hr));
            }
            sigs.push_back ({ "programa estéreo", l, r }); }
        {   const int n = (int) fs * 3; std::vector<float> a ((size_t) n); double ph = 0;
            for (int i = 0; i < n; ++i) { const double f = 20.0 * std::pow (1000.0, (double) i / n); ph += 2 * kPi * f / fs; a[(size_t) i] = (float) (0.95 * std::sin (ph)); }
            sigs.push_back ({ "varrimento 20 Hz-20 kHz", a, a }); }
        for (auto& s : sigs)
        {
            Limiter L; L.prepare (fs, 2); LimiterParams p; p.gainDb = 6; p.ceilingDb = -1; p.truePeak = true; p.character = 2; L.setParams (p); L.settle();
            runLimiter (L, s.l, &s.r);
            const int from = (int) fs;                                   // ignora o primeiro segundo (arranque)
            const double ideal = std::max (idealTruePeakDb (s.l, from), idealTruePeakDb (s.r, from)), ebu = ebuTruePeakDb (s.l, s.r, fs, from);
            worst = std::max (worst, ideal);
            // 0.5: estimador de 128 taps -> tambem o programa com muito ruido perto de Nyquist fica dentro de 0,25 dB
            // (com o estimador antigo de 48 taps passava ~0,3 dB).
            const double tol = 0.25;
            CHECK (ideal <= -1.0 + tol, "%5.1f kHz  %-24s ideal 32x: %+.2f dBTP   (libebur128 4x: %+.2f)   tolerancia %.2f dB", fs / 1000, s.name, ideal, ebu, tol);
        }
    }
    std::printf ("\nPior pico verdadeiro (interpolador ideal): %+.2f dBTP para teto -1,0 (tolerância 0,25 dB)\n", worst);
    CHECK (worst <= -1.0 + 0.15, "pior caso em todo o material: excesso <= 0,15 dB (%.2f dB)", worst + 1.0);

    std::printf ("\n[3. Sanidade da referência]\n");
    {
        const double fs = 48000; const int n = (int) fs * 10; std::vector<float> a ((size_t) n);
        for (int i = 0; i < n; ++i) a[(size_t) i] = (float) (0.1 * std::sin (2 * kPi * 1000 * i / fs));
        double I = 0, R = 0; ebuLoudness (a, a, fs, I, R);
        CHECK (std::fabs (I + 20.0) < 0.05, "seno 1 kHz -20 dBFS estéreo: libebur128 %.2f LUFS (esperado -20,0)", I);
        std::vector<float> b ((size_t) (fs * 2)); for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (0.5 * std::sin (kPi / 2 * (double) i + kPi / 4));
        const double t = idealTruePeakDb (b, 0);
        CHECK (std::fabs (t + 6.0206) < 0.01, "interpolador ideal: seno fs/4 amplitude 0,5 = %.3f dBTP (exato -6,021)", t);
    }
    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails ? 1 : 0;
}
