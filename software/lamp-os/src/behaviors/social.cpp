#include "./social.hpp"

#include <Arduino.h>

#include "../components/network/bluetooth.hpp"
#include "../util/color.hpp"
#include "../util/fade.hpp"

namespace lamp {
void SocialBehavior::draw() {
  for (int i = 0; i < fb->pixelCount; i++) {
    if (frame < easeFrames) {
      fb->buffer[i] = fade(savedBuffer[i], foundLampColor, easeFrames, frame);
    } else if (frame > (frames - easeFrames)) {
      if (isLastFrame()) {
        // On last frame, directly set to saved buffer to avoid interpolation error
        fb->buffer[i] = savedBuffer[i];
      } else {
        uint32_t step = frame - (frames - easeFrames);
        fb->buffer[i] = fade(foundLampColor, savedBuffer[i], easeFrames, step);
      }
    } else {
      fb->buffer[i] = foundLampColor;
    }
  }

  nextFrame();
};

void SocialBehavior::control() {
  foundLamps = bt->getLamps();

  if (animationState == STOPPED && millis() > nextAcknowledgeTimeMs) {
    for (std::vector<BluetoothLampRecord>::reverse_iterator revIter =
             foundLamps->rbegin();
         revIter != foundLamps->rend(); ++revIter) {
      if (!revIter->acknowledged) {
#ifdef LAMP_DEBUG
        Serial.printf("Acknowledging %s\n", revIter->name.c_str());
#endif
        revIter->acknowledged = true;
        foundLampColor = revIter->baseColor;
        nextAcknowledgeTimeMs = millis() + LAMP_TIME_BETWEEN_ACKNOWLEDGEMENT_MS;
        // Save buffer state before starting animation
        savedBuffer = fb->buffer;
        playOnce();
        break;
      }
    }
  }
};

void SocialBehavior::setBluetoothComponent(BluetoothComponent* inBt) {
  bt = inBt;
};
}  // namespace lamp