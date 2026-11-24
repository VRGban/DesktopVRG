/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QStringList>
#include <vector>

namespace Vrg {

class Pins final {
public:
	[[nodiscard]] static bool IsPinned(uint64 peerId) {
		return Index(peerId) > 0;
	}

	[[nodiscard]] static int Index(uint64 peerId) {
		const auto list = List();
		for (int i = 0; i < int(list.size()); ++i) {
			if (list[i] == peerId) {
				return 10 + i;
			}
		}
		return 0;
	}

	static void Toggle(uint64 peerId) {
		auto list = List();
		const auto it = std::find(list.begin(), list.end(), peerId);
		if (it != list.end()) {
			list.erase(it);
		} else {
			list.push_back(peerId);
		}
		Save(list);
	}

private:
	[[nodiscard]] static std::vector<uint64> List() {
		auto result = std::vector<uint64>();
		const auto raw = Conf::Str("local_pins");
		if (raw.isEmpty()) {
			return result;
		}
		for (const auto &part : raw.split(',', Qt::SkipEmptyParts)) {
			const auto id = part.toULongLong();
			if (id) {
				result.push_back(id);
			}
		}
		return result;
	}

	static void Save(const std::vector<uint64> &list) {
		QStringList parts;
		parts.reserve(int(list.size()));
		for (const auto id : list) {
			parts.push_back(QString::number(id));
		}
		Conf::Set("local_pins", parts.join(','));
	}
};

}

