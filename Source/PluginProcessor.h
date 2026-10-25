// =============================================================================
//  LABLOUD by Piradex — processador de áudio (JUCE)
//  Três modos: LIMITER (lookahead, pico verdadeiro), CLIPPER (curvas, multibanda, oversampling até 32x) e HYBRID (Clip>Limit ou Limit>Clip).
//  A latência comunicada ao anfitrião é CONSTANTE em todos os modos e estágios de oversampling (compensada com linhas de atraso).
// =============================================================================
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "DSP/ClipPath.h"
#include "Access.h"

namespace ids
{
    inline const char* mode = "mode";           // 0 Limiter, 1 Clipper, 2 Hybrid
    inline const char* gain = "gain";           // dB 0..24
    inline const char* ceiling = "ceiling";     // dB -12..0
    inline const char* mix = "mix";             // %
    inline const char* release = "release";     // ms
    inline const char* lookahead = "lookahead"; // ms
    inline const char* assist = "assist";       // %
    inline const char* character = "character"; // 0 Clean, 1 Punch, 2 Loud
    inline const char* link = "link";           // %
    inline const char* bass = "bass";           // bool
    inline const char* tp = "tp";               // bool
    inline const char* shape = "shape";         // 0 Soft, 1 Cubic, 2 Hard, 3 Custom
    inline const char* knee = "knee";           // %
    inline const char* os = "os";               // 0 1x, 1 4x, 2 16x, 3 32x
    inline const char* bassfreq = "bassfreq";   // Hz 40..400 (frequência do Bass Guard)
    inline const char* target = "target";       // alvo de loudness (só informativo + Auto-Dose)
    inline const char* gainmatch = "gainmatch"; // bool: iguala o loudness da saída ao da entrada
    // ---- limiter (PDF §3/§4) ----
    inline const char* relmode = "relmode";     // 0 Single, 1 Dual, 2 Adaptive
    inline const char* punch = "punch";         // % Punch Protect
    inline const char* bassamt = "bassamt";     // % Bass Guard amount
    inline const char* spectral = "spectral";   // 0 Flat, 1 Perceptual, 2 Bass-safe
    inline const char* rlink = "rlink";         // % Release Link
    inline const char* distguard = "distguard"; // 0 Off, 1 Soft, 2 Normal, 3 Strict
    // ---- clipper (PDF §5/§6) ----
    inline const char* postgain = "postgain";   // dB -12..0
    inline const char* clippos = "clippos";     // dB -12..0 (teto da metade positiva)
    inline const char* clipneg = "clipneg";     // dB -12..0 (teto da metade negativa)
    inline const char* dcblock = "dcblock";     // bool
    inline const char* cexp = "cexp";           // 1..16 dureza da curva Custom
    inline const char* bias = "bias";           // % viés harmónico (Custom)
    inline const char* dual = "dual";           // bool dual-stage
    inline const char* dualoff = "dualoff";     // dB 0..6
    inline const char* transel = "transel";     // % transient-selective
    inline const char* hchar = "hchar";         // 0 Manual, 1 Clean, 2 Warm, 3 Punch, 4 Aggressive
    inline const char* mb = "mb";               // bool multibanda
    inline const char* xo1 = "xo1";             // Hz
    inline const char* xo2 = "xo2";             // Hz
    inline const char* mbg1 = "mbg1";           // dB ganho extra por banda
    inline const char* mbg2 = "mbg2";
    inline const char* mbg3 = "mbg3";
    inline const char* alias = "alias";         // 0 Manual (usa 'os'), 1 ECO, 2 AUTO, 3 MAX
    // ---- hybrid / sistema ----
    inline const char* route = "route";         // 0 Clip>Limit, 1 Limit>Clip
    inline const char* ratio = "ratio";         // % Clip/Limit
    inline const char* balance = "balance";     // % macro Punch..Loud (aplicada por applyBalance())
    inline const char* ceilock = "ceilock";     // bool: assistentes/slots/undo não mexem no teto
    inline const char* delta = "delta";         // bool: ouvir a diferença
    inline const char* deltasel = "deltasel";   // 0 Total, 1 Clipper, 2 Limiter
    inline const char* ms = "ms";               // bool: processar em Mid/Side (com segurança final em L/R)
    inline const char* hq = "hq";               // bool: oversampling máximo em renderização offline
}
inline double targetLufsOf (int idx) { static const double v[6] = { 0.0, -23.0, -16.0, -14.0, -9.0, -7.0 }; return v[idx < 0 ? 0 : (idx > 5 ? 5 : idx)]; }

class LoudnexProcessor : public juce::AudioProcessor
{
public:
    LoudnexProcessor();
    ~LoudnexProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "LABLOUD by Piradex"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;                 // declarado antes do apvts (ordem de construção)
    juce::AudioProcessorValueTreeState apvts;

    // ---- medidores (escritos pela thread de áudio, lidos pelo editor) -----------------------------------
    struct Meters
    {
        std::atomic<float> inPeakDb { -120.f }, outPeakDb { -120.f }, grDb { 0.f }, momentary { -120.f }, shortTerm { -120.f };
        std::atomic<float> integrated { -120.f }, lra { 0.f }, tpMaxDb { -120.f }, grMaxDb { 0.f }, inShortTerm { -120.f }, trimDb { 0.f };
        std::atomic<int> clamps { 0 };
        // Clipper (PDF §5/§7)
        std::atomic<float> clipDensityPct { 0.f }, clipMaxDb { 0.f }, clipAvgSamples { 0.f };
        std::atomic<int>   clipEvents { 0 }, osStage { 0 };
        // Loudness Efficiency (três números, sem nota única)
        std::atomic<float> grAvgDb { 0.f }, distIdxDb { -120.f }, effDeltaLufs { 0.f }, effLufsPerGr { 0.f };
    } meters;

    // histórico de GR (anel lock-free para o editor desenhar): um valor por bloco
    struct GrHistory
    {
        static constexpr int N = 1024;
        std::array<std::atomic<float>, N> v;  std::atomic<int> head { 0 };
        GrHistory() { for (auto& x : v) x.store (0.f); }
        void push (float gr) { const int h = head.load (std::memory_order_relaxed); v[(size_t) (h % N)].store (gr, std::memory_order_relaxed); head.store (h + 1, std::memory_order_release); }
    } grHistory;
    // últimos 16384 samples de saída (mono), para o monitor de espectro/harmónicos; cópia aproximada (sem bloqueio)
    void snapshotOutput (float* dst, int n) const;

    void requestMeasurementReset() { resetRequested.store (true); }

    // Auto-Dose: ouve a ENTRADA durante ~8 s (precisa de áudio acima de -70 LUFS) e calcula uma sugestão (heurística, ver DSP/LoudnexDsp.h)
    struct Dose
    {
        std::atomic<int>   state { 0 };       // 0 inativo, 1 a analisar, 2 pronto
        std::atomic<float> progress { 0.f };
        std::atomic<float> gainDb { 0.f }, releaseMs { 100.f }, lookaheadMs { 2.f }, assist { 0.f }, expectedGrDb { 0.f }, measuredLufs { -120.f };
        std::atomic<int>   character { 0 };
        std::atomic<bool>  reachable { true };
    } dose;
    void startAutoDose() { doseRequested.store (true); }
    void applyAutoDose();      // thread de mensagens: escreve a sugestão nos parâmetros (respeita o Ceiling Lock)

    // Assistentes sobre o mesmo áudio capturado (Smart Clip Amount, Auto Routing, Smart Distribution, Quality Optimizer)
    struct AssistInfo { bool valid = false; lx::dsp::MaterialProfile profile; double clipGainDb = 0, clipShareDb = 0, limitShareDb = 0, ratio = 0.5; lx::dsp::HybridOrder order = lx::dsp::HybridOrder::ClipThenLimit; lx::dsp::QoResult qo; };
    bool runAssistants();              // thread de mensagens (aloca): usa o áudio capturado pelo Auto-Dose; devolve false se ainda não há captura
    void applyAssistants();            // escreve Hybrid + rota + ratio + carácter harmónico nos parâmetros
    const AssistInfo& assistInfo() const { return assist_; }
    void applyBalance();               // macro Punch..Loud → punch, release, ratio, clip assist

    // A/B/C/D + undo (thread de mensagens)
    void storeSlot (int i);
    void recallSlot (int i);           // respeita o Ceiling Lock
    bool slotUsed (int i) const { return i >= 0 && i < 4 && slots[(size_t) i].isValid(); }
    void undo() { undoManager.undo(); }
    void redo() { undoManager.redo(); }

    int  reportedLatency() const { return targetLatency; }
    double currentSampleRate() const { return sr; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processSlice (juce::AudioBuffer<float>&, int n);
    void applyDelay (juce::AudioBuffer<float>&, int n, int samples);
    float rawf (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    lx::dsp::LimiterParams limParams (double gainDb, double ceilingDb) const;
    lx::dsp::ClipperParams clipParams (double gainDb, double ceilingDb) const;
    lx::dsp::MultibandParams mbParams() const;
    void setParam (const char* id, float v);
    void setParamKeepingCeiling (const char* id, float v) { if (std::strcmp (id, ids::ceiling) == 0 && rawf (ids::ceilock) > 0.5f) return; setParam (id, v); }

    struct ExpiryTimer : juce::Timer { void timerCallback() override { AccessManager::get().checkExpiry(); } } expiryTimer;

    static constexpr int kMaxCh = 2;
    double sr = 44100.0;
    int blockSize = 512, targetLatency = 0, lastMode = -1;
    bool freshStart = true;
    lx::dsp::Limiter limiter, safety, safetyMs;
    lx::dsp::ClipSwitcher clipSw;
    lx::dsp::AliasingGuard aliasGuard;
    std::array<lx::dsp::DelayLine, kMaxCh> pad, padSafety, padSafetyMs, dryPad, tapDlyClip, tapDlyLim, padTap;
    lx::dsp::LoudnessMeter loudness, loudnessIn, doseMeter;
    lx::dsp::TruePeakMeter tpOut, tpDose;
    std::atomic<bool> resetRequested { false }, doseRequested { false };
    std::atomic<bool> capReady { false };
    int lastRoute = -1;
    double doseSeconds = 0.0, trimDbSm = 0.0, grMaxHold = 0.0, lastClipGainLin = 1.0, lastDensityPct = 0.0, lastHf = 0.0;
    juce::AudioBuffer<float> scratch, stageIn, dryAl, tapBuf;

    // captura para os assistentes (8 s de entrada com sinal, mono)
    std::vector<float> capBuf; int capPos = 0;
    AssistInfo assist_;
    // anel de saída para o monitor
    std::array<float, 16384> outRing {}; std::atomic<int> outHead { 0 };
    std::array<juce::ValueTree, 4> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnexProcessor)
};
