// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/version.hpp"

#include <cstdlib>

int main() { return mmxisf::version() == "0.0.0" ? EXIT_SUCCESS : EXIT_FAILURE; }
