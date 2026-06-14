#include "StemAnalyzer.h"

#include <juce_dsp/juce_dsp.h>
#include <thread>

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
        // Drums — kick
        { "kick",      "Kick",      "Drums",   0.95, true  },
        { "kik",       "Kick",      "Drums",   0.85, false },
        { "bassdrum",  "Kick",      "Drums",   0.95, true  },
        { "bd",        "Kick",      "Drums",   0.80, false },
        // Drums — snare / rim
        { "snare",     "Snare",     "Drums",   0.95, true  },
        { "snr",       "Snare",     "Drums",   0.85, false },
        { "sn",        "Snare",     "Drums",   0.70, false },
        { "rimshot",   "Rimshot",   "Drums",   0.90, true  },
        { "rim",       "Rim",       "Drums",   0.75, false },
        { "sidestick", "Sidestick", "Drums",   0.90, true  },
        { "ghost",     "Ghost Snare","Drums",  0.65, false },
        // Drums — hihat (open/closed/pedal + abbreviations)
        { "hihat",     "HiHat",     "Drums",   0.95, true  },
        { "hat",       "HiHat",     "Drums",   0.85, false },
        { "hh",        "HiHat",     "Drums",   0.75, false },
        { "ohh",       "Open Hat",  "Drums",   0.85, false },
        { "chh",       "Closed Hat","Drums",   0.85, false },
        // Drums — toms
        { "tom",       "Toms",      "Drums",   0.85, false },
        { "ft",        "Floor Tom", "Drums",   0.60, false },
        // Drums — cymbals
        { "cymbal",    "Cymbals",   "Drums",   0.90, true  },
        { "ride",      "Ride",      "Drums",   0.85, false },
        { "crash",     "Crash",     "Drums",   0.85, false },
        { "splash",    "Splash",    "Drums",   0.88, true  },
        { "china",     "China",     "Drums",   0.80, false },
        // Drums — overheads / room
        { "overhead",  "Overheads", "Drums",   0.95, true  },
        { "oh",        "Overheads", "Drums",   0.65, false },
        // Drums — claps / snaps
        { "clap",      "Claps",     "Drums",   0.90, true  },
        { "snap",      "Snaps",     "Drums",   0.85, true  },
        { "finger",    "Finger Snaps","Drums", 0.70, true  },
        // Drums — percussion family
        { "perc",      "Percussion","Drums",   0.90, true  },
        { "conga",     "Congas",    "Drums",   0.90, true  },
        { "bongo",     "Bongos",    "Drums",   0.90, true  },
        { "timbale",   "Timbales",  "Drums",   0.88, true  },
        { "cowbell",   "Cowbell",   "Drums",   0.90, true  },
        { "clave",     "Clave",     "Drums",   0.80, true  },
        { "triangle",  "Triangle",  "Drums",   0.85, true  },
        { "woodblock", "Woodblock", "Drums",   0.88, true  },
        { "cabasa",    "Cabasa",    "Drums",   0.88, true  },
        { "djembe",    "Djembe",    "Drums",   0.90, true  },
        { "cajon",     "Cajon",     "Drums",   0.90, true  },
        { "tabla",     "Tabla",     "Drums",   0.88, true  },
        { "shaker",    "Shaker",    "Drums",   0.90, true  },
        { "tamb",      "Tambourine","Drums",   0.85, true  },
        // Drums — generic / loops
        { "drum",      "Drums",     "Drums",   0.90, true  },
        { "schlagzeug","Drums",     "Drums",   0.95, true  },
        { "drumloop",  "Drum Loop", "Drums",   0.85, true  },
        { "toploop",   "Top Loop",  "Drums",   0.85, true  },
        { "drumfill",  "Drum Fill", "Drums",   0.85, true  },

        // Bass (incl. 808/sub conventions)
        { "bass",      "Bass",      "Bass",    0.95, false },
        { "808",       "808",       "Bass",    0.90, false },
        { "subbass",   "Sub Bass",  "Bass",    0.92, true  },
        { "sub",       "Sub",       "Bass",    0.75, false },
        { "reese",     "Reese Bass","Bass",    0.85, true  },
        { "upright",   "Upright Bass","Bass",  0.70, true  },
        { "contrabass","Contrabass","Bass",    0.85, true  },

        // Guitars
        { "guitar",    "Guitar",    "Guitars", 0.95, true  },
        { "gitarre",   "Guitar",    "Guitars", 0.95, true  },
        { "gtr",       "Guitar",    "Guitars", 0.90, true  },
        { "git",       "Guitar",    "Guitars", 0.65, false },
        { "akustik",   "Ac. Guitar","Guitars", 0.75, true  },
        { "acoustic",  "Ac. Guitar","Guitars", 0.75, true  },
        { "strat",     "E-Guitar",  "Guitars", 0.85, true  },
        { "tele",      "E-Guitar",  "Guitars", 0.75, false },
        { "lespaul",   "E-Guitar",  "Guitars", 0.85, true  },
        { "riff",      "Guitar Riff","Guitars",0.60, false },
        { "nylon",     "Nylon Guitar","Guitars",0.70, true },
        { "banjo",     "Banjo",     "Guitars", 0.85, true  },
        { "mandolin",  "Mandolin",  "Guitars", 0.85, true  },
        { "ukulele",   "Ukulele",   "Guitars", 0.88, true  },
        { "uke",       "Ukulele",   "Guitars", 0.60, false },

        // Vocals
        { "vocal",     "Vocals",    "Vocals",  0.95, true  },
        { "vox",       "Vocals",    "Vocals",  0.95, true  },
        { "voc",       "Vocals",    "Vocals",  0.80, false },
        { "voice",     "Vocals",    "Vocals",  0.90, true  },
        { "gesang",    "Vocals",    "Vocals",  0.95, true  },
        { "stimme",    "Vocals",    "Vocals",  0.90, true  },
        { "bgv",       "BGVs",      "Vocals",  0.90, false },
        { "bvox",      "BGVs",      "Vocals",  0.85, false },
        { "backing",   "BGVs",      "Vocals",  0.80, true  },   // backing/backings (substr)
        { "harmony",   "Harmony",   "Vocals",  0.88, true  },   // harmony/harmonies (substr)
        { "harm",      "Harmony",   "Vocals",  0.65, false },
        { "double",    "Doubles",   "Vocals",  0.78, true  },   // doubles (substr); "double bass" → bass 0.95 wins
        { "dbl",       "Doubles",   "Vocals",  0.80, false },
        { "adlib",     "Adlibs",    "Vocals",  0.90, true  },   // adlib/adlibs/adlibz (substr)
        { "shout",     "Shouts",    "Vocals",  0.85, true  },   // shout/shoutout/shoutoutz (substr)
        { "shoutout",  "Shouts",    "Vocals",  0.90, true  },
        { "shotz",     "Shouts",    "Vocals",  0.80, false },
        { "chant",     "Chants",    "Vocals",  0.80, true  },
        { "gang",      "Gang",      "Vocals",  0.65, false },
        { "hook",      "Hook",      "Vocals",  0.70, false },
        { "choir",     "Choir",     "Vocals",  0.90, true  },
        { "chor",      "Choir",     "Vocals",  0.85, false },
        { "rap",       "Vocals",    "Vocals",  0.80, false },
        { "lvox",      "Lead",      "Vocals",  0.90, false },
        { "leadvox",   "Lead",      "Vocals",  0.92, true  },
        // "lead" alone is ambiguous (lead vox vs lead synth) — low confidence
        { "lead",      "Lead",      "Vocals",  0.50, false },

        // Keys (acoustic/electric keyboard family)
        { "piano",     "Piano",     "Keys",    0.95, true  },
        { "klavier",   "Piano",     "Keys",    0.95, true  },
        { "keys",      "Keys",      "Keys",    0.90, false },
        { "rhodes",    "Rhodes",    "Keys",    0.95, true  },
        { "wurli",     "Wurlitzer", "Keys",    0.90, true  },
        { "wurlitzer", "Wurlitzer", "Keys",    0.92, true  },
        { "organ",     "Organ",     "Keys",    0.90, true  },
        { "orgel",     "Organ",     "Keys",    0.90, true  },
        { "hammond",   "Organ",     "Keys",    0.90, true  },
        { "clavinet",  "Clavinet",  "Keys",    0.90, true  },
        { "clav",      "Clavinet",  "Keys",    0.70, false },
        { "harpsichord","Harpsichord","Keys",  0.90, true  },
        { "celesta",   "Celesta",   "Keys",    0.88, true  },
        { "ep",        "E-Piano",   "Keys",    0.55, false },

        // Synths — own group (user feedback 2026-06-12: 30-stem real test)
        { "synth",     "Synth",     "Synths",  0.90, true  },
        { "supersaw",  "Supersaw",  "Synths",  0.88, true  },
        { "pad",       "Pad",       "Synths",  0.75, false },
        { "pluck",     "Pluck",     "Synths",  0.85, true  },
        { "arp",       "Arp",       "Synths",  0.70, false },
        { "stab",      "Synth Stab","Synths",  0.75, false },
        { "saw",       "Synth",     "Synths",  0.60, false },
        { "wobble",    "Synth",     "Synths",  0.75, true  },
        { "seq",       "Sequence",  "Synths",  0.60, false },
        { "moog",      "Synth",     "Synths",  0.80, true  },
        { "juno",      "Synth",     "Synths",  0.80, true  },
        { "drone",     "Drone",     "Synths",  0.65, false },

        // Orchestral/winds family — own "Instruments" group (user feedback
        // 2026-06-12: "flute ist halt flute" — real instruments, no other family)
        { "strings",   "Strings",   "Instruments", 0.90, true  },
        { "violin",    "Strings",   "Instruments", 0.90, true  },
        { "viola",     "Strings",   "Instruments", 0.85, true  },
        { "cello",     "Cello",     "Instruments", 0.90, true  },
        { "brass",     "Brass",     "Instruments", 0.90, true  },
        { "horn",      "Horns",     "Instruments", 0.85, true  },
        { "trombone",  "Trombone",  "Instruments", 0.90, true  },
        { "tuba",      "Tuba",      "Instruments", 0.88, true  },
        { "sax",       "Sax",       "Instruments", 0.90, true  },
        { "trumpet",   "Trumpet",   "Instruments", 0.90, true  },
        { "clarinet",  "Clarinet",  "Instruments", 0.90, true  },
        { "oboe",      "Oboe",      "Instruments", 0.88, true  },
        { "bassoon",   "Bassoon",   "Instruments", 0.88, true  },
        { "flute",     "Flute",     "Instruments", 0.85, true  },
        { "floete",    "Flute",     "Instruments", 0.85, true  },
        { "harmonica", "Harmonica", "Instruments", 0.88, true  },
        { "accordion", "Accordion", "Instruments", 0.88, true  },
        { "bells",     "Bells",     "Instruments", 0.80, true  },
        { "glock",     "Glockenspiel","Instruments",0.80, true },
        { "vibraphone","Vibraphone","Instruments", 0.90, true  },
        { "vibes",     "Vibraphone","Instruments", 0.70, false },
        { "marimba",   "Marimba",   "Instruments", 0.90, true  },
        { "xylophone", "Xylophone", "Instruments", 0.90, true  },
        { "harp",      "Harp",      "Instruments", 0.80, false },
        { "sitar",     "Sitar",     "Instruments", 0.88, true  },
        { "kalimba",   "Kalimba",   "Instruments", 0.90, true  },
        { "orchestra", "Orchestra", "Instruments", 0.85, true  },
        { "orch",      "Orchestra", "Instruments", 0.65, false },
        { "ensemble",  "Ensemble",  "Instruments", 0.70, true  },

        // FX / ambience / transitions
        { "sfx",       "SFX",       "FX",      0.88, false },
        { "fx",        "FX",        "FX",      0.85, false },
        { "riser",     "Riser",     "FX",      0.90, true  },
        { "uplifter",  "Uplifter",  "FX",      0.90, true  },
        { "downlifter","Downlifter","FX",      0.90, true  },
        { "sweep",     "Sweep",     "FX",      0.85, true  },
        { "whoosh",    "Whoosh",    "FX",      0.88, true  },
        { "impact",    "Impact",    "FX",      0.85, true  },
        { "boom",      "Boom",      "FX",      0.70, false },
        { "transition","Transition","FX",      0.85, true  },
        { "reverse",   "Reverse FX","FX",      0.80, true  },
        { "foley",     "Foley",     "FX",      0.85, true  },
        { "vinyl",     "Vinyl",     "FX",      0.75, true  },
        { "crackle",   "Crackle",   "FX",      0.80, true  },
        { "texture",   "Texture",   "FX",      0.65, true  },
        { "siren",     "Siren",     "FX",      0.80, true  },
        { "scratch",   "Scratch",   "FX",      0.75, true  },
        { "noise",     "Noise",     "FX",      0.75, true  },
        { "atmo",      "Ambience",  "FX",      0.80, true  },
        { "ambience",  "Ambience",  "FX",      0.85, true  },
        { "ambient",   "Ambience",  "FX",      0.75, true  },
        { "tag",       "Producer Tag","FX",    0.55, false },
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

    // L/R side from the name: standalone token "l"/"r" or a left/right word (DE/EN).
    StemSide detectSide (const juce::StringArray& tokens)
    {
        for (const auto& t : tokens)
        {
            if (t == "l" || t == "left"  || t == "links")  return StemSide::Left;
            if (t == "r" || t == "right" || t == "rechts") return StemSide::Right;
        }
        return StemSide::None;
    }

    // Pro vocal sub-role from the name (only meaningful once the group is "Vocals").
    // Order = specificity: a "Lead Double" is a Double, an "Adlib" is an Adlib.
    juce::String detectVocalSubRole (const juce::StringArray& tokens, const juce::String& lower)
    {
        auto has = [&] (const char* k) { return tokens.contains (k) || lower.contains (k); };

        if (has ("double") || tokens.contains ("dbl") || has ("dub"))      return "Double";
        if (has ("adlib"))                                                  return "Adlib";
        if (has ("shout") || tokens.contains ("shotz") || has ("yell"))     return "Shout";
        if (has ("harmon") || tokens.contains ("harm"))                     return "Harmony";
        if (has ("choir") || tokens.contains ("chor") || has ("chant")
            || has ("gang"))                                                return "Choir";
        if (has ("backing") || tokens.contains ("bgv") || tokens.contains ("bvox")
            || has ("bgvox"))                                               return "Backing";
        if (has ("hook"))                                                    return "Hook";
        if (has ("lead") || tokens.contains ("lvox") || has ("leadvox"))    return "Lead";
        return {};
    }
}

//==============================================================================
StemClassification StemAnalyzer::classify (const juce::String& name)
{
    StemClassification out;

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

    // L/R side is independent of the instrument and applies to anything.
    out.side = detectSide (tokens);

    if (best == nullptr)
        return out;   // matched stays false; side may still be set (rare)

    out.matched    = true;
    out.instrument = best->instrument;
    out.group      = best->group;
    out.confidence = best->confidence;

    // Sub-role refines the mix WITHIN the Vocals group (pro jargon).
    if (out.group == "Vocals")
    {
        out.subRole = detectVocalSubRole (tokens, lower);
        if (out.subRole.isEmpty())
            out.subRole = "Lead";   // a plain "Vocals"/"Vox" stem is the lead by default
    }

    return out;
}

bool StemAnalyzer::classifyFromName (const juce::String& name,
                                     juce::String& instrumentOut,
                                     juce::String& groupOut,
                                     double& confidenceOut)
{
    const auto c = classify (name);
    if (! c.matched)
        return false;

    instrumentOut = c.instrument;
    groupOut      = c.group;
    confidenceOut = c.confidence;
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

    // Tier 1 — filename (instrument + group + pro sub-role + L/R side)
    const auto c = classify (file.getFileNameWithoutExtension());
    const bool named = c.matched;
    info.side = c.side;   // side is parsed even when the instrument is unknown
    if (named)
    {
        info.instrument   = c.instrument;
        info.group        = c.group;
        info.subRole      = c.subRole;
        info.confidence   = c.confidence;
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
    // Each file analysis reads up to 30 s of audio + runs FFTs — sequential
    // analysis of 20+ stems took tens of seconds (user feedback 2026-06-12).
    // analyzeFile is self-contained (own format manager, no shared state), so
    // fan out across worker threads; each thread writes only its own slots.
    const int numFiles   = files.size();
    const int numThreads = juce::jlimit (1, 8,
        juce::jmin (numFiles, (int) std::thread::hardware_concurrency()));

    std::vector<StemInfo> results ((size_t) numFiles);

    if (numThreads <= 1)
    {
        for (int i = 0; i < numFiles; ++i)
            results[(size_t) i] = analyzeFile (files[i]);
    }
    else
    {
        std::vector<std::thread> workers;
        for (int t = 0; t < numThreads; ++t)
            workers.emplace_back ([&files, &results, t, numThreads, numFiles]()
            {
                for (int i = t; i < numFiles; i += numThreads)
                    results[(size_t) i] = analyzeFile (files[i]);
            });
        for (auto& w : workers)
            w.join();
    }

    juce::Array<StemInfo> out;
    for (auto& r : results)
        out.add (std::move (r));
    return out;
}
