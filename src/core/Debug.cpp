#include "Debug.h"

#if DEBUG_ENABLED
Debug::LogLevel Debug::currentLevel = static_cast<Debug::LogLevel>(DEBUG_LEVEL);
#else
Debug::LogLevel Debug::currentLevel = Debug::ERROR; // Default to ERROR level when debugging is disabled
#endif
