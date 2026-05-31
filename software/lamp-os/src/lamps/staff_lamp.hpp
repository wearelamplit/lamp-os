#pragma once
#ifndef STAFF_LAMP_H
#define STAFF_LAMP_H

#define LAMP_STOKE_PIN 19
#define LAMP_SHOUT_PIN 21
#define LAMP_TOPTOUCH_PIN 4
#define LAMP_BTMTOUCH_PIN 15

#define LAMP_SHADE_PIN 12
#define LAMP_BASE_PIN 14
#define LAMP_MAX_BRIGHTNESS 100
#define LAMP_MAX_STRIP_PIXELS_SHADE 80
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

#ifdef LAMP_MQTT_ENABLED
/**
 * MQTT smart home connection management
 */
void handleMqtt();
#endif

#endif

void stokePoke();
void moodShift();
void topTouch();
void adjustBrightness();