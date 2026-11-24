/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QString>
#include <array>
#include <cmath>
#include <vector>

namespace Vrg::Music {

struct EqPreset {
	QString name;
	std::array<float, 5> gainsDb;
};

inline const std::vector<EqPreset> &DefaultEqPresets() {
	static const auto presets = std::vector<EqPreset>{
		{ QStringLiteral("Flat"), { 0.f, 0.f, 0.f, 0.f, 0.f } },
		{ QStringLiteral("Bass Boost"), { 8.f, 5.f, 0.f, 1.f, 2.f } },
		{ QStringLiteral("Phonk / Trap"), { 9.f, 4.f, -2.f, 4.f, 6.f } },
		{ QStringLiteral("Rock"), { 5.f, 2.f, -2.f, 3.f, 5.f } },
		{ QStringLiteral("Vocal"), { -3.f, -1.f, 4.f, 5.f, 2.f } },
	};
	return presets;
}

class EqualizerDsp final {
public:
	static constexpr std::array<float, 5> kFrequencies = {
		60.f, 230.f, 910.f, 3600.f, 14000.f
	};

	void setSampleRate(int sampleRate) {
		QMutexLocker lock(&_mutex);
		if (sampleRate > 0 && _sampleRate != sampleRate) {
			_sampleRate = sampleRate;
			recomputeAll();
		}
	}

	void setBandGain(int band, float gainDb) {
		if (band < 0 || band >= 5) {
			return;
		}
		QMutexLocker lock(&_mutex);
		_gains[band] = std::clamp(gainDb, -12.f, 12.f);
		recomputeBand(band);
	}

	void setAllGains(const std::array<float, 5> &gainsDb) {
		QMutexLocker lock(&_mutex);
		for (int i = 0; i < 5; ++i) {
			_gains[i] = std::clamp(gainsDb[i], -12.f, 12.f);
		}
		recomputeAll();
	}

	[[nodiscard]] std::array<float, 5> gains() const {
		QMutexLocker lock(&_mutex);
		return _gains;
	}

	void setEnabled(bool enabled) {
		QMutexLocker lock(&_mutex);
		_enabled = enabled;
	}

	[[nodiscard]] bool enabled() const {
		QMutexLocker lock(&_mutex);
		return _enabled;
	}

	void setVolumeGain(float gain) {
		QMutexLocker lock(&_mutex);
		_volumeGain = std::clamp(gain, 0.f, 1.f);
	}

	[[nodiscard]] float volumeGain() const {
		QMutexLocker lock(&_mutex);
		return _volumeGain;
	}

	void setBoostGain(float boost) {
		QMutexLocker lock(&_mutex);
		_boostGain = std::clamp(boost, 1.f, 10.f);
	}

	[[nodiscard]] float boostGain() const {
		QMutexLocker lock(&_mutex);
		return _boostGain;
	}

	void resetState() {
		QMutexLocker lock(&_mutex);
		for (auto &b : _bands) {
			b.z1[0] = b.z1[1] = 0.f;
			b.z2[0] = b.z2[1] = 0.f;
		}
	}

	void processStereoFloat(float *samples, int frameCount) {
		QMutexLocker lock(&_mutex);
		const auto eqOn = _enabled;
		const auto vol = _volumeGain * _boostGain;
		for (int i = 0; i < frameCount; ++i) {
			float l = samples[i * 2];
			float r = samples[i * 2 + 1];
			if (eqOn) {
				for (int b = 0; b < 5; ++b) {
					l = _bands[b].process(0, l);
					r = _bands[b].process(1, r);
				}
			}
			l *= vol;
			r *= vol;
			if (vol > 1.f || std::abs(l) > 0.95f || std::abs(r) > 0.95f) {
				l = std::tanh(l);
				r = std::tanh(r);
			} else {
				l = std::clamp(l, -1.f, 1.f);
				r = std::clamp(r, -1.f, 1.f);
			}
			samples[i * 2] = l;
			samples[i * 2 + 1] = r;
		}
	}

private:
	struct Biquad {
		float b0 = 1.f, b1 = 0.f, b2 = 0.f;
		float a1 = 0.f, a2 = 0.f;
		float z1[2] = { 0.f, 0.f };
		float z2[2] = { 0.f, 0.f };

		float process(int ch, float in) {
			const float out = in * b0 + z1[ch];
			z1[ch] = in * b1 + z2[ch] - a1 * out;
			z2[ch] = in * b2 - a2 * out;
			return out;
		}
	};

	void recomputeAll() {
		for (int i = 0; i < 5; ++i) {
			recomputeBand(i);
		}
	}

	void recomputeBand(int i) {
		const float freq = kFrequencies[i];
		const float gainDb = _gains[i];
		if (std::abs(gainDb) < 0.05f || _sampleRate <= 0) {
			_bands[i].b0 = 1.f;
			_bands[i].b1 = 0.f;
			_bands[i].b2 = 0.f;
			_bands[i].a1 = 0.f;
			_bands[i].a2 = 0.f;
			return;
		}
		constexpr float kPi = 3.14159265358979323846f;
		const float A = std::pow(10.f, gainDb / 40.f);
		const float w0 = 2.f * kPi * std::min(freq, _sampleRate * 0.45f) / float(_sampleRate);
		const float cosW0 = std::cos(w0);
		const float sinW0 = std::sin(w0);
		const float Q = 1.0f;
		const float alpha = sinW0 / (2.f * Q);

		const float b0 = 1.f + alpha * A;
		const float b1 = -2.f * cosW0;
		const float b2 = 1.f - alpha * A;
		const float a0 = 1.f + alpha / A;
		const float a1 = -2.f * cosW0;
		const float a2 = 1.f - alpha / A;

		_bands[i].b0 = b0 / a0;
		_bands[i].b1 = b1 / a0;
		_bands[i].b2 = b2 / a0;
		_bands[i].a1 = a1 / a0;
		_bands[i].a2 = a2 / a0;
	}

	mutable QMutex _mutex;
	int _sampleRate = 44100;
	bool _enabled = true;
	float _volumeGain = 0.8f;
	float _boostGain = 1.0f;
	std::array<float, 5> _gains = { 0.f, 0.f, 0.f, 0.f, 0.f };
	std::array<Biquad, 5> _bands;

};

}

