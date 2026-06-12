#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

/**
 * AudioThumbnailPool — ONE AudioFormatManager + AudioThumbnailCache for the
 * whole app (via juce::SharedResourcePointer).
 *
 * Fixes the documented anti-pattern (silent-failure catalogue: per-component
 * AudioThumbnailCache → cache thrashing): ClipComponents are recreated on every
 * rebuildTrackRows, and with a per-component cache every rebuild restarted ALL
 * waveform reads from zero — with 20+ tracks the reads never finished between
 * rebuilds and clips stayed solid-colour (user report 2026-06-12).
 *
 * With this shared cache, a finished thumbnail survives component destruction
 * keyed by its source hash, so recreated clips repaint their waveform instantly.
 *
 * Usage:  juce::SharedResourcePointer<AudioThumbnailPool> pool;
 *         juce::AudioThumbnail thumb { 512, pool->formatManager, pool->cache };
 */
struct AudioThumbnailPool
{
    AudioThumbnailPool()
    {
        formatManager.registerBasicFormats();
    }

    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache cache { 512 };   // plenty for large sessions
};
