#include "ClipGainEnvelope.h"

using TimePos = tracktion::core::TimePosition;

te::VolumeAndPanPlugin* ClipGainEnvelope::find (te::WaveAudioClip& clip)
{
    if (auto* list = clip.getPluginList())
        for (auto* p : list->getPlugins())
            if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*> (p))
                return vp;

    return nullptr;
}

te::VolumeAndPanPlugin* ClipGainEnvelope::getOrCreate (te::WaveAudioClip& clip)
{
    if (auto* existing = find (clip))
        return existing;

    auto* list = clip.getPluginList();
    if (list == nullptr)
        return nullptr;

    auto plugin = clip.edit.getPluginCache().createNewPlugin (
        te::VolumeAndPanPlugin::xmlTypeName, {});
    list->insertPlugin (plugin, -1, nullptr);

    return dynamic_cast<te::VolumeAndPanPlugin*> (plugin.get());
}

te::AutomatableParameter* ClipGainEnvelope::param (te::WaveAudioClip& clip)
{
    if (auto* vp = find (clip))
        return vp->volParam.get();

    return nullptr;
}

bool ClipGainEnvelope::hasEnvelope (te::WaveAudioClip& clip)
{
    if (auto* p = param (clip))
        return p->getCurve().getNumPoints() > 0;

    return false;
}

int ClipGainEnvelope::numPoints (te::WaveAudioClip& clip)
{
    if (auto* p = param (clip))
        return p->getCurve().getNumPoints();

    return 0;
}

double ClipGainEnvelope::pointTime (te::WaveAudioClip& clip, int index)
{
    if (auto* p = param (clip))
        if (index >= 0 && index < p->getCurve().getNumPoints())
            return p->getCurve().getPointTime (index).inSeconds();

    return 0.0;
}

float ClipGainEnvelope::pointDb (te::WaveAudioClip& clip, int index)
{
    if (auto* p = param (clip))
        if (index >= 0 && index < p->getCurve().getNumPoints())
            return te::volumeFaderPositionToDB (p->getCurve().getPointValue (index));

    return 0.0f;
}

int ClipGainEnvelope::addPoint (te::WaveAudioClip& clip, double editTimeSecs, float gainDb)
{
    auto* vp = getOrCreate (clip);
    if (vp == nullptr || vp->volParam == nullptr)
        return -1;

    const float pos = te::decibelsToVolumeFaderPosition (
        juce::jlimit (kMinDb, kMaxDb, gainDb));

    return vp->volParam->getCurve().addPoint (TimePos::fromSeconds (editTimeSecs),
                                              pos, 0.0f /* linear */);
}

int ClipGainEnvelope::movePoint (te::WaveAudioClip& clip, int index,
                                 double editTimeSecs, float gainDb)
{
    auto* p = param (clip);
    if (p == nullptr || index < 0 || index >= p->getCurve().getNumPoints())
        return index;

    const float pos = te::decibelsToVolumeFaderPosition (
        juce::jlimit (kMinDb, kMaxDb, gainDb));

    return p->getCurve().movePoint (index, TimePos::fromSeconds (editTimeSecs),
                                    pos, true);
}

void ClipGainEnvelope::removePoint (te::WaveAudioClip& clip, int index)
{
    if (auto* p = param (clip))
        if (index >= 0 && index < p->getCurve().getNumPoints())
            p->getCurve().removePoint (index);
}

void ClipGainEnvelope::clear (te::WaveAudioClip& clip)
{
    if (auto* vp = find (clip))
    {
        if (vp->volParam != nullptr)
            vp->volParam->getCurve().clear();

        vp->removeFromParent();
    }
}

void ClipGainEnvelope::shift (te::WaveAudioClip& clip, double deltaSecs)
{
    auto* p = param (clip);
    if (p == nullptr || deltaSecs == 0.0)
        return;

    auto& curve = p->getCurve();
    const int n = curve.getNumPoints();
    if (n == 0)
        return;

    // Read out, clear, re-add shifted — point order is preserved because the
    // whole set moves by the same delta.
    juce::Array<double> times;
    juce::Array<float>  values;
    for (int i = 0; i < n; ++i)
    {
        times.add (curve.getPointTime (i).inSeconds() + deltaSecs);
        values.add (curve.getPointValue (i));
    }

    curve.clear();
    for (int i = 0; i < n; ++i)
        curve.addPoint (TimePos::fromSeconds (times[i]), values[i], 0.0f);
}

void ClipGainEnvelope::copyFrom (te::WaveAudioClip& dest, te::WaveAudioClip& src)
{
    auto* srcParam = param (src);
    if (srcParam == nullptr || srcParam->getCurve().getNumPoints() == 0)
        return;

    auto* vp = getOrCreate (dest);
    if (vp == nullptr || vp->volParam == nullptr)
        return;

    auto& from = srcParam->getCurve();
    auto& to   = vp->volParam->getCurve();
    to.clear();

    for (int i = 0; i < from.getNumPoints(); ++i)
        to.addPoint (from.getPointTime (i), from.getPointValue (i), 0.0f);
}

float ClipGainEnvelope::gainDbAt (te::WaveAudioClip& clip, double editTimeSecs)
{
    auto* p = param (clip);
    if (p == nullptr || p->getCurve().getNumPoints() == 0)
        return 0.0f;

    return te::volumeFaderPositionToDB (
        p->getCurve().getValueAt (TimePos::fromSeconds (editTimeSecs)));
}
