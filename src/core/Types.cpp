#include "geeyoou/core/Types.hpp"

namespace geeyoou {

Color Color::lerp(const Color& other, float t) const {
  t = std::clamp(t, 0.0f, 1.0f);
  const auto mix = [t](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(float(a) + (float(b) - float(a)) * t + 0.5f);
  };
  return Color::rgba(mix(red(), other.red()), mix(green(), other.green()),
                     mix(blue(), other.blue()), mix(alpha(), other.alpha()));
}

}  // namespace geeyoou
