#include "SessionPaths.h"

namespace aizana::sessionpaths
{

juce::File sessionFileForChosenPath (const juce::File& chosen)
{
    if (chosen == juce::File())
        return {};

    const auto stem   = sessionStemFrom (chosen);
    const auto parent = chosen.getParentDirectory();

    // Schon im eigenen Ordner? Dann bleibt alles, wie es ist.
    if (parent.getFileName() == stem)
        return parent.getChildFile (stem + sessionExtension());

    return parent.getChildFile (stem).getChildFile (stem + sessionExtension());
}

juce::File sessionFolder (const juce::File& sessionFile)
{
    return sessionFile.getParentDirectory();
}

juce::File mediaFolder (const juce::File& sessionFile)
{
    return sessionFolder (sessionFile)
             .getChildFile (sessionStemFrom (sessionFile) + " Audio");
}

juce::File backupFolder (const juce::File& sessionFile)
{
    return sessionFolder (sessionFile).getChildFile (backupFolderName());
}

juce::String backupFileName (const juce::File& sessionFile, int slot)
{
    return sessionStemFrom (sessionFile)
             + "_Sicherung_" + juce::String (slot) + ".tracktionedit";
}

juce::File recoveryFile (const juce::File& sessionFile)
{
    return backupFolder (sessionFile)
             .getChildFile (sessionStemFrom (sessionFile) + "_Wiederherstellung.tracktionedit");
}

juce::String sessionStemFrom (const juce::File& file)
{
    auto stem = file.getFileNameWithoutExtension();

    // Alte und neue Sicherungs-Endungen wieder abschneiden — auch mehrfach, weil
    // die Kette real bis `_autosave_1_autosave_2` gewachsen ist.
    for (;;)
    {
        const auto before = stem;

        for (auto* marker : { "_autosave_", "_Sicherung_" })
        {
            const auto idx = stem.lastIndexOf (marker);

            if (idx > 0 && stem.substring (idx + (int) juce::String (marker).length())
                               .containsOnly ("0123456789"))
                stem = stem.substring (0, idx);
        }

        if (stem.endsWith ("_Wiederherstellung"))
            stem = stem.dropLastCharacters ((int) juce::String ("_Wiederherstellung").length());

        if (stem == before)
            return stem;
    }
}

} // namespace aizana::sessionpaths
