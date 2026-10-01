#include "NidalheimVoiceSpatializer.h"

void FNidalheimVoiceSpatializer::Compute(const FVector& Source, const FVector& ListenerLocation, const FVector& ListenerRight,
    float MinDistance, float MaxDistance, float Rolloff, float& OutGain, float& OutPan)
{
    MinDistance = FMath::Max(MinDistance, 0.f);
    MaxDistance = FMath::Max(MaxDistance, MinDistance + 1.f);

    const FVector ToSource = Source - ListenerLocation;
    const float Distance = static_cast<float>(ToSource.Size());

    if (Distance <= MinDistance) OutGain = 1.f;
    else if (Distance >= MaxDistance) OutGain = 0.f;
    else OutGain = FMath::Pow((MaxDistance - Distance) / (MaxDistance - MinDistance), FMath::Max(Rolloff, 0.01f));

    // Direction needs a meaningful distance; right on top of the listener the voice stays centered,
    // and it eases in across the near zone so walking past does not whip the sound side to side.
    if (Distance < KINDA_SMALL_NUMBER)
    {
        OutPan = 0.f;
        return;
    }
    const float Side = static_cast<float>(FVector::DotProduct(ToSource / Distance, ListenerRight.GetSafeNormal()));
    const float Near = MinDistance > 0.f ? FMath::Clamp(Distance / MinDistance, 0.f, 1.f) : 1.f;
    OutPan = FMath::Clamp(Side * Near * MaxPan, -MaxPan, MaxPan);
}

void FNidalheimVoiceSpatializer::SetTarget(float Gain, float Pan)
{
    TargetGain.store(FMath::Clamp(Gain, 0.f, 1.f));
    TargetPan.store(FMath::Clamp(Pan, -1.f, 1.f));
}

void FNidalheimVoiceSpatializer::MixToStereo(const int16* Mono, int16* Stereo, uint32 Frames)
{
    if (Frames == 0) return;

    const float EndGain = TargetGain.load();
    const float EndPan = TargetPan.load();
    if (!bSnapped)
    {
        // First block of the device: start where we should be instead of fading in from center.
        CurrentGain = EndGain;
        CurrentPan = EndPan;
        bSnapped = true;
    }

    const float GainStep = (EndGain - CurrentGain) / static_cast<float>(Frames);
    const float PanStep = (EndPan - CurrentPan) / static_cast<float>(Frames);
    float Gain = CurrentGain;
    float Pan = CurrentPan;

    for (uint32 i = 0; i < Frames; ++i)
    {
        Gain += GainStep;
        Pan += PanStep;
        // Balance law: the near ear keeps full level, the far one fades. Center is unity on both
        // channels, so a non-spatialized voice is exactly as loud as the old mono output.
        const float Left = Gain * FMath::Min(1.f, 1.f - Pan);
        const float Right = Gain * FMath::Min(1.f, 1.f + Pan);
        const float Sample = static_cast<float>(Mono[i]);
        Stereo[2 * i] = static_cast<int16>(FMath::Clamp(Sample * Left, -32768.f, 32767.f));
        Stereo[2 * i + 1] = static_cast<int16>(FMath::Clamp(Sample * Right, -32768.f, 32767.f));
    }

    CurrentGain = EndGain;
    CurrentPan = EndPan;
}
