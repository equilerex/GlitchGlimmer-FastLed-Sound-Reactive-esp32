#define THEME        "CYBERPUNK"       // Sampling rate in Hz

// Optional peripherals. Keep these disabled for an LED-only board; enable them
// when the corresponding hardware is actually fitted. These are compile-time
// switches because the TFT and I2S drivers should not be initialized at all when
// their hardware is absent.
#ifndef GG_HAS_DISPLAY
  #define GG_HAS_DISPLAY      0
#endif
#ifndef GG_HAS_MICROPHONE
  #define GG_HAS_MICROPHONE   0
#endif
#ifndef GG_IDLE_VISUAL_LEVEL
  #define GG_IDLE_VISUAL_LEVEL 0.35f
#endif


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
// The one-pole coefficient for loudness, per analysed block, as a fraction of the
// gap to the new reading rather than a time constant. At 0.85 the gap closes in
// about seven blocks, which at 86 blocks a second is a fifth of a second.
//
// This macro used to declare 0.92 while the value that actually ran was a literal
// 0.85 in AudioProcessor.h, and four of its neighbours in this block had no call
// site at all. A macro that disagrees with the code that runs is worse than no
// macro, because it is read as the tuning site and is not one, so the literal is
// gone and this is now the only statement of the number.
#define GAIN_SMOOTHING      0.85f

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

// The least the level's reference may be, as a multiple of the measured noise
// floor. Without it the reference follows whatever the loudest recent signal was,
// including room tone: a mic reading 0.0005 against a floor of 0.0003 set the
// reference to about 0.0005 and level read 91 percent for what is very nearly
// silence, because a ratio against a tiny reference fills the meter. Eight times
// the floor is about 18 dB above the room, so anything quieter than that cannot
// read as loud. A multiple of the floor rather than a constant, so it scales with
// the microphone's gain the way the floor does.
#define LEVEL_REF_MIN_OVER_NOISE 8.0f

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
#define DYN_EDGE_FALL       0.0005f

// ==== FFT Configuration ====
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
#define BEAT_RISE_NOISE     2.0f
// Milliseconds the silence gate stays held open after signal drops below threshold.
// Prevents stop consonants and brief pauses between beats from strobing or chattering the gate.
#define GATE_HANGOVER_MS    350
// Milliseconds between beats. Caps the detector at 187.5 BPM, preventing
// syncopated 8th-note off-beats and subdivisions on 100-130 BPM material
// from phase-locking the tempo tracker.
#define MIN_BEAT_INTERVAL   320
// How much the bass band's energy has to rise over the block before it for the
// rise to count as a beat.
#define BEAT_BASS_RISE      1.15f
// The minimum normalized bass level (0..1, relative to recent peak bass) required
// for an onset to count as a beat. Lowered to 0.15 to allow beats during quiet passages
// and on tracks without overwhelming sub-bass.
#define BEAT_MIN_BASS_LEVEL 0.15f
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

// ==== Structural detection ====
//
// Five moods name the shape of a passage rather than how loud it is, and each
// answers a question the ladder cannot. They are evaluated before the ladder,
// because a passage can be structurally a drop at any rung. SILENT, DROP and
// TEASE are the ones that are events rather than levels, so their thresholds are
// about displacement and tension rather than about loudness.
//
// Every window here is in milliseconds rather than in blocks. The device analyses
// a block every 33 ms and the page steps once per animation frame, so a block
// count would make the same window last twice as long in one of them.

// The gate's ramp below which a passage counts as silent. gateGain already
// carries its own hysteresis and ramps at GATE_RAMP, so a signal sitting on the
// presence threshold cannot chatter this. Ramped rather than switched for the same
// reason the gate itself is.
#define SILENT_GATE          0.5f

// The gate's ramp above which the structural detectors are allowed to run.
//
// The gate is an exponential approach to one, so opening it from silence is a rise
// across the whole range of level, and read as a level it is a movement of nearly
// the full span. Every detector below would report it: the follower is seeded from
// a gated block, so it starts at zero and the gate's own opening is a displacement
// against it, which is a BUILDUP by the letter of the rule and a DROP once the
// bands come up behind it.
//
// So the detectors do not run until the signal behind the gate is real, and the
// follower re-seeds at that moment rather than keeping a mean that was measured
// through a closing gate. The threshold is set by what it has to leave behind: the
// gate still climbs from here to one, and that remainder has to stay under
// BUILDUP_LEVEL and DROP_SCALE, which it does with room at 0.10. Below that the
// remainder is a movement on its own and the false positive comes back.
#define GATE_SETTLED         0.90f

// BUILDUP: displacement above the signal's own slow mean, which is a different
// measurement from dynamics. dynamics is the width of the window the envelope has
// covered, so it is equally large on a fall as on a rise and stays large once the
// rise stops. This is positive only while still climbing and decays to zero within
// about BUILDUP_TAU_SEC of the climb stopping. A track that has been loud for a
// minute has dynamics and no buildup. A track that is climbing has both.
//
// Ten seconds, and the number is set by a property of the follower rather than
// picked. A first-order follower lags a ramp by roughly the ramp's rate times the
// time constant, so the displacement this produces is rate * tau and the
// threshold below has to sit under that for the speeds a track actually uses. At
// three seconds a build that takes thirty seconds to cross the range lags by
// 0.07, which is under the threshold, so the slow builds would be the ones this
// missed. At ten a thirty second build lags by 0.33 and a sixty second one by
// 0.17, and both clear it.
//
// The cost of a long tau is on the other side: the displacement also takes about
// that long to decay, so a plateau holds BUILDUP for some seconds after the climb
// stops. That is the right direction. The drop lands at the peak and overrides it
// anyway, and the alternative was a short tau that could not see a slow build at
// all.
#define BUILDUP_TAU_SEC      10.0f
// How far above the slow mean the level has to sit.
#define BUILDUP_LEVEL        0.15f
// How far it has to have climbed since the displacement began, so a wobble on a
// plateau that briefly crosses the threshold does not read as a climb.
#define BUILDUP_CLIMB        0.10f
// How long the displacement has to hold before it counts.
//
// No cooldown beside it, and the same reasoning that removed TEASE's. A second
// pair of windows here would open a dead zone where a genuine climb is not
// reported, which is the failure the total partition exists to remove, and the
// mood dwell already supplies both a confirmation and a minimum hold. There is
// also nothing for a cooldown to protect against: a plateau ends the displacement
// on its own as the follower catches up, so a build cannot re-fire from a level
// that has stopped moving.
#define BUILDUP_HOLD_MS      1500

// DESCENT: the same measurement on the other side of the slow mean, and the
// mirror of BUILDUP. Displacement below the mean, still falling, held.
//
// The two are mirror images deliberately, including in their numbers, because
// they are the same shape with the sign flipped and there is no reason for a
// climb and a fall to be judged on different terms. The one asymmetry is in the
// classifier rather than here: BUILDUP needs the ladder below ENERGETIC, since a
// climb from the top is just loud music, and DESCENT needs it above CALM, since a
// fall from the bottom is just quiet.
//
// Barely reachable in the twelve seconds after a drop, because TEASE claims that
// window and is evaluated first. What it catches is a wind-down that is not a
// drop's aftermath: the outro, the retreat out of a chorus, the long fade.
//
// One follower serves both, so it has one time constant and that is BUILDUP_TAU_SEC.
// The hold has no cooldown beside it, for the reason BUILDUP_HOLD_MS gives.
#define DESCENT_LEVEL        0.15f
#define DESCENT_FALL         0.10f
#define DESCENT_HOLD_MS      1500

// DROP: four conditions at once, and none of them is a condition a beat can
// satisfy.
//
// Breadth is the one that matters. A beat is low end and nothing else, which is
// exactly what BEAT_BASS_RISE exploits to tell a rhythm from a voice. A kick
// cannot light all three bands against their own recent maxima at once. A drop
// does it by construction.
#define DROP_BAND_LEVEL      0.60f
// The slam itself, as displacement above the slow mean in a single block. Strictly
// larger than BUILDUP_LEVEL, so a passage that merely rises is not a drop.
#define DROP_SCALE           0.35f
// And the seconds before it have to have been quiet, which is what stops a drop
// firing mid-chorus. This encodes the musical fact that a drop follows a breakdown
// or a buildup rather than arriving in the middle of a loud passage.
#define DROP_QUIET_LEVEL     0.55f
#define DROP_ARM_MS          400
// At most one drop every eight seconds, so the mood cannot park there.
#define DROP_COOLDOWN_MS     8000
// How long the mood reads DROP after one fires. dropDetected is a single block's
// pulse, which the confirmation window would reject before it could ever be shown,
// so MoodHistory adopts it at once and holds the display for this long.
#define DROP_PIN_MS          3000

// TEASE: tension without full energy. The broadest reading, so it is a composite
// and any one of three triggers is enough. A breakdown after a drop, a hush before
// one, and a pulse that will not sustain are all the same thing to a listener.
//
// No cooldown and no hold of its own. The mood system already has both, at
// confirmMs and minHoldMs, and a second pair here would open a dead zone where a
// genuinely teasing passage is not reported at all. That is the failure mode the
// ladder exists to remove.
//
// The first cut of these numbers is a guess from the shape of a track rather than
// from a measurement, and is the most likely thing here to need retuning once
// there is a recording longer than the one in docs/.
#define TEASE_POST_DROP_MS   12000
// A hush is a low level, and TEASE_WINDOW blocks of it is the fake-out test: a
// level whose mean stays mid while its variance is high is a pulse that does not
// sustain, which is what teasing is.
#define TEASE_LOW_LEVEL      0.50f
// The second half of the hush, for a passage that is quiet but animated rather
// than quiet and still: dynamics over this share of its own window means the level
// has covered more than half its recent range, which is a rumble under a hush
// rather than an absence.
#define TEASE_HUSH_DYNAMICS  0.50f
#define TEASE_WINDOW         30
#define TEASE_VARIANCE       0.020f
#define TEASE_MEAN_LOW       0.25f
#define TEASE_MEAN_HIGH      0.70f

// WEIRD: two of three, each a rate of change or a band membership rather than a
// level, so a stable passage scores zero whatever its spectrum is. A quiet
// drifting ambient passage has to stay CALM rather than becoming this, which is
// why the classifier also requires the ladder to be at DANCY or above.
//
// The centroid test is a distance from the middle of the window it has covered,
// as a share of that window. The maximum such a distance can be is 0.5, since the
// middle is the middle, so the cut has to sit between a steady passage's near zero
// and that ceiling.
#define WEIRD_CENTROID_SHARE    0.40f
// Below this the window is too narrow to measure a distance against, and a steady
// tone would report a huge share of a near-zero denominator.
#define WEIRD_CENTROID_MIN_SPAN 40.0f
// The tempo's spread over its own median, which is the strongest eclectic signal
// available and costs one pass over twelve numbers. Four-on-the-floor sits under
// 0.15 and breakcore or glitch runs over 0.4.
#define WEIRD_TEMPO_SPREAD      0.35f
#define WEIRD_TEMPO_MIN_HITS    6
// Spectral flatness in the middle. Below is plainly tonal and above is plainly a
// room or a cymbal wash, so the interesting band is the one between.
#define WEIRD_FLAT_MIN          0.15f
#define WEIRD_FLAT_MAX          0.45f
#define WEIRD_ANOMALY_MIN       2.0f
// Held, with no cooldown after. Every one of the three conditions is a rate of
// change rather than a level, so none of them can sit satisfied over a passage
// that has stopped moving, which is the case a cooldown would be protecting
// against. Clearing the hold the moment the score drops already means re-entry
// costs another full hold of sustained evidence.
#define WEIRD_HOLD_MS           2500

// ==== Display ====
#define DEFAULT_BRIGHTNESS  150



// ==== LED ====
#ifndef CONFIG_LED_DATA_PIN
  #define CONFIG_LED_DATA_PIN 4
#endif
#define LED_0_PIN            CONFIG_LED_DATA_PIN
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

// The device's buffer capacity is its physical count. The WASM build overrides
// these capacities so the shared animation library can exercise a longer
// software-defined strip without changing the board configuration above.
#ifndef LED_0_CAPACITY
  #ifdef __EMSCRIPTEN__
    // 20 m at the densest built-in profile (480 LEDs/m), with headroom.
    #define LED_0_CAPACITY 12000
  #else
    #define LED_0_CAPACITY LED_0_NUM
  #endif
#endif
#ifndef LED_1_CAPACITY
  #ifdef __EMSCRIPTEN__
    #define LED_1_CAPACITY 12000
  #else
    #define LED_1_CAPACITY LED_1_NUM
  #endif
#endif





//cant put this in the array, needs to be defined on compile
#define MIN_SWITCH_INTERVAL 10000





// ==== ENCODER ====
#define ENCODER_PIN_A      39
#define ENCODER_PIN_B      38
#define ENCODER_BTN_PIN    17

// ==== BUTTON ====
#ifndef CONFIG_BTN1
  #define CONFIG_BTN1 0
#endif
#define BUTTON_PIN_1         CONFIG_BTN1
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
