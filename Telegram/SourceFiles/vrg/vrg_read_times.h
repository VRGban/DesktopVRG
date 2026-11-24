#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QString>

#include <vector>

namespace Vrg::ReadTimes {

struct Marker {
	qint64 upTo = 0;
	qint64 time = 0;
};

constexpr auto kMaxMarkers = 400;

[[nodiscard]] inline QString StorePath() {
	return QDir::homePath() + QStringLiteral("/.config/vrg/read_times.json");
}

[[nodiscard]] inline QHash<quint64, std::vector<Marker>> &Store() {
	static QHash<quint64, std::vector<Marker>> map;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		QFile file(StorePath());
		if (file.open(QIODevice::ReadOnly)) {
			const auto obj = QJsonDocument::fromJson(file.readAll()).object();
			for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
				auto list = std::vector<Marker>();
				for (const auto &value : it.value().toArray()) {
					const auto pair = value.toArray();
					if (pair.size() == 2) {
						list.push_back({
							qint64(pair[0].toDouble()),
							qint64(pair[1].toDouble()) });
					}
				}
				map.insert(it.key().toULongLong(), std::move(list));
			}
		}
	}
	return map;
}

inline void Save() {
	QDir().mkpath(QDir::homePath() + QStringLiteral("/.config/vrg"));
	auto obj = QJsonObject();
	const auto &map = Store();
	for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
		auto list = QJsonArray();
		for (const auto &marker : it.value()) {
			list.append(QJsonArray{ double(marker.upTo), double(marker.time) });
		}
		obj.insert(QString::number(it.key()), list);
	}
	QSaveFile file(StorePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

inline void Record(quint64 peerId, qint64 upTo) {
	if (!peerId || upTo <= 0) {
		return;
	}
	auto &list = Store()[peerId];
	if (!list.empty() && list.back().upTo >= upTo) {
		return;
	}
	list.push_back({ upTo, QDateTime::currentSecsSinceEpoch() });
	if (int(list.size()) > kMaxMarkers) {
		list.erase(list.begin(), list.begin() + (list.size() - kMaxMarkers));
	}
	Save();
}

[[nodiscard]] inline qint64 When(quint64 peerId, qint64 msgId) {
	const auto &map = Store();
	const auto it = map.constFind(peerId);
	if (it == map.constEnd()) {
		return 0;
	}
	for (const auto &marker : it.value()) {
		if (marker.upTo >= msgId) {
			return marker.time;
		}
	}
	return 0;
}

}

