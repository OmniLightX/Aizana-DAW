#include "ApplicationState.h"

// ApplicationState is a simple aggregate with an inline constructor in the header.
// This .cpp file exists to satisfy the CMakeLists target_sources declaration and
// provides a natural place for future per-method implementations when the struct
// grows (e.g., project file open/save methods in plan 01-08).
