// Linux: writes frames to a v4l2loopback device (/dev/video10 by default).
// Requires: sudo modprobe v4l2loopback devices=1 video_nr=10 card_label="PeerCam"
// Frames are converted from RGBA to YUYV (YUY2) before writing.

#include <napi.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

static int g_fd = -1;

static inline void rgba_to_yuyv(const uint8_t* rgba, uint8_t* yuyv,
                                  uint32_t width, uint32_t height) {
  for (uint32_t i = 0; i < width * height; i += 2) {
    const uint8_t* p0 = rgba + i * 4;
    const uint8_t* p1 = rgba + (i + 1) * 4;
    uint8_t y0 = (uint8_t)(0.299f * p0[0] + 0.587f * p0[1] + 0.114f * p0[2]);
    uint8_t y1 = (uint8_t)(0.299f * p1[0] + 0.587f * p1[1] + 0.114f * p1[2]);
    uint8_t u  = (uint8_t)(128 - 0.168736f * p0[0] - 0.331264f * p0[1] + 0.5f * p0[2]);
    uint8_t v  = (uint8_t)(128 + 0.5f * p0[0] - 0.418688f * p0[1] - 0.081312f * p0[2]);
    *yuyv++ = y0; *yuyv++ = u; *yuyv++ = y1; *yuyv++ = v;
  }
}

Napi::Value Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  const char* dev = "/dev/video10";
  g_fd = open(dev, O_WRONLY);
  if (g_fd < 0) return Napi::Boolean::New(env, false);
  return Napi::Boolean::New(env, true);
}

void Stop(const Napi::CallbackInfo&) {
  if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}

Napi::Value PushFrame(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (g_fd < 0) return env.Undefined();

  uint32_t width  = info[0].As<Napi::Number>().Uint32Value();
  uint32_t height = info[1].As<Napi::Number>().Uint32Value();
  Napi::Buffer<uint8_t> buf = info[2].As<Napi::Buffer<uint8_t>>();

  size_t yuyvSize = static_cast<size_t>(width) * height * 2;
  uint8_t* yuyv = new uint8_t[yuyvSize];
  rgba_to_yuyv(buf.Data(), yuyv, width, height);
  write(g_fd, yuyv, yuyvSize);
  delete[] yuyv;
  return env.Undefined();
}
