// Functional core for the selector ring. See RingPaint.h for why this
// is a separate translation unit with no Arduino in it.
//
// Every time comparison goes through reached() rather than a plain
// `now > deadline`, so millis()' rollover is handled the same way the
// rest of the shell does it. ringDetent()'s spinner wraps the same way.

#include "RingPaint.h"

namespace retroroom_core {

const uint8_t kRingBaseLevel = 255;
const uint8_t kRingHoldLevel = 255;
const uint8_t kRingFlashLevel = 255;

namespace {

// Has `since` been reached by `now`? True also when the clock has
// wrapped, which is the whole reason this is not `now >= since`.
bool reached(uint32_t now, uint32_t since) {
	return static_cast<int32_t>(now - since) >= 0;
}

// How far into a fade of `durationMs` that started at `startMs` we are,
// clamped to the duration. Returns 0 before the fade has begun, so a
// clock that appears to have gone backwards yields "full brightness"
// rather than a wild level.
uint32_t fadeElapsed(uint32_t now, uint32_t start, uint32_t durationMs) {
	if (!reached(now, start)) {
		return 0;
	}
	const uint32_t t = now - start;
	return t > durationMs ? durationMs : t;
}

// Enter FADING, ramping down from whatever is on the ring right now.
//
// `startMs` is when the fade *began*, which is not always nowMs. Coming
// out of a strike it is the moment the strike should have ended, so a
// late tick resumes the fade rather than restarting it -- otherwise a
// 250 ms gap in the loop would push the whole fade 250 ms later, and
// "how long does the ring take to go out" would quietly go back to
// depending on the loop rate. That is the bug this core exists to make
// unrepresentable, so it is worth the extra argument.
void beginFade(RingState& s, uint32_t startMs) {
	s.fadeFrom = s.lastPaint;
	s.mode = RingMode::FADING;
	s.modeStartMs = startMs;
}

}  // namespace

void ringDetent(RingState& s, uint32_t nowMs, const RingConfig& cfg,
				int direction) {
	// A strike owns the ring outright. Letting a detent through would
	// move the highlight and re-arm a hold the strike is about to end,
	// so the turn would be silently swallowed in one direction and
	// visibly change the ring in the other.
	if (s.mode == RingMode::FLASH) {
		return;
	}
	if (cfg.pixelCount > 0) {
		int p = (s.pixel + direction) % cfg.pixelCount;
		if (p < 0) {
			p += cfg.pixelCount;
		}
		s.pixel = p;
	}
	if (s.mode == RingMode::PROXIMITY) {
		// The hand is still on the pad, so the hold is not ours to
		// re-arm -- ringTick() pushes the deadline out every tick while
		// an approach is engaged. Moving the pixel is the whole of what
		// a detent does here.
		return;
	}
	// A detent during a fade brings the ring back up to full. That is
	// the old behaviour and it is right: the operator is turning the
	// knob, so the ring should answer immediately, and the fade is
	// simply abandoned for a new hold.
	s.mode = RingMode::IDLE;
	s.modeStartMs = nowMs;
	s.holdUntilMs = nowMs + cfg.idleMs;
}

void ringCommit(RingState& s, uint32_t nowMs, const RingConfig& cfg) {
	// The interaction is over, whatever the pad says. Clearing
	// proximityEngaged rather than proximityNear is deliberate: the
	// hand may well still be resting there, and a commit is the
	// operator saying they are done. Because no *new* edge can arrive
	// while the reading is unchanged, the ring stays dark until they
	// actually lift and come back.
	s.proximityEngaged = false;
	if (cfg.flashMs > 0) {
		s.mode = RingMode::FLASH;
		s.modeStartMs = nowMs;
		return;
	}
	// No strike configured: the commit is a force-off with no flash, so
	// fade from whatever is currently lit rather than going dark in one
	// step.
	beginFade(s, nowMs);
}

void ringProximity(RingState& s, uint32_t nowMs, bool near) {
	if (near == s.proximityNear) {
		return;  // not an edge
	}
	s.proximityNear = near;
	if (near) {
		s.proximityEngaged = true;
		return;
	}
	// Departure. The interaction ended, so the ring goes -- the old
	// shell set a fade request here rather than waiting out the idle
	// timeout, and that is the better behaviour: making the operator
	// wait after they have already taken their hand away is just a
	// delay on a decision that has been made.
	s.proximityEngaged = false;
	if (s.mode == RingMode::PROXIMITY) {
		beginFade(s, nowMs);
	}
}

RingUpdate ringTick(RingState& s, uint32_t nowMs, const RingConfig& cfg,
					bool retreatInProgress) {
	RingUpdate u;

	// A strike that has run its course becomes a fade. This is first so
	// that the flash is never extended by anything below, and so that a
	// flash begun with the last paint still on screen ramps from *that*
	// rather than from whatever the tick after it happens to draw.
	if (s.mode == RingMode::FLASH &&
		fadeElapsed(nowMs, s.modeStartMs, cfg.flashMs) >= cfg.flashMs) {
		beginFade(s, s.modeStartMs + cfg.flashMs);
	}

	// Two things hold the ring open. An engaged hand outranks a fade
	// outright: the operator has come back, and answering them is the
	// point of the ring. A retreat holds the *timeout* rather than the
	// ring, and only applies to a ring that is already lit.
	if (s.proximityEngaged) {
		s.mode = RingMode::PROXIMITY;
		s.holdUntilMs = nowMs + cfg.idleMs;
	} else if (s.mode == RingMode::IDLE && retreatInProgress) {
		// Pushed out, not shortened: the operator gets the full idle
		// timeout after the last LED of the retreat goes, rather than
		// whatever was left of it.
		s.holdUntilMs = nowMs + cfg.idleMs;
	} else if (s.mode == RingMode::IDLE && cfg.idleMs != 0 &&
			   reached(nowMs, s.holdUntilMs)) {
		// Likewise from the deadline rather than from nowMs, for the
		// same reason: a late tick should not hand the ring extra time.
		beginFade(s, s.holdUntilMs);
	}

	switch (s.mode) {
	case RingMode::DARK:
		break;  // RingPaint's defaults are "everything off"

	case RingMode::IDLE:
	case RingMode::PROXIMITY:
		u.paint.baseLevel = kRingBaseLevel;
		u.paint.highlightLevel = kRingHoldLevel;
		u.paint.highlightIndex = s.pixel;
		u.paint.highlightCount = 1;
		break;

	case RingMode::FLASH:
		u.paint.baseLevel = kRingBaseLevel;
		u.paint.highlightLevel = kRingFlashLevel;
		u.paint.highlightIndex = 0;
		u.paint.highlightCount = cfg.pixelCount;
		break;

	case RingMode::FADING: {
		// The ramp. A pure function of elapsed time over the snapshotted
		// start paint, so a tick that is late, early, or the only one
		// there is produces the same brightness as any other.
		const uint32_t t = fadeElapsed(nowMs, s.modeStartMs, cfg.fadeMs);
		if (t >= cfg.fadeMs) {
			s.mode = RingMode::DARK;
			u.fadeCompleted = true;
			break;  // DARK's all-off paint
		}
		// 255 at the start of the fade, 0 at the end. fadeMs = 0 means
		// an instant fade, and is handled by the branch above.
		const uint32_t remain = (cfg.fadeMs - t) * 255u / cfg.fadeMs;
		u.paint.baseLevel =
			static_cast<uint8_t>((s.fadeFrom.baseLevel * remain) / 255u);
		u.paint.highlightLevel = static_cast<uint8_t>(
			(s.fadeFrom.highlightLevel * remain) / 255u);
		// The geometry is carried through unchanged. A post-strike fade
		// still fades the whole ring, not one pixel, and getting this
		// wrong is the sort of thing that looks like "the ring is
		// partly broken" rather than like a bug.
		u.paint.highlightIndex = s.fadeFrom.highlightIndex;
		u.paint.highlightCount = s.fadeFrom.highlightCount;
		break;
	}
	}

	s.lastPaint = u.paint;
	return u;
}

}  // namespace retroroom_core
