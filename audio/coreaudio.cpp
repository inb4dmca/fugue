#ifdef __APPLE__
#include "audio/sink.h"
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace fugue::audio {
namespace {
bool checked(OSStatus status, std::string* err) {
    if (status == noErr) return true;
    if (err) *err = "Audio Queue error " + std::to_string(status);
    return false;
}

// Three ~23 ms buffers bound latency and memory. The output thread owns the queue; the Audio Queue callback only
// returns buffers. A stalled/disconnected device times out rather than hanging Player's shutdown forever.
class CoreAudioSink final : public Sink {
public:
    ~CoreAudioSink() override { close(); }
    bool open(int rate, std::string* err) {
        if (rate <= 0) { if (err) *err = "invalid sample rate"; return false; }
        AudioStreamBasicDescription format{};
        format.mSampleRate = rate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
        format.mBytesPerPacket = format.mBytesPerFrame = 2 * sizeof(float);
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = 2;
        format.mBitsPerChannel = 8 * sizeof(float);
        free_.reserve(3);
        if (!checked(AudioQueueNewOutput(&format, returned, this, nullptr, nullptr, 0, &queue_), err)) return false;
        for (int i = 0; i < 3; ++i) {
            AudioQueueBufferRef buffer = nullptr;
            if (!checked(AudioQueueAllocateBuffer(queue_, kFrames * 2 * sizeof(float), &buffer), err)) return false;
            free_.push_back(buffer);
        }
        return true;
    }
    bool write(const float* data, size_t frames, std::string* err) override {
        if (!queue_) { if (err) *err = "closed"; return false; }
        while (frames) {
            AudioQueueBufferRef buffer;
            {
                std::unique_lock lock(mutex_);
                if (!ready_.wait_for(lock, std::chrono::milliseconds(500), [&] { return !free_.empty(); })) {
                    if (err) *err = "audio device stalled";
                    return false;
                }
                buffer = free_.back();
                free_.pop_back();
            }
            size_t n = std::min(frames, kFrames);
            buffer->mAudioDataByteSize = UInt32(n * 2 * sizeof(float));
            std::memcpy(buffer->mAudioData, data, buffer->mAudioDataByteSize);
            if (!checked(AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr), err)) return false;
            if (!started_) {
                if (!checked(AudioQueueStart(queue_, nullptr), err)) return false;
                started_ = true;
            }
            frames -= n;
            data += n * 2;
        }
        return true;
    }
    void close() override {
        // Dispose synchronously, without the mutex: an in-flight callback may need it before disposal can finish.
        if (queue_) { AudioQueueDispose(queue_, true); queue_ = nullptr; }
        free_.clear();
        started_ = false;
    }
    std::string name() const override { return "coreaudio"; }
private:
    static void returned(void* self, AudioQueueRef, AudioQueueBufferRef buffer) {
        auto& sink = *static_cast<CoreAudioSink*>(self);
        std::lock_guard lock(sink.mutex_);
        sink.free_.push_back(buffer);
        sink.ready_.notify_one();
    }
    static constexpr size_t kFrames = 1024;
    AudioQueueRef queue_ = nullptr;
    bool started_ = false;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<AudioQueueBufferRef> free_;
};
}  // namespace

std::unique_ptr<Sink> openCoreAudioSink(int rate, std::string* err) {
    auto sink = std::make_unique<CoreAudioSink>();
    if (!sink->open(rate, err)) return nullptr;
    return sink;
}
}  // namespace fugue::audio
#endif
