// AileFlowKiLib.cpp
// Satisfies kilib's link-time requirements when using wWinMain instead of kilib_startUp().
//
// AileFlow uses AileEx's wWinMain, so kilib_startUp() is never called.
// kilib_create_new_app() is declared extern in kilib.h and referenced from
// kl_app.cpp, so it must be defined even though it is never invoked.
//
// NOTE (Phase 3): Before B2E archive operations can run, kiStr::init() and
// kiWindow::init() must be called (they are normally invoked by kilib_startUp()).
// Add a kilib_standalone_init() call from App::Init() or similar once Phase 3 begins.

#include "AileFlowApp.h"

// Required by kilib.h / kl_app.cpp — called by kilib_startUp() which AileFlow does not use.
void kilib_create_new_app() {}
