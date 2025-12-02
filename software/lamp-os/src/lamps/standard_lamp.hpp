#pragma once

#include <Adafruit_NeoPixel.h>

#define LAMP_SHADE_PIN 12
#define LAMP_SHADE_LED_CONFIG NEO_GRBW + NEO_KHZ800
#define LAMP_BASE_PIN 14
#define LAMP_BASE_LED_CONFIG NEO_GRBW + NEO_KHZ800
#define LAMP_MAX_BRIGHTNESS 180
#define LAMP_MAX_STRIP_PIXELS_SHADE 38
#define LAMP_MAX_STRIP_PIXELS_BASE 50

void setup();
void loop();

/**
 * - Initialize all of the lamps behaviors
 * - Initialize the animation compositor
 */
void initBehaviors();

/**
 * ArtNet DMX actions shared between the base and shade
 */
void handleArtnet();

/**
 * Handle wifi mode swaps when a stage router is present
 */
void handleStageMode();

/**
 * Whole lamp changes from the configuration tool
 */
void handleWebSocket();
