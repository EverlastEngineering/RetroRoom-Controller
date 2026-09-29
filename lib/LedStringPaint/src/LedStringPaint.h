#pragma once

// Functional core for the second-strip LED paint math.
//
// Pure data + math: no Arduino headers, no FastLED dependency. Compiles
// and unit-tests on the host via PlatformIO + Unity. The shell-side
// wrapper in src/ledstring.{h,cpp} copies the computed window into the
// live CRGB selectedLeds[] buffer and pushes FastLED.show(); this core
// has no knowledge of either.
//
// Why split: the host environment ([env:test_native]) excludes src/ and
// does not link FastLED. Lifting the paint decision into a pure lib
// makes the invariant ("only pixels in [ledPos, ledPos+ledWidth) are
// non-zero") testable on the host without emulating FastLED.

#include <cstddef>
#include <cstdint>

namespace retroroom_core {

// An 8-bit RGB triple. The core resolves *what role* each pixel is
// playing -- stack, fill, travelling block, proposal, selection -- and
// the palette below says what colour that role is, so the colour
// choices live in one place (src/configuration.h) rather than being
// spread through the shell as percentage-of-one-base-hue.
struct LedColor {
	int r;
	int g;
	int b;
};

// Black. Used for every pixel no role claims.
extern const LedColor kLedBlack;

// The roles the strip can be showing. Each maps to one colour in
// StripFrame::palette, which is assembled from the LEDSTRING_COLOR_*
// defines.
enum class LedRole {
	OFF,      // a pixel no console owns
	STACK,    // consoles above the selection
	LEAVING,  // the console being turned away from, during a browse
	FILL,     // the knob-turn progression indicator
	TRAVEL,   // the block of light moving between consoles
	PROPOSAL, // the console a click would select, pulsing
	SELECTED, // the console that is actually selected
	COUNT,
};

// The colour each role is drawn in.
struct RolePalette {
	LedColor colors[static_cast<int>(LedRole::COUNT)];
};

// One pixel of a resolved frame: which role it is playing, and how
// brightly. The colour comes from the frame's palette, so the shell
// only ever converts to hardware values.
struct StripPixel {
	LedRole role;
	int level;  // 0..100
};

// Inclusive-exclusive pixel range computed from a console's LED metadata.
// width == 0 means "nothing to paint" -- caller should clear the strip
// to its off-color instead of touching individual pixels.
struct LedRange {
	int start;   // >= 0, <= totalLeds
	int width;   // >= 0; clamped so start + width <= totalLeds
};

// Compute the inclusive-exclusive window [start, start+width) of pixels
// that should be lit for the given console metadata.
//
// Returns {0, 0} when:
//   - ledWidth <= 0 (a console with no strip mapping)
//   - ledPosition < 0 (malformed config)
//   - ledPosition >= totalLeds (window starts past the end of the strip)
//   - ledWidth would push past totalLeds (defensive clamp; a console
//     whose [ledPosition, ledPosition+ledWidth) range would overflow
//     the strip is left unpainted rather than wrapped -- if you see a
//     console unexpectedly dark, check ledPosition + ledWidth <= totalLeds)
//
// Inputs:
//   ledPosition: pixel index of the first LED in the window (>= 0
//                 expected; <0 returns a zero-width range to defend
//                 against corrupted JSON).
//   ledWidth:     number of pixels in the window (>= 1 expected for
//                 non-zero widths; <= 0 returns a zero-width range).
//   totalLeds:    the size of the physical strip (NUM_SELECTED_CONSOLE_LED_STRING_LEDS);
//                 used to clamp the window.
LedRange computeConsoleWindow(int ledPosition, int ledWidth, int totalLeds);

// ---------------------------------------------------------------------------
// Rotary detent gating
// ---------------------------------------------------------------------------

// The browse cursor no longer moves one console per detent. Instead a
// console step takes `detentsPerStep` physical detents, and the strip
// shows a "blob" creeping toward the next console while they accumulate.
// The final detent snaps the cursor and starts the preview pulse.
//
// If the operator keeps spinning, the requirement drops to
// `fastDetentsPerStep` so a long run through the list stays quick.
struct DetentGateConfig {
	DetentGateConfig()
		: detentsPerStep(5), fastDetentsPerStep(2), fastSpinWindowMs(1000) {}
	DetentGateConfig(int per, int fast, std::uint32_t window)
		: detentsPerStep(per), fastDetentsPerStep(fast), fastSpinWindowMs(window) {}

	// Detents required to move one console when the operator is turning
	// deliberately. Clamped to >= 1 on use.
	int detentsPerStep;
	// Detents required once the operator is spinning fast. Clamped to
	// >= 1 on use, and to <= detentsPerStep (a "fast" threshold slower
	// than the deliberate one would be nonsense).
	int fastDetentsPerStep;
	// A detent that arrives within this many ms of the previous one
	// marks the spin as fast. 0 disables escalation entirely.
	std::uint32_t fastSpinWindowMs;
};

// What one physical detent did to the gate. Returned by value so the
// caller can act on the whole decision without reaching back into the
// gate for the intermediate state.
struct DetentEvent {
	// How far the operator is from the anchor console, in permille of
	// one console step. Positive = past the anchor toward the forward
	// neighbour, negative = past it toward the backward one. 0 means
	// "sitting exactly on the anchor".
	//
	// This is deliberately a *continuous* position rather than a detent
	// counter: the shell lerps the blob between the anchor and the
	// target console by this fraction, so turning the knob back
	// mid-transit walks the blob back the way it came instead of
	// teleporting it to the far side.
	int fractionPermille;

	// The threshold in force for this detent (clamped to >= 1), and
	// whether the fast one was used. Surfaced for the serial log so the
	// escalation is visible when tuning the feel.
	int detentsPerStep;
	bool fastMode;

	// Which neighbour of the anchor the operator is currently heading
	// toward: +1 forward, -1 backward. The shell computes the target
	// console with this. It is the sign of the *position*, not of the
	// detent that just arrived -- turning back while part-way to the
	// forward console keeps this +1, because the blob is still out on
	// that side and must retreat along the same path.
	int targetDirection;

	// True on the single detent that completed a step. Only then does
	// the shell move the browse anchor. When true, `targetDirection`
	// is the direction the cursor moved.
	bool advanced;

	// True when there was nowhere to step: the anchor is at one end of
	// the list and the operator is turning off the end of it. The
	// position is frozen -- this is the signal for the caller to stop
	// moving its indicators, because a knob that keeps turning while
	// the light goes nowhere reads as a fault rather than as an end.
	//
	// The detent is still recorded, so the gap since the previous one
	// stays honest and the fast-mode decision is unaffected.
	bool frozen;

	// Progress through the current step, remapped so that the *last*
	// detent before the step completes reads as 1000.
	//
	// This is not the same as fractionPermille, and the difference is
	// visible: fractionPermille reaches 1000 only on the detent that
	// commits the step, by which point the knob-turn indicator is no
	// longer being drawn. Driving the fill from it left the indicator a
	// detent short of the console it was heading for -- on the fourth
	// of five detents the pixel nearest the target console was still
	// dark.
	int stepPermille;

	// Whole detents consumed toward the current step. For logging.
	int detents;

	// -1 when the encoder reported a direction that isn't a real detent
	// (rotaryEncoderTick() drops those before calling us, but the gate
	// defends rather than trusting).
	int direction;
};

// Accumulates detents into a continuous position between consoles.
class DetentGate {
  public:
	DetentGate();

	void configure(const DetentGateConfig& cfg);
	const DetentGateConfig& config() const { return cfg_; }

	// Drop all accumulated progress and the fast-spin memory. Called
	// when the browse is abandoned (ring faded out) and after a commit.
	void reset();

	// Feed one detent. `direction` is +1 or -1; anything else is
	// reported as a no-op so the caller can still repaint. `nowMs` is
	// a millis() reading -- the gate stores the previous one to decide
	// whether the spin has picked up.
	//
	// `listSize` and `anchorIndex` are the list being browsed and where
	// the anchor sits in it, so the gate can tell whether a step in the
	// current direction exists at all. At either end there is none, and
	// the gate freezes: `advanced` stays false, `frozen` comes back
	// true, and the position does not move. The shell freezes its
	// indicators on that flag rather than filling toward a console that
	// is not there.
	//
	// The gate needs the bounds because it is the thing that decides
	// whether a step completes. Handing it a direction and asking it to
	// trust that the caller checked first is how the two disagree.
	DetentEvent onDetent(int direction, std::uint32_t nowMs, int listSize,
	                     int anchorIndex);

	int fractionPermille() const { return fraction_; }
	bool fastMode() const { return fastMode_; }
	// Detents required for the next step under the current mode.
	int detentsPerStep() const;

  private:
	// Progress through the current step with the last *drawn* detent
	// reading as 1000, which is what drives the knob-turn indicator.
	int stepProgressPermille(int detentsPerStep) const;

	// Whole detents consumed toward the current step, derived from
	// fraction_ and the threshold in force. For logging only.
	int detentProgressPermille(int detentsPerStep) const;

	DetentGateConfig cfg_;
	// Position relative to the anchor, in permille of one console step.
	int fraction_;
	// Carries the division remainder of `1000 / detentsPerStep` so a
	// threshold that doesn't divide 1000 evenly (3, 6, 7...) still
	// completes in exactly `detentsPerStep` detents rather than
	// rounding down and never arriving.
	int detentRemainder_;
	bool fastMode_;
	bool hasLastDetent_;
	std::uint32_t lastDetentMs_;
};

// ---------------------------------------------------------------------------
// Blob travel
// ---------------------------------------------------------------------------

// Smoothstep-eased fraction in permille: 0 -> 0, 1000 -> 1000, with the
// midpoint at 500. Used for the blob's travel so it eases out of the
// anchor and decelerates into the snap instead of sliding linearly --
// the deceleration is what makes the final detent read as an arrival.
int easeInOutPermille(int fractionPermille);

// The blob's window during a browse. Interpolates a band `blobWidth`
// pixels wide between the centre of `from` and the centre of `to`.
// `fractionPermille` is the DetentGate position: 0 sits the blob on
// `from`, 1000 lands it dead centre on `to`.
//
// A zero-width window (a console with no LED mapping) degrades to
// using its start pixel as the centre, so the blob still has somewhere
// to travel from. The result is clamped into the strip; a blob wider
// than the strip is truncated rather than wrapped.
LedRange computeBlobWindow(int fromStart, int fromWidth, int toStart,
                           int toWidth, int fractionPermille, int blobWidth,
                           int totalLeds);

// ---------------------------------------------------------------------------
// Preview pulse
// ---------------------------------------------------------------------------

// Brightness percentage (0..100) for the console currently under
// consideration, as a function of how long it has been pulsing.
// A parabola over `periodMs` rather than a triangle or a sine: it peaks
// in the middle of each cycle and falls away at both ends, which reads
// as a soft breathing glow and needs no libm.
int computePulseScale(std::uint32_t elapsedMs, std::uint32_t periodMs,
                      int minPercent, int maxPercent);

// ---------------------------------------------------------------------------
// Selection effect
// ---------------------------------------------------------------------------

// Shape of the twinkle-then-settle played when a browse is committed.
// Every field is surfaced so the "feel" can be tuned from
// src/configuration.h without touching this core.
struct SelectionEffectConfig {
	SelectionEffectConfig()
		: totalMs(900), twinkleMs(350), staggerMs(6), twinkleMin(12),
		  twinkleMax(100), abovePct(22), selfPct(100) {}
	SelectionEffectConfig(std::uint32_t total, std::uint32_t twinkle,
	                      std::uint32_t stagger, int tmin, int tmax,
	                      int above, int self)
		: totalMs(total), twinkleMs(twinkle), staggerMs(stagger),
		  twinkleMin(tmin), twinkleMax(tmax), abovePct(above),
		  selfPct(self) {}

	// Whole effect. The user-visible contract is that it finishes in
	// under a second.
	std::uint32_t totalMs;
	// Portion of the effect spent twinkling the entire strip before it
	// starts settling. Clamped to totalMs.
	std::uint32_t twinkleMs;
	// Per-pixel delay added to the settle ramp, so the collapse ripples
	// down the strip instead of snapping in one frame. The whole
	// stagger is subtracted from the ramp length, so raising this too
	// far shortens the ramp rather than stretching the effect.
	std::uint32_t staggerMs;
	// Brightness percentage of the darkest and brightest twinkle samples.
	int twinkleMin;
	int twinkleMax;
	// Brightness percentages the strip settles to. The split is not
	// cosmetic: the effect has to land on *exactly* the resting paint,
	// which is the stack above the console dim and the console's own
	// window bright. A single flat level would leave the whole prefix at
	// console brightness and then visibly dim the moment the effect
	// handed back to the resting paint.
	int abovePct;
	int selfPct;
};

// The exclusive pixel index the strip settles to: the lit prefix is
// [0, result). Superseded by the per-window `aboveWindows` list on
// StripFrame -- the consoles do not tile the strip, so "above" is a set
// of windows rather than a prefix -- and retained only as the
// degenerate single-window case for callers that want one.
int computeKeepEnd(int ledPosition, int ledWidth, int totalLeds,
                   bool includeSelf);

// Deterministic per-pixel pseudo-random brightness sample in [0, 1023].
// Integer-only so host tests can assert exact frames. `tick` is a frame
// counter, not milliseconds -- the caller divides by whatever cadence
// the twinkle should visibly run at.
int twinkleSample(int pixel, std::uint32_t tick);

// Brightness percentage (0..100) for one pixel at `elapsedMs` into the
// selection effect. `pixel` must be < totalLeds.
//
// `finalPct[pixel]` is what the pixel settles to, and is expected to be
// the same array computeStripFrame() would produce for the RESTING
// picture. Taking the target as an input rather than deriving it from
// split points is what makes the handoff from the effect back to the
// resting paint exact by construction: there is only one resting
// picture, and both paths use it. (It also means the lit set can be an
// arbitrary set of windows rather than two contiguous bands, which the
// consoles-above list needs.)
//
// Twinkles with the rest of the strip for the first `twinkleMs`, then
// ramps to its final value over a ramp that starts `pixel *
// staggerMs` late. Returns the settled value once the effect is over.
int computeSelectScale(int pixel, const int* finalPct, int totalLeds,
                       std::uint32_t elapsedMs,
                       const SelectionEffectConfig& cfg);

// The same, but keeping the role the pixel settles into rather than
// flattening it to a level. The settle target is a rendered frame, so
// it already knows whether a pixel ends up as the selection or as the
// stack above it -- flattening that to a level first would throw away
// the distinction the colours are for.
StripPixel computeSelectPixel(int pixel, const StripPixel* finalPixels,
                              int totalLeds, std::uint32_t elapsedMs,
                              const SelectionEffectConfig& cfg);

// ---------------------------------------------------------------------------
// Whole-strip frames
// ---------------------------------------------------------------------------

// What the strip is showing right now. One of the four states the shell
// animates between.
enum class StripEffect {
	RESTING,    // the stack, lit down to the selection
	FILLING,    // a browse in progress: the knob-turn progression
	TRAVEL,     // a browse snapped: the scripted move onto the target
	PREVIEW,    // the travel finished: the target console pulsing
	SELECTING,  // a commit in progress: twinkle, then settle
};

// One frame of the strip, fully resolved: the caller has already turned
// console indices into LED windows, and filled in the brightness
// percentages it wants. computeStripFrame() then produces the whole
// frame as one percentage per pixel.
//
// This exists so the *only* thing left in src/ledstring.cpp is
// percentage -> CRGB and FastLED.show(). Everything that decides which
// pixels are lit lives here, which means it is host-testable and can be
// replayed offline by agent-script/ledstring-sim.sh -- see the
// simulator's header for why that matters.
struct StripFrame {
	StripEffect effect = StripEffect::RESTING;

	// Size of the physical strip. Everything is clamped to this.
	int totalLeds = 0;

	// Console windows, resolved by the caller (it owns the console
	// list). `from` is the console being shown, TRANSIT's departure, or
	// the selection at rest. `aboveWindows` are the windows of the
	// consoles above it, in whatever order collectAboveWindows() put
	// them; `to` is TRANSIT's destination or the preview target.
	LedRange from;
	LedRange to;

	// The consoles above the one being shown, and how many entries
	// `aboveWindows` actually has. Read by RESTING and SELECTING.
	//
	// This is a *list*, not a prefix, on purpose. The consoles do not
	// tile the strip -- between one console's window and the next there
	// are pixels belonging to no console at all, because that is where
	// the physical gap between shelves is. Filling those gaps makes the
	// strip read as one continuous bar from the top of the cabinet
	// rather than as a stack of separate consoles, which is not what
	// "the ones above the selected console" describes. So the lit set is
	// the union of the windows above, and the gaps stay dark.
	//
	// May be null with aboveCount 0, which lights nothing above.

    // The console that is *selected* right now, which is not the same as
    // the one the browse is on. It is what stays lit, dim, through every
    // browse state: the only thing on the strip besides whatever the
    // operator is currently being shown.
    //
    // It used to be the console the browse departed from, which is the
    // same thing for the first step and a different thing for every
    // step after it -- so the pulsing candidate took over the dim role
    // the moment you kept turning, and the console you were actually
    // playing went dark.
    // Value-initialised, and not merely default-initialised: LedRange
    // is a plain aggregate with no member initialisers of its own, so
    // an uninitialised StripFrame member of that type holds whatever
    // was on the stack. That showed up as a frame drawing a proposal
    // over a random span of the strip.
    LedRange activeWindow = {0, 0};

    // The console the browse is currently offering, which keeps
    // pulsing for as long as it remains the console a press would
    // select -- including while the operator is filling onward toward
    // the next one.
    //
    // This is the reason it is separate from `activeWindow` and not
    // implied by the fill: the fill is about where the operator is
    // *going*, and the proposal is about what a press right now would
    // do. Collapsing the candidate into the fill made it stop pulsing
    // the moment the knob moved again, so the console you were being
    // offered disappeared one detent after it appeared -- and the
    // console you were actually playing had already gone dark, leaving
    // nothing but the fill itself.
    LedRange candidateWindow = {0, 0};
	const LedRange* aboveWindows = 0;
	int aboveCount = 0;

	// What each role looks like. Assembled from the LEDSTRING_COLOR_*
	// defines in src/configuration.h; see there for what each is for.
	RolePalette palette;

	// TRANSIT only.
	int fractionPermille = 0;

	// ---- FILLING: the knob-turn progression indicator -----------------
	//
	// The gap the fill runs across, and how far along it is. The
	// operator's four detents walk the fill from the start of the gap
	// to its end, and the fifth triggers the travel.
	//
	// `fillGap` is deliberately not the literal gap between the two
	// console windows. A step that crosses shelves is a couple of
	// pixels in index space and physically a long way round, so filling
	// the literal gap would mean the progression indicator barely moves
	// on exactly the steps where the operator most needs to see it.
	// `fillFrom` / `fillTo` are the ends of the run, which the caller
	// may widen, and `fillPermille` is progress along that run.
	LedRange fillFrom;
	LedRange fillTo;
	int fillPermille = 0;

	// ---- TRAVEL: the scripted move onto the target --------------------
	//
	// The travel is defined by two *edges* rather than a centre and a
	// width, because the two edges move differently: the leading edge
	// decelerates into the target while the trailing edge accelerates
	// away and then settles, which is what makes the block stretch
	// across the gap and then narrow onto the target window. A blob with
	// a fixed width cannot do that.
	//
	// Both are in whole LEDs, so antialiasing is exact: the caller
	// supplies the edges already scaled by 1000 and this interpolates
	// them in permille. See travelEdges() for the easing.
	int travelFromLeftPermille = 0;
	int travelFromRightPermille = 0;
	int travelToLeftPermille = 0;
	int travelToRightPermille = 0;
	// The console the block departs from. Drawn at dimPct for the whole
	// travel and used as the origin of both the fill run and the block's
	// start edges.
	LedRange travelFrom;

	// Total duration of the travel, so the caller can turn elapsedMs
	// into a progress value without duplicating the arithmetic.
	std::uint32_t travelMs = 0;
	// How wide the block is at the moment it is furthest from both
	// ends. The stretch is the whole point, and without a peak it is
	// just a slide.
	int travelPeakWidth = 6;

	// Brightness percentages, all 0..100, as a *level* on top of the
	// role's colour. abovePct applies to `aboveWindows`; setting it to 0
	// lights nothing above the selection, which is the resting default.
	int abovePct = 0;    // RESTING + SELECTING: the consoles above
	int selfPct = 100;   // RESTING + SELECTING: the console itself
	int fillPct = 45;    // FILLING + TRAVEL: the knob-turn progression
	int dimPct = 22;     // FILLING + TRAVEL: the console left behind
	int travelPct = 100; // TRAVEL: the travelling block
	// Floor on the knob-turn fill's run, in LEDs. See computeFillEnd().
	int minFillLeds = 3;

    // The knob-turn fill, as an anchor and a leading edge rather than a
    // pair of windows. The run is the GAP between two console windows:
    // the anchor is the end of it nearest the console being left and
    // never moves; the lead is the edge the operator watches travel
    // towards the console being reached. Expressing it this way rather
    // than as "from A to B" is what makes it behave the same whichever
    // way the knob is turned -- the earlier version measured from the
    // source's trailing edge in both directions, so a backwards step's
    // run lay inside the console being left and the indicator crept the
    // wrong way.
    int fillAnchor = 0;
    int fillLead = 0;
    bool fillForward = true;
	int fromPct = 0;    // TRANSIT: the console being left
	int toPct = 0;      // TRANSIT: the console being approached
	int blobPct = 100;  // TRANSIT: the travelling blob

	// TRANSIT. Clamped to totalLeds.
	int blobWidth = 3;

	// PREVIEW. The glow follows a parabola over the period, so it peaks
	// mid-cycle and falls to pulseMinPct at both ends.
	int pulseMinPct = 0;
	int pulseMaxPct = 100;
	std::uint32_t pulsePeriodMs = 0;

	// PREVIEW and SELECTING: how far into the animation we are.
	std::uint32_t elapsedMs = 0;

	// SELECTING only.
	SelectionEffectConfig select;
};

// Pick the console windows that sit entirely above `selectedStart`,
// writing at most `outCapacity` of them into `out` and returning how
// many were written.
//
// "Above" is `window.start + window.width <= selectedStart`: a window
// only counts if it is wholly on the far side, so a hand-edited config
// with overlapping or out-of-order windows degrades to skipping the
// odd one rather than lighting a console's pixels twice at the wrong
// brightness. Order of the input is preserved and no sorting is done,
// so the lit set is exactly the set, which is what the eye reads.
int collectAboveWindows(const LedRange* windows, int count,
                        int selectedStart, LedRange* out,
                        int outCapacity);

// Resolve one whole frame into `out[0 .. totalLeds)`. `out` is fully
// overwritten, so callers do not need to clear it first.
//
// Zeros the output and returns immediately if `out` is null or
// totalLeds <= 0, so a caller with an uninitialised strip size cannot
// scribble past its buffer.
//
// The layering inside TRANSIT is deliberate and is the order the pixels
// are meant to be read in: departure window, then destination window on
// top of it, then the blob on top of both. A wider destination therefore
// wins over the window it is approaching, and the blob is always the
// brightest thing on the strip while it moves.
//
// SELECTING animates *toward* the RESTING picture and shares its code
// path, so the last frame of the selection effect is the resting paint
// by construction rather than by two pieces of code happening to agree.
void computeStripFrame(const StripFrame& frame, StripPixel* out);

// The colour a pixel resolves to: its role's colour at its level.
LedColor resolvePixel(const StripFrame& frame, const StripPixel& pixel);

// ---------------------------------------------------------------------------
// Browse: the knob-turn fill and the scripted travel
// ---------------------------------------------------------------------------

// The two edges of the travelling block at `progressPermille` (0..1000).
//
// Modeled as two independent edges because they move differently. The
// leading edge decelerates into the target (ease-out) while the trailing
// edge accelerates away and then settles (ease-in-out), so the block
// stretches across the path and then narrows onto the target window.
// Interpolating a centre with a width cannot produce that shape, and a
// shape the maths cannot produce is a shape you cannot tune.
//
// Edges are in permille of a LED, so the caller scales the whole-pixel
// window ends by 1000 and this interpolates in the same units. The
// antialiasing downstream is then exact rather than sampled.
//
// `peakWidthPermille` is how far the two edges are apart at the widest
// point of the travel, again in permille. The caller derives it from the
// two windows so the stretch is bounded by geometry instead of being a
// magic number in here.
struct TravelEdges {
	int leftPermille;
	int rightPermille;
	int widthPermille;
};
TravelEdges travelEdges(int fromLeftPermille, int fromRightPermille,
                        int toLeftPermille, int toRightPermille,
                        int progressPermille, int peakWidthPermille);

// The block's start and end edges for a travel, in permille.
//
// `leave` is the console the block departs from; the block starts as a
// `sparkLeds`-wide spark at that console's trailing edge, so it reads as
// peeling off the console rather than appearing in the gap.
//
// `entryPixel` is where the block's spark sits at the start of the
// travel. For an ordinary step within a shelf that is the trailing edge
// of the console being left. For a step *between* shelves it is the far
// end of the destination shelf, which is what makes the block sweep the
// whole shelf and land on the console -- the knob goes one way and the
// light goes the other -- rather than crossing a two-pixel gap.
//
// `target` is always the console being reached, and the block's last
// frame lands exactly on it.
//
// Exposed as a helper rather than left to the caller because the
// permille scaling is easy to get subtly wrong and the shell, the
// simulator and the tests all need to agree on it.
void computeTravelPath(const LedRange& leave, int entryPixel,
                       const LedRange& target, int sparkLeds, StripFrame& frame);

// How much of pixel `i` the block [left, right) covers, as a percentage
// in 0..100.
//
// This is the antialiasing, and for a one-dimensional block it is exact
// rather than approximated: the overlap between the block's span and
// the LED's one-unit pitch, as a fraction of that pitch. Sampling the
// profile at N points per LED would converge on the same number at N
// times the cost, so there is nothing to approximate -- the shape would
// have to stop being piecewise linear before sampling earned its keep.
int coveragePercent(int leftPermille, int rightPermille, int pixel);

// Resolve the knob-turn fill's run for a step from `leave` to `target`,
// with the block entering at `entryPixel`.
//
// The run is the gap between the two console windows. `fillAnchor` is
// the end of the gap nearest the console being left and never moves;
// `fillLead` is the leading edge at full progress, and the shell walks
// it with scaleFillLead() as the operator turns.
//
// `minLengthLeds` floors the run so a step between shelves -- a couple
// of pixels in index space and a long way round the cabinet -- still has
// something to show.
void computeFillGeometry(const LedRange& leave, int entryPixel,
                         const LedRange& target, int minLengthLeds,
                         int totalLeds, StripFrame& frame);

// Walk the fill's leading edge to `progressPermille` (0..1000). In the
// core so the shell, the simulator and the tests cannot disagree about
// which way it moves.
int scaleFillLead(int anchor, int lead, bool forward, int progressPermille);

// Brightness percentage for one pixel during the knob-turn fill.
//
// Three distinct levels, deliberately: the consoles already in the
// stack, the fill running ahead of the operator's turn, and the console
// itself. Collapsing the first two into one brightness would make
// "where the stack ends" and "how far I have got" the same fact, and
// the operator would have nothing to read progress from.
int computeFillScale(int pixel, const StripFrame& frame);

// Brightness percentage for one pixel during the travel.
//
// The block erases the fill behind it as it passes: a pixel that was
// part of the knob-turn fill goes dark once the block's leading edge
// has moved past it. That is what makes the block look like it is
// consuming the path it just travelled rather than sliding over a
// backdrop.
int computeTravelScale(int pixel, const StripFrame& frame,
                       int leftPermille, int rightPermille);

}  // namespace retroroom_core
