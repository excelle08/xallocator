#include "Fault.h"
#include "DataTypes.h"
#include <assert.h>

#if defined(_WIN32) || defined(_WIN64)
#include <debugapi.h>
void trap() { DebugBreak(); }
#else
#include <signal.h>
void trap() {
#ifdef SIGTRAP
  raise(SIGTRAP);
#else
  raise(SIGABRT);
#endif
}
#endif

//----------------------------------------------------------------------------
// FaultHandler
//----------------------------------------------------------------------------
void FaultHandler(const char* file, unsigned short line) {
  // If you hit this line, it means one of the ASSERT macros failed.
  trap();

  assert(0);
}
