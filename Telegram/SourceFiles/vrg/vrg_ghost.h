/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Vrg {

class Ghost final {
public:
	[[nodiscard]] static bool Read() {
		return Conf::Bool("ghost_read", false);
	}
	[[nodiscard]] static bool Typing() {
		return Conf::Bool("ghost_typing", false);
	}
	[[nodiscard]] static bool Online() {
		return Conf::Bool("ghost_online", false);
	}
	[[nodiscard]] static bool Stories() {
		return Conf::Bool("ghost_stories", false);
	}
	[[nodiscard]] static bool OfflineAfterSend() {
		return Conf::Bool("ghost_offline_send", false);
	}
	[[nodiscard]] static bool SilentSend() {
		return Conf::Bool("ghost_silent_send", false);
	}
	[[nodiscard]] static bool ReadOnSend() {
		return Conf::Bool("ghost_read_on_send", false);
	}
	[[nodiscard]] static bool VoiceContents() {
		return Conf::Bool("ghost_voice_contents", false);
	}

	[[nodiscard]] static int ReadOverride(uint64 peerId) {
		const auto key = QStringLiteral("ghost_chat_") + QString::number(peerId);
		const auto mode = Conf::Int(key, 0);
		return (mode == 1 || mode == 2) ? mode : 0;
	}

	static void SetReadOverride(
			uint64 peerId,
			int mode,
			const QString &title = QString()) {
		const auto key = QStringLiteral("ghost_chat_") + QString::number(peerId);
		const auto nameKey = QStringLiteral("ghost_chat_name_") + QString::number(peerId);
		if (mode != 1 && mode != 2) {
			Conf::Set(key, QStringLiteral("0"));
			Conf::Set(nameKey, QString());
			return;
		}
		Conf::SetInt(key, mode);
		if (!title.isEmpty()) {
			Conf::Set(nameKey, title);
		}
	}

	[[nodiscard]] static bool ShouldBlockRead(uint64 peerId) {
		const auto over = ReadOverride(peerId);
		if (over == 1) {
			return false;
		}
		if (over == 2) {
			return true;
		}
		return Read();
	}

	[[nodiscard]] static qint64 LocalReadTill(uint64 peerId) {
		return LocalReadMap().value(peerId, 0);
	}

	static void SetLocalReadTill(uint64 peerId, qint64 msgId) {
		if (!peerId || msgId <= 0) {
			return;
		}
		auto &map = LocalReadMap();
		if (map.value(peerId, 0) >= msgId) {
			return;
		}
		map.insert(peerId, msgId);
		SaveLocalReadMap(map);
	}

	struct ChatOverride {
		uint64 peerId = 0;
		int mode = 0;
		QString title;
	};

	[[nodiscard]] static std::vector<ChatOverride> ListReadOverrides() {
		auto result = std::vector<ChatOverride>();
		QFile file(QDir::homePath() + QStringLiteral("/.config/vrg/vrg.conf"));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			return result;
		}
		auto names = QMap<uint64, QString>();
		auto modes = QMap<uint64, int>();
		while (!file.atEnd()) {
			const auto line = QString::fromUtf8(file.readLine()).trimmed();
			const auto eq = line.indexOf('=');
			if (eq <= 0) {
				continue;
			}
			const auto k = line.left(eq);
			const auto v = line.mid(eq + 1);
			if (k.startsWith(QStringLiteral("ghost_chat_name_"))) {
				const auto id = k.mid(16).toULongLong();
				if (id) {
					names.insert(id, v);
				}
			} else if (k.startsWith(QStringLiteral("ghost_chat_"))) {
				const auto id = k.mid(11).toULongLong();
				const auto m = v.toInt();
				if (id && (m == 1 || m == 2)) {
					modes.insert(id, m);
				}
			}
		}
		for (auto it = modes.constBegin(); it != modes.constEnd(); ++it) {
			result.push_back({
				.peerId = it.key(),
				.mode = it.value(),
				.title = names.value(it.key(), QString::number(it.key())),
			});
		}
		return result;
	}

private:
	[[nodiscard]] static QString LocalReadPath() {
		return QDir::homePath() + QStringLiteral("/.config/vrg/ghost_read.json");
	}

	[[nodiscard]] static QHash<uint64, qint64> &LocalReadMap() {
		static QHash<uint64, qint64> map;
		static bool loaded = false;
		if (!loaded) {
			loaded = true;
			QFile f(LocalReadPath());
			if (f.open(QIODevice::ReadOnly)) {
				const auto doc = QJsonDocument::fromJson(f.readAll());
				const auto obj = doc.object();
				for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
					const auto pid = it.key().toULongLong();
					const auto mid = qint64(it.value().toDouble(0));
					if (pid && mid > 0) {
						map.insert(pid, mid);
					}
				}
			}
		}
		return map;
	}

	static void SaveLocalReadMap(const QHash<uint64, qint64> &map) {
		QDir().mkpath(QDir::homePath() + QStringLiteral("/.config/vrg"));
		QJsonObject obj;
		for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
			obj.insert(QString::number(it.key()), double(it.value()));
		}
		QSaveFile f(LocalReadPath());
		if (f.open(QIODevice::WriteOnly)) {
			f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
			f.commit();
		}
	}
};

}

