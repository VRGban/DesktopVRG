/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "core/file_utilities.h"
#include "vrg/vrg_conf.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/labels.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <memory>
#include <vector>

namespace Vrg::Updater {

inline constexpr auto kCurrentVersion = "1.0.4";

namespace Detail {

[[nodiscard]] inline std::vector<int> ParseParts(const QString &v) {
	const auto core = v.section(QChar('-'), 0, 0).section(QChar('+'), 0, 0);
	const auto split = core.split(QChar('.'));
	auto out = std::vector<int>();
	for (const auto &part : split) {
		auto ok = false;
		const auto val = part.toInt(&ok);
		out.push_back(ok ? val : 0);
	}
	while (out.size() < 3) {
		out.push_back(0);
	}
	return out;
}

[[nodiscard]] inline bool IsNewer(const QString &remote, const QString &local) {
	const auto r = ParseParts(remote);
	const auto l = ParseParts(local);
	for (size_t i = 0; i < std::max(r.size(), l.size()); ++i) {
		const auto rv = (i < r.size()) ? r[i] : 0;
		const auto lv = (i < l.size()) ? l[i] : 0;
		if (rv != lv) {
			return rv > lv;
		}
	}
	return false;
}

}

inline void Check(std::shared_ptr<Ui::Show> show, bool silentIfUpToDate = false) {
	if (!show) {
		return;
	}
	static bool checking = false;
	if (checking) {
		return;
	}
	checking = true;

	static auto *nam = new QNetworkAccessManager();
	QNetworkRequest req(QUrl(u"https://vrgclient.app/version.json"_q));
	req.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(8000);
	auto *reply = nam->get(req);
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		checking = false;
		if (reply->error() != QNetworkReply::NoError) {
			if (!silentIfUpToDate) {
				show->showToast(QString::fromUtf8("Не удалось проверить обновления"));
			}
			reply->deleteLater();
			return;
		}
		const auto doc = QJsonDocument::fromJson(reply->readAll());
		reply->deleteLater();
		if (!doc.isObject()) {
			if (!silentIfUpToDate) {
				show->showToast(QString::fromUtf8("Ошибка формата version.json"));
			}
			return;
		}
		const auto obj = doc.object();
		const auto remoteVer = obj.value(u"version"_q).toString().trimmed();
		const auto changelog = obj.value(u"changelog"_q).toString().trimmed();
		const auto url = obj.value(u"url"_q).toString().trimmed();
		const auto targetUrl = url.isEmpty()
			? u"https://vrgclient.app/#download"_q
			: url;

		if (remoteVer.isEmpty()
			|| !Detail::IsNewer(remoteVer, QString::fromLatin1(kCurrentVersion))) {
			if (!silentIfUpToDate) {
				show->showToast(QString::fromUtf8("У вас актуальная версия VRG v")
					+ QString::fromLatin1(kCurrentVersion));
			}
			return;
		}

		if (silentIfUpToDate) {
			const auto now = QDateTime::currentSecsSinceEpoch();
			if (Conf::Str("update_shown_version") == remoteVer
				&& (now - qint64(Conf::Int("update_shown_at", 0))) < 24 * 3600) {
				return;
			}
		}
		Conf::Set("update_shown_version", remoteVer);
		Conf::SetInt("update_shown_at", int(QDateTime::currentSecsSinceEpoch()));
		show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(
				QString::fromUtf8("Доступно обновление VRG v") + remoteVer));
			const auto body = changelog.isEmpty()
				? QString::fromUtf8("Вышла новая версия VRG Desktop.")
				: changelog;
			box->addRow(
				object_ptr<Ui::FlatLabel>(box, body, st::boxLabel),
				st::boxRowPadding);
			box->addButton(rpl::single(QString::fromUtf8("Скачать")), [=] {
				File::OpenUrl(targetUrl);
				box->closeBox();
			});
			box->addButton(rpl::single(QString::fromUtf8("Позже")), [=] {
				box->closeBox();
			});
		}));
	});
}

inline void CheckOnce(std::shared_ptr<Ui::Show> show) {
	static bool checkedOnce = false;
	if (checkedOnce) {
		return;
	}
	checkedOnce = true;
	static auto *timer = new QTimer();
	timer->setInterval(2 * 60 * 60 * 1000);
	QObject::connect(timer, &QTimer::timeout, [show] {
		Check(show, true);
	});
	timer->start();
	Check(std::move(show), true);
}

}
