#pragma once

#include <stdint.h>

/** Fixed-size rolling average over the last N pushed samples. */
template <int N>
class RollingAverage
{
public:
	/** Add a sample and return the new average across all samples held. */
	int32_t push(int32_t v)
	{
		m_buf[m_next] = v;
		m_next = (m_next + 1) % N;
		if (m_count < N)
			m_count++;

		int32_t sum = 0;
		for (int i = 0; i < m_count; i++)
			sum += m_buf[i];
		return sum / m_count;
	}

private:
	int32_t m_buf[N]{};
	int m_count = 0;
	int m_next = 0;
};
