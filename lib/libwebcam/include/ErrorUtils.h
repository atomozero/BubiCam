/*
 * ErrorUtils.h compatibility redirect for WebcamKit library builds.
 * Maps to WebcamLog.h which provides LOG_* macros and ColorSpaceName()
 * without the BAlert-based helpers that depend on the application layer.
 */
#include "WebcamLog.h"
