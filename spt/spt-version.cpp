#include <stdafx.hpp>

/*
* We've previously had issues with the version information not updating automatically and the
* version string being different across different files because it was defined as a macro. This
* file should be recompiled every time to keep the version info up to date - this is done through
* a post build step, see https://stackoverflow.com/a/70000231.
*/

#define SPT_VERSION_MACRO __DATE__ " " __TIME__

const char* SPT_VERSION = SPT_VERSION_MACRO;
const char* SPT_DESCRIPTION = "SourcePauseTool v" SPT_VERSION_MACRO;
