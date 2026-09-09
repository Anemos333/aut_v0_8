#include <JuceHeader.h>

// The focused supervisor test intentionally exposes ModernPitchEngine internals.
// MSVC encodes access control in the decorated name for these private out-of-line
// functions, so compile the implementation under the same test-only access view
// used by SupervisorContinuityTest. JUCE is included first so the macro cannot
// affect framework or standard-library declarations.
#define private public
#include "../Source/ModernPitchEngine.cpp"
#undef private
