#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PitchEngine.h"

namespace ids
{
    inline const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline const juce::StringArray scaleNames { "Cromatica", "Mayor", "Menor natural", "Menor armonica",
                                                "Pentatonica mayor", "Pentatonica menor", "Dorica",
                                                "Mixolidia", "Blues" };
}

class UltimateVocalProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    UltimateVocalProcessor();
    ~UltimateVocalProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ultimate Vocal"; }
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

    juce::AudioProcessorValueTreeState apvts;

    // ---- datos para la interfaz (escritos en el hilo de audio) ---------------
    static constexpr int kTraceSize = 2048;
    std::array<std::atomic<float>, kTraceSize> traceIn, traceOut, traceTarget;   // MIDI fraccionario, NaN = sin voz
    std::atomic<uint64_t> traceWrite { 0 };

    std::atomic<float> meterIn { 0.0f }, meterOut { 0.0f }, meterGR { 0.0f };
    std::atomic<float> liveMidiIn { 0.0f }, liveMidiOut { 0.0f }, liveMidiTarget { 0.0f };
    std::atomic<bool>  liveVoiced { false };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyScaleToNotes (int key, int scale);

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;

    uv::PitchEngine engine;
    uv::EngineSettings engineSettings;

    juce::dsp::IIR::Filter<float> hpf[2];
    juce::dsp::NoiseGate<float> gate;
    juce::dsp::Compressor<float> comp;
    juce::SmoothedValue<float> mixSmooth, inGainSmooth, outGainSmooth;
    juce::AudioBuffer<float> wetBuffer, dryBuffer;
    float lastHpf = -1.0f;
    double currentSampleRate = 48000.0;
    uint64_t lastFrameSeen = 0;
    int lastAppliedKey = -1, lastAppliedScale = -1;

    std::atomic<float>* pKey, *pScale, *pRetune, *pAmount, *pTranspose, *pFine, *pFormant, *pMix,
                       *pInGain, *pOutGain, *pHpf, *pGate, *pCompThr, *pCompRatio, *pBypass;
    std::array<std::atomic<float>*, 12> pNote {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltimateVocalProcessor)
};
