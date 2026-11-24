/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include <QtCore/QString>
#include <array>
#include <utility>

namespace Vrg::RegDate {

[[nodiscard]] inline QString Estimate(uint64 userId) {
	if (!userId) {
		return QString();
	}
	struct Anchor {
		uint64 id;
		int year;
		int month;
	};
	static constexpr std::array<Anchor, 20> kAnchors = {{
		{ 2768409ULL,    2013, 11 },
		{ 7679610ULL,    2014, 1 },
		{ 20000000ULL,   2014, 8 },
		{ 100000000ULL,  2015, 5 },
		{ 200000000ULL,  2016, 3 },
		{ 350000000ULL,  2017, 4 },
		{ 500000000ULL,  2018, 2 },
		{ 750000000ULL,  2018, 11 },
		{ 1000000000ULL, 2019, 11 },
		{ 1400000000ULL, 2020, 9 },
		{ 2000000000ULL, 2021, 9 },
		{ 5000000000ULL, 2021, 11 },
		{ 5500000000ULL, 2022, 7 },
		{ 6000000000ULL, 2023, 2 },
		{ 6500000000ULL, 2023, 9 },
		{ 7000000000ULL, 2024, 3 },
		{ 7500000000ULL, 2024, 9 },
		{ 8000000000ULL, 2025, 4 },
		{ 8500000000ULL, 2025, 11 },
		{ 9000000000ULL, 2026, 6 },
	}};
	static constexpr const char *kMonths[12] = {
		"Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
		"Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"
	};

	if (userId <= kAnchors.front().id) {
		return QString::fromUtf8("до ноября 2013");
	}
	if (userId >= kAnchors.back().id) {
		return QString::fromUtf8("после июня 2026");
	}
	for (size_t i = 1; i < kAnchors.size(); ++i) {
		const auto &a = kAnchors[i - 1];
		const auto &b = kAnchors[i];
		if (userId <= b.id) {
			const auto t = double(userId - a.id) / double(b.id - a.id);
			const auto mA = a.year * 12 + (a.month - 1);
			const auto mB = b.year * 12 + (b.month - 1);
			const auto m = int(mA + t * (mB - mA) + 0.5);
			const auto year = m / 12;
			const auto month = m % 12;
			return u"~"_q
				+ QString::fromUtf8(kMonths[month])
				+ u" "_q
				+ QString::number(year);
		}
	}
	return QString();
}

}

