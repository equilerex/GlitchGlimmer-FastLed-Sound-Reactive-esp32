#define THEME        "CYBERPUNK"       // Sampling rate in Hz


// ==== I2S Audio Configuration ====
#define I2S_PORT        I2S_NUM_0
#define I2S_WS          26  // Word Select (LRCL)
#define I2S_SD          32  // Serial Data in (DOUT from mic)
#define I2S_SCK         27  // Bit Clock (BCLK)

// ==== Sampling ====
#define SAMPLE_RATE     44100        // Standard audio sampling rate
#define NUM_SAMPLES     512          // Must be a power of 2 (used by FFT)
#define CHANNEL_COUNT   1            // Mono input
#define BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_32BIT

// ==== Gain / Normalization ====
#define NOISE_THRESHOLD     0.02f    // Minimum input level before considered real signal
#define MAX_AUDIO_LEVEL     1.0f     // Normalized max range after scaling
#define GAIN_SMOOTHING      0.92f    // Smoothing for gain level
#define LOUDNESS_SMOOTHING  0.9f     // Smoothing for loudness calculation

// The curve applied to level when it drives pixel brightness, as an exponent.
// 1.0 is linear, 0.5 is a square root.
//
// PWM duty is linear in emitted light and the eye is not, so level mapped straight
// to duty reads much darker than its number suggests. Measured against the
// microphone in use, level sits near 0.16 for most of a track, which is 16 percent
// duty, and the strips read as black between peaks while every check passed.
//
// This lifts only the low end: it passes through 0 and 1 and changes neither. Not
// a floor, because a floor lifts silence too and silence has to stay dark for the
// signal gate to mean anything.
//
// Applies to brightness written as a scale on CRGB. Brightness written through
// CHSV's val needs AudioFeatures::hsvLevel instead, because FastLED squares that
// field again on the way out and the two would not match. Which curve a given
// animation wants is decided by which of the two it writes, not by preference.
#define BRIGHTNESS_GAMMA    0.5f

// The envelope of the input that level is measured from, as one-pole
// coefficients: the fraction of the gap closed per analysed block, about 86
// blocks a second.
//
// level answers "how loud is this room right now", which is a property of the
// surroundings and not of the beat. Measured from a single block's RMS it answers
// a different question: a kick is one block, so the reading jumped on every hit
// and level behaved as a second beat detector. The envelope is what separates the
// two.
//
// Both coefficients are several times slower than they first were. At the block
// rate 0.02 is a time constant near half a second and 0.006 near two seconds. The
// previous pair, 0.05 and 0.02, followed the syllable rather than the room. The
// gap between two words is a few hundred milliseconds, which the old release
// crossed most of the way, so the reading fell toward zero between words and
// climbed back to full on the next one, and that is the whole of the reported
// swing from zero to a hundred in a tenth of a second. A room's loudness does not
// change at that rate and the reading no longer claims it does. Asymmetric
// because a room that gets louder should be visible promptly while one that gets
// quieter can take a moment.
#define LEVEL_ENV_ATTACK    0.02f
#define LEVEL_ENV_RELEASE   0.006f

// How fast the level's reference forgets, per block. The reference is the loudest
// recent thing the envelope has reached, and level is a fraction of it, which is
// what lets a quiet passage read as quiet instead of climbing back to full.
//
// Slow, and much slower than it first was, because this is the term that decides
// whether level means "how loud is this room" or "how long since something loud
// happened". At 0.9995 it holds for about twenty seconds, so a quiet stretch stays
// quiet while it lasts. The counterweight is the noise floor, which climbs toward
// the signal over the same stretch and pulls the reading down further, so a room
// that stays quiet settles low rather than drifting up to full.
#define LEVEL_REF_DECAY     0.9995f

// How fast that reference rises onto a louder envelope, per block. It used to rise
// instantly, which made the loudest envelope value of the last twenty seconds the
// reference for all of them: one cough or door set the denominator and everything
// after it read dim for as long as the decay lasted. At 0.05 it takes a sustained
// passage to move the reference, which is what "the loudest recent thing" should
// mean.
#define LEVEL_REF_RISE      0.05f

// How fast the silence gate opens and closes, per block. It opened at 0.12, which
// is the spectrum gone inside a tenth of a second, and level multiplied by the
// same ramp fell off a cliff at the end of every phrase. That is the gate's doing
// rather than the envelope's, so slowing the envelope alone would not have fixed
// the reported swing. 0.04 is about a third of a second.
#define GATE_RAMP           0.04f

// dynamics is the span the loudness covers while its window is open, as a fraction
// of the top of that span. The two coefficients are the rates at which the
// remembered top and bottom of the envelope follow it: fast when the envelope goes
// beyond them, slow when it retreats, so the window is the last few seconds rather
// than the last block.
#define DYN_EDGE_RISE       0.5f
#define DYN_EDGE_FALL       0.002f

// ==== FFT Configuration ====
#define FFT_SMOOTHING       0.8f     // Spectral smoothing for more stable bars
#define FFT_BANDS           16       // Number of bands for visualization/spectrum

// ==== Beat Detection ====
// How far a block has to rise above the one before it to count as a beat, as a
// fraction of that block. A fraction rather than an amount, because the amount is
// a claim about one microphone's gain and the firmware runs at several. The block
// before it is a single 11.6 ms window, which is the right reference for an onset:
// a kick is an edge at that timescale, and anything averaging over more than a few
// blocks is already carrying the beat it is supposed to be detecting.
#define BEAT_RISE_FRACTION  0.35f
// Below that fraction the previous block is small enough that the fraction is a
// few counts of the ADC, so a quiet room would pass it on noise alone. This floor
// is a multiple of the measured noise floor rather than a fixed number, for the
// same reason the fraction is relative.
#define BEAT_RISE_NOISE     4.0f
// Milliseconds between beats. Caps the detector at 240 BPM.
#define MIN_BEAT_INTERVAL   250
// How much the bass band's energy has to rise over the block before it for the
// rise to count as a beat. The level rise above is cleared by any onset, and
// speech is almost nothing but onsets: a syllable starts on a consonant, the block
// before it was quieter, so the detector fired on every word and reported 140 to
// 200 with no beat anywhere in the room. What a beat has that a word does not is
// low end. Bass here is everything under 200 Hz, where a kick lives and where
// speech carries almost nothing, so requiring that band to move is what separates
// a rhythm from someone talking. A ratio to the previous block, so it holds at any
// gain, and measured on the band's raw magnitude rather than its share, because a
// block that is mostly bass already has a share at the top of its range and a
// share cannot rise.
#define BEAT_BASS_RISE      1.15f
// How many inter-beat intervals the tempo is the median of. One interval moves the
// readout by tens of BPM when a beat lands a block early, and a missed beat halves
// it, so the tempo comes from several and the outliers are discarded. Twelve is
// about six seconds of a 120 BPM track, which is several bars rather than one.
#define BEAT_BPM_WINDOW     12
// The tempo readout after the beats stop, in milliseconds. It holds its last value
// for the hold and then fades from it with the fade as the time constant. A
// function of the clock rather than a per-block multiplier, which is what the 0.94
// a block it replaces was: that made the speed of the fade depend on the loop rate
// and took 103 BPM to zero inside a second once the two second hold had passed,
// which reads as a plunge rather than as a fade. The hold doubles as the longest
// gap that still counts as one more interval of the same tempo, since a gap longer
// than it is a tempo already being faded.
#define TEMPO_HOLD_MS       2000
#define TEMPO_FADE_MS       4000.0f

// ==== Noise Floor ====
// A block may raise the noise floor only if its spectrum is noise-like, measured as
// spectral flatness: the geometric mean of the magnitudes over their arithmetic
// mean, which is near 1 for white noise and near 0 for a tone. It is the one thing
// that separates a room from a track, because level does not. A loud room and a
// loud compressed track are the same number, and that is how the floor came to
// climb onto the music: every block cleared it, so it rose on nearly every block,
// and at the end of a forty second track it sat at 89 percent of the signal. Both
// multiples of it then failed, 2.5 for the silence gate and 4.0 for the beat
// threshold, so the gate shut and the tempo went deaf together.
//
// Room tone and a cymbal wash and heavy distortion all sit above this, so a
// broadband mix still raises the floor. The rise is slow and bounded for that case,
// and NOISE_FLOOR_MAX is the bound.
#define NOISE_FLAT_MIN   0.25f
// The highest the floor may reach. This bound is what keeps a track that reads as
// noise-like from ratcheting the gate's threshold above the signal: without it the
// floor climbs for as long as the track plays, the gate shuts on the first quiet
// passage after it, and the strip goes black for the rest of the track. That is the
// defect the flatness test exists to fix, arriving by a longer route.
//
// The value is set from the ratio the gate has to work in, which on this
// microphone is about 0.006 RMS for a room against 0.02 to 0.03 for music. A floor
// at 0.004 puts the gate's open threshold at 0.011 and the beat threshold's floor
// term at 0.016, both under anything this microphone reports for music, so neither
// can go deaf by construction. The cost is the benign direction: a room louder than
// 0.004 is not tracked, so the gate stays open on it and the strip animates to room
// noise. Animated noise is a worse-looking installation than a correct one and a far
// better one than a black strip, and the ratio is thin enough on this input that the
// margin has to be spent somewhere.
#define NOISE_FLOOR_MAX  0.004f

// ==== Display ====
#define DEFAULT_BRIGHTNESS  150



// ==== LED ====
#define LED_0_PIN            25
#define LED_1_PIN            33
// #define LED_2_PIN            25
// #define LED_3_PIN            25
// #define LED_4_PIN            25
// #define LED_5_PIN            25
// #define LED_6_PIN            25
// #define LED_7_PIN            25
// #define LED_8_PIN            25
// #define LED_9_PIN            25


#define LED_0_NUM 100
#define LED_1_NUM 10
#define LED_2_NUM 0
#define LED_3_NUM 0
#define LED_4_NUM 0
#define LED_5_NUM 0
#define LED_6_NUM 0
#define LED_7_NUM 0
#define LED_8_NUM 0
#define LED_9_NUM 0





//cant put this in the array, needs to be defined on compile
#define MIN_SWITCH_INTERVAL 10000





// ==== ENCODER ====
#define ENCODER_PIN_A      39
#define ENCODER_PIN_B      38
#define ENCODER_BTN_PIN    17

// ==== BUTTON ====
#define BUTTON_PIN_1         0
#define BUTTON_PIN_2         35

// ==== OTHER ====
#define ENABLE_WEB_UI      false        // Enable/disable WebUI
#define DEBUG_ENABLED      true        // Toggle debug logging

// ==== DISPLAY ====
#define DISPLAY_WIDTH      240
#define DISPLAY_HEIGHT     135
#define DISPLAY_PIN         4

// ==== VISUALIZATION ====
#define FFT_MAX_SCALE      50.0        // Scale factor for normalizing FFT bars
#define BAR_HEIGHT_MAX     30          // Max height for bass/mid/treble bars

// ==== DEBUG CONFIG ====
#define DEBUG_ENABLED      true        // Master debug switch
#define DEBUG_LEVEL       2           // 0=ERROR, 1=INFO, 2=DEBUG
#define DEBUG_BAUDRATE    115200      // Debug serial baudrate

// Fallback for std::make_unique if not available
#if __cplusplus < 201402L
#include <memory>
#endif
