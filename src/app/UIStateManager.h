#pragma once

#include <juce_gui_extra/juce_gui_extra.h>   // juce::ChangeBroadcaster (+ String, leak macro)

// Forward-declare TE's Engine to keep this header light (matches TE's own
// `inline namespace engine` — a plain `namespace engine` would be an ODR mismatch).
namespace tracktion { inline namespace engine { class Engine; } }
namespace te = tracktion::engine;

/**
 * UIStateManager — "Das Dial" (Phase 2, Block A backbone).
 *
 * Cloudes is not a DAW with a beginner mode; it is an Intent→Mechanik engine whose
 * *prominence is a dial*. This class IS that dial — the single source of truth for:
 *
 *   - UIMode        : Beginner vs Pro. Picks the *default* assist prominence.
 *   - assistEnabled : whether AI assist is prominent. Independently overridable in
 *                     BOTH modes ("ein Dial, zwei Defaults"). Switching mode applies
 *                     that mode's default (Beginner = on, Pro = off).
 *   - WorkContext   : runtime "what is the user focused on" hint for Block B's
 *                     IntentEngine / HeuristicFeedbackEngine. Granularity intentionally
 *                     minimal for now — extend as Block B's needs become concrete.
 *
 * Persistence: UIMode and assistEnabled survive restarts via the engine's
 * PropertyStorage PropertiesFile (the idiomatic slot for app-specific keys — the
 * SettingID enum is a closed TE set). WorkContext is runtime-only, never persisted.
 *
 * Threading: message-thread only. Notifies the UI via juce::ChangeBroadcaster; a
 * single coalesced change message means "re-read me" (listeners re-query getters).
 */
class UIStateManager : public juce::ChangeBroadcaster
{
public:
    enum class UIMode { Beginner, Pro };

    /** Minimal runtime context. Block B fleshes this out; -1 = none / master. */
    struct WorkContext
    {
        int focusedTrackIndex = -1;
    };

    /** Reads any persisted mode/assist from the engine's PropertyStorage on construction. */
    explicit UIStateManager (te::Engine& engine);
    ~UIStateManager() override = default;

    // --- The dial: mode ---
    UIMode getMode() const noexcept { return mode; }

    /** Switch the dial. Changing mode applies that mode's *default* assist prominence
     *  and persists. A call with the current mode is a no-op (does NOT reset an
     *  in-mode assist override). Broadcasts only on an actual change. */
    void setMode (UIMode newMode);

    /** Flip Beginner <-> Pro. */
    void toggleMode();

    // --- The dial: assist prominence (independent override within a mode) ---
    bool isAssistEnabled() const noexcept { return assistEnabled; }

    /** Override assist prominence within the current mode. Persists. Broadcasts on change. */
    void setAssistEnabled (bool shouldBeEnabled);

    void toggleAssist();

    // --- Work context (runtime only, not persisted) ---
    const WorkContext& getWorkContext() const noexcept { return workContext; }

    /** Set the track the user is currently focused on (-1 = none). Broadcasts on change. */
    void setFocusedTrackIndex (int trackIndex);

    /** The default assist prominence for a mode: Beginner = on, Pro = off. */
    static bool defaultAssistFor (UIMode m) noexcept { return m == UIMode::Beginner; }

    // --- First-run onboarding gate (Phase 2, Block C) ---
    bool isOnboardingCompleted() const noexcept { return onboardingCompleted; }

    /** Mark the first-run wizard as done. Persists so it never shows again. */
    void setOnboardingCompleted (bool completed);

private:
    void persist();
    void loadFromStorage();

    te::Engine& engine;
    UIMode      mode          { UIMode::Beginner };
    bool        assistEnabled { true };            // = defaultAssistFor(Beginner)
    bool        onboardingCompleted { false };
    WorkContext workContext;

    // PropertiesFile keys (app-specific; isolated from TE's SettingID namespace).
    static constexpr const char* kModeKey       = "cloudes_ui_mode";        // "beginner" | "pro"
    static constexpr const char* kAssistKey     = "cloudes_assist_enabled"; // bool
    static constexpr const char* kOnboardingKey = "cloudes_onboarding_done";// bool

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UIStateManager)
};
