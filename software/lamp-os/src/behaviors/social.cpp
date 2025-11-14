#include "./social.hpp"

#include <Arduino.h>

#include "../components/network/bluetooth.hpp"
#include "../util/color.hpp"
#include "../util/fade.hpp"

namespace lamp {
void SocialBehavior::draw() {
  for (int i = 0; i < fb->pixelCount; i++) {
    if (frame < easeFrames) {
      fb->buffer[i] = fade(fb->buffer[i], foundLampColor, easeFrames, frame);
    } else if (frame > (frames - easeFrames)) {
      fb->buffer[i] = fade(foundLampColor, fb->buffer[i], easeFrames, frame % easeFrames);
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