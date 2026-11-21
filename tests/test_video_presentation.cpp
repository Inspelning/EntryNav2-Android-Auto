// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Actual EvoVideo lifecycle with fake hardware boundaries. No DRM, compositor or vehicle connection.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "evo_video.hpp"

static unsigned height = 0, opacityCommands = 0, decodeFailures = 0;
static uint32_t decoded = 0;  // length of the last buffer handed to the decoder
static bool visible = false;
static float opacity = 1.f;
static bool decoderHoldsAll = false;  // a stalled decoder: no buffer ever comes back
static uint8_t bufferMemory[256];
namespace evo {
// One buffer, always free: the fake decoder never holds it, unless it stalled.
DmaBufPool::DmaBufPool() : drmFd_(-1), dev_(0) {}
DmaBufPool::~DmaBufPool() {}
bool DmaBufPool::init(unsigned, uint32_t) {
    Buf b = {nullptr, 7, bufferMemory, sizeof(bufferMemory), false};
    bufs_.assign(1, b);
    return true;
}
void DmaBufPool::destroy() {
    bufs_.clear();
}
int DmaBufPool::acquireFree() {
    return bufs_.empty() || decoderHoldsAll ? -1 : 0;
}
int DmaBufPool::find(int fd) {
    return !bufs_.empty() && fd == 7 ? 0 : -1;
}
EvoIlm::EvoIlm() {}
bool EvoIlm::validateTarget(uint32_t, uint32_t, uint32_t, uint32_t, bool) {
    return true;
}
bool EvoIlm::attachSurface(uint32_t, uint32_t, uint32_t, uint32_t h) {
    height = h;
    opacity = 1.f;
    visible = false;
    return true;
}
bool EvoIlm::detachSurface(uint32_t, uint32_t) {
    return true;
}
bool EvoIlm::setSurfaceVisibility(uint32_t, bool v) {
    visible = v;
    return true;
}
bool EvoIlm::setSurfaceOpacity(uint32_t, float v) {
    ++opacityCommands;
    opacity = v;
    return true;
}
}  // namespace evo
extern "C" {
xsv_handle xsv_open(int, uint32_t, const char*) {
    return reinterpret_cast<void*>(1);
}
void xsv_close(xsv_handle) {}
int xsv_configure_stream(xsv_handle, uint32_t, uint32_t, float) {
    return 0;
}
int xsv_set_output_dimension(xsv_handle, uint32_t, uint32_t) {
    return 0;
}
int xsv_enable_frame_events(xsv_handle) {
    return 0;
}
int xsv_update(xsv_handle) {
    return 0;
}
int xsv_start(xsv_handle) {
    return 0;
}
int xsv_stop(xsv_handle) {
    return 0;
}
int xsv_decode_video_frame(xsv_handle, int fd, uint32_t len) {
    assert(fd == 7);
    decoded = len;
    if (decodeFailures) {
        --decodeFailures;
        return -1;
    }
    return 0;
}
int xsv_receive_event(xsv_handle, int*, int) {
    usleep(1000);
    return 0;
}
}

static const uint8_t kSpsPps[] = {0, 0, 0, 1, 0x67, 1, 2, 3, 0, 0, 0, 1, 0x68, 4};
static const uint8_t kIdr[] = {0, 0, 0, 1, 0x65, 9, 9, 9, 9};
static const uint8_t kSlice[] = {0, 0, 0, 1, 0x41, 5, 5};

// The decoder received SPS/PPS followed by the IDR frame.
static bool configThenIdr() {
    return decoded == sizeof(kSpsPps) + sizeof(kIdr) && !memcmp(bufferMemory, kSpsPps, sizeof(kSpsPps)) &&
           !memcmp(bufferMemory + sizeof(kSpsPps), kIdr, sizeof(kIdr));
}

static long long nowMs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<long long>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

// When a decoder that keeps every buffer counts as stalled, on a clock of our own.
static void decoderStallRule() {
    const uint32_t limit = evo::DecoderStall::kLimitMs;
    evo::DecoderStall stall;
    // On screen: reported once, when no buffer was free for the whole limit.
    const uint32_t t0 = 0xfffffc00u;  // across the clock's wrap-around
    assert(!stall.starved(t0, true) && !stall.stuck(t0));
    assert(!stall.starved(t0 + limit - 1, true) && !stall.stuck(t0 + limit - 1));
    assert(stall.starved(t0 + limit, true) && stall.stuck(t0 + limit));
    assert(!stall.starved(t0 + limit + 500, true) && stall.stuck(t0 + limit + 500));  // once
    stall.bufferBack();
    assert(!stall.stuck(t0 + limit + 600));
    assert(!stall.starved(t0 + limit + 700, true) && !stall.starved(t0 + 3 * limit, true));  // still once
    // A buffer back before the limit starts the count again.
    stall.restart();
    assert(!stall.starved(1000, true) && !stall.starved(1000 + limit - 1, true));
    stall.bufferBack();
    assert(!stall.starved(1000 + limit, true) && !stall.stuck(1000 + limit + limit - 1));
    assert(stall.starved(1000 + limit + limit, true));
    // Hidden: never counted, and the count starts when the picture is shown again.
    stall.restart();
    assert(!stall.starved(0, false) && !stall.starved(2 * limit, false) && !stall.stuck(2 * limit));
    assert(!stall.starved(2 * limit + 1, true) && !stall.starved(3 * limit, true));
    assert(!stall.starved(3 * limit + 1, false) && !stall.starved(3 * limit + 2, true));
    assert(!stall.starved(4 * limit + 1, true) && stall.starved(4 * limit + 2, true));
}

// libxsvideo gives no buffer back: once the limit has passed on screen, the frame says so once, and the frames after
// it no longer wait 20 ms each for a buffer. A new start() feeds a decoder again.
static void stalledDecoder(evo::EvoVideo& video, const evo::EvoVideoConfig& cfg) {
    typedef evo::FrameFate Fate;
    assert(video.init(cfg) && video.start());
    video.setVisible(true);
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kDecoding);
    decoderHoldsAll = true;
    const long long since = nowMs();
    Fate fate = Fate::kLost;
    unsigned dropped = 0;
    while (fate == Fate::kLost && nowMs() - since < evo::DecoderStall::kLimitMs + 2000) {
        fate = video.postVideoFrame(kSlice, sizeof(kSlice), 0);
        ++dropped;
    }
    const long long lasted = nowMs() - since;
    assert(fate == Fate::kStalled && lasted >= evo::DecoderStall::kLimitMs && dropped > 1);
    const long long after = nowMs();
    for (int i = 0; i < 10; ++i) {
        assert(video.postVideoFrame(kSlice, sizeof(kSlice), 0) == Fate::kLost);  // reported once
    }
    assert(nowMs() - after < 50);  // 10 frames, no 20 ms wait each
    video.stop();
    decoderHoldsAll = false;
    assert(video.init(cfg) && video.start());
    video.setVisible(true);
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kDecoding);
    video.stop();
}

int main() {
    typedef evo::FrameFate Fate;
    evo::EvoVideoConfig cfg;
    evo::EvoVideo video;
    assert(!video.isReady());
    assert(video.init(cfg) && video.start() && video.isReady() && height == 480);
    video.setVisible(true);
    assert(video.isVisible() && visible);
    assert(video.setOpacity(0.25f) && opacity == 0.25f);
    const unsigned n = opacityCommands;
    assert(video.setOpacity(0.25f) && opacityCommands == n);  // no repeated command on each main-loop tick
    assert(video.setOpacity(1.f) && opacity == 1.f);
    video.setVisible(false);  // camera/menu always hides the whole AA surface
    assert(!visible && !video.isVisible());
    video.setVisible(true);
    assert(visible);

    // SPS/PPS go in front of the first IDR; a frame the decoder refuses keeps them for the next IDR.
    video.postCodecConfig(kSpsPps, sizeof(kSpsPps));
    assert(video.postVideoFrame(kSlice, sizeof(kSlice), 0) == Fate::kDecoding && decoded == sizeof(kSlice));
    decodeFailures = 1;
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kLost && configThenIdr());
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kDecoding && configThenIdr());
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kDecoding && decoded == sizeof(kIdr));  // sent once

    video.stop();
    assert(!visible && !video.isVisible() && !video.isReady());
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kNoStream);  // stopped: nothing lost
    assert(video.init(cfg) && video.start() && video.isReady());
    assert(opacity == 1.f && video.setOpacity(0.25f) && opacity == 0.25f);  // fresh surface, fresh cache
    assert(video.postVideoFrame(kIdr, sizeof(kIdr), 0) == Fate::kDecoding && configThenIdr());  // SPS/PPS again
    video.stop();
    puts("PASS video presentation: visibility, opacity cache, camera hide, SPS/PPS after a refused IDR, reconnect");

    decoderStallRule();
    stalledDecoder(video, cfg);
    puts("PASS video presentation: a decoder that keeps every buffer 3 s on screen is reported once, then no waiting");
}
