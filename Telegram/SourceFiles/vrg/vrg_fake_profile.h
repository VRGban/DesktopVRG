/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "data/data_star_gift.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QString>

#include <map>
#include <optional>
#include <vector>

namespace Vrg::FakeProfile {

struct FakeUsername {
	QString username;
	bool collectible = true;
};

struct Data {
	QString phone;
	std::vector<FakeUsername> usernames;
	std::vector<QString> giftSlugs;
};

[[nodiscard]] inline QString ExtractSlug(QString raw) {
	raw = raw.trimmed();
	const auto idx = raw.indexOf(u"t.me/nft/"_q, 0, Qt::CaseInsensitive);
	if (idx >= 0) {
		raw = raw.mid(idx + 9);
	}
	const auto q = raw.indexOf('?');
	if (q >= 0) {
		raw = raw.left(q);
	}
	const auto s = raw.indexOf('/');
	if (s >= 0) {
		raw = raw.left(s);
	}
	return raw.trimmed();
}

[[nodiscard]] inline bool &ViewingFlag() {
	static bool v = false;
	return v;
}

[[nodiscard]] inline bool IsViewing() {
	return ViewingFlag();
}

inline void SetViewing(bool v) {
	ViewingFlag() = v;
}

[[nodiscard]] inline rpl::event_stream<> &UpdatedStream() {
	static rpl::event_stream<> s;
	return s;
}

[[nodiscard]] inline rpl::producer<> Updated() {
	return UpdatedStream().events();
}

[[nodiscard]] inline std::map<QString, ::Data::StarGift> &GiftCache() {
	static std::map<QString, ::Data::StarGift> cache;
	return cache;
}

[[nodiscard]] inline std::optional<::Data::StarGift> CachedGift(const QString &slug) {
	const auto it = GiftCache().find(slug);
	if (it == GiftCache().end()) {
		return std::nullopt;
	}
	return it->second;
}

inline void CacheGift(const QString &slug, const ::Data::StarGift &gift) {
	GiftCache().insert_or_assign(slug, gift);
}

[[nodiscard]] inline QString FilePath() {
	return QDir::homePath() + u"/.config/vrg/fake_profile.json"_q;
}

[[nodiscard]] inline Data Load() {
	Data out;
	QFile f(FilePath());
	if (!f.open(QIODevice::ReadOnly)) {
		return out;
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		return out;
	}
	const auto obj = doc.object();
	out.phone = obj.value(u"phone"_q).toString().trimmed();

	const auto usersArr = obj.value(u"usernames"_q).toArray();
	for (const auto &v : usersArr) {
		const auto uo = v.toObject();
		auto name = uo.value(u"username"_q).toString().trimmed();
		if (name.startsWith('@')) {
			name = name.mid(1);
		}
		if (!name.isEmpty()) {
			out.usernames.push_back({
				.username = name,
				.collectible = uo.value(u"collectible"_q).toBool(true),
			});
		}
	}

	const auto slugsArr = obj.value(u"giftSlugs"_q).toArray();
	for (const auto &v : slugsArr) {
		const auto slug = ExtractSlug(v.toString());
		if (!slug.isEmpty()) {
			out.giftSlugs.push_back(slug);
		}
	}
	return out;
}

inline void Save(const Data &data) {
	QDir().mkpath(QDir::homePath() + u"/.config/vrg"_q);
	QJsonObject obj;
	obj.insert(u"phone"_q, data.phone.trimmed());

	QJsonArray usersArr;
	for (const auto &u : data.usernames) {
		QJsonObject uo;
		uo.insert(u"username"_q, u.username);
		uo.insert(u"collectible"_q, u.collectible);
		usersArr.append(uo);
	}
	obj.insert(u"usernames"_q, usersArr);

	QJsonArray slugsArr;
	for (const auto &slug : data.giftSlugs) {
		if (!slug.isEmpty()) {
			slugsArr.append(slug);
		}
	}
	obj.insert(u"giftSlugs"_q, slugsArr);

	QSaveFile f(FilePath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
		f.commit();
	}
	UpdatedStream().fire({});
}

inline void Reset() {
	QFile::remove(FilePath());
	UpdatedStream().fire({});
}

}
