// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) && !defined(MMXISF_STATIC_DEFINE)
#if defined(MMXISF_BUILDING_LIBRARY)
#define MMXISF_API __declspec(dllexport)
#else
#define MMXISF_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MMXISF_API __attribute__((visibility("default")))
#else
#define MMXISF_API
#endif
