#include "NidalheimSpeechPlaybackBuffer.h"

void FNidalheimSpeechPlaybackBuffer::BeginSpeech(bool bExplicitEnd)
{
    EndSpeech();
    ProducerSpeech = MakeShared<FSpeech, ESPMode::ThreadSafe>();
    ProducerSpeech->Epoch = Epoch.load();
    ProducerSpeech->bExplicitEnd = bExplicitEnd;
}

bool FNidalheimSpeechPlaybackBuffer::Enqueue(TArray<uint8>&& PCM, double NowSeconds)
{
    if (PCM.Num() % sizeof(int16) != 0) return false;
    const int64 Frames = PCM.Num() / sizeof(int16);
    if (Frames == 0) return true;
    // Fail the response explicitly at the caller instead of deleting parts of words.
    if (Frames > MaxQueuedFrames - PendingFrames.load() || PendingChunks.load() >= MaxQueuedChunks)
        return false;

    if (!ProducerSpeech || ProducerSpeech->Complete.load() ||
        (!ProducerSpeech->bExplicitEnd && NowSeconds - ProducerSpeech->LastDataAt.load() >= 0.5))
        BeginSpeech(false);

    FChunk Chunk;
    Chunk.Speech = ProducerSpeech;
    Chunk.PCM = MoveTemp(PCM);
    ProducerSpeech->LastDataAt.store(NowSeconds);
    ProducerSpeech->BufferedFrames.fetch_add(Frames);
    PendingFrames.fetch_add(Frames);
    PendingChunks.fetch_add(1);
    Received.fetch_add(Frames);
    Chunks.Enqueue(MoveTemp(Chunk));
    return true;
}

void FNidalheimSpeechPlaybackBuffer::EndSpeech()
{
    if (ProducerSpeech) ProducerSpeech->Complete.store(true);
}

void FNidalheimSpeechPlaybackBuffer::Reset()
{
    Epoch.fetch_add(1);
    EndSpeech();
    ProducerSpeech.Reset();
}

FNidalheimSpeechPlaybackBuffer::FStats FNidalheimSpeechPlaybackBuffer::GetStats() const
{
    return {Received.load(), Played.load(), Cancelled.load(), PendingFrames.load(),
        Rebuffered.load(), UnderrunCount.load(), Completed.load()};
}

void FNidalheimSpeechPlaybackBuffer::DiscardCurrentChunk()
{
    const int64 Remaining = (CurrentChunk.PCM.Num() - ByteOffset) / sizeof(int16);
    if (Remaining > 0)
    {
        CurrentChunk.Speech->BufferedFrames.fetch_sub(Remaining);
        PendingFrames.fetch_sub(Remaining);
        Cancelled.fetch_add(Remaining);
    }
    CurrentChunk = FChunk{};
    ByteOffset = 0;
}

void FNidalheimSpeechPlaybackBuffer::Render(void* Output, uint32 FrameCount, double NowSeconds)
{
    if (!Output || FrameCount == 0) return;
    FMemory::Memzero(Output, SIZE_T(FrameCount) * sizeof(int16));
    uint8* Out = static_cast<uint8*>(Output);
    uint32 Remaining = FrameCount;
    int32 Dequeued = 0;

    while (Remaining > 0)
    {
        const uint64 CurrentEpoch = Epoch.load();
        if (ActiveSpeech && ActiveSpeech->Epoch != CurrentEpoch)
        {
            DiscardCurrentChunk();
            ActiveSpeech.Reset();
            bPlaying = bStarted = false;
        }

        if (ActiveSpeech && !ActiveSpeech->bExplicitEnd &&
            NowSeconds - ActiveSpeech->LastDataAt.load() >= 0.5)
            ActiveSpeech->Complete.store(true);

        if (ActiveSpeech && ActiveSpeech->Complete.load() && ActiveSpeech->BufferedFrames.load() == 0)
        {
            Completed.fetch_add(1);
            ActiveSpeech.Reset();
            bPlaying = bStarted = false;
        }

        if (ByteOffset == CurrentChunk.PCM.Num())
        {
            CurrentChunk = FChunk{};
            ByteOffset = 0;
            // Bound work during a cancellation with a large backlog.
            if (++Dequeued > 64) return;
            if (!Chunks.Dequeue(CurrentChunk))
            {
                if (ActiveSpeech && !ActiveSpeech->Complete.load() && bStarted)
                {
                    if (bPlaying) UnderrunCount.fetch_add(1);
                    bPlaying = false;
                    Rebuffered.fetch_add(Remaining);
                }
                return;
            }
            PendingChunks.fetch_sub(1);
            if (CurrentChunk.Speech->Epoch != CurrentEpoch)
            {
                DiscardCurrentChunk();
                continue;
            }
            if (ActiveSpeech != CurrentChunk.Speech)
            {
                ActiveSpeech = CurrentChunk.Speech;
                bPlaying = bStarted = false;
            }
        }

        if (!bPlaying)
        {
            if (ActiveSpeech->BufferedFrames.load() < PrebufferFrames && !ActiveSpeech->Complete.load())
            {
                if (bStarted) Rebuffered.fetch_add(Remaining);
                return;
            }
            bPlaying = bStarted = true;
        }

        const uint32 Available = (CurrentChunk.PCM.Num() - ByteOffset) / sizeof(int16);
        const uint32 Count = FMath::Min(Remaining, Available);
        const uint32 Bytes = Count * sizeof(int16);
        FMemory::Memcpy(Out, CurrentChunk.PCM.GetData() + ByteOffset, Bytes);
        ByteOffset += Bytes;
        Out += Bytes;
        Remaining -= Count;
        ActiveSpeech->BufferedFrames.fetch_sub(Count);
        PendingFrames.fetch_sub(Count);
        Played.fetch_add(Count);
    }
}
