/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QString>
#include <map>

namespace Vrg::Rename {

namespace Detail {

[[nodiscard]] inline std::map<uint64, QString> &Cache() {
	static std::map<uint64, QString> map;
	return map;
}

[[nodiscard]] inline QString Key(uint64 peerId) {
	return u"rename_"_q + QString::number(peerId);
}

}

[[nodiscard]] inline const QString *Lookup(uint64 peerId) {
	auto &cache = Detail::Cache();
	auto it = cache.find(peerId);
	if (it == cache.end()) {
		const auto val = Conf::Str(Detail::Key(peerId)).trimmed();
		it = cache.emplace(peerId, val).first;
	}
	return it->second.isEmpty() ? nullptr : &it->second;
}

inline void Set(uint64 peerId, const QString &name) {
	const auto trimmed = name.trimmed();
	Conf::Set(Detail::Key(peerId), trimmed);
	Detail::Cache()[peerId] = trimmed;
}

}
