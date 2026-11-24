#pragma once

#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QString>

namespace Vrg::GhostMentions {

[[nodiscard]] inline QString StorePath() {
	return QDir::homePath() + QStringLiteral("/.config/vrg/ghost_mentions.json");
}

[[nodiscard]] inline QString KeyOf(quint64 peerId, qint64 root) {
	return QString::number(peerId) + QChar('/') + QString::number(root);
}

[[nodiscard]] inline QHash<QString, QSet<qint64>> &Store() {
	static QHash<QString, QSet<qint64>> map;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		QFile file(StorePath());
		if (file.open(QIODevice::ReadOnly)) {
			const auto obj = QJsonDocument::fromJson(file.readAll()).object();
			for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
				auto ids = QSet<qint64>();
				for (const auto &value : it.value().toArray()) {
					ids.insert(qint64(value.toDouble()));
				}
				if (!ids.isEmpty()) {
					map.insert(it.key(), ids);
				}
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
		auto ids = QJsonArray();
		for (const auto id : it.value()) {
			ids.append(double(id));
		}
		obj.insert(it.key(), ids);
	}
	QSaveFile file(StorePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

inline void Record(quint64 peerId, qint64 root, qint64 msgId) {
	if (!peerId || msgId <= 0) {
		return;
	}
	auto &map = Store();
	auto changed = false;
	changed |= !map[KeyOf(peerId, 0)].contains(msgId);
	map[KeyOf(peerId, 0)].insert(msgId);
	if (root > 0) {
		changed |= !map[KeyOf(peerId, root)].contains(msgId);
		map[KeyOf(peerId, root)].insert(msgId);
	}
	if (changed) {
		Save();
	}
}

[[nodiscard]] inline bool Contains(quint64 peerId, qint64 msgId) {
	const auto &map = Store();
	const auto it = map.constFind(KeyOf(peerId, 0));
	return (it != map.constEnd()) && it->contains(msgId);
}

[[nodiscard]] inline int Adjusted(quint64 peerId, qint64 root, int serverCount) {
	auto &map = Store();
	const auto key = KeyOf(peerId, root);
	const auto it = map.find(key);
	if (it == map.end()) {
		return serverCount;
	}
	const auto have = int(it->size());
	if (serverCount <= 0 || serverCount < have) {
		map.erase(it);
		Save();
		return std::max(serverCount, 0);
	}
	return serverCount - have;
}

}

