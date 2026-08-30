#pragma once

namespace RE8HT {

// Per-element GUI draw callback dispatcher.
// Returns true to keep drawing the element, false to hide.
bool OnPreGuiDrawElement(void* element, void* context);

// F9: hide the world-anchored GUI markers, leaving the rest of the HUD alone.
//
// The hotkey callback fires on the poller's background thread and the flag is
// read by the GUI draw callback on the render thread, which also mutates the
// element dumper's unordered_set. Toggling from the hotkey thread would race
// that set, so the hotkey only requests and ProcessDeferredActions() runs it.
void RequestToggleMarkersHidden();
void ProcessDeferredActions();
bool AreMarkersHidden();

} // namespace RE8HT
