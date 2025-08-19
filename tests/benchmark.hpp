// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <nanobench.h>

inline ankerl::nanobench::Bench default_benchmark() {
	ankerl::nanobench::Bench b;
	b.relative(true).performanceCounters(true).minEpochIterations(5).warmup(1);
	return b;
}