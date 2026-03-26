#include "PluginProcessor.h"
#include "PluginEditor.h"

// -- Colours -------------------------------------------------------------------
static const juce::Colour kBg(0xffF7F9F7);
static const juce::Colour kCard(0xffffffff);
static const juce::Colour kGreen(0xff4C724D);
static const juce::Colour kKero(0xff99CC00);
static const juce::Colour kEqBg(0xffEAF4EA);
static const juce::Colour kCompBg(0xffFFF8EE);
static const juce::Colour kDelayBg(0xffF3EEFF);
static const juce::Colour kVerbBg(0xffE6F4FF);
static const juce::Colour kAiBg(0xffF3F8F3);
static const juce::Colour kPatchBg(0xffFAFAFA);
static const juce::Colour kBorder(0xffe8e8e8);
static const juce::Colour kLabel(0xff5c7c5d);
static const juce::Colour kLockOn(0xffff9800);
static const juce::Colour kLockOff(0xffe8e8e8);

// group per param
const int KeroMixAIAudioProcessorEditor::PARAM_GROUP[NUM_PARAMS] =
{ 0,0, 0,0,0, 0,0,  1,1,1,1,1,  2,2,2,  3,3,3,3,  4 };

// -- Constructor ---------------------------------------------------------------
KeroMixAIAudioProcessorEditor::KeroMixAIAudioProcessorEditor(KeroMixAIAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), juce::Thread("GroqThread")
{
    setSize(900, 540);

    // Sliders
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        auto& s = sliders[i];
        if (i == NUM_PARAMS - 1) // MASTER
        {
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        }
        else
        {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
        }
        s.setColour(juce::Slider::rotarySliderOutlineColourId, kKero.withAlpha(0.18f));
        s.setColour(juce::Slider::rotarySliderFillColourId, kKero);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxTextColourId, kGreen);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xffdddddd));
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::white);
        s.setColour(juce::Slider::trackColourId, kKero.withAlpha(0.3f));
        addAndMakeVisible(s);

        auto& l = labels[i];
        l.setText(paramNames[i], juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(9.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, kLabel);
        addAndMakeVisible(l);

        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, paramIDs[i], s);
    }

    // Lock buttons -- Fix #4: restore saved state
    audioProcessor.loadLockState(locked);
    for (int g = 0; g < NUM_GROUPS; ++g)
    {
        auto& btn = lockBtns[g];
        btn.setButtonText(groupNames[g]);
        btn.setClickingTogglesState(true);
        btn.setToggleState(locked[g], juce::dontSendNotification);
        btn.setColour(juce::TextButton::buttonColourId,
            locked[g] ? kLockOn : kLockOff);
        btn.setColour(juce::TextButton::textColourOffId,
            locked[g] ? juce::Colours::white : juce::Colour(0xff888888));
        btn.onClick = [this, g]() {
            locked[g] = lockBtns[g].getToggleState();
            lockBtns[g].setColour(juce::TextButton::buttonColourId,
                locked[g] ? kLockOn : kLockOff);
            lockBtns[g].setColour(juce::TextButton::textColourOffId,
                locked[g] ? juce::Colours::white : juce::Colour(0xff888888));
            audioProcessor.saveLockState(locked); // persist immediately
            };
        addAndMakeVisible(btn);
    }

    // Prompt
    promptInput.setMultiLine(false);
    promptInput.setReturnKeyStartsNewLine(false);
    promptInput.setTextToShowWhenEmpty("Describe what you want...", juce::Colour(0xffaaaaaa));
    promptInput.setFont(juce::Font(12.f));
    promptInput.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    promptInput.setColour(juce::TextEditor::outlineColourId, kKero.withAlpha(0.5f));
    promptInput.setColour(juce::TextEditor::textColourId, juce::Colour(0xff333333));
    promptInput.onReturnKey = [this]() { sendToGroq(promptInput.getText()); };
    addAndMakeVisible(promptInput);

    sendBtn.setButtonText("Apply AI");
    sendBtn.setColour(juce::TextButton::buttonColourId, kKero);
    sendBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    sendBtn.onClick = [this]() { sendToGroq(promptInput.getText()); };
    addAndMakeVisible(sendBtn);

    undoBtn.setButtonText("Undo");
    undoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffffe0b2));
    undoBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8d4a00));
    undoBtn.setEnabled(false);
    undoBtn.onClick = [this]() { restoreSnapshot(); };
    addAndMakeVisible(undoBtn);

    statusLabel.setText("", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(11.f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff666666));
    statusLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(statusLabel);

    // Quick command buttons
    for (int i = 0; i < NUM_QUICK; ++i)
    {
        quickBtns[i].setButtonText(quickCmds[i]);
        quickBtns[i].setColour(juce::TextButton::buttonColourId, juce::Colour(0xffE8F5E9));
        quickBtns[i].setColour(juce::TextButton::textColourOffId, kGreen);
        quickBtns[i].onClick = [this, i]() { sendToGroq(quickCmds[i]); };
        addAndMakeVisible(quickBtns[i]);
    }

    // Settings button
    settingsBtn.setButtonText(juce::CharPointer_UTF8("\xe2\x9a\x99"));
    settingsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    settingsBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff666666));
    settingsBtn.onClick = [this]() { showSettings(); };
    addAndMakeVisible(settingsBtn);

    // Patch UI
    patchNameInput.setTextToShowWhenEmpty("Patch name...", juce::Colour(0xffaaaaaa));
    patchNameInput.setFont(juce::Font(11.f));
    patchNameInput.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    patchNameInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xffdddddd));
    patchNameInput.setColour(juce::TextEditor::textColourId, juce::Colour(0xff333333));
    addAndMakeVisible(patchNameInput);

    saveBtn.setButtonText("Save");
    saveBtn.setColour(juce::TextButton::buttonColourId, kKero);
    saveBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    saveBtn.onClick = [this]() {
        auto name = patchNameInput.getText().trim();
        if (name.isEmpty()) { statusLabel.setText("Enter patch name!", juce::dontSendNotification); return; }
        audioProcessor.savePatch(name);
        refreshPatchList();
        statusLabel.setText("Saved: " + name, juce::dontSendNotification);
        };
    addAndMakeVisible(saveBtn);

    loadBtn.setButtonText("Load");
    loadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a90d9));
    loadBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadBtn.onClick = [this]() {
        auto name = patchList.getText();
        if (name.isEmpty()) return;
        audioProcessor.loadPatch(name);
        statusLabel.setText("Loaded: " + name, juce::dontSendNotification);
        };
    addAndMakeVisible(loadBtn);

    deleteBtn.setButtonText("Del");
    deleteBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe57373));
    deleteBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteBtn.onClick = [this]() {
        auto name = patchList.getText();
        if (name.isEmpty()) return;
        audioProcessor.deletePatch(name);
        refreshPatchList();
        statusLabel.setText("Deleted: " + name, juce::dontSendNotification);
        };
    addAndMakeVisible(deleteBtn);

    patchList.setTextWhenNothingSelected("-- select --");
    patchList.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    patchList.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffdddddd));
    addAndMakeVisible(patchList);
    refreshPatchList();

    groqApiKey = loadApiKey();
    if (groqApiKey.isEmpty()) showSettings();

    setResizable(true, true);
    resizeConstrainer.setSizeLimits(700, 440, 1400, 860);
    setConstrainer(&resizeConstrainer);

    startTimerHz(30);
}

KeroMixAIAudioProcessorEditor::~KeroMixAIAudioProcessorEditor()
{
    stopTimer();
    stopThread(2000);
}

// -- Settings panel ------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::showSettings()
{
    settingsPanel = std::make_unique<SettingsComponent>();
    settingsPanel->setBounds(getWidth() - 280, 40, 270, 170);
    settingsPanel->onKeyEntered = [this](const juce::String& key) {
        groqApiKey = key;
        saveApiKey(key);
        hideSettings();
        statusLabel.setText("API key saved!", juce::dontSendNotification);
        };
    settingsPanel->onClose = [this]() { hideSettings(); };
    addAndMakeVisible(*settingsPanel);
    settingsPanel->toFront(true);
}

void KeroMixAIAudioProcessorEditor::hideSettings()
{
    if (settingsPanel) { removeChildComponent(settingsPanel.get()); settingsPanel.reset(); }
}

// -- Patch ---------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::refreshPatchList()
{
    patchList.clear();
    auto names = audioProcessor.getSavedPatchNames();
    for (int i = 0; i < names.size(); ++i)
        patchList.addItem(names[i], i + 1);
}

// -- Undo ----------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::saveSnapshot()
{
    for (int i = 0; i < NUM_PARAMS; ++i)
        undoSnapshot[i] = (float)*audioProcessor.apvts.getRawParameterValue(paramIDs[i]);
    hasUndoSnapshot = true;
    undoBtn.setEnabled(true);
}

void KeroMixAIAudioProcessorEditor::restoreSnapshot()
{
    if (!hasUndoSnapshot) return;
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (auto* param = audioProcessor.apvts.getParameter(paramIDs[i]))
            param->setValueNotifyingHost(
                audioProcessor.apvts.getParameterRange(paramIDs[i]).convertTo0to1(undoSnapshot[i]));
    undoBtn.setEnabled(false);
    hasUndoSnapshot = false;
    statusLabel.setText("Reverted.", juce::dontSendNotification);
}

// -- API key -------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::saveApiKey(const juce::String& key)
{
    juce::File dir;
    
#if JUCE_MAC
    // Mac: ~/Library/Application Support/KeroMixAI/
    dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI");
#else
    // Windows: AppData/KeroMixAI/
    dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI");
#endif
    
    if (!dir.createDirectory())
    {
        // Fallback to Music directory if AppData fails (rare but safe for AU)
        dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile(".keromix");
        dir.createDirectory();
    }
    
    auto configFile = dir.getChildFile("config.txt");
    configFile.replaceWithText(key);
}

juce::String KeroMixAIAudioProcessorEditor::loadApiKey()
{
    juce::File f;
    
#if JUCE_MAC
    // Mac: try ~/Library/Application Support/KeroMixAI/config.txt first
    f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI").getChildFile("config.txt");
    if (f.existsAsFile())
        return f.loadFileAsString().trim();
    
    // Fallback to ~/.keromix/config.txt
    f = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile(".keromix").getChildFile("config.txt");
    if (f.existsAsFile())
        return f.loadFileAsString().trim();
#else
    // Windows: ~/AppData/Roaming/KeroMixAI/config.txt
    f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI").getChildFile("config.txt");
    if (f.existsAsFile())
        return f.loadFileAsString().trim();
#endif
    
    return juce::String();
}

// -- FFT helpers ---------------------------------------------------------------
static float fftBandDb(const float* data, int bStart, int bEnd, int bins, int fftSize)
{
    float sum = 0.f;
    int   cnt = 0;
    int   eClamp = bEnd < bins ? bEnd : bins;
    for (int b = bStart; b < eClamp; ++b)
    {
        sum += data[b] * data[b];
        ++cnt;
    }
    if (cnt == 0) return -60.f;
    float rms = std::sqrt(sum / (float)cnt) / (float)fftSize;
    return juce::jmax(-60.f, juce::Decibels::gainToDecibels(rms));
}

static juce::String escapeJson(const juce::String& s)
{
    juce::String r;
    for (int i = 0; i < s.length(); ++i)
    {
        juce::juce_wchar c = s[i];
        if      (c == '"')  r << "\\\"";
        else if (c == '\\') r << "\\\\";
        else if (c == '\n') r << "\\n";
        else if (c == '\r') r << "\\r";
        else if (c == '\t') r << "\\t";
        else                r << juce::String::charToString(c);
    }
    return r;
}

// -- FFT -----------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::processFFT()
{
    fftWindow.multiplyWithWindowingTable(fftData, FFT_SIZE);
    fft.performFrequencyOnlyForwardTransform(fftData);

    const double sr = audioProcessor.getSampleRate();
    if (sr <= 0.0) return;
    const int    bins   = FFT_SIZE / 2;
    const double bw     = sr / (double)FFT_SIZE;
    const int    fftSz  = FFT_SIZE;

    int subEnd   = (int)(80.0   / bw);
    int lEnd     = (int)(300.0  / bw);
    int loMidEnd = (int)(800.0  / bw);
    int mEnd     = (int)(4000.0 / bw);
    int presEnd  = (int)(8000.0 / bw);
    int hEnd     = (int)(20000.0 / bw);

    const float smth = 0.15f;
    specLow  += smth * (fftBandDb(fftData, 1,        lEnd,    bins, fftSz) - specLow);
    specMid  += smth * (fftBandDb(fftData, lEnd,     mEnd,    bins, fftSz) - specMid);
    specHigh += smth * (fftBandDb(fftData, mEnd,     hEnd,    bins, fftSz) - specHigh);

    float subBassDb  = fftBandDb(fftData, 1,        subEnd,  bins, fftSz);
    float loMidDb    = fftBandDb(fftData, lEnd,     loMidEnd,bins, fftSz);
    float hiMidDb    = fftBandDb(fftData, loMidEnd, mEnd,    bins, fftSz);
    float presenceDb = fftBandDb(fftData, mEnd,     presEnd, bins, fftSz);
    float airDb      = fftBandDb(fftData, presEnd,  hEnd,    bins, fftSz);

    // peak frequency
    float peakVal = 0.f;
    int   peakBin = 1;
    int   peakLim = (int)(5000.0 / bw);
    for (int k = 1; k < bins && k < peakLim; ++k)
    {
        if (fftData[k] > peakVal) { peakVal = fftData[k]; peakBin = k; }
    }
    float peakHz = (float)((double)peakBin * bw);

    // dynamic range
    float maxVal = 0.f;
    float rmsSum = 0.f;
    int   rmsN   = 0;
    for (int k = 1; k < bins; ++k)
    {
        if (fftData[k] > maxVal) maxVal = fftData[k];
        rmsSum += fftData[k] * fftData[k];
        ++rmsN;
    }
    float rmsVal   = (rmsN > 0) ? std::sqrt(rmsSum / (float)rmsN) : 0.f;
    float dynRange = (maxVal > 0.f && rmsVal > 0.f)
                     ? juce::Decibels::gainToDecibels(maxVal / (rmsVal + 1e-9f))
                     : 0.f;

    float transientScore = juce::jlimit(0.f, 1.f, dynRange / 30.f);
    float spectralTilt   = specHigh - specLow;

    {
        juce::ScopedLock sl(threadLock);
        audioAnalysis = juce::String::formatted(
            "SubBass:%.1fdB Low:%.1fdB LoMid:%.1fdB HiMid:%.1fdB Presence:%.1fdB Air:%.1fdB | "
            "PeakHz:%.0f DynRange:%.1fdB Transient:%.2f SpectralTilt:%.1f",
            subBassDb, specLow, loMidDb, hiMidDb, presenceDb, airDb,
            peakHz, dynRange, transientScore, spectralTilt);
    }

    // Per-bin smoothing for spectrum analyser
    {
        const float sm = 0.2f;
        for (int k = 1; k < FFT_SIZE / 2; ++k)
        {
            float raw = juce::Decibels::gainToDecibels(fftData[k] / (float)FFT_SIZE + 1e-9f);
            spectrumSmooth[k] += sm * (juce::jmax(-90.f, raw) - spectrumSmooth[k]);
        }
    }

    fftDataReady = false;
}

void KeroMixAIAudioProcessorEditor::timerCallback()
{
    {
        juce::ScopedLock sl(audioProcessor.fftLock);
        if (audioProcessor.fftDataReady)
        {
            juce::zeromem(fftData, sizeof(fftData));
            memcpy(fftData, audioProcessor.fftFifo, sizeof(float) * FFT_SIZE);
            audioProcessor.fftDataReady = false;
            fftDataReady = true;
        }
    }
    if (fftDataReady) processFFT();

    // Level meters
    float newL = audioProcessor.levelMeterL.load();
    float newR = audioProcessor.levelMeterR.load();
    levelL = levelL * 0.80f + newL * 0.20f;
    levelR = levelR * 0.80f + newR * 0.20f;

    if (newL > peakL) { peakL = newL; peakHoldTimer = 2.0f; }
    if (newR > peakR) { peakR = newR; peakHoldTimer = 2.0f; }
    peakHoldTimer -= 1.f / 30.f;
    if (peakHoldTimer <= 0.f) { peakL = levelL; peakR = levelR; peakHoldTimer = 0.f; }

    // GR meter: fast attack, slow release
    float gr = audioProcessor.grMeterDb.load();
    grMeterSmooth += (gr < grMeterSmooth ? 0.45f : 0.04f) * (gr - grMeterSmooth);

    repaint();
}

// -- Groq ----------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::sendToGroq(const juce::String& prompt)
{
    if (prompt.trim().isEmpty()) return;
    if (groqApiKey.isEmpty()) { showSettings(); return; }

    saveSnapshot();

    juce::String lockedList, currentParams = "{";
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        if (i > 0) currentParams << ",";
        currentParams << "\"" << paramIDs[i] << "\":"
            << (float)*audioProcessor.apvts.getRawParameterValue(paramIDs[i]);
        if (isParamInLockedGroup(i))
            lockedList += (lockedList.isEmpty() ? "" : ",") + paramIDs[i];
    }
    currentParams << "}";

    juce::String spec;
    spec << "Low:" << juce::String(specLow, 1) << "dB Mid:" << juce::String(specMid, 1)
        << "dB High:" << juce::String(specHigh, 1) << "dB";

    juce::String analysis;
    { juce::ScopedLock sl(threadLock); analysis = audioAnalysis; }

    {
        juce::ScopedLock sl(threadLock);
        pendingPrompt = prompt + "|||" + currentParams + "|||" + lockedList + "|||" + spec + "|||" + analysis;
    }

    statusLabel.setText("Processing...", juce::dontSendNotification);
    sendBtn.setEnabled(false);
    for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(false);
    if (!isThreadRunning()) startThread();
}

void KeroMixAIAudioProcessorEditor::run()
{
    juce::String full;
    { juce::ScopedLock sl(threadLock); full = pendingPrompt; }

    auto parts      = juce::StringArray::fromTokens(full, "|||", "");
    auto uText      = parts[0];
    auto curJson    = parts.size() > 1 ? parts[1] : juce::String("{}");
    auto lockedStr  = parts.size() > 2 ? parts[2] : juce::String("");
    auto spec       = parts.size() > 3 ? parts[3] : juce::String("");
    auto analysis   = parts.size() > 4 ? parts[4] : juce::String("");

    juce::String lockNote = lockedStr.isEmpty()
        ? "No params locked."
        : "LOCKED (do NOT change these): " + lockedStr;

    juce::String sys =
        "You are a professional audio mixing engineer AI embedded in a DAW plugin. "
        "Your job is to translate natural language requests into precise parameter changes.\n\n"

        "=== CRITICAL RULES ===\n"
        "1. If the request is meaningless, random characters, gibberish, or unrelated to audio/music, "
        "return EXACTLY: {} (empty JSON, change nothing)\n"
        "2. Only make changes when you clearly understand the musical or sonic intent\n"
        "3. Return a JSON with TWO fields:\n"
        "   - 'params': object with parameter changes\n"
        "   - 'reason': one short sentence in ENGLISH explaining what you changed and why\n"
        "   Example: {\"params\":{\"highG\":4.5,\"revMix\":0.28},\"reason\":\"Boosted highs and added reverb for brightness and space\"}\n"
        "   If nothing to change: {\"params\":{},\"reason\":\"Request not recognized as audio-related\"}\n\n"

        "=== AUDIO ANALYSIS DATA ===\n"
        "Use real-time audio analysis to make context-aware decisions:\n"
        "- SubBass/Low/LoMid/HiMid/Presence/Air: frequency band levels in dB\n"
        "- PeakHz: dominant frequency (80-200=bass/kick, 200-500=piano/guitar, 500-2000=vocals/snare, 2000+=highs)\n"
        "- DynRange: dynamic range (low=already compressed, high=needs compression)\n"
        "- Transient: 0=sustained/pads, 1=percussive/drums\n"
        "- SpectralTilt: negative=dark, positive=bright\n\n"

        "=== PARAMETER RANGES ===\n"
        "lowG/midG/highG: -18~+18dB | lowFreq: 60-600Hz | midFreq: 300-5000Hz | midQ: 0.3-4 | highFreq: 3000-16000Hz\n"
        "compThresh: -40~0dB | compRatio: 1-20 | compAttack: 1-100ms | compRelease: 20-500ms | compMakeup: 0-24dB\n"
        "delayTime: 0.05-1.0s | delayFeedback: 0-0.9 | delayMix: 0-1\n"
        "revDecay/revSize/revDamp/revMix: 0-1 | aimix: 0-1\n\n"

        "=== INTENSITY SCALE ===\n"
        "Detect intensity and scale changes:\n"
        "'ajujogeum'/'tiny'/'barely' => x0.15 (EQ:+/-0.5~1dB, mix:+/-0.03)\n"
        "'jogeom'/'slightly'/'a bit'  => x0.3  (EQ:+/-1~2dB,   mix:+/-0.06)\n"
        "'yakgan'/'gently'/'subtle'   => x0.5  (EQ:+/-2~3dB,   mix:+/-0.10)\n"
        "(no qualifier = default)   => x1.0  (EQ:+/-3~5dB,   mix:+/-0.18)\n"
        "'mani'/'quite'/'more'      => x1.5  (EQ:+/-5~8dB,   mix:+/-0.28)\n"
        "'ajumani'/'a lot'/'heavy' => x2.0  (EQ:+/-8~12dB,  mix:+/-0.40)\n"
        "'extreme-KR'/'extreme'/'max'   => x3.0  (push limits)\n\n"

        "=== NATURAL LANGUAGE UNDERSTANDING ===\n"
        "'stuffy-KR'/'muddy'     => cut LoMid 300-600Hz, reduce lowG\n"
        "'stiff-KR'/'harsh'     => cut Presence 3-6kHz, reduce highG\n"
        "'thin-KR'/'thin'    => boost LoMid, subtle reverb\n"
        "'full-KR'/'full/fat'  => boost low-mid, moderate compression\n"
        "'spacious-KR'/'spacious'  => increase revMix, revSize\n"
        "'moody-KR'/'moody'   => dark EQ, long reverb, light delay\n"
        "'lyrical-KR2'/'lyrical'   => smooth comp, warm EQ, subtle reverb\n"
        "'epic-KR'/'epic'    => boost lows, big reverb\n"
        "'clean-KR'/'clean'   => reduce reverb/delay, gentle comp\n"
        "'piano-KR'/'piano'     => focus 200-1000Hz, light reverb\n"
        "'vocal-KR'/'vocal'       => presence boost 2-4kHz, comp\n"
        "'drums-KR'/'drums'       => punch 60-200Hz, transient comp\n\n"

        "=== LOCK ===\n"
        + lockNote + "\n\n"
        "Base all values on CURRENT params. Never reset to defaults. Return ONLY the JSON object.";

    juce::String msgs = "[{\"role\":\"system\",\"content\":\"" + escapeJson(sys) + "\"}";
    for (int hi = 0; hi < (int)chatHistory.size(); ++hi)
        msgs += ",{\"role\":\"" + escapeJson(chatHistory[hi].role) + "\",\"content\":\"" + escapeJson(chatHistory[hi].content) + "\"}";

    juce::String uc = "=AUDIO ANALYSIS= " + analysis
        + " =CURRENT PARAMS= " + curJson
        + " =USER REQUEST= " + escapeJson(uText);
    msgs += ",{\"role\":\"user\",\"content\":\"" + escapeJson(uc) + "\"}]";

    juce::String body;
    body << "{\"model\":\"llama-3.3-70b-versatile\","
        << "\"messages\":" << msgs << ","
        << "\"max_tokens\":500,\"temperature\":0.35}";

    juce::URL url("https://api.groq.com/openai/v1/chat/completions");
    std::unique_ptr<juce::InputStream> ws;
    
    try {
        ws = url.withPOSTData(body).createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withExtraHeaders("Authorization: Bearer " + groqApiKey + "\r\nContent-Type: application/json")
            .withConnectionTimeoutMs(30000)
            .withReadTimeoutMs(30000));
    } catch (...) {
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Network error. Check internet connection.", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }

    if (!ws) {
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Connection failed. Check API key.", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }

    juce::String resp;
    try {
        resp = ws->readEntireStreamAsString();
    } catch (...) {
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Response timeout. Try again.", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }
    
    if (resp.isEmpty()) {
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Empty response from server.", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }
    juce::String pj;
    int ci = resp.indexOf("\"content\":\"");
    if (ci >= 0) {
        int bs = resp.indexOf(ci, "{");
        if (bs >= 0) {
            int depth = 0, be = -1;
            for (int i = bs; i < resp.length(); ++i) {
                if (resp[i] == '{') ++depth;
                else if (resp[i] == '}') { if (--depth == 0) { be = i; break; } }
            }
            if (be > bs)
                pj = resp.substring(bs, be + 1).replace("\\n", " ").replace("\\\"", "\"").replace("\\/", "/");
        }
    }

    if (pj.isEmpty()) {
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("keromix_debug.txt").replaceWithText(resp);
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Parse failed. See keromix_debug.txt", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }

    chatHistory.push_back({ "user",      "Analysis:" + analysis + " Params:" + curJson + " Request:" + uText });
    chatHistory.push_back({ "assistant", pj });
    if ((int)chatHistory.size() > MAX_HISTORY * 2)
        chatHistory.erase(chatHistory.begin(), chatHistory.begin() + 2);

    juce::MessageManager::callAsync([this, pj]() { applyParamsFromJson(pj); });
}

void KeroMixAIAudioProcessorEditor::applyParamsFromJson(const juce::String& json)
{
    //  : {"params":{...},"reason":"..."}
    //   : {"lowG":...}  
    juce::String paramJson = json;
    juce::String reason;

    // reason 
    int reasonPos = json.indexOf("\"reason\":");
    if (reasonPos >= 0) {
        int q1 = json.indexOf(reasonPos + 9, "\""); // "reason":   
        if (q1 >= 0) {
            int q2 = q1 + 1;
            while (q2 < json.length() && json[q2] != '"') {
                if (json[q2] == '\\') ++q2; // escape 
                ++q2;
            }
            reason = json.substring(q1 + 1, q2);
        }
    }

    // params  
    int paramsPos = json.indexOf("\"params\":");
    if (paramsPos >= 0) {
        int bs = json.indexOf(paramsPos, "{");
        if (bs >= 0) {
            int depth = 0, be = -1;
            for (int i = bs; i < json.length(); ++i) {
                if (json[i] == '{') ++depth;
                else if (json[i] == '}') { if (--depth == 0) { be = i; break; } }
            }
            if (be > bs) paramJson = json.substring(bs, be + 1);
        }
    }

    //  params   
    if (paramJson.trim() == "{}" || paramJson.trim().isEmpty()) {
        juce::String msg = reason.isNotEmpty() ? reason : "No changes made.";
        statusLabel.setText(msg, juce::dontSendNotification);
        sendBtn.setEnabled(true);
        for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
        return;
    }

    int applied = 0;
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        if (isParamInLockedGroup(i)) continue;
        juce::String search = "\"" + paramIDs[i] + "\":";
        int pos = paramJson.indexOf(search);
        if (pos < 0) continue;
        pos += search.length();
        while (pos < paramJson.length() && paramJson[pos] == ' ') ++pos;
        int end = pos;
        while (end < paramJson.length() &&
            (paramJson[end] == '-' || paramJson[end] == '.' ||
             (paramJson[end] >= '0' && paramJson[end] <= '9'))) ++end;
        if (end > pos) {
            float v = paramJson.substring(pos, end).getFloatValue();
            if (auto* param = audioProcessor.apvts.getParameter(paramIDs[i])) {
                auto range = audioProcessor.apvts.getParameterRange(paramIDs[i]);
                param->setValueNotifyingHost(range.convertTo0to1(juce::jlimit(range.start, range.end, v)));
                ++applied;
            }
        }
    }

    promptInput.clear();

    // reason  ,   
    juce::String statusMsg = reason.isNotEmpty()
        ? reason
        : "AI applied! (" + juce::String(applied) + " params)";
    statusLabel.setText(statusMsg, juce::dontSendNotification);
    sendBtn.setEnabled(true);
    for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
}

// -- Draw helpers --------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::drawKeropi(juce::Graphics& g, float x, float y, float sc)
{
    g.setColour(kKero);
    g.fillEllipse(x, y + 13 * sc, 55 * sc, 45 * sc);
    g.setColour(juce::Colours::black);
    g.drawEllipse(x, y + 13 * sc, 55 * sc, 45 * sc, 2 * sc);
    auto eye = [&](float ex, float ey) {
        g.setColour(juce::Colours::white); g.fillEllipse(ex, ey, 22 * sc, 22 * sc);
        g.setColour(juce::Colours::black); g.drawEllipse(ex, ey, 22 * sc, 22 * sc, 2 * sc);
        };
    eye(x - 2 * sc, y); eye(x + 35 * sc, y);
    g.setColour(juce::Colours::black);
    g.fillEllipse(x + 5 * sc, y + 7 * sc, 7 * sc, 7 * sc);
    g.fillEllipse(x + 42 * sc, y + 7 * sc, 7 * sc, 7 * sc);
    juce::Path mouth;
    mouth.startNewSubPath(x + 20 * sc, y + 38 * sc);
    mouth.lineTo(x + 27 * sc, y + 43 * sc);
    mouth.lineTo(x + 34 * sc, y + 38 * sc);
    g.strokePath(mouth, juce::PathStrokeType(2 * sc));
}

void KeroMixAIAudioProcessorEditor::drawSpectrumBar(juce::Graphics& g,
    float x, float y, float w, float h, float dB, juce::Colour col)
{
    float norm = juce::jlimit(0.f, 1.f, (dB + 60.f) / 60.f);
    g.setColour(col.withAlpha(0.2f)); g.fillRoundedRectangle(x, y, w, h, 3.f);
    g.setColour(col.withAlpha(0.85f)); g.fillRoundedRectangle(x, y + h - norm * h, w, norm * h, 3.f);
}

void KeroMixAIAudioProcessorEditor::drawSectionBg(juce::Graphics& g,
    juce::Rectangle<float> r, juce::Colour bg, const juce::String& title)
{
    g.setColour(bg); g.fillRoundedRectangle(r, 12.f);
    g.setColour(kGreen.withAlpha(0.7f));
    g.setFont(juce::Font("Arial", 11.f, juce::Font::bold));
    g.drawText(title, (int)r.getX() + 10, (int)r.getY() + 5, 60, 16, juce::Justification::left, false);
}


// ── drawMonitorSection ────────────────────────────────────────────────────────
// Styled to match existing light-green theme (kBg/kCard/kGreen/kKero/kBorder)
void KeroMixAIAudioProcessorEditor::drawMonitorSection(juce::Graphics& g,
                                                        juce::Rectangle<float> area)
{
    const double sr = audioProcessor.getSampleRate();
    const float x0 = area.getX(), y0 = area.getY();
    const float w  = area.getWidth(), h  = area.getHeight();

    // Card — same radius/border as other sections
    g.setColour(juce::Colour(0xffF3F8F3));   // matches kAiBg
    g.fillRoundedRectangle(area, 10.f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(area, 10.f, 1.f);

    // Section title — same font/colour as other section titles
    g.setFont(juce::Font(9.f, juce::Font::bold));
    g.setColour(kGreen.withAlpha(0.65f));
    g.drawText("MONITOR", (int)x0 + 10, (int)y0 + 5, 70, 13, juce::Justification::left);

    const float mx = x0 + 8.f;
    const float mw = w - 16.f;
    float cy = y0 + 21.f;

    // ── Level bar helper ──────────────────────────────────────────────────
    auto drawLevelBar = [&](float bx, float by, float bw, float bh,
                             float level, float peak, const char* lbl)
    {
        // track
        g.setColour(juce::Colour(0xffe0eee0));
        g.fillRoundedRectangle(bx, by, bw, bh, 2.f);

        const float dbMin = -60.f, dbMax = 0.f;
        float norm = juce::jlimit(0.f, 1.f, (level - dbMin) / (dbMax - dbMin));
        float fw   = bw * norm;
        float g1   = bw * ((-12.f - dbMin) / (dbMax - dbMin));
        float g2   = bw * ((-6.f  - dbMin) / (dbMax - dbMin));

        if (fw > 0.001f)
        {
            // green zone (kKero colour)
            g.setColour(kKero.withAlpha(0.9f));
            g.fillRoundedRectangle(bx, by, std::min(fw, g1), bh, 2.f);
            if (fw > g1) {
                g.setColour(juce::Colour(0xffffbb00));
                g.fillRect(bx + g1, by, std::min(fw - g1, g2 - g1), bh);
            }
            if (fw > g2) {
                g.setColour(juce::Colour(0xffee3322));
                g.fillRect(bx + g2, by, fw - g2, bh);
            }
        }

        // peak hold
        if (peak > -59.f)
        {
            float pp = juce::jlimit(0.f, 1.f, (peak - dbMin) / (dbMax - dbMin));
            g.setColour(peak > -6.f ? juce::Colour(0xffee3322) : kGreen);
            g.fillRect(bx + bw * pp - 1.f, by, 2.f, bh);
        }

        // label L / R / GR
        g.setFont(juce::Font(8.f, juce::Font::bold));
        g.setColour(kLabel);
        g.drawText(lbl, (int)(bx - 14), (int)by, 14, (int)bh, juce::Justification::centredRight);

        // dB readout
        if (level > -59.f)
        {
            g.setFont(juce::Font(7.f));
            g.setColour(kGreen.withAlpha(0.75f));
            g.drawText(juce::String(level, 1),
                       (int)(bx + bw - 30), (int)by, 28, (int)bh,
                       juce::Justification::centredRight);
        }

        g.setColour(kBorder);
        g.drawRoundedRectangle(bx, by, bw, bh, 2.f, 1.f);
    };

    const float barH  = 11.f;
    const float barX  = mx + 16.f;
    const float barW  = mw - 16.f;

    // L meter
    drawLevelBar(barX, cy, barW, barH, levelL, peakL, "L");
    cy += 14.f;
    // R meter
    drawLevelBar(barX, cy, barW, barH, levelR, peakR, "R");
    cy += 14.f;

    // GR meter
    {
        g.setColour(juce::Colour(0xffe0eee0));
        g.fillRoundedRectangle(barX, cy, barW, barH, 2.f);

        const float grMax = -30.f;
        float norm = juce::jlimit(0.f, 1.f, grMeterSmooth / grMax);
        if (norm > 0.001f)
        {
            g.setColour(norm < 0.4f ? kKero.withAlpha(0.85f)
                       : norm < 0.7f ? juce::Colour(0xffffbb00)
                                     : juce::Colour(0xffee3322));
            g.fillRoundedRectangle(barX, cy, barW * norm, barH, 2.f);
        }

        for (float db : { -6.f, -12.f, -18.f, -24.f })
        {
            float tx = barX + barW * (db / grMax);
            g.setColour(kBorder);
            g.drawVerticalLine((int)tx, cy, cy + barH);
        }

        g.setFont(juce::Font(8.f, juce::Font::bold));
        g.setColour(kLabel);
        g.drawText("GR", (int)(barX - 14), (int)cy, 14, (int)barH,
                   juce::Justification::centredRight);

        if (grMeterSmooth < -0.5f)
        {
            g.setFont(juce::Font(7.f));
            g.setColour(kGreen.withAlpha(0.75f));
            g.drawText(juce::String(grMeterSmooth, 1) + "dB",
                       (int)(barX + barW - 34), (int)cy, 32, (int)barH,
                       juce::Justification::centredRight);
        }
        g.setColour(kBorder);
        g.drawRoundedRectangle(barX, cy, barW, barH, 2.f, 1.f);
        cy += 14.f;
    }

    // ── Spectrum analyser ─────────────────────────────────────────────────
    float specH = h - (cy - y0) - 7.f;
    if (specH < 18.f || sr <= 0.0) return;

    // Dark bg inside the card — like a mini inset screen
    g.setColour(juce::Colour(0xff1a261a));
    g.fillRoundedRectangle(mx, cy, mw, specH, 4.f);

    // dB grid
    for (float db : { -12.f, -24.f, -48.f })
    {
        float gy = cy + specH * (1.f - (db + 90.f) / 90.f);
        g.setColour(juce::Colour(0xff223322));
        g.drawHorizontalLine((int)gy, mx + 1, mx + mw - 1);
        g.setFont(juce::Font(6.f));
        g.setColour(juce::Colour(0xff3d5c3d));
        g.drawText(juce::String((int)db), (int)mx + 2, (int)(gy - 5), 22, 10,
                   juce::Justification::left);
    }

    // Freq grid
    for (float f : { 100.f, 1000.f, 10000.f })
    {
        float gx = mx + mw * (std::log10(f / 20.f) / std::log10(1000.f));
        g.setColour(juce::Colour(0xff223322));
        g.drawVerticalLine((int)gx, cy, cy + specH - 10);
        g.setFont(juce::Font(6.f));
        g.setColour(juce::Colour(0xff3d5c3d));
        juce::String lbl = f >= 1000.f ? juce::String((int)(f / 1000)) + "k"
                                       : juce::String((int)f);
        g.drawText(lbl, (int)gx - 8, (int)(cy + specH - 10), 16, 10,
                   juce::Justification::centred);
    }

    // Spectrum fill + line (kKero colour to match knobs)
    {
        const int halfBins  = FFT_SIZE / 2;
        const double binHz  = sr / (double)FFT_SIZE;
        juce::Path fill, line;
        bool started = false;

        for (int i = 0; i < (int)mw; ++i)
        {
            double norm2 = (double)i / (mw - 1.0);
            double freq  = 20.0 * std::pow(1000.0, norm2);
            float  binF  = (float)(freq / binHz);
            int    b0    = juce::jlimit(1, halfBins - 2, (int)binF);
            float  frac  = binF - (float)b0;
            float  db2   = spectrumSmooth[b0] * (1.f - frac) + spectrumSmooth[b0 + 1] * frac;
            float  n3    = juce::jlimit(0.f, 1.f, (db2 + 90.f) / 90.f);
            float  sy    = cy + specH - specH * n3;

            if (!started) { fill.startNewSubPath(mx + i, sy); line.startNewSubPath(mx + i, sy); started = true; }
            else          { fill.lineTo(mx + i, sy); line.lineTo(mx + i, sy); }
        }

        if (started)
        {
            juce::Path fillCopy = fill;
            fillCopy.lineTo(mx + mw - 1, cy + specH);
            fillCopy.lineTo(mx, cy + specH);
            fillCopy.closeSubPath();
            g.setColour(kKero.withAlpha(0.18f));
            g.fillPath(fillCopy);
            g.setColour(kKero.withAlpha(0.85f));
            g.strokePath(line, juce::PathStrokeType(1.3f));
        }
    }

    g.setColour(juce::Colour(0xff2d4a2d));
    g.drawRoundedRectangle(mx, cy, mw, specH, 4.f, 1.f);
}

// -- Paint ---------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float W = (float)getWidth(), H = (float)getHeight();
    const float pad = 10.f;

    g.setColour(kBg); g.fillAll();

    juce::Rectangle<float> card(pad, pad, W - pad * 2, H - pad * 2);
    g.setColour(kCard); g.fillRoundedRectangle(card, 18.f);
    g.setColour(kBorder); g.drawRoundedRectangle(card, 18.f, 1.5f);

    g.setColour(kGreen.withAlpha(0.06f));
    g.fillRoundedRectangle(pad, pad, W - pad * 2, 44.f, 18.f);

    drawKeropi(g, pad + 14, pad + 2, 0.62f);

    g.setColour(kGreen);
    g.setFont(juce::Font("Arial", 20.f, juce::Font::bold));
    g.drawText("KeroMixAI", (int)(pad + 62), (int)(pad + 8), 180, 26, juce::Justification::left, false);
    g.setFont(juce::Font(9.f));
    g.setColour(juce::Colour(0xffaaaaaa));
    g.drawText("by NADAHONG", (int)(pad + 62), (int)(pad + 28), 140, 14, juce::Justification::left, false);

    const float dspY = pad + 48.f;
    const float leftW = W * 0.62f;
    const float rightX = leftW + 4.f;
    const float rightW = W - rightX - pad;
    float colW = (leftW - pad * 2 - 8.f) / 2.f;

    drawSectionBg(g, { pad + 4, dspY,        colW, 190.f }, kEqBg, "EQ");
    drawSectionBg(g, { pad + 4, dspY + 196.f,  colW, 240.f }, kCompBg, "COMP");
    drawSectionBg(g, { pad + 4 + colW + 6, dspY,        colW, 130.f }, kDelayBg, "DELAY");
    drawSectionBg(g, { pad + 4 + colW + 6, dspY + 136.f,  colW, 300.f }, kVerbBg, "REVERB");

    g.setColour(juce::Colour(0xffF0F4F0));
    g.fillRoundedRectangle(pad + 4, H - pad - 36.f, leftW - 8.f, 30.f, 8.f);
    g.setColour(kGreen.withAlpha(0.5f));
    g.setFont(juce::Font(9.f, juce::Font::bold));
    g.drawText("MASTER", (int)(pad + 8), (int)(H - pad - 34), 50, 14, juce::Justification::left, false);

    g.setColour(kBorder);
    g.drawLine(leftW + 2, dspY, leftW + 2, H - pad, 1.f);

    drawSectionBg(g, { rightX, dspY,        rightW, 178.f }, kAiBg, "AI MIX");
    // Fix: PATCHES section moved down 4px and taller so title isn't clipped
    drawSectionBg(g, { rightX, dspY + 182.f, rightW, 74.f }, kPatchBg, "PATCHES");

    {
        float monY = dspY + 260.f;
        float monH = H - pad - monY - 6.f;
        if (monH > 30.f)
            drawMonitorSection(g, { rightX, monY, rightW, monH });
    }
}

// -- Resized -------------------------------------------------------------------
void KeroMixAIAudioProcessorEditor::resized()
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float pad = 10.f;
    const float dspY = pad + 52.f;
    const float leftW = W * 0.62f;
    const float colW = (leftW - pad * 2 - 8.f) / 2.f;
    const float rightX = leftW + 4.f;
    const float rightW = W - rightX - pad;

    // EQ section
    {
        const int kW = 58, kH = 58, lH = 14, topSkip = 22;
        float rx = pad + 6, ry = dspY, rw = colW - 4;
        float sp = rw / 3.f;
        int gainIdx[3] = { 0, 2, 5 };
        for (int i = 0; i < 3; ++i)
        {
            int cx = (int)(rx + i * sp + (sp - kW) * 0.5f);
            int cy = (int)(ry + topSkip);
            labels[gainIdx[i]].setBounds(cx, cy, kW, lH);
            sliders[gainIdx[i]].setBounds(cx, cy + lH, kW, kH);
        }
        int fIdx[4] = { 1, 3, 4, 6 };
        float fSp = rw / 4.f;
        for (int i = 0; i < 4; ++i)
        {
            int cx = (int)(rx + i * fSp + (fSp - kW) * 0.5f);
            int cy = (int)(ry + topSkip + kH + lH + 4);
            labels[fIdx[i]].setBounds(cx, cy, kW - 4, lH);
            sliders[fIdx[i]].setBounds(cx, cy + lH, kW - 4, 50);
        }
    }

    // placeRow helper -- inlined to avoid MSVC lambda issues
    // COMP row (idx 7..11, 5 knobs)
    {
        const int kW = 58, kH = 58, lH = 14, topSkip = 22;
        float rx = pad + 6, ry = dspY + 196.f, rw = colW - 4;
        float spacing = rw / 5.f;
        for (int ii = 0; ii < 5; ++ii)
        {
            int cx = (int)(rx + ii * spacing + (spacing - kW) * 0.5f);
            int cy = (int)(ry + topSkip);
            labels[7 + ii].setBounds(cx, cy, kW, lH);
            sliders[7 + ii].setBounds(cx, cy + lH, kW, kH);
        }
    }
    // DELAY row (idx 12..14, 3 knobs)
    {
        const int kW = 58, kH = 58, lH = 14, topSkip = 22;
        float rx = pad + 6 + colW + 6, ry = dspY, rw = colW - 4;
        float spacing = rw / 3.f;
        for (int ii = 0; ii < 3; ++ii)
        {
            int cx = (int)(rx + ii * spacing + (spacing - kW) * 0.5f);
            int cy = (int)(ry + topSkip);
            labels[12 + ii].setBounds(cx, cy, kW, lH);
            sliders[12 + ii].setBounds(cx, cy + lH, kW, kH);
        }
    }
    // REVERB row (idx 15..18, 4 knobs)
    {
        const int kW = 58, kH = 58, lH = 14, topSkip = 22;
        float rx = pad + 6 + colW + 6, ry = dspY + 136.f, rw = colW - 4;
        float spacing = rw / 4.f;
        for (int ii = 0; ii < 4; ++ii)
        {
            int cx = (int)(rx + ii * spacing + (spacing - kW) * 0.5f);
            int cy = (int)(ry + topSkip);
            labels[15 + ii].setBounds(cx, cy, kW, lH);
            sliders[15 + ii].setBounds(cx, cy + lH, kW, kH);
        }
    }

    {
        labels[19].setBounds((int)(pad + 8), (int)(H - pad - 34), 52, 16);
        sliders[19].setBounds((int)(pad + 62), (int)(H - pad - 38), (int)(leftW - 74), 32);
    }

    {
        float lbW = (leftW - pad * 2 - 16.f) / NUM_GROUPS;
        for (int g = 0; g < NUM_GROUPS; ++g)
            lockBtns[g].setBounds((int)(pad + 6 + g * (lbW + 4)), (int)(pad + 30), (int)lbW, 16);
    }

    settingsBtn.setBounds((int)(W - pad - 32), (int)(pad + 10), 24, 24);

    {
        float aiX = rightX + 8, aiW = rightW - 16;
        float aiY = dspY + 22;

        float qW = (aiW - 12.f) / 4.f;
        for (int i = 0; i < NUM_QUICK; ++i) {
            int row = i / 4, col = i % 4;
            quickBtns[i].setBounds((int)(aiX + col * (qW + 4)), (int)(aiY + row * 28), (int)(qW), 24);
        }

        float pY = aiY + 64.f;
        promptInput.setBounds((int)aiX, (int)pY, (int)(aiW - 68), 28);
        sendBtn.setBounds((int)(aiX + aiW - 64), (int)pY, 60, 28);

        undoBtn.setBounds((int)aiX, (int)(pY + 34), 56, 22);
        statusLabel.setBounds((int)aiX, (int)(pY + 58), (int)aiW, 32);
    }

    {
        // Fix: pY starts 20px into the section (title is ~18px tall) so nothing overlaps
        float pX = rightX + 8, pW = rightW - 16, pY = dspY + 182.f + 22.f;
        float nW = (pW - 8.f) * 0.38f;
        patchNameInput.setBounds((int)pX, (int)pY, (int)nW, 26);
        saveBtn.setBounds((int)(pX + nW + 4), (int)pY, 44, 26);
        patchList.setBounds((int)(pX + nW + 52), (int)pY, (int)(pW - nW - 100), 26);
        loadBtn.setBounds((int)(pX + pW - 44), (int)pY, 40, 26);
        deleteBtn.setVisible(false);
    }

    if (settingsPanel)
        settingsPanel->setBounds((int)(W - 290), 40, 270, 170);
}

void KeroMixAIAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    hideSettings();
}
