// The animate() easing curves. The nine curve ids and the EASE_IN/EASE_OUT flag bits follow the compiler's constants.
// The curve shapes are a formula choice, not DreamSeeker's own curve. All arithmetic is float64 with cos/sqrt; ELASTIC
// uses libm pow/sin.
#pragma once
#include <cstdint>

namespace fugue::render {

// Ease: raw progress t (0 = From, 1 = To) through the easing byte. Out-of-range t is clamped.
double ease(double t, uint8_t easing);

// Test-only hook; 0 is the shipped behaviour.
void setEasingMutation(int m);

}  // namespace fugue::render
