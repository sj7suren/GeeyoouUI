#pragma once
//
// Tooltip bubble: the small hover hint the Window floats over everything else.
//
// The drawing lives here, not inside Window, for one reason: it is the ONE part
// of the tooltip that a screenshot can check.  The Window owns the *behaviour*
// -- when a hint arms, how long it rests, when it hides -- and that is covered
// by the tooltip tests driving real timers.  The *look* -- the bubble shape,
// the padding, that the text is not clipped and CJK is not tofu -- is only ever
// seen by a human, so the offscreen gallery (examples/tutorial/widget_shots)
// calls this exact function to render a shot.  Sharing the code is what makes
// the shot proof of the real render rather than of a lookalike.
//
#include <string>

#include "geeyoou/core/Types.hpp"

namespace geeyoou {

class Painter;

// Draws a tooltip bubble carrying `text`, offset from `anchor` (in the same
// coordinate space as `bounds`) and clamped to stay inside `bounds` -- flipping
// to the other side of the anchor when there is no room below or to the right.
// Returns the bubble rectangle actually drawn.  Draws nothing and returns an
// empty rect when `text` is empty.
Rect paintTooltipBubble(Painter& p, const Rect& bounds, Point anchor,
                        const std::string& text);

}  // namespace geeyoou
