#pragma once

template<uint16_t FROM, uint16_t TO>
struct SmallFloat {
	static uint16_t convertTo(float f) {
		return (max(min(f, static_cast<float>(TO)), static_cast<float>(FROM)) - FROM) / (TO - FROM) * UINT16_MAX;
	}

	static float convertFrom(uint16_t i) {
		return static_cast<float>(i) / UINT16_MAX * (TO - FROM) + FROM;
	}

	SmallFloat() : m_data(0) {}
	SmallFloat(float f) : m_data(convertTo(f)) {}
	SmallFloat(const SmallFloat& f) : m_data(f.m_data) {}

	SmallFloat& operator=(const SmallFloat& f) {
		m_data = f.m_data;
		return *this;
	}

	SmallFloat& operator=(float f) {
		m_data = convertTo(f);
		return *this;
	}

	operator float() const {
		return convertFrom(m_data);
	}

	uint16_t m_data;
};
