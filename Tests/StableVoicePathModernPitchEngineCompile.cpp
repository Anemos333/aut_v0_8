#include <JuceHeader.h>

// Compile the implementation with the same test-only access view used by the
// focused invariant test. This avoids MSVC access-control name mangling issues.
#define private public
#include "../Source/ModernPitchEngine.cpp"
#undef private
