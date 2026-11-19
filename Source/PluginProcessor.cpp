#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Intervalos de cada escala (semitonos sobre la tónica)
    const std::vector<std::vector<int>> kScales {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },   // cromática
        { 0, 2, 4, 5, 7, 9, 11 },                   // mayor
        { 0, 2, 3, 5, 7, 8, 10 },                   // menor natural
        { 0, 2, 3, 5, 7, 8, 11 },                   // menor armónica
        { 0, 2, 4, 7, 9 },                          // pentatónica mayor
        { 0, 3, 5, 7, 10 },                         // pentatónica menor
        { 0, 2, 3, 5, 7, 9, 10 },                   // dórica
        { 0, 2, 4, 5, 7, 9, 10 },                   // mixolidia
        { 0, 3, 5, 6, 7, 10 }                       // blues
    };
}

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout UltimateVocalProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;

    auto fl = [] (const char* id, const char* name, float lo, float hi, float def, float skew, const char* unit)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                   NormalisableRange<float> (lo, hi, 0.01f, skew), def,
                   AudioParameterFloatAttributes().withLabel (unit));
    };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Tonalidad", ids::noteNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Escala", ids::scaleNames, 0));
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "n" + String (i), 1 },
                                                          "Nota " + ids::noteNames[i], true));

    layout.add (fl ("retune",    "Velocidad de retune", 0.0f, 400.0f, 20.0f, 0.4f, "ms"));
    layout.add (fl ("amount",    "Cantidad",            0.0f, 100.0f, 100.0f, 1.0f, "%"));
    layout.add (fl ("transpose", "Transposicion",     -12.0f, 12.0f, 0.0f, 1.0f, "st"));
    layout.add (fl ("fine",      "Ajuste fino",       -100.0f, 100.0f, 0.0f, 1.0f, "cents"));
    layout.add (fl ("formant",   "Formante",           -6.0f, 6.0f, 0.0f, 1.0f, "st"));
    layout.add (fl ("mix",       "Mezcla",              0.0f, 100.0f, 100.0f, 1.0f, "%"));
    layout.add (fl ("inGain",    "Ganancia entrada",  -24.0f, 24.0f, 0.0f, 1.0f, "dB"));
    layout.add (fl ("outGain",   "Ganancia salida",   -24.0f, 12.0f, 0.0f, 1.0f, "dB"));
    layout.add (fl ("hpf",       "Filtro paso alto",   20.0f, 300.0f, 80.0f, 0.5f, "Hz"));
    layout.add (fl ("gate",      "Puerta de ruido",   -80.0f, -20.0f, -80.0f, 1.0f, "dB"));
    layout.add (fl ("compThr",   "Umbral compresor",  -40.0f, 0.0f, 0.0f, 1.0f, "dB"));
    layout.add (fl ("compRatio", "Ratio compresor",     1.0f, 10.0f, 3.0f, 0.6f, ":1"));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

UltimateVocalProcessor::UltimateVocalProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ULTIMATE_VOCAL", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pKey = get ("key"); pScale = get ("scale"); pRetune = get ("retune"); pAmount = get ("amount");
    pTranspose = get ("transpose"); pFine = get ("fine"); pFormant = get ("formant"); pMix = get ("mix");
    pInGain = get ("inGain"); pOutGain = get ("outGain"); pHpf = get ("hpf"); pGate = get ("gate");
    pCompThr = get ("compThr"); pCompRatio = get ("compRatio"); pBypass = get ("bypass");
    for (int i = 0; i < 12; ++i) pNote[(size_t) i] = get (("n" + juce::String (i)).toRawUTF8());

    for (auto& a : traceIn)     a.store (std::nanf (""));
    for (auto& a : traceOut)    a.store (std::nanf (""));
    for (auto& a : traceTarget) a.store (std::nanf (""));

    lastAppliedKey   = (int) pKey->load();
    lastAppliedScale = (int) pScale->load();
    apvts.addParameterListener ("key", this);
    apvts.addParameterListener ("scale", this);
}

UltimateVocalProcessor::~UltimateVocalProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("key", this);
    apvts.removeParameterListener ("scale", this);
}

// --- escala -> notas habilitadas -------------------------------------------------
void UltimateVocalProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

void UltimateVocalProcessor::handleAsyncUpdate()
{
    const int key = (int) pKey->load(), scale = (int) pScale->load();
    if (key == lastAppliedKey && scale == lastAppliedScale) return;
    applyScaleToNotes (key, scale);
}

void UltimateVocalProcessor::applyScaleToNotes (int key, int scale)
{
    lastAppliedKey = key; lastAppliedScale = scale;
    std::array<bool, 12> mask {};
    for (int iv : kScales[(size_t) juce::jlimit (0, (int) kScales.size() - 1, scale)])
        mask[(size_t) ((key + iv) % 12)] = true;
    for (int i = 0; i < 12; ++i)
        if (auto* p = apvts.getParameter ("n" + juce::String (i)))
            p->setValueNotifyingHost (mask[(size_t) i] ? 1.0f : 0.0f);
}

// --- ciclo de audio ------------------------------------------------------------
bool UltimateVocalProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void UltimateVocalProcessor::prepareToPlay (double sr, int blockSize)
{
    currentSampleRate = sr;
    const int nch = std::max (1, std::min (2, getTotalNumInputChannels()));

    engine.prepare (sr, nch);
    setLatencySamples (engine.getLatencySamples());

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, (juce::uint32) nch };
    for (auto& f : hpf) f.reset();
    lastHpf = -1.0f;

    gate.prepare (spec);
    gate.setRatio (8.0f); gate.setAttack (1.0f); gate.setRelease (120.0f);
    comp.prepare (spec);
    comp.setAttack (8.0f); comp.setRelease (120.0f);

    wetBuffer.setSize (2, blockSize);
    dryBuffer.setSize (2, blockSize);

    mixSmooth.reset (sr, 0.03);     mixSmooth.setCurrentAndTargetValue (*pMix * 0.01f);
    inGainSmooth.reset (sr, 0.03);  inGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) *pInGain));
    outGainSmooth.reset (sr, 0.03); outGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) *pOutGain));
    lastFrameSeen = 0;
}

void UltimateVocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = std::max (1, std::min (2, getTotalNumInputChannels()));
    const int n = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    if (wetBuffer.getNumSamples() < n) { wetBuffer.setSize (2, n, false, false, true); dryBuffer.setSize (2, n, false, false, true); }

    // --- ajustes del motor
    for (int i = 0; i < 12; ++i) engineSettings.noteEnabled[(size_t) i] = pNote[(size_t) i]->load() > 0.5f;
    engineSettings.retuneMs  = *pRetune;
    engineSettings.amount    = *pAmount * 0.01f;
    engineSettings.transpose = *pTranspose;
    engineSettings.fineCents = *pFine;
    engineSettings.formantSt = *pFormant;
    engine.setSettings (engineSettings);

    // --- ganancia de entrada + filtro paso alto
    inGainSmooth.setTargetValue (juce::Decibels::decibelsToGain ((float) *pInGain));
    const float hpfFreq = *pHpf;
    if (hpfFreq != lastHpf)
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, hpfFreq, 0.707f);
        for (auto& f : hpf) f.coefficients = coeffs;
        lastHpf = hpfFreq;
    }

    float peakIn = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSmooth.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            float* d = buffer.getWritePointer (c);
            d[i] = hpf[c].processSample (d[i] * g);
            peakIn = std::max (peakIn, std::abs (d[i]));
        }
    }

    // --- puerta de ruido
    const float gateThr = *pGate;
    if (gateThr > -79.5f)
    {
        gate.setThreshold (gateThr);
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), (size_t) nch, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        gate.process (ctx);
    }

    // --- motor de afinación (muestra a muestra)
    float in[2] = { 0.0f, 0.0f }, wet[2] = { 0.0f, 0.0f }, dry[2] = { 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < nch; ++c) in[c] = buffer.getReadPointer (c)[i];
        engine.processSample (in, wet, dry);
        for (int c = 0; c < nch; ++c) { wetBuffer.setSample (c, i, wet[c]); dryBuffer.setSample (c, i, dry[c]); }
    }

    // --- historial para la interfaz
    const uint64_t frames = engine.getFrameCount();
    for (uint64_t f = std::max<uint64_t> (lastFrameSeen, frames > 64 ? frames - 64 : 0); f < frames; ++f)
    {
        const auto fr = engine.getFrame (f);
        const uint64_t w = traceWrite.load (std::memory_order_relaxed);
        const size_t slot = (size_t) (w % kTraceSize);
        if (fr.voiced)
        {
            traceIn[slot].store (fr.midiIn, std::memory_order_relaxed);
            traceOut[slot].store (fr.midiIn + fr.shiftSt, std::memory_order_relaxed);
            traceTarget[slot].store (fr.midiTarget, std::memory_order_relaxed);
        }
        else
        {
            traceIn[slot].store (std::nanf (""), std::memory_order_relaxed);
            traceOut[slot].store (std::nanf (""), std::memory_order_relaxed);
            traceTarget[slot].store (std::nanf (""), std::memory_order_relaxed);
        }
        traceWrite.store (w + 1, std::memory_order_release);
    }
    lastFrameSeen = frames;
    {
        const auto fr = engine.getLatestFrame();
        liveVoiced.store (fr.voiced);
        if (fr.voiced)
        {
            liveMidiIn.store (fr.midiIn);
            liveMidiOut.store (fr.midiIn + fr.shiftSt);
            liveMidiTarget.store (fr.midiTarget);
        }
    }

    // --- mezcla seco/húmedo (con bypass)
    const bool bypass = *pBypass > 0.5f;
    mixSmooth.setTargetValue (bypass ? 0.0f : *pMix * 0.01f);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixSmooth.getNextValue();
        for (int c = 0; c < nch; ++c)
            buffer.getWritePointer (c)[i] = dryBuffer.getSample (c, i) * (1.0f - m) + wetBuffer.getSample (c, i) * m;
    }

    // --- compresor de salida
    const float compThr = *pCompThr;
    float grDb = 0.0f;
    if (compThr < -0.5f && ! bypass)
    {
        comp.setThreshold (compThr);
        comp.setRatio (*pCompRatio);
        float before = 0.0f, after = 0.0f;
        for (int c = 0; c < nch; ++c) before = std::max (before, buffer.getMagnitude (c, 0, n));
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), (size_t) nch, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        comp.process (ctx);
        for (int c = 0; c < nch; ++c) after = std::max (after, buffer.getMagnitude (c, 0, n));
        if (before > 1.0e-6f && after > 1.0e-6f) grDb = std::max (0.0f, juce::Decibels::gainToDecibels (before) - juce::Decibels::gainToDecibels (after));
    }

    // --- ganancia de salida + medidores
    outGainSmooth.setTargetValue (juce::Decibels::decibelsToGain ((float) *pOutGain));
    float peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSmooth.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            float& s = buffer.getWritePointer (c)[i];
            s *= g;
            peakOut = std::max (peakOut, std::abs (s));
        }
    }
    // el procesamiento es mono-compatible: en entrada mono solo se usa el canal 0

    auto smoothMeter = [] (std::atomic<float>& m, float v)
    {
        const float old = m.load();
        m.store (v > old ? v : old * 0.92f + v * 0.08f);
    };
    smoothMeter (meterIn, peakIn);
    smoothMeter (meterOut, peakOut);
    smoothMeter (meterGR, grDb);
}

// --- estado ---------------------------------------------------------------------
void UltimateVocalProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void UltimateVocalProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // evita que el cambio de tonalidad/escala restaurado pise las notas guardadas
            lastAppliedKey   = (int) pKey->load();
            lastAppliedScale = (int) pScale->load();
        }
}

juce::AudioProcessorEditor* UltimateVocalProcessor::createEditor() { return new UltimateVocalEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UltimateVocalProcessor(); }
