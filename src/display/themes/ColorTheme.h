#pragma once

#include <stdint.h>
#include <TFT_eSPI.h>
#include "../../config/Config.h"

// Define the
struct WidgetColorTheme {
    // General theme colors
    uint16_t primary;
    uint16_t secondary;
    uint16_t text;
    uint16_t bg;
    uint16_t border;

    // Widget-specific fill colors
    uint16_t barFill;
    uint16_t barBg;

    // Audio feature-based colors
    uint16_t bassColor;
    uint16_t midColor;
    uint16_t trebleColor;
    uint16_t powerColor;
    uint16_t waveformColor;
};

// Cyberpunk palette with audio-feature mapping
static const WidgetColorTheme CyberpunkTheme = {
    .primary = TFT_MAGENTA,
    .secondary = TFT_PURPLE,
    .text = TFT_YELLOW,
    .bg = TFT_BLACK,
    .border = TFT_DARKGREY,
    .barFill = TFT_PINK,
    .barBg = TFT_DARKGREY,

    .bassColor = TFT_BLUE,
    .midColor = TFT_GREEN,
    .trebleColor = TFT_YELLOW,
    .powerColor = TFT_RED,
    .waveformColor = TFT_CYAN
};

// Blade Runner style (just an example, tweak as needed)
static const WidgetColorTheme BladeRunnerTheme = {
    .primary = TFT_ORANGE,
    .secondary = TFT_SKYBLUE,
    .text = TFT_WHITE,
    .bg = TFT_BLACK,
    .border = TFT_BLUE,
    .barFill = TFT_ORANGE,
    .barBg = TFT_DARKGREY,

    .bassColor = TFT_SKYBLUE,
    .midColor = TFT_ORANGE,
    .trebleColor = TFT_PURPLE,
    .powerColor = TFT_RED,
    .waveformColor = TFT_WHITE
};

// Function to get theme based on config
const WidgetColorTheme& getTheme();