#pragma once
#include <JuceHeader.h>

class KeroMixAIAudioProcessor : public juce::AudioProcessor
{
public:
    KeroMixAIAudioProcessor();
    ~KeroMixAIAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KeroMixAI"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void savePatch(const juce::String& name);
    juce::StringArray getSavedPatchNames();
    bool loadPatch(const juce::String& name);
    bool deletePatch(const juce::String& name);
    juce::File getPatchDirectory();

    void saveLockState(const bool locked[5]);
    void loadLockState(bool locked[5]);

    void saveWindowSize(int w, int h);
    void loadWindowSize(int& w, int& h);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> bypassed{ false };

    static const int FFT_SIZE = 2048;
    float fftFifo[FFT_SIZE] = {};
    int   fftFifoIndex = 0;
    bool  fftDataReady = false;
    juce::CriticalSection fftLock;

    // Metering (written in processBlock, read in editor timer)
    std::atomic<float> grMeterDb{ 0.f };
    std::atomic<float> levelMeterL{ -90.f };
    std::atomic<float> levelMeterR{ -90.f };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::IIRFilter eqFilters[8][3];
    float           compEnv[8]    = {};
    float           compGainDb[8] = {};

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;

    juce::Reverb reverbEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeroMixAIAudioProcessor)
};
