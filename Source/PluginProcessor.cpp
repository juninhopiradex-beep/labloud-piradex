#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

using namespace lx::dsp;

namespace
{
    constexpr double kSafetyLookaheadMs = 0.5;
    constexpr double kMaxLookaheadMs = 5.0;
        inline float toDbf (double lin) { return (float) std::max (-120.0, linToDb (lin)); }
    inline int osIdxToStage (int idx) { static const int s[4] = { 0, 2, 4, 5 }; return s[idx < 0 ? 0 : (idx > 3 ? 3 : idx)]; }   // 1x,4x,16x,32x
}

juce::AudioProcessorValueTreeState::ParameterLayout LoudnexProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto F = [&] (const char* id, const char* name, float lo, float hi, float step, float def, const char* unit, float skew = 1.f, int ver = 2)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, ver }, name, NormalisableRange<float> (lo, hi, step, skew), def, AudioParameterFloatAttributes().withLabel (unit))); };
    auto C = [&] (const char* id, const char* name, StringArray s, int def, int ver = 2) { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, ver }, name, s, def)); };
    auto B = [&] (const char* id, const char* name, bool def, int ver = 2) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, ver }, name, def)); };

    C (ids::mode, "Modo", { "Limiter", "Clipper", "Hybrid" }, 0, 1);
    F (ids::gain, "Ganho", 0.f, 24.f, 0.01f, 0.f, "dB", 1.f, 1);
    F (ids::ceiling, "Teto", -12.f, 0.f, 0.01f, -1.f, "dB", 1.f, 1);
    F (ids::mix, "Mix", 0.f, 100.f, 0.1f, 100.f, "%", 1.f, 1);
    F (ids::release, "Release", 0.1f, 1000.f, 0.01f, 60.f, "ms", 0.3f, 1);
    F (ids::lookahead, "Lookahead", 0.2f, (float) kMaxLookaheadMs, 0.01f, 1.5f, "ms", 1.f, 1);
    F (ids::assist, "Clip Assist", 0.f, 100.f, 0.1f, 0.f, "%", 1.f, 1);
    C (ids::character, "Carater", { "Clean", "Punch", "Loud" }, 0, 1);
    F (ids::link, "Stereo Link", 0.f, 100.f, 0.1f, 100.f, "%", 1.f, 1);
    B (ids::bass, "Bass Guard", true, 1);
    B (ids::tp, "True Peak", true, 1);
    C (ids::shape, "Forma", { "Soft", "Cubic", "Hard", "Custom" }, 0, 1);
    F (ids::knee, "Joelho", 0.f, 100.f, 0.1f, 55.f, "%", 1.f, 1);
    C (ids::os, "Oversampling", { "1x", "4x", "16x", "32x" }, 1, 1);
    F (ids::bassfreq, "Bass Guard Hz", 40.f, 400.f, 1.f, 120.f, "Hz", 0.5f, 1);
    C (ids::target, "Alvo", { "Off", "-23 LUFS (EBU R128)", "-16 LUFS (Apple / podcast)", "-14 LUFS (Streaming)", "-9 LUFS (Clube)", "-7 LUFS (Loud)" }, 3, 1);
    B (ids::gainmatch, "Gain Match", false, 1);
    // limiter
    C (ids::relmode, "Release Mode", { "Single", "Dual", "Adaptive" }, 0);
    F (ids::punch, "Punch Protect", 0.f, 100.f, 0.1f, 0.f, "%");
    F (ids::bassamt, "Bass Guard Amount", 0.f, 100.f, 0.1f, 100.f, "%");
    C (ids::spectral, "Spectral Sensitivity", { "Flat", "Perceptual", "Bass-safe" }, 2);
    F (ids::rlink, "Release Link", 0.f, 100.f, 0.1f, 0.f, "%");
    C (ids::distguard, "Distortion Guard", { "Off", "Soft", "Normal", "Strict" }, 0);
    // clipper
    F (ids::postgain, "Post Gain", -12.f, 0.f, 0.01f, 0.f, "dB");
    F (ids::clippos, "Clip +", -12.f, 0.f, 0.01f, 0.f, "dB");
    F (ids::clipneg, "Clip -", -12.f, 0.f, 0.01f, 0.f, "dB");
    B (ids::dcblock, "DC Blocker", false);
    F (ids::cexp, "Curve Hardness", 1.f, 16.f, 0.01f, 4.f, "");
    F (ids::bias, "Harmonic Bias", 0.f, 100.f, 0.1f, 0.f, "%");
    B (ids::dual, "Dual-Stage", false);
    F (ids::dualoff, "Dual Offset", 0.f, 6.f, 0.01f, 2.f, "dB");
    F (ids::transel, "Transient Select", 0.f, 100.f, 0.1f, 0.f, "%");
    C (ids::hchar, "Harmonic Character", { "Manual", "Clean", "Warm", "Punch", "Aggressive" }, 0);
    B (ids::mb, "Multiband", false);
    F (ids::xo1, "Crossover 1", 40.f, 400.f, 1.f, 150.f, "Hz", 0.5f);
    F (ids::xo2, "Crossover 2", 1000.f, 8000.f, 1.f, 2500.f, "Hz", 0.5f);
    F (ids::mbg1, "Low Band Gain", -6.f, 6.f, 0.01f, 0.f, "dB");
    F (ids::mbg2, "Mid Band Gain", -6.f, 6.f, 0.01f, 0.f, "dB");
    F (ids::mbg3, "High Band Gain", -6.f, 6.f, 0.01f, 0.f, "dB");
    C (ids::alias, "Aliasing Guard", { "Manual", "ECO", "AUTO", "MAX" }, 0);
    // hybrid / sistema
    C (ids::route, "Hybrid Route", { "Clip>Limit", "Limit>Clip" }, 0);
    F (ids::ratio, "Clip/Limit", 0.f, 100.f, 0.1f, 50.f, "%");
    F (ids::balance, "Balance", 0.f, 100.f, 0.1f, 50.f, "%");
    B (ids::ceilock, "Ceiling Lock", false);
    B (ids::delta, "Delta", false);
    C (ids::deltasel, "Delta Source", { "Total", "Clipper", "Limiter" }, 0);
    B (ids::ms, "Mid/Side", false);
    B (ids::hq, "HQ Offline", true);
    return l;
}

LoudnexProcessor::LoudnexProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      undoManager (30000, 30),
      apvts (*this, &undoManager, "LABLOUD", createLayout())
{
    expiryTimer.startTimer (30 * 1000);
}

LoudnexProcessor::~LoudnexProcessor() { expiryTimer.stopTimer(); }

bool LoudnexProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

void LoudnexProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate; blockSize = std::max (1, samplesPerBlock);
    const int nCh = std::max (1, std::min (getTotalNumInputChannels(), kMaxCh));

    limiter.prepare (sr, nCh, kMaxLookaheadMs);
    safety.prepare (sr, nCh, kSafetyLookaheadMs);
    safetyMs.prepare (sr, nCh, kSafetyLookaheadMs);
    for (auto* s : { &safety, &safetyMs })
    {   // limitador de segurança: não amplifica, só apanha os picos entre amostras
        LimiterParams p; p.gainDb = 0; p.releaseMs = 40; p.lookaheadMs = kSafetyLookaheadMs; p.character = 0; p.bassGuard = false; p.stereoLink = 1;
        s->setParams (p); s->settle();
    }
    clipSw.prepare (sr, nCh, 2);
    aliasGuard.reset();

    // latência CONSTANTE: o caminho mais longo (Limit>Clip + segurança + segurança M/S) define o valor para todos os modos
    targetLatency = limiter.maxLatencySamples() + ClipPath::kLatency + 2 * safety.latencySamples();
    setLatencySamples (targetLatency);

    for (int c = 0; c < kMaxCh; ++c)
        for (auto* d : { &pad[(size_t) c], &padSafety[(size_t) c], &padSafetyMs[(size_t) c], &dryPad[(size_t) c], &tapDlyClip[(size_t) c], &tapDlyLim[(size_t) c], &padTap[(size_t) c] })
            d->init (targetLatency + 8);
    loudness.prepare (sr, nCh); loudnessIn.prepare (sr, nCh); doseMeter.prepare (sr, nCh); tpOut.reset(); tpDose.reset();
    trimDbSm = 0.0; grMaxHold = 0.0; doseSeconds = 0.0; lastClipGainLin = 1.0; lastDensityPct = 0.0; lastHf = 0.0;
    scratch.setSize (nCh, blockSize, false, false, true);
    stageIn.setSize (nCh, blockSize, false, false, true); dryAl.setSize (nCh, blockSize, false, false, true); tapBuf.setSize (nCh, blockSize, false, false, true);
    capBuf.assign ((size_t) (9.0 * sr), 0.f); capPos = 0; capReady.store (false);
    lastMode = -1; lastRoute = -1;
}

void LoudnexProcessor::applyDelay (juce::AudioBuffer<float>& b, int n, int samples)
{
    samples = std::max (0, samples);
    for (int c = 0; c < std::min (b.getNumChannels(), kMaxCh); ++c)
    {
        float* p = b.getWritePointer (c);
        for (int i = 0; i < n; ++i) p[i] = pad[(size_t) c].process (p[i], samples);
    }
}

LimiterParams LoudnexProcessor::limParams (double gainDb, double ceilingDb) const
{
    LimiterParams p;
    p.gainDb = gainDb; p.ceilingDb = ceilingDb; p.releaseMs = rawf (ids::release); p.lookaheadMs = rawf (ids::lookahead);
    p.clipAssist = rawf (ids::assist) * 0.01; p.mix = rawf (ids::mix) * 0.01; p.stereoLink = rawf (ids::link) * 0.01; p.releaseLink = rawf (ids::rlink) * 0.01;
    p.character = juce::roundToInt (rawf (ids::character)); p.bassGuard = rawf (ids::bass) > 0.5f; p.bassAmount = rawf (ids::bassamt) * 0.01; p.bassFreqHz = rawf (ids::bassfreq);
    p.spectral = (Spectral) juce::jlimit (0, 2, juce::roundToInt (rawf (ids::spectral))); p.releaseMode = (ReleaseMode) juce::jlimit (0, 2, juce::roundToInt (rawf (ids::relmode)));
    p.punchProtect = rawf (ids::punch) * 0.01; p.guard = (GuardMode) juce::jlimit (0, 3, juce::roundToInt (rawf (ids::distguard))); p.truePeak = rawf (ids::tp) > 0.5f;
    return p;
}

ClipperParams LoudnexProcessor::clipParams (double gainDb, double ceilingDb) const
{
    ClipperParams cp;
    cp.gainDb = gainDb; cp.ceilingDb = ceilingDb; cp.knee = rawf (ids::knee) * 0.01; cp.mix = rawf (ids::mix) * 0.01;
    cp.shape = (ClipShape) juce::jlimit (0, 3, juce::roundToInt (rawf (ids::shape))); cp.cexp = rawf (ids::cexp); cp.bias = rawf (ids::bias) * 0.01;
    cp.posOffsetDb = rawf (ids::clippos); cp.negOffsetDb = rawf (ids::clipneg); cp.dual = rawf (ids::dual) > 0.5f; cp.dualOffsetDb = rawf (ids::dualoff);
    cp.transSel = rawf (ids::transel) * 0.01; cp.postGainDb = rawf (ids::postgain); cp.dcBlock = rawf (ids::dcblock) > 0.5f;
    const int hc = juce::roundToInt (rawf (ids::hchar));
    if (hc > 0) cp = applyCharacter (cp, (HarmChar) juce::jlimit (0, 3, hc - 1));
    return cp;
}

MultibandParams LoudnexProcessor::mbParams() const
{
    MultibandParams m; m.xo1 = rawf (ids::xo1); m.xo2 = rawf (ids::xo2);
    m.bandGainDb[0] = rawf (ids::mbg1); m.bandGainDb[1] = rawf (ids::mbg2); m.bandGainDb[2] = rawf (ids::mbg3);
    return m;
}

void LoudnexProcessor::processSlice (juce::AudioBuffer<float>& buf, int n)
{
    const int nCh = std::min (buf.getNumChannels(), kMaxCh);
    float* const* w = buf.getArrayOfWritePointers();
    const int mode = juce::jlimit (0, 2, juce::roundToInt (rawf (ids::mode)));
    const int route = juce::roundToInt (rawf (ids::route)) != 0 ? 1 : 0;
    const double G = rawf (ids::gain), C = rawf (ids::ceiling), ratio = rawf (ids::ratio) * 0.01;
    const bool tpOn = rawf (ids::tp) > 0.5f, msOn = rawf (ids::ms) > 0.5f && nCh == 2, deltaOn = rawf (ids::delta) > 0.5f, mbOn = rawf (ids::mb) > 0.5f;
    const int dsel = juce::jlimit (0, 2, juce::roundToInt (rawf (ids::deltasel)));
    int lat = 0, tapLat = 0; bool tapValid = false, clipRan = false; double grMin = 1.0;

    // entrada alinhada ao instante da saída (Delta total e mix do Hybrid)
    for (int c = 0; c < nCh; ++c) { float* d = dryAl.getWritePointer (c); for (int i = 0; i < n; ++i) d[i] = dryPad[(size_t) c].process (w[c][i], targetLatency); }

    // estimativa de conteúdo agudo da entrada (para o Aliasing Guard): sqrt(soma d² / soma x²)
    { double sx = 0, sd = 0; const float* x = w[0]; for (int i = 1; i < n; ++i) { sx += (double) x[i] * x[i]; const double d = (double) x[i] - x[i - 1]; sd += d * d; }
      lastHf = sx > 1e-12 ? clampd (std::sqrt (sd / sx), 0.0, 1.0) : 0.0; }

    if (msOn) for (int i = 0; i < n; ++i) { const float l = w[0][i], r = w[1][i]; w[0][i] = 0.5f * (l + r); w[1][i] = 0.5f * (l - r); }   // M=(L+R)/2, S=(L-R)/2: mono mantém o nível

    // estágio de oversampling do clipper
    int wantStage;
    {
        const int am = juce::jlimit (0, 3, juce::roundToInt (rawf (ids::alias)));
        if (isNonRealtime() && rawf (ids::hq) > 0.5f) wantStage = 5;
        else if (am == 0) { wantStage = osIdxToStage (juce::roundToInt (rawf (ids::os))); aliasGuard.reset(); }
        else { aliasGuard.holdBlocks = std::max (4, (int) (0.8 * sr / std::max (1, blockSize))); wantStage = aliasGuard.update (lastDensityPct, lastHf, (AliasMode) (am - 1)); }
    }
    if (freshStart) { limiter.setParams (limParams (G, C)); limiter.reset(); limiter.settle(); safety.reset(); safetyMs.reset(); clipSw.restart (wantStage); for (auto& d : pad) d.reset(); for (auto& d : padSafety) d.reset(); for (auto& d : padSafetyMs) d.reset(); for (auto& d : tapDlyClip) d.reset(); for (auto& d : tapDlyLim) d.reset(); for (auto& d : padTap) d.reset(); lastClipGainLin = 1.0; }

    auto clipStage = [&] (double gainDb, double ceilDb)
    {
        ClipperParams cp = clipParams (gainDb, ceilDb); if (mode == 2) cp.mix = 1.0;
        clipSw.setParams (cp, mbParams(), mbOn); clipSw.requestStage (wantStage);
        if (freshStart) clipSw.settle();
        const bool tap = deltaOn && dsel == 1;
        if (tap)
        {
            const double g1 = dbToLin (clampd (cp.gainDb, -24.0, 24.0)); const double g0 = lastClipGainLin;
            for (int c = 0; c < nCh; ++c) { float* s = stageIn.getWritePointer (c); for (int i = 0; i < n; ++i) s[i] = (float) (w[c][i] * (g0 + (g1 - g0) * (double) (i + 1) / (double) n)); }
        }
        lastClipGainLin = dbToLin (clampd (cp.gainDb, -24.0, 24.0));
        clipSw.processBlock (w, n);
        if (tap)
        {
            for (int c = 0; c < nCh; ++c) { float* t = tapBuf.getWritePointer (c); const float* s = stageIn.getReadPointer (c); for (int i = 0; i < n; ++i) t[i] = w[c][i] - tapDlyClip[(size_t) c].process (s[i], ClipPath::kLatency); }
            tapValid = true; tapLat = lat + ClipPath::kLatency;
        }
        lat += ClipPath::kLatency; clipRan = true;
    };
    auto limStage = [&] (double gainDb, double ceilDb)
    {
        LimiterParams lp = limParams (gainDb, ceilDb); if (mode == 2) lp.mix = 1.0;
        limiter.setParams (lp);
        const bool tap = deltaOn && dsel == 2;
        if (tap) for (int c = 0; c < nCh; ++c) std::memcpy (stageIn.getWritePointer (c), w[c], sizeof (float) * (size_t) n);
        limiter.process (w, nCh, n);
        const int L = limiter.latencySamples();
        if (tap)
        {
            for (int c = 0; c < nCh; ++c) { float* t = tapBuf.getWritePointer (c); const float* s = stageIn.getReadPointer (c); for (int i = 0; i < n; ++i) t[i] = w[c][i] - tapDlyLim[(size_t) c].process (s[i], L); }
            tapValid = true; tapLat = lat + L;
        }
        lat += L; grMin = std::min (grMin, limiter.takeGainMin());
        meters.clamps.store ((int) std::min<int64_t> (limiter.safetyClampCount(), 1 << 30));
    };
    auto safetyStage = [&] (Limiter& s, std::array<DelayLine, kMaxCh>& padS, bool force = false)
    {
        if (tpOn || force)       // 'force': a barreira corre sempre (pico de amostra se o TP estiver desligado) quando o último estágio é um clipper (o filtro de decimação do oversampler faz overshoot) ou depois do Mid/Side
        {
            LimiterParams p; p.gainDb = 0; p.ceilingDb = C; p.releaseMs = 40; p.lookaheadMs = kSafetyLookaheadMs; p.character = 0; p.bassGuard = false; p.stereoLink = 1.0; p.truePeak = tpOn;
            s.setParams (p); s.process (w, nCh, n); grMin = std::min (grMin, s.takeGainMin());
        }
        else for (int c = 0; c < nCh; ++c) for (int i = 0; i < n; ++i) w[c][i] = padS[(size_t) c].process (w[c][i], s.latencySamples());
        lat += s.latencySamples();
    };

    if (mode == 0)      { limStage (G, C); }
    else if (mode == 1) { clipStage (G, C); safetyStage (safety, padSafety, true); }
    else if (route == 0){ clipStage (G, hybridClipThresholdDb (C, ratio)); limStage (0.0, C); }                      // Clip > Limit
    else                { limStage (G, C + ratio * 3.0); clipStage (0.0, C); safetyStage (safety, padSafety, true); }      // Limit > Clip (+ segurança de pico verdadeiro)

    if (msOn)
    {
        for (int i = 0; i < n; ++i) { const float m = w[0][i], s = w[1][i]; w[0][i] = m + s; w[1][i] = m - s; }
        if (tapValid) for (int i = 0; i < n; ++i) { float* t0 = tapBuf.getWritePointer (0); float* t1 = tapBuf.getWritePointer (1); const float m = t0[i], s = t1[i]; t0[i] = m + s; t1[i] = m - s; }
        safetyStage (safetyMs, padSafetyMs, true);       // M/S pode levantar o pico em L/R: última barreira, em L/R
    }
    applyDelay (buf, n, targetLatency - lat);
    freshStart = false;

    // Hybrid: mix global entre a entrada alinhada e a saída
    if (mode == 2 && ! deltaOn) { const double mx = rawf (ids::mix) * 0.01; if (mx < 0.999) for (int c = 0; c < nCh; ++c) { const float* d = dryAl.getReadPointer (c); for (int i = 0; i < n; ++i) w[c][i] = (float) (d[i] + (w[c][i] - d[i]) * mx); } }

    // Delta: ouvir só o que o processamento acrescenta/retira
    if (deltaOn)
    {
        if (tapValid) for (int c = 0; c < nCh; ++c) { float* t = tapBuf.getWritePointer (c); for (int i = 0; i < n; ++i) w[c][i] = padTap[(size_t) c].process (t[i], std::max (0, targetLatency - tapLat)); }
        else for (int c = 0; c < nCh; ++c) { const float* d = dryAl.getReadPointer (c); for (int i = 0; i < n; ++i) w[c][i] -= d[i]; }
    }

    // medidores desta fatia
    float grDb = toDbf (grMin);
    meters.grDb.store (grDb); grHistory.push (grDb);
    if (clipRan)
    {
        const ClipStats st = clipSw.takeStats(); lastDensityPct = st.densityPct();
        meters.clipDensityPct.store ((float) st.densityPct()); meters.clipMaxDb.store ((float) st.maxClipDb); meters.clipAvgSamples.store ((float) st.avgEventSamples());
        meters.clipEvents.store ((int) std::min<int64_t> (st.events, 1 << 30)); meters.osStage.store (clipSw.stage());
    }
    else { lastDensityPct = 0.0; meters.clipDensityPct.store (0.f); meters.clipMaxDb.store (0.f); }
    if (mode != 1) { meters.grAvgDb.store ((float) limiter.takeGrAvgDb()); meters.distIdxDb.store ((float) limiter.distortionIndexDb()); }
    else { meters.grAvgDb.store ((float) safety.takeGrAvgDb()); meters.distIdxDb.store (-120.f); }
}

void LoudnexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    if (n <= 0 || getTotalNumInputChannels() == 0) return;

    // medidor de entrada (pico, depois do ganho do anfitrião, antes do LABLOUD)
    float inPk = 0.f;
    for (int c = 0; c < buffer.getNumChannels(); ++c) inPk = std::max (inPk, buffer.getMagnitude (c, 0, n));
    meters.inPeakDb.store (toDbf (inPk));
    loudnessIn.process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), n);
    meters.inShortTerm.store ((float) loudnessIn.shortTermLufs());
    if (resetRequested.exchange (false)) { loudness.reset(); loudnessIn.reset(); tpOut.reset(); grMaxHold = 0.0; meters.grMaxDb.store (0.f); }
    if (doseRequested.exchange (false)) { doseMeter.reset(); tpDose.reset(); doseSeconds = 0.0; dose.progress.store (0.f); dose.state.store (1); capPos = 0; capReady.store (false); }
    if (dose.state.load() == 1)
    {
        doseMeter.process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), n); tpDose.process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), n);
        if (loudnessIn.momentaryLufs() > -70.0)
        {
            doseSeconds += (double) n / sr;     // só conta o tempo com sinal
            const int room = (int) capBuf.size() - capPos, take = std::min (room, n);      // captura mono para os assistentes
            for (int i = 0; i < take; ++i) { float s = 0.f; for (int c = 0; c < buffer.getNumChannels(); ++c) s += buffer.getReadPointer (c)[i]; capBuf[(size_t) (capPos + i)] = s / (float) buffer.getNumChannels(); }
            capPos += take;
        }
        dose.progress.store ((float) std::min (1.0, doseSeconds / 8.0));
        if (doseSeconds >= 8.0)
        {
            const double lufs = doseMeter.integratedLufs(), ceilDb = rawf (ids::ceiling), tgt = targetLufsOf (juce::roundToInt (rawf (ids::target)));
            const auto s = lx::dsp::suggestDose (lufs, tpDose.maxDbTp(), tgt == 0.0 ? -14.0 : tgt, ceilDb);
            dose.gainDb.store ((float) s.gainDb); dose.releaseMs.store ((float) s.releaseMs); dose.lookaheadMs.store ((float) s.lookaheadMs); dose.assist.store ((float) s.clipAssist * 100.f);
            dose.expectedGrDb.store ((float) s.expectedGrDb); dose.character.store (s.character); dose.reachable.store (s.reachable); dose.measuredLufs.store ((float) lufs);
            capReady.store (true); dose.state.store (2);
        }
    }

    if (! AccessManager::get().isUnlocked())
    {
        // bloqueado: sem processamento (só o atraso, para o anfitrião não ver a latência mudar)
        applyDelay (buffer, n, targetLatency);
        meters.grDb.store (0.f); lastMode = -1;
        meters.outPeakDb.store (toDbf (buffer.getMagnitude (0, n)));
        return;
    }

    const int mode = juce::roundToInt (rawf (ids::mode)), route = juce::roundToInt (rawf (ids::route));
    if (mode != lastMode || (mode == 2 && route != lastRoute)) { freshStart = true; lastMode = mode; lastRoute = route; }

    // blocos maiores do que o previsto: processar em fatias
    for (int off = 0; off < n; off += blockSize)
    {
        const int m = std::min (blockSize, n - off);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), off, m);
        processSlice (slice, m);
    }

    // Gain Match: ajuste lento (~2,5 s) para a saída ter o loudness de curto prazo da entrada; o trim usa o valor SEM trim para não oscilar
    {
        const bool gm = rawf (ids::gainmatch) > 0.5f && rawf (ids::delta) < 0.5f;
        double target = 0.0;
        if (gm)
        {
            const double inS = loudnessIn.shortTermLufs(), outS = loudness.shortTermLufs() - trimDbSm;
            if (inS > -60.0 && outS > -60.0) target = juce::jlimit (-24.0, 24.0, inS - outS);
        }
        const double a = 1.0 - std::exp (-(double) n / (2.5 * sr));
        const double prev = trimDbSm; trimDbSm += (target - trimDbSm) * a;
        if (std::fabs (trimDbSm) > 1.0e-4 || std::fabs (prev) > 1.0e-4)
            buffer.applyGainRamp (0, n, (float) std::pow (10.0, prev / 20.0), (float) std::pow (10.0, trimDbSm / 20.0));
        meters.trimDb.store ((float) trimDbSm);
    }

    float outPk = 0.f;
    for (int c = 0; c < buffer.getNumChannels(); ++c) outPk = std::max (outPk, buffer.getMagnitude (c, 0, n));
    meters.outPeakDb.store (toDbf (outPk));
    loudness.process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), n);
    meters.momentary.store ((float) loudness.momentaryLufs());
    meters.shortTerm.store ((float) loudness.shortTermLufs());
    meters.integrated.store ((float) loudness.integratedLufs());
    meters.lra.store ((float) loudness.loudnessRange());
    tpOut.process (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), n);
    meters.tpMaxDb.store ((float) tpOut.maxDbTp());
    grMaxHold = std::max (grMaxHold, -(double) meters.grDb.load()); meters.grMaxDb.store ((float) grMaxHold);
    {   // Loudness Efficiency: três números, sem nota única
        const double dIn = loudnessIn.shortTermLufs(), dOut = loudness.shortTermLufs();
        if (dIn > -60.0 && dOut > -60.0) { const Efficiency e = loudnessEfficiency (dIn, dOut, meters.grAvgDb.load(), meters.distIdxDb.load()); meters.effDeltaLufs.store ((float) e.deltaLufs); meters.effLufsPerGr.store ((float) e.lufsPerGrDb); }
    }
    {   // anel de saída (mono) para o monitor
        int h = outHead.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { float s = 0.f; for (int c = 0; c < buffer.getNumChannels(); ++c) s += buffer.getReadPointer (c)[i]; outRing[(size_t) (h++ & 16383)] = s / (float) buffer.getNumChannels(); }
        outHead.store (h, std::memory_order_release);
    }
}

void LoudnexProcessor::snapshotOutput (float* dst, int n) const
{
    n = std::min (n, 16384); const int h = outHead.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dst[i] = outRing[(size_t) ((h - n + i) & 16383)];
}

void LoudnexProcessor::setParam (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void LoudnexProcessor::applyAutoDose()
{
    if (dose.state.load() != 2) return;
    setParam (ids::mode, 0.f); setParam (ids::gain, dose.gainDb.load()); setParam (ids::release, dose.releaseMs.load()); setParam (ids::lookahead, dose.lookaheadMs.load());
    setParam (ids::assist, dose.assist.load()); setParam (ids::character, (float) dose.character.load());
    dose.state.store (0);       // a captura (capReady) fica disponível para os assistentes
}

bool LoudnexProcessor::runAssistants()
{
    if (! capReady.load() || capPos < (int) (2.0 * sr)) return false;
    const int n = capPos; const double ceilDb = rawf (ids::ceiling), totalGain = std::max (dose.gainDb.load(), 0.f);
    assist_.profile = profileMaterial (capBuf.data(), n, sr);
    assist_.clipGainDb = smartClipAmountDb (assist_.profile, ceilDb);
    assist_.order = autoRouting (assist_.profile);
    const Distribution d = smartDistribution (totalGain, assist_.profile);
    assist_.clipShareDb = d.clipDb; assist_.limitShareDb = d.limitDb; assist_.ratio = d.ratio;
    ClipperParams b; b.gainDb = clampd (d.clipDb, 0.0, 12.0); b.ceilingDb = ceilDb; b.mix = 1.0;
    assist_.qo = qualityOptimize (capBuf.data(), std::min (n, (int) (5.0 * sr)), sr, b, 1.0, 2);
    assist_.valid = true; return true;
}

void LoudnexProcessor::applyAssistants()
{
    if (! assist_.valid) return;
    setParam (ids::mode, 2.f); setParam (ids::route, assist_.order == HybridOrder::ClipThenLimit ? 0.f : 1.f); setParam (ids::ratio, (float) (assist_.ratio * 100.0));
    setParam (ids::gain, (float) juce::jlimit (0.0, 24.0, assist_.clipShareDb + assist_.limitShareDb)); setParam (ids::hchar, (float) (assist_.qo.best + 1));
}

void LoudnexProcessor::applyBalance()
{
    const BalanceMap m = mapBalance (rawf (ids::balance) * 0.01);
    setParam (ids::punch, (float) (m.punchProtect * 100.0)); setParam (ids::release, (float) (60.0 * m.releaseScale));
    setParam (ids::ratio, (float) (m.clipRatio * 100.0)); setParam (ids::assist, (float) (m.clipAssist * 100.0));
}

void LoudnexProcessor::storeSlot (int i) { if (i >= 0 && i < 4) slots[(size_t) i] = apvts.copyState(); }

void LoudnexProcessor::recallSlot (int i)
{
    if (i < 0 || i >= 4 || ! slots[(size_t) i].isValid()) return;
    juce::ValueTree t = slots[(size_t) i].createCopy();
    if (rawf (ids::ceilock) > 0.5f)       // Ceiling Lock: o teto atual sobrevive à troca de slot
    {
        const float keep = rawf (ids::ceiling);
        for (int k = 0; k < t.getNumChildren(); ++k) { auto ch = t.getChild (k); if (ch.getProperty ("id").toString() == ids::ceiling) ch.setProperty ("value", keep, nullptr); }
    }
    apvts.replaceState (t);
}

juce::AudioProcessorEditor* LoudnexProcessor::createEditor() { return new LoudnexEditor (*this); }

void LoudnexProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    for (int i = 0; i < 4; ++i) if (slots[(size_t) i].isValid()) state.setProperty ("slot" + juce::String (i), slots[(size_t) i].toXmlString(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}
void LoudnexProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            for (int i = 0; i < 4; ++i)
            {
                const juce::String key = "slot" + juce::String (i);
                if (tree.hasProperty (key)) { if (auto sx = juce::XmlDocument::parse (tree.getProperty (key).toString())) slots[(size_t) i] = juce::ValueTree::fromXml (*sx); tree.removeProperty (key, nullptr); }
            }
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LoudnexProcessor(); }
