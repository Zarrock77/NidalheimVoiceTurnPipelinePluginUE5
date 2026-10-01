#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include <atomic>

/** Single game-thread producer, single miniaudio consumer. Owns PCM until played.
 * Reply boundaries preserve FIFO order even when several replies arrive ahead of playback.
 * Reset invalidates queued audio without stopping the device or racing its consumer.
 */
class FNidalheimSpeechPlaybackBuffer
{
public:
    static constexpr int32 SampleRate = 24000;
    static constexpr int32 PrebufferFrames = SampleRate / 5;
    static constexpr int64 MaxQueuedFrames = SampleRate * 120;
    static constexpr int32 MaxQueuedChunks = 2048;

    struct FStats
    {
        int64 ReceivedFrames;
        int64 PlayedFrames;
        int64 CancelledFrames;
        int64 QueuedFrames;
        int64 RebufferFrames;
        int64 Underruns;
        int64 CompletedReplies;
    };

    // Producer only. A legacy stream without start/end messages flushes after 500 ms idle.
    void BeginSpeech(bool bExplicitEnd = true);
    bool Enqueue(TArray<uint8>&& PCM, double NowSeconds);
    void EndSpeech();
    void Reset();
    FStats GetStats() const;

    // Consumer only. Always initializes the whole output; never logs or touches UObjects.
    void Render(void* Output, uint32 FrameCount, double NowSeconds);

private:
    struct FSpeech
    {
        uint64 Epoch = 0;
        bool bExplicitEnd = true;
        std::atomic<int64> BufferedFrames{0};
        std::atomic<double> LastDataAt{0.0};
        std::atomic<bool> Complete{false};
    };
    struct FChunk
    {
        TSharedPtr<FSpeech, ESPMode::ThreadSafe> Speech;
        TArray<uint8> PCM;
    };

    TQueue<FChunk, EQueueMode::Spsc> Chunks;
    TSharedPtr<FSpeech, ESPMode::ThreadSafe> ProducerSpeech;
    // The following state is owned exclusively by Render.
    TSharedPtr<FSpeech, ESPMode::ThreadSafe> ActiveSpeech;
    FChunk CurrentChunk;
    int32 ByteOffset = 0;
    bool bPlaying = false;
    bool bStarted = false;

    std::atomic<uint64> Epoch{1};
    std::atomic<int64> PendingFrames{0};
    std::atomic<int32> PendingChunks{0};
    std::atomic<int64> Received{0};
    std::atomic<int64> Played{0};
    std::atomic<int64> Cancelled{0};
    std::atomic<int64> Rebuffered{0};
    std::atomic<int64> UnderrunCount{0};
    std::atomic<int64> Completed{0};

    void DiscardCurrentChunk();
};
