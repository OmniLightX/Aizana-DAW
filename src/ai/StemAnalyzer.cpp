#include "StemAnalyzer.h"

#include <juce_dsp/juce_dsp.h>

namespace
{
    struct Keyword
    {
        const char* token;       // matched against lowercase filename tokens
        const char* instrument;
        const char* group;
        double      confidence;
        bool        substringOk; // also match inside compounds ("kickdrum")
    };

    // Order matters: more specific entries first. Token match beats substring
    // match ("bass" must not fire on "brass" — tokens only for short words).
    const Keyword kKeywords[] =
    {
        // Drums
        { "kick",      "Kick",      "Drums",   0.95, true  },
        { "bd",        "Kick",      "Drums",   0.80, false },
        { "snare",     "Snare",     "Drums",   0.95, true  },
        { "sn",        "Snare",     "Drums",   0.70, false },
        { "hihat",     "HiHat",     "Drums",   0.95, true  },
        { "hat",       "HiHat",     "Drums",   0.85, false },
        { "hh",        "HiHat",     "Drums",   0.75, false },
        { "tom",       "Toms",      "Drums",   0.85, false },
        { "cymbal",    "Cymbals",   "Drums",   0.90, true  },
        { "ride",      "Ride",      "Drums",   0.85, false },
        { "crash",     "Crash",     "Drums",   0.85, false },
        { "overhead",  "Overheads", "Drums",   0.95, true  },
        { "oh",        "Overheads", "Drums",   0.65, false },
        { "clap",      "Claps",     "Drums",   0.90, true  },
        { "perc",      "Percussion","Drums",   0.90, true  },
        { "drum",      "Drums",     "Drums",   0.90, true  },
        { "schlagzeug","Drums",     "Drums",   0.95, true  },
        { "shaker",    "Shaker",    "Drums",   0.90, true  },
        { "tamb",      "Tambourine","Drums",   0.85, true  },

        // Bass (incl. 808/sub conventions)
        { "bass",      "Bass",      "Bass",    0.95, false },
        { "808",       "808",       "Bass",    0.90, false },
        { "sub",       "Sub",       "Bass",    0.75, false },

        // Guitars
        { "guitar",    "Guitar",    "Guitars", 0.95, true  },
        { "gitarre",   "Guitar",    "Guitars", 0.95, true  },
        { "gtr",       "Guitar",    "Guitars", 0.90, true  },
        { "git",       "Guitar",    "Guitars", 0.65, false },
        { "akustik",   "Ac. Guitar","Guitars", 0.75, true  },
        { "acoustic",  "Ac. Guitar","Guitars", 0.75, true  },

        // Vocals
        { "vocal",     "Vocals",    "Vocals",  0.95, true  },
        { "vox",       "Vocals",    "Vocals",  0.95, true  },
        { "voc",       "Vocals",    "Vocals",  0.80, false },
        { "voice",     "Vocals",    "Vocals",  0.90, true  },
        { "gesang",    "Vocals",    "Vocals",  0.95, true  },
        { "stimme",    "Vocals",    "Vocals",  0.90, true  },
        { "bgv",       "BGVs",      "Vocals",  0.90, false },
        { "backing",   "BGVs",      "Vocals",  0.80, true  },
        { "adlib",     "Adlibs",    "Vocals",  0.90, true  },
        { "choir",     "Choir",     "Vocals",  0.90, true  },
        { "chor",      "Choir",     "Vocals",  0.85, false },
        { "rap",       "Vocals",    "Vocals",  0.80, false },
        // "lead" alone is ambiguous (lead vox vs lead synth) — low confidence
        { "lead",      "Lead",      "Vocals",  0.50, false },

        // Keys / synths
        { "piano",     "Piano",     "Keys",    0.95, true  },
        { "klavier",   "Piano",     "Keys",    0.95, true  },
        { "keys",      "Keys",      "Keys",    0.90, false },
        { "rhodes",    "Rhodes",    "Keys",    0.95, true  },
        { "organ",     "Organ",     "Keys",    0.90, true  },
        { "orgel",     "Organ",     "Keys",    0.90, true  },
        { "synth",     "Synth",     "Keys",    0.90, true  },
        { "pad",       "Pad",       "Keys",    0.75, false },
        { "pluck",     "Pluck",     "Keys",    0.85, true  },
        { "arp",       "Arp",       "Keys",    0.70, false },
        { "ep",        "E-Piano",   "Keys",    0.55, false },

        // Other melodic/orchestral — preserved label, generic group
        { "strings",   "Strings",   "Other",   0.90, true  },
        { "violin",    "Strings",   "Other",   0.90, true  },
        { "cello",     "Cello",     "Other",   0.90, true  },
        { "brass",     "Brass",     "Other",   0.90, true  },
        { "horn",      "Horns",     "Other",   0.85, true  },
        { "sax",       "Sax",       "Other",   0.90, true  },
        { "trumpet",   "Trumpet",   "Other",   0.90, true  },
        { "flute",     "Flute",     "Other",   0.85, true  },

        // FX / ambience
        { "fx",        "FX",        "FX",      0.85, false },
        { "riser",     "Riser",     "FX",      0.90, true  },
        { "sweep",     "Sweep",     "FX",      0.85, true  },
        { "impact",    "Impact",    "FX",      0.85, true  },
        { "noise",     "Noise",     "FX",      0.75, true  },
        { "atmo",      "Ambience",  "FX",      0.80, true  },
        { "ambience",  "Ambience",  "FX",      0.85, true  },
        { "ambient",   "Ambience",  "FX",      0.75, true  },
    };

    juce::StringArray tokenise (const juce::String& name)
    {
        juce::StringArray tokens;
        juce::String current;

        for (auto c : name.toLowerCase())
        {
            if (juce::CharacterFunctions::isLetterOrDigit (c))
                current << c;
            else
            {
                if (current.isNotEmpty()) tokens.add (current);
                current.clear();
            }
        }
        if (current.isNotEmpty()) tokens.add (current);

        // Split letter/digit boundaries too ("kick2" → "kick","2")
        juce::StringArray out;
        for (const auto& t : tokens)
        {
            juce::String run;
            bool runIsDigit = false;
            for (auto c : t)
            {
                const bool isDigit = juce::CharacterFunctions::isDigit (c);
                if (run.isNotEmpty() && isDigit != runIsDigit)
                {
                    out.add (run);
                    run.clear();
                }
                run << c;
                runIsDigit = isDigit;
            }
            if (run.isNotEmpty()) out.add (run);
        }
        return out;
    }
}

//==============================================================================
bool StemAnalyzer::classifyFromName (const juce::String& name,
                                     juce::String& instrumentOut,
                                     juce::String& groupOut,
                                     double& confidenceOut)
{
    const auto tokens = tokenise (name);
    const auto lower  = name.toLowerCase();

    const Keyword* best = nullptr;

    for (const auto& kw : kKeywords)
    {
        const juce::String kwStr (kw.token);

        // Exact token, or token-prefix for compounds ("bassline" → "bass") —
        // prefix only for keywords ≥3 chars so "ep" can't fire on "epic",
        // and length-capped so unrelated long words don't match.
        bool tokenHit = tokens.contains (kwStr);
        if (! tokenHit && kwStr.length() >= 3)
            for (const auto& t : tokens)
                if (t.startsWith (kwStr) && t.length() <= kwStr.length() + 4)
                    { tokenHit = true; break; }

        const bool substringHit = kw.substringOk && lower.contains (kw.token);

        if ((tokenHit || substringHit)
            && (best == nullptr || kw.confidence > best->confidence))
            best = &kw;
    }

    if (best == nullptr)
        return false;

    instrumentOut = best->instrument;
    groupOut      = best->group;
    confidenceOut = best->confidence;
    return true;
}

//==============================================================================
void StemAnalyzer::classifyFromAudio (const float* mono, int numSamples, double sampleRate,
                                      juce::String& instrumentOut,
                                      juce::String& groupOut,
                                      double& confidenceOut)
{
    instrumentOut = "Audio";
    groupOut      = "Other";
    confidenceOut = 0.2;

    if (mono == nullptr || sampleRate <= 0.0 || numSamples < (int) sampleRate / 2)
        return;

    // --- Onset rate: RMS envelope in 10 ms hops, count rising jumps over the median.
    const int hop = juce::jmax (1, (int) (0.010 * sampleRate));
    std::vector<float> env;
    env.reserve ((size_t) (numSamples / hop) + 1);

    for (int start = 0; start + hop <= numSamples; start += hop)
    {
        double sum = 0.0;
        for (int i = 0; i < hop; ++i)
        {
            const double v = mono[start + i];
            sum += v * v;
        }
        env.push_back ((float) std::sqrt (sum / hop));
    }

    if (env.size() < 8)
        return;

    auto sorted = env;
    std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    const float median = juce::jmax (1.0e-6f, sorted[sorted.size() / 2]);

    int onsets = 0;
    bool above = false;
    for (size_t i = 1; i < env.size(); ++i)
    {
        const bool hit = env[i] > 2.0f * median && env[i] > env[i - 1] * 1.5f;
        if (hit && ! above)
            ++onsets;
        above = env[i] > 2.0f * median;
    }
    const double seconds   = numSamples / sampleRate;
    const double onsetRate = onsets / seconds;

    // --- Spectral centroid: average a few 4096-point FFTs across the file.
    constexpr int fftOrder = 12;
    constexpr int fftSize  = 1 << fftOrder;             // 4096
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
        juce::dsp::WindowingFunction<float>::hann);

    double centroidSum = 0.0;
    int    centroidCount = 0;
    std::vector<float> fftBuf ((size_t) fftSize * 2);

    const int numWindows = juce::jlimit (1, 8, numSamples / fftSize);
    const int stride     = juce::jmax (fftSize, numSamples / numWindows);

    for (int w = 0; w < numWindows; ++w)
    {
        const int start = w * stride;
        if (start + fftSize > numSamples)
            break;

        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        std::copy (mono + start, mono + start + fftSize, fftBuf.begin());
        window.multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());

        double num = 0.0, den = 0.0;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const double mag  = fftBuf[(size_t) bin];
            const double freq = bin * sampleRate / fftSize;
            num += mag * freq;
            den += mag;
        }
        if (den > 1.0e-9)
        {
            centroidSum += num / den;
            ++centroidCount;
        }
    }

    if (centroidCount == 0)
        return;

    const double centroid = centroidSum / centroidCount;

    // --- Conservative rules (better "Other" than wrong — user corrects in preview):
    if (onsetRate > 1.5 && centroid > 1200.0)
    {
        instrumentOut = "Drums";
        groupOut      = "Drums";
        confidenceOut = 0.6;
    }
    else if (centroid < 350.0)
    {
        instrumentOut = "Bass";
        groupOut      = "Bass";
        confidenceOut = 0.6;
    }
}

//==============================================================================
StemInfo StemAnalyzer::analyzeFile (const juce::File& file)
{
    StemInfo info;
    info.file       = file;
    info.instrument = file.getFileNameWithoutExtension();
    info.group      = "Other";

    // Tier 1 — filename
    juce::String instr, group;
    double conf = 0.0;
    const bool named = classifyFromName (file.getFileNameWithoutExtension(), instr, group, conf);
    if (named)
    {
        info.instrument   = instr;
        info.group        = group;
        info.confidence   = conf;
        info.fromFilename = true;
    }

    // Level stats always (gain staging, AI-03) + Tier 2 when the name said nothing.
    juce::AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return info;

    // Analysis window: first 30 s are plenty for features and level estimates.
    const int numToRead = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                         (juce::int64) (30.0 * reader->sampleRate));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, numToRead);
    reader->read (&buf, 0, numToRead, 0, true, true);

    // Mono mixdown
    juce::AudioBuffer<float> mono (1, numToRead);
    mono.clear();
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buf, ch, 0, numToRead, 1.0f / (float) buf.getNumChannels());

    const float peak = mono.getMagnitude (0, 0, numToRead);
    const float rms  = mono.getRMSLevel (0, 0, numToRead);
    info.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
    info.rmsDb  = juce::Decibels::gainToDecibels (rms,  -120.0f);

    if (! named)
        classifyFromAudio (mono.getReadPointer (0), numToRead, reader->sampleRate,
                           info.instrument, info.group, info.confidence);

    return info;
}

//==============================================================================
juce::Array<StemInfo> StemAnalyzer::analyzeFiles (const juce::Array<juce::File>& files)
{
    juce::Array<StemInfo> out;
    for (const auto& f : files)
        out.add (analyzeFile (f));
    return out;
}
