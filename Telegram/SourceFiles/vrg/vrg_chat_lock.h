/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <set>
#include <vector>

namespace Vrg {

class ChatLock final {
public:
	struct LockedEntry {
		uint64 peerId = 0;
		QString title;
	};

	[[nodiscard]] static bool HasPassword(uint64 peerId) {
		return !Conf::Str(Key(peerId)).isEmpty();
	}

	[[nodiscard]] static bool IsUnlocked(uint64 peerId) {
		return UnlockedSet().contains(peerId);
	}

	static void Unlock(uint64 peerId) {
		UnlockedSet().insert(peerId);
	}

	static void LockAll() {
		UnlockedSet().clear();
	}

	static void SetPassword(
			uint64 peerId,
			const QString &password,
			const QString &title = QString()) {
		if (password.isEmpty()) {
			ClearPassword(peerId);
			return;
		}
		Conf::Set(Key(peerId), Hash(password));
		if (!title.isEmpty()) {
			Conf::Set(NameKey(peerId), title);
		}
		UnlockedSet().erase(peerId);
	}

	static void ClearPassword(uint64 peerId) {
		Conf::Set(Key(peerId), QString());
		Conf::Set(NameKey(peerId), QString());
		UnlockedSet().erase(peerId);
	}

	[[nodiscard]] static bool Verify(uint64 peerId, const QString &password) {
		const auto expected = Conf::Str(Key(peerId));
		return !expected.isEmpty() && (expected == Hash(password));
	}

	[[nodiscard]] static std::vector<LockedEntry> ListLocked() {
		auto result = std::vector<LockedEntry>();
		QFile file(QDir::homePath() + QStringLiteral("/.config/vrg/vrg.conf"));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			return result;
		}
		auto names = QMap<uint64, QString>();
		auto locked = std::vector<uint64>();
		while (!file.atEnd()) {
			const auto line = QString::fromUtf8(file.readLine()).trimmed();
			const auto eq = line.indexOf('=');
			if (eq <= 0) {
				continue;
			}
			const auto k = line.left(eq);
			const auto v = line.mid(eq + 1);
			if (k.startsWith(QStringLiteral("lock_chat_name_"))) {
				const auto id = k.mid(15).toULongLong();
				if (id && !v.isEmpty()) {
					names.insert(id, v);
				}
			} else if (k.startsWith(QStringLiteral("lock_chat_"))) {
				const auto id = k.mid(10).toULongLong();
				if (id && !v.isEmpty()) {
					locked.push_back(id);
				}
			}
		}
		for (const auto id : locked) {
			result.push_back({
				.peerId = id,
				.title = names.value(id, QString::number(id)),
			});
		}
		return result;
	}

private:
	[[nodiscard]] static QString Key(uint64 peerId) {
		return QStringLiteral("lock_chat_") + QString::number(peerId);
	}

	[[nodiscard]] static QString NameKey(uint64 peerId) {
		return QStringLiteral("lock_chat_name_") + QString::number(peerId);
	}

	[[nodiscard]] static QString Hash(const QString &password) {
		return QString::fromLatin1(
			QCryptographicHash::hash(
				password.toUtf8(),
				QCryptographicHash::Sha256).toHex());
	}

	[[nodiscard]] static std::set<uint64> &UnlockedSet() {
		static std::set<uint64> set;
		return set;
	}
};

}
