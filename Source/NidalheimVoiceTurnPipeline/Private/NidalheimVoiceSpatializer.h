#pragma once

#include "CoreMinimal.h"
#include <atomic>

/**
 * Places the (mono) NPC voice in the stereo field: distance attenuation plus left/right balance.
 *
 * Deliberately not routed through the engine AudioMixer, whose latency is what the miniaudio
 * playback path exists to avoid. The game thread publishes a target gain and balance; the audio
 * thread applies them with a ramp across each block, so a 30 Hz position update never clicks.
 */
class FNidalheimVoiceSpatializer
{
public:
    /** Largest balance applied: never fully silence one ear, a voice at 90 degrees stays natural. */
    static constexpr float MaxPan = 0.8f;

    /**
     * Gain (0..1) and balance (-1 left .. +1 right) of a voice heard at `ListenerLocation`.
     * Full volume inside `MinDistance`, silent past `MaxDistance`, `pow(t, Rolloff)` in between
     * where t goes 1 to 0 across that range. Distances are in Unreal units (cm).
     */
    static void Compute(const FVector& Source, const FVector& ListenerLocation, const FVector& ListenerRight,
        float MinDistance, float MaxDistance, float Rolloff, float& OutGain, float& OutPan);

    /** Game thread. The next block ramps towards these values. */
    void SetTarget(float Gain, float Pan);

    /** Audio thread only. Writes `Frames` interleaved stereo frames from `Frames` mono frames. */
    void MixToStereo(const int16* Mono, int16* Stereo, uint32 Frames);

private:
    std::atomic<float> TargetGain{1.f};
    std::atomic<float> TargetPan{0.f};
    // Owned by the audio thread.
    float CurrentGain = 1.f;
    float CurrentPan = 0.f;
    bool bSnapped = false;
};
