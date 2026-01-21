#pragma once
#ifndef COMMON_STOPWATCH_CLASS_H
#define COMMON_STOPWATCH_CLASS_H

#include <chrono>

namespace cmn {
	class Stopwatch {
		std::chrono::steady_clock::time_point m_start, m_end;

		auto getTime() const {
			return std::chrono::steady_clock::now();
		}

	public:
		Stopwatch() {}

		void start() {
			m_start=std::chrono::steady_clock::now();
		}

		void stop() {
			m_end=std::chrono::steady_clock::now();
		}

		long long getNanos() const {
			return std::chrono::duration_cast<std::chrono::nanoseconds>(m_end-m_start).count();
		}

		long long getMicros() const {
			return std::chrono::duration_cast<std::chrono::microseconds>(m_end-m_start).count();
		}

		long long getMillis() const {
			return std::chrono::duration_cast<std::chrono::milliseconds>(m_end-m_start).count();
		}
	};
}
#endif