#include "PluginProcessor.h"
#include "PluginEditor.h"

KeroMixAIAudioProcessor::KeroMixAIAudioProcessor()
    : AudioProcessor(BusesProperties()
        // Fix #1: Support both mono and stereo input for Logic Pro mono tracks
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withInput ("Mono In", juce::AudioChannelSet::mono(),  false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

KeroMixAIAudioProcessor::~KeroMixAIAudioProcessor() {}

bool KeroMixAIAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Fix #1: Accept mono and stereo (Logic Pro mono track support)
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() &&
        out != juce::AudioChannelSet::mono())
        return false;
    // Input can be mono or stereo, but must match output or input can be mono with stereo out
    auto in = layouts.getMainInputChannelSet();
    return (in == out ||
            (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()));
}

juce::AudioProcessorValueTreeState::ParameterLayout
KeroMixAIAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back(std::make_unique<juce::AudioParameterFloat>("lowG",        "Low Gain",      -18.f,  18.f,    0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("lowFreq",     "Low Freq",       60.f,  600.f,  200.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("midG",        "Mid Gain",      -18.f,  18.f,    0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("midFreq",     "Mid Freq",      300.f, 5000.f, 1000.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("midQ",        "Mid Q",           0.3f,  4.0f,   0.8f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("highG",       "High Gain",     -18.f,  18.f,    0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("highFreq",    "High Freq",    3000.f, 16000.f, 8000.f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("compThresh",  "Threshold",     -40.f,   0.f,  -12.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compRatio",   "Ratio",           1.f,  20.f,    4.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compAttack",  "Attack",          1.f,  100.f,  10.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compRelease", "Release",        20.f,  500.f, 100.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compMakeup",  "Makeup",          0.f,   24.f,   0.f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("delayTime",     "Dly Time",     0.05f, 1.0f, 0.4f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("delayFeedback", "Dly Feedback", 0.00f, 0.9f, 0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("delayMix",      "Dly Mix",      0.00f, 1.0f, 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("revDecay", "Rev Decay", 0.f, 1.f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revSize",  "Rev Size",  0.f, 1.f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revDamp",  "Rev Damp",  0.f, 1.f, 0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revMix",   "Rev Mix",   0.f, 1.f, 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("aimix", "Output", 0.f, 2.f, 1.0f));

    return { p.begin(), p.end() };
}

void KeroMixAIAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    const int numCh = juce::jmin(8, juce::jmax(1, getTotalNumOutputChannels()));

    for (int ch = 0; ch < numCh; ++ch)
    {
        for (int b = 0; b < 3; ++b)
            eqFilters[ch][b].reset();
        compEnv[ch]    = 0.f;
        compGainDb[ch] = 0.f;
    }

    reverbEngine.setSampleRate(sampleRate);

    delayBuffer.setSize(numCh, (int)(sampleRate * 2.1));
    delayBuffer.clear();
    writePos = 0;

    juce::ScopedLock sl(fftLock);
    juce::zeromem(fftFifo, sizeof(fftFifo));
    fftFifoIndex = 0;
    fftDataReady = false;
}

void KeroMixAIAudioProcessor::releaseResources() {}

void KeroMixAIAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() >= 2 && getTotalNumInputChannels() == 1)
        buffer.copyFrom(1, 0, buffer.getReadPointer(0), buffer.getNumSamples());
    grMeterDb.store(0.f);
    levelMeterL.store(-90.f);
    levelMeterR.store(-90.f);
}

void KeroMixAIAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int    numCh   = buffer.getNumChannels();
    const int    numSamp = buffer.getNumSamples();
    const double sr      = getSampleRate();
    if (sr <= 0.0) return;

    if (bypassed.load()) return;

    // ── EQ ──────────────────────────────────────────────────────────────────
    {
        const float lG = juce::Decibels::decibelsToGain((float)*apvts.getRawParameterValue("lowG"));
        const float lF = (float)*apvts.getRawParameterValue("lowFreq");
        const float mG = juce::Decibels::decibelsToGain((float)*apvts.getRawParameterValue("midG"));
        const float mF = (float)*apvts.getRawParameterValue("midFreq");
        const float mQ = (float)*apvts.getRawParameterValue("midQ");
        const float hG = juce::Decibels::decibelsToGain((float)*apvts.getRawParameterValue("highG"));
        const float hF = (float)*apvts.getRawParameterValue("highFreq");

        for (int ch = 0; ch < numCh; ++ch)
        {
            eqFilters[ch][0].setCoefficients(juce::IIRCoefficients::makeLowShelf (sr, lF, 0.71, lG));
            eqFilters[ch][1].setCoefficients(juce::IIRCoefficients::makePeakFilter(sr, mF, mQ,  mG));
            eqFilters[ch][2].setCoefficients(juce::IIRCoefficients::makeHighShelf(sr, hF, 0.71, hG));

            for (int b = 0; b < 3; ++b)
                eqFilters[ch][b].processSamples(buffer.getWritePointer(ch), numSamp);
        }
    }

    // ── Compressor ──────────────────────────────────────────────────────────
    {
        const float threshDb  = (float)*apvts.getRawParameterValue("compThresh");
        const float ratio     = (float)*apvts.getRawParameterValue("compRatio");
        const float attackMs  = (float)*apvts.getRawParameterValue("compAttack");
        const float releaseMs = (float)*apvts.getRawParameterValue("compRelease");
        const float makeupDb  = (float)*apvts.getRawParameterValue("compMakeup");

        const float attackCoef  = std::exp(-1.f / (float)(sr * attackMs  * 0.001));
        const float releaseCoef = std::exp(-1.f / (float)(sr * releaseMs * 0.001));
        const float makeup      = juce::Decibels::decibelsToGain(makeupDb);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamp; ++i)
            {
                const float inDb         = juce::Decibels::gainToDecibels(std::abs(data[i]) + 1e-9f);
                const float overDb       = inDb - threshDb;
                const float targetGainDb = overDb > 0.f ? -(overDb * (1.f - 1.f / ratio)) : 0.f;

                if (targetGainDb < compGainDb[ch])
                    compGainDb[ch] = attackCoef  * compGainDb[ch] + (1.f - attackCoef)  * targetGainDb;
                else
                    compGainDb[ch] = releaseCoef * compGainDb[ch] + (1.f - releaseCoef) * targetGainDb;

                data[i] *= juce::Decibels::decibelsToGain(compGainDb[ch]) * makeup;
            }
        }
        grMeterDb.store(compGainDb[0]);
    }

    // ── Delay ───────────────────────────────────────────────────────────────
    {
        const float dMix = (float)*apvts.getRawParameterValue("delayMix");
        if (dMix > 0.001f)
        {
            const float dT  = (float)*apvts.getRawParameterValue("delayTime");
            const float dFb = (float)*apvts.getRawParameterValue("delayFeedback");
            const int   dS  = juce::jmin((int)(sr * dT), delayBuffer.getNumSamples() - 1);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = buffer.getWritePointer(ch);
                auto* dBuf = delayBuffer.getWritePointer(ch);
                int   lPos = writePos;

                for (int i = 0; i < numSamp; ++i)
                {
                    const float dry = data[i];
                    const int   rP  = (lPos - dS + delayBuffer.getNumSamples())
                                      % delayBuffer.getNumSamples();
                    const float wet = dBuf[rP];
                    dBuf[lPos] = dry + wet * dFb;
                    data[i]    = dry + wet * dMix;
                    lPos = (lPos + 1) % delayBuffer.getNumSamples();
                }
                if (ch == 0) writePos = lPos;
            }
        }
    }

    // ── Reverb ──────────────────────────────────────────────────────────────
    {
        const float rMix = (float)*apvts.getRawParameterValue("revMix");
        if (rMix > 0.001f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = juce::jlimit(0.f, 1.f,
                (float)*apvts.getRawParameterValue("revDecay") * 0.85f +
                (float)*apvts.getRawParameterValue("revSize")  * 0.14f);
            rp.damping  = (float)*apvts.getRawParameterValue("revDamp");
            rp.wetLevel = rMix;
            rp.dryLevel = 1.0f;
            rp.width    = 1.0f;
            reverbEngine.setParameters(rp);

            // Fix #1: Always check actual channel count, not assumed stereo
            if (numCh >= 2)
                reverbEngine.processStereo(buffer.getWritePointer(0),
                                           buffer.getWritePointer(1), numSamp);
            else
                reverbEngine.processMono(buffer.getWritePointer(0), numSamp);
        }
    }

    // ── Master ──────────────────────────────────────────────────────────────
    buffer.applyGain((float)*apvts.getRawParameterValue("aimix"));

    // Level meters (post-master)
    {
        float pL = 0.f, pR = 0.f;
        const auto* chL = buffer.getReadPointer(0);
        const auto* chR = (numCh > 1) ? buffer.getReadPointer(1) : chL;
        for (int i = 0; i < numSamp; ++i)
        {
            pL = std::max(pL, std::abs(chL[i]));
            pR = std::max(pR, std::abs(chR[i]));
        }
        levelMeterL.store(juce::Decibels::gainToDecibels(pL + 1e-9f));
        levelMeterR.store(juce::Decibels::gainToDecibels(pR + 1e-9f));
    }

    // ── FFT fifo ────────────────────────────────────────────────────────────
    {
        juce::ScopedLock sl(fftLock);
        const auto* L = buffer.getReadPointer(0);
        const auto* R = (numCh > 1) ? buffer.getReadPointer(1) : L;
        for (int i = 0; i < numSamp; ++i)
        {
            fftFifo[fftFifoIndex++] = (L[i] + R[i]) * 0.5f;
            if (fftFifoIndex >= FFT_SIZE)
            {
                fftDataReady = true;
                fftFifoIndex = 0;
            }
        }
    }
}

// ── State ────────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void KeroMixAIAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ── Patch ────────────────────────────────────────────────────────────────────
juce::File KeroMixAIAudioProcessor::getPatchDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("KeroMixAI")
                   .getChildFile("Patches");
    dir.createDirectory();
    return dir;
}

void KeroMixAIAudioProcessor::savePatch(const juce::String& name)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->setAttribute("patchName", name);
    xml->writeToFile(getPatchDirectory().getChildFile(name + ".xml"), {});
}

juce::StringArray KeroMixAIAudioProcessor::getSavedPatchNames()
{
    juce::StringArray names;
    for (auto& f : getPatchDirectory().findChildFiles(
             juce::File::findFiles, false, "*.xml"))
        names.add(f.getFileNameWithoutExtension());
    names.sort(true);
    return names;
}

bool KeroMixAIAudioProcessor::loadPatch(const juce::String& name)
{
    auto file = getPatchDirectory().getChildFile(name + ".xml");
    if (!file.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse(file);
    if (!xml) return false;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    return true;
}

bool KeroMixAIAudioProcessor::deletePatch(const juce::String& name)
{
    return getPatchDirectory().getChildFile(name + ".xml").deleteFile();
}

// ── Lock State (Fix #4) ───────────────────────────────────────────────────────
void KeroMixAIAudioProcessor::saveLockState(const bool locked[5])
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("KeroMixAI");
    dir.createDirectory();
    juce::String s;
    for (int i = 0; i < 5; ++i) s += (locked[i] ? "1" : "0");
    dir.getChildFile("lockstate.txt").replaceWithText(s);
}

void KeroMixAIAudioProcessor::loadLockState(bool locked[5])
{
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("KeroMixAI").getChildFile("lockstate.txt");
    if (!f.existsAsFile()) { for (int i = 0; i < 5; ++i) locked[i] = false; return; }
    auto s = f.loadFileAsString().trim();
    for (int i = 0; i < 5 && i < s.length(); ++i)
        locked[i] = (s[i] == '1');
}

// ── Editor / Factory ─────────────────────────────────────────────────────────
void KeroMixAIAudioProcessor::saveWindowSize(int w, int h)
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("KeroMixAI");
    dir.createDirectory();
    dir.getChildFile("windowsize.txt").replaceWithText(juce::String(w) + "," + juce::String(h));
}

void KeroMixAIAudioProcessor::loadWindowSize(int& w, int& h)
{
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("KeroMixAI").getChildFile("windowsize.txt");
    if (!f.existsAsFile()) { w = 900; h = 540; return; }
    auto tok = juce::StringArray::fromTokens(f.loadFileAsString().trim(), ",", "");
    w = tok.size() >= 1 ? tok[0].getIntValue() : 900;
    h = tok.size() >= 2 ? tok[1].getIntValue() : 540;
    w = juce::jlimit(700, 1400, w);
    h = juce::jlimit(440, 860,  h);
}

juce::AudioProcessorEditor* KeroMixAIAudioProcessor::createEditor()
{
    return new KeroMixAIAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeroMixAIAudioProcessor();
}