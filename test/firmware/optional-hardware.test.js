const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const config = fs.readFileSync('src/config/Config.h', 'utf8');
const controller = fs.readFileSync('src/core/MainController.cpp', 'utf8');
const audio = fs.readFileSync('src/audio/AudioProcessor.cpp', 'utf8');

test('firmware defaults to LED-only hardware without touching TFT or I2S', () => {
  assert.match(config, /#define GG_HAS_DISPLAY\s+0/);
  assert.match(config, /#define GG_HAS_MICROPHONE\s+0/);
  assert.doesNotMatch(controller, /MainController::MainController[\s\S]*?tft\.init\(\)/);
  assert.match(controller, /#if GG_HAS_DISPLAY[\s\S]*?displayManager = new DisplayManager/);
  assert.match(audio, /#if GG_HAS_MICROPHONE[\s\S]*?i2s_read/);
  assert.match(audio, /Microphone disabled; skipping I2S initialization[\s\S]*?submitSamples\(nullptr, 0\)/);
  assert.match(controller, /#if !GG_HAS_MICROPHONE[\s\S]*?audioFeatures\.level = GG_IDLE_VISUAL_LEVEL/);
});
