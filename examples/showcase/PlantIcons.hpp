#pragma once
//
// A domain icon pack, registered at startup.
//
// This is what "extending icons" looks like from the outside: no library edit,
// no enum change, no build step.  Both ingest paths are exercised --
//
//   * code-drawn, for symbols invented here (pump / valve / tank)
//   * SVG path data, for adopting an existing 24x24 stroke set
//
// and both come back as ordinary `Icon` handles, so they go straight into
// PushButton::setIcon, MenuItem::icon, WindowHeader::setIcon and the rest.
//
namespace showcase {

// Idempotent: safe to call more than once.
void registerPlantIcons();

}  // namespace showcase
