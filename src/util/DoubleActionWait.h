#pragma once

// Whether a Short press must wait out the double-click window before it is reported.
//
// Only when a double action is configured AND it can do something where the press lands. An
// action that works in the reader only (CrossPointSettings::isReaderScopedAction) falls through to
// the screen anywhere else, so waiting for it there only delays every press, and turns two quick
// presses into one Double that no list understands.
inline bool doubleActionNeedsWait(const bool configured, const bool readerScoped, const bool readerOnTop) {
  return configured && (!readerScoped || readerOnTop);
}
