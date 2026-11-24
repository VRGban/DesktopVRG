/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <map>
#include <utility>
#include <vector>

namespace Vrg {

class EditHistory final {
public:
	struct Entry {
		QString text;
		qint64 timestamp = 0;
	};

	static void Record(uint64 peerId, int64 msgId, const QString &oldText) {
		if (oldText.isEmpty() || !Conf::Bool("keep_edited", true)) {
			return;
		}
		auto &vec = Map()[{ peerId, msgId }];
		if (!vec.empty() && vec.back().text == oldText) {
			return;
		}
		const auto entry = Entry{
			.text = oldText,
			.timestamp = QDateTime::currentSecsSinceEpoch(),
		};
		vec.push_back(entry);
		AppendToDisk(peerId, msgId, entry);
	}

	[[nodiscard]] static std::vector<Entry> For(uint64 peerId, int64 msgId) {
		const auto &map = Map();
		const auto it = map.find({ peerId, msgId });
		return (it != map.end()) ? it->second : std::vector<Entry>{};
	}

	[[nodiscard]] static bool Has(uint64 peerId, int64 msgId) {
		const auto &map = Map();
		const auto it = map.find({ peerId, msgId });
		return (it != map.end()) && !it->second.empty();
	}

private:
	using Key = std::pair<uint64, int64>;
	static constexpr quint32 kMagic = 0x56524745;
	static constexpr quint32 kVersion = 1;

	[[nodiscard]] static QString FilePath() {
		return QDir::homePath() + u"/.local/share/vrg/edit_history.dat"_q;
	}

	static void AppendToDisk(
			uint64 peerId,
			int64 msgId,
			const Entry &entry) {
		const auto path = FilePath();
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile f(path);
		const auto needHeader = !f.exists() || f.size() == 0;
		if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) {
			return;
		}
		QDataStream out(&f);
		out.setVersion(QDataStream::Qt_6_0);
		if (needHeader) {
			out << kMagic << kVersion;
		}
		out << quint64(peerId)
			<< qint64(msgId)
			<< qint64(entry.timestamp)
			<< entry.text;
	}

	static void LoadFromDisk(std::map<Key, std::vector<Entry>> &map) {
		QFile f(FilePath());
		if (!f.open(QIODevice::ReadOnly)) {
			return;
		}
		QDataStream in(&f);
		in.setVersion(QDataStream::Qt_6_0);
		quint32 magic = 0;
		quint32 version = 0;
		in >> magic >> version;
		if (magic != kMagic || version != kVersion || in.status() != QDataStream::Ok) {
			return;
		}
		while (!in.atEnd()) {
			quint64 peerId = 0;
			qint64 msgId = 0;
			qint64 timestamp = 0;
			QString text;
			in >> peerId >> msgId >> timestamp >> text;
			if (in.status() != QDataStream::Ok) {
				break;
			}
			if (!text.isEmpty()) {
				auto &vec = map[{ peerId, msgId }];
				if (vec.empty() || vec.back().text != text) {
					vec.push_back({ .text = std::move(text), .timestamp = timestamp });
				}
			}
		}
	}

	[[nodiscard]] static std::map<Key, std::vector<Entry>> &Map() {
		static std::map<Key, std::vector<Entry>> map;
		static bool loaded = false;
		if (!loaded) {
			loaded = true;
			LoadFromDisk(map);
		}
		return map;
	}
};

}

