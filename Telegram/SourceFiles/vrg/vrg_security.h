/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QString>

namespace Vrg {

class Security final {
public:
	[[nodiscard]] static bool HasMaster() {
		return !Conf::Str(QStringLiteral("vrg_master_hash")).isEmpty();
	}

	[[nodiscard]] static bool VerifyMaster(const QString &password) {
		const auto expected = Conf::Str(QStringLiteral("vrg_master_hash"));
		return !expected.isEmpty() && (expected == HashMaster(password));
	}

	static void SetMaster(const QString &password) {
		Conf::Set(
			QStringLiteral("vrg_master_hash"),
			password.isEmpty() ? QString() : HashMaster(password));
	}

	[[nodiscard]] static bool HasAccountLock(uint64 userId) {
		return userId != 0 && !Conf::Str(AccKey(userId)).isEmpty();
	}

	[[nodiscard]] static bool VerifyAccount(uint64 userId, const QString &password) {
		if (!userId) {
			return false;
		}
		const auto expected = Conf::Str(AccKey(userId));
		return !expected.isEmpty() && (expected == HashAcc(userId, password));
	}

	static void SetAccountLock(uint64 userId, const QString &password) {
		if (!userId) {
			return;
		}
		Conf::Set(
			AccKey(userId),
			password.isEmpty() ? QString() : HashAcc(userId, password));
	}

	static void ClearAccountLock(uint64 userId) {
		if (userId) {
			Conf::Set(AccKey(userId), QString());
		}
	}

private:
	[[nodiscard]] static QString AccKey(uint64 userId) {
		return QStringLiteral("lock_acc_") + QString::number(userId);
	}

	[[nodiscard]] static QString HashMaster(const QString &password) {
		const auto raw = (QStringLiteral("vrg:master:") + password).toUtf8();
		return QString::fromLatin1(
			QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
	}

	[[nodiscard]] static QString HashAcc(uint64 userId, const QString &password) {
		const auto raw = (QStringLiteral("vrg:acc:")
			+ QString::number(userId)
			+ ':'
			+ password).toUtf8();
		return QString::fromLatin1(
			QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
	}
};

}

