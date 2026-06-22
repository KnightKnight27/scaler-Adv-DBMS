// ============================================================================
// common.h  --  One header to include the standard-library types we use a lot,
// plus MiniDB's own fundamental types and the exception class.
//
// Instead of writing a long list of #include <...> lines at the top of every
// file, each MiniDB source file just includes "common/common.h".  This keeps
// the files short and makes it obvious what the project relies on.
// ============================================================================
#pragma once

// --- Standard library bits used across the codebase ---
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <list>
#include <mutex>
#include <fstream>
#include <iostream>
#include <sstream>
#include <variant>
#include <stdexcept>
#include <algorithm>
#include <functional>

// Pull the standard library into scope so the rest of the code can write
// `string`, `vector`, `map`, `make_unique`, ... without the `std::` prefix.
using namespace std;

// --- MiniDB fundamentals (page ids, RID, error type) ---
#include "common/types.h"
#include "common/exception.h"
