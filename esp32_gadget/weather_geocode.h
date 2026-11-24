#pragma once

// The two parsers above live in one translation unit because they share the same
// small JSON helpers and the same "refuse a half answer" rule. This header only
// re-exports their declarations so callers include one thing.
#include "weather_parse.h"
