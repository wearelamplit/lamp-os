#include "./fade_out.hpp"

#include "../components/network/wifi.hpp"
#include "../util/color.hpp"
#include "../util/fade.hpp"

namespace lamp {
void FadeOutBehavior::draw() {
  for (int i = 0; i < fb->pixelCount; i++) {
    fb->buffer[i] = fade(fb->buffer[i], Color(0, 0, 0, 0), frames - 1, frame);
  }

  nextFrame();
};

void FadeOutBehavior::control() {
  reboot = wifi->requiresReboot;

  if (animationState == STOPPED && reboot) {
    playOnce();
  }
  if (reboot & isLastFrame()) {
    ESP.restart();
  }
};

void FadeOutBehavior::setWifiComponent(WifiComponent* inWifi) {
  wifi = inWifi;
};
}  // namespace lamp