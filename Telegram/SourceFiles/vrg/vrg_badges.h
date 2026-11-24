/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <set>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QRadialGradient>
#include <QtWidgets/QWidget>
#include <cmath>
#include <vector>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtSvg/QSvgRenderer>
#include <map>
#include <memory>
#include <optional>

namespace Vrg::Badges {

struct Entry {
	QString type;
	QString icon;
	int num = 0;

	[[nodiscard]] bool valid() const {
		return (type == u"dev"_q
			|| type == u"supporter"_q
			|| type == u"luna"_q);
	}
};

namespace Detail {

[[nodiscard]] inline QString CachePath() {
	return QDir::homePath() + u"/.local/share/vrg/badges_cache.json"_q;
}

[[nodiscard]] inline std::map<uint64, Entry> &Map() {
	static std::map<uint64, Entry> map;
	return map;
}

[[nodiscard]] inline rpl::event_stream<> &UpdatedStream() {
	static rpl::event_stream<> stream;
	return stream;
}

inline void ParseJson(const QByteArray &bytes, std::map<uint64, Entry> &out) {
	const auto doc = QJsonDocument::fromJson(bytes);
	if (!doc.isObject()) {
		return;
	}
	const auto root = doc.object();
	for (auto it = root.begin(); it != root.end(); ++it) {
		auto ok = false;
		const auto id = it.key().toULongLong(&ok);
		if (!ok || !id) {
			continue;
		}
		Entry entry;
		if (it.value().isObject()) {
			const auto obj = it.value().toObject();
			entry.type = obj.value(u"type"_q).toString().trimmed().toLower();
			entry.num = obj.value(u"num"_q).toInt(0);
			entry.icon = obj.value(u"icon"_q).toString().trimmed().toLower();
		} else if (it.value().isString()) {
			entry.type = it.value().toString().trimmed().toLower();
		}
		if (entry.valid()) {
			out[id] = std::move(entry);
		}
	}
}

inline void LoadCache() {
	QFile f(CachePath());
	if (!f.open(QIODevice::ReadOnly)) {
		return;
	}
	ParseJson(f.readAll(), Map());
}

inline void SaveCache(const std::map<uint64, Entry> &map) {
	auto root = QJsonObject();
	for (const auto &[id, entry] : map) {
		auto obj = QJsonObject();
		obj.insert(u"type"_q, entry.type);
		obj.insert(u"num"_q, entry.num);
		if (!entry.icon.isEmpty()) {
			obj.insert(u"icon"_q, entry.icon);
		}
		root.insert(QString::number(id), obj);
	}
	const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
	const auto path = CachePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(bytes);
		f.commit();
	}
}

[[nodiscard]] inline std::map<uint64, qint64> &Checked() {
	static std::map<uint64, qint64> map;
	return map;
}

[[nodiscard]] inline std::set<uint64> &PendingIds() {
	static std::set<uint64> set;
	return set;
}

[[nodiscard]] inline std::vector<QPointer<QWidget>> &MissWidgets() {
	static std::vector<QPointer<QWidget>> list;
	return list;
}

[[nodiscard]] inline QImage *DevImageFor(bool white) {
	static std::map<bool, QImage> cache;
	auto it = cache.find(white);
	if (it == cache.end()) {
		const auto path = white
			? u":/gui/art/vrg_badges/dev_white.png"_q
			: u":/gui/art/vrg_badges/dev_black.png"_q;
		QImage img(path);
		it = cache.emplace(white, std::move(img)).first;
	}
	return it->second.isNull() ? nullptr : &it->second;
}

[[nodiscard]] inline QSvgRenderer *RendererFor(const QString &type, bool white) {
	if (type != u"supporter"_q
		&& type != u"dev"_q
		&& !type.startsWith(u"supp2_"_q)) {
		return nullptr;
	}
	static std::map<QString, std::unique_ptr<QSvgRenderer>> cache;
	const auto key = type + (white ? u"_white"_q : u"_black"_q);
	auto it = cache.find(key);
	if (it == cache.end()) {
		const auto path = u":/gui/art/vrg_badges/"_q + key + u".svg"_q;
		auto r = std::make_unique<QSvgRenderer>(path);
		it = cache.emplace(key, std::move(r)).first;
	}
	return it->second->isValid() ? it->second.get() : nullptr;
}

struct TrackedWidget {
	QPointer<QWidget> widget;
	QRect rect;
	qint64 last = 0;
};

[[nodiscard]] inline std::vector<TrackedWidget> &Tracked() {
	static std::vector<TrackedWidget> list;
	return list;
}

inline void TickTracked() {
	const auto now = QDateTime::currentMSecsSinceEpoch();
	auto &list = Tracked();
	for (auto i = list.begin(); i != list.end();) {
		if (!i->widget || (now - i->last) > 250) {
			i = list.erase(i);
		} else {
			i->widget->update(i->rect);
			++i;
		}
	}
}

inline void Track(QPainter &p, const QRect &rect) {
	const auto widget = dynamic_cast<QWidget*>(p.device());
	if (!widget) {
		return;
	}
	const auto mapped = p.worldTransform().mapRect(QRectF(rect))
		.toAlignedRect()
		.adjusted(-10, -10, 10, 10);
	const auto now = QDateTime::currentMSecsSinceEpoch();
	auto &list = Tracked();
	for (auto &entry : list) {
		if (entry.widget == widget && entry.rect == mapped) {
			entry.last = now;
			return;
		}
	}
	list.push_back({ QPointer<QWidget>(widget), mapped, now });
	static QTimer *timer = nullptr;
	if (!timer) {
		timer = new QTimer();
		timer->setInterval(40);
		QObject::connect(timer, &QTimer::timeout, [] {
			TickTracked();
			if (Tracked().empty()) {
				timer->stop();
			}
		});
	}
	if (!timer->isActive()) {
		timer->start();
	}
}

inline void PaintEffects(
		QPainter &p,
		const QRectF &r,
		const QString &type) {
	const auto t = (QDateTime::currentMSecsSinceEpoch() % 100000) / 1000.;
	auto base = QColor(255, 204, 85);
	if (type == u"dev"_q) {
		base = QColor(110, 168, 255);
	} else if (type == u"supporter"_q || type == u"supp2_heart"_q) {
		base = QColor(255, 122, 168);
	}
	const auto c = r.center();
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	const auto radius = std::max(r.width(), r.height()) * 0.85;
	auto glow = QRadialGradient(c, radius);
	auto inner = base;
	inner.setAlpha(int(46 + 26 * std::sin(t * 2.1)));
	auto outer = base;
	outer.setAlpha(0);
	glow.setColorAt(0., inner);
	glow.setColorAt(1., outer);
	p.setBrush(glow);
	p.drawEllipse(c, radius, radius);
	const auto rx = r.width() * 0.5 + 4.;
	const auto ry = r.height() * 0.5 + 4.;
	for (auto i = 0; i != 6; ++i) {
		const auto angle = t * (0.8 + 0.13 * i) + i * 1.0472;
		const auto wobble = 1. + 0.18 * std::sin(t * 1.7 + i * 2.1);
		const auto pos = QPointF(
			c.x() + std::cos(angle) * rx * wobble,
			c.y() + std::sin(angle) * ry * wobble * ((i % 2) ? 1. : -1.));
		const auto size = 0.9 + 0.5 * (0.5 + 0.5 * std::sin(t * 3. + i * 1.7));
		auto color = base;
		color.setAlpha(int(110 + 110 * std::sin(t * 3.3 + i * 1.3)));
		p.setBrush(color);
		p.drawEllipse(pos, size, size);
	}
	p.restore();
}

}

[[nodiscard]] inline QString KindOf(const Entry &entry) {
	if (entry.type == u"luna"_q) {
		const auto icon = (entry.icon == u"moon"_q || entry.icon == u"bolt"_q || entry.icon == u"heart"_q)
			? entry.icon
			: u"crown"_q;
		return u"supp2_"_q + icon;
	}
	return entry.type;
}

inline void EnsureCacheLoaded() {
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Detail::LoadCache();
	}
}

inline void FetchPending() {
	static bool fetching = false;
	auto &pending = Detail::PendingIds();
	if (fetching || pending.empty()) {
		return;
	}
	fetching = true;
	auto ids = std::vector<uint64>();
	auto parts = QStringList();
	for (auto it = pending.begin(); it != pending.end() && ids.size() < 50;) {
		ids.push_back(*it);
		parts.push_back(QString::number(*it));
		it = pending.erase(it);
	}

	static auto *nam = new QNetworkAccessManager();
	QNetworkRequest req(QUrl(
		u"https://vrgclient.app/api/badges?ids="_q + parts.join(u","_q)));
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(8000);
	auto *reply = nam->get(req);
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		fetching = false;
		const auto now = QDateTime::currentSecsSinceEpoch();
		if (reply->error() == QNetworkReply::NoError) {
			std::map<uint64, Entry> parsed;
			Detail::ParseJson(reply->readAll(), parsed);
			auto &map = Detail::Map();
			auto changed = false;
			for (const auto id : ids) {
				Detail::Checked()[id] = now;
				const auto found = parsed.find(id);
				const auto had = map.find(id);
				if (found != parsed.end()) {
					if (had == map.end()
						|| had->second.type != found->second.type
						|| had->second.num != found->second.num
						|| had->second.icon != found->second.icon) {
						map[id] = found->second;
						changed = true;
					}
				} else if (had != map.end()) {
					map.erase(had);
					changed = true;
				}
			}
			if (changed) {
				Detail::SaveCache(map);
				Detail::UpdatedStream().fire({});
				auto &misses = Detail::MissWidgets();
				for (const auto &widget : misses) {
					if (widget) {
						widget->update();
					}
				}
				misses.clear();
			}
		} else {
			for (const auto id : ids) {
				Detail::Checked()[id] = now - 30;
			}
		}
		reply->deleteLater();
		if (!Detail::PendingIds().empty()) {
			QTimer::singleShot(100, [] { FetchPending(); });
		}
	});
}

[[nodiscard]] inline rpl::producer<> Updates() {
	return Detail::UpdatedStream().events();
}

[[nodiscard]] inline std::optional<Entry> Lookup(uint64 userId) {
	EnsureCacheLoaded();
	const auto now = QDateTime::currentSecsSinceEpoch();
	const auto &checked = Detail::Checked();
	const auto c = checked.find(userId);
	if (c == checked.end() || (now - c->second) > 40) {
		if (Detail::PendingIds().insert(userId).second) {
			QTimer::singleShot(200, [] { FetchPending(); });
		}
	}
	const auto &map = Detail::Map();
	const auto it = map.find(userId);
	if (it == map.end()) {
		return std::nullopt;
	}
	return it->second;
}

[[nodiscard]] inline std::optional<Entry> LookupFor(
		QPainter &p,
		uint64 userId) {
	const auto result = Lookup(userId);
	if (!result && Detail::Checked().find(userId) == Detail::Checked().end()) {
		if (const auto widget = dynamic_cast<QWidget*>(p.device())) {
			auto &misses = Detail::MissWidgets();
			auto known = false;
			for (const auto &w : misses) {
				if (w == widget) {
					known = true;
					break;
				}
			}
			if (!known) {
				misses.push_back(QPointer<QWidget>(widget));
			}
		}
	}
	return result;
}

inline void SetLocalIcon(uint64 userId, const QString &icon) {
	auto &map = Detail::Map();
	const auto it = map.find(userId);
	if (it == map.end() || it->second.type != u"luna"_q) {
		return;
	}
	it->second.icon = icon;
	Detail::SaveCache(map);
	Detail::UpdatedStream().fire({});
}

inline void Refresh(uint64 userId) {
	Detail::Checked().erase(userId);
	if (Detail::PendingIds().insert(userId).second) {
		QTimer::singleShot(100, [] { FetchPending(); });
	}
}

[[nodiscard]] inline bool IsDarkTheme() {
	return st::windowBg->c.lightness() < 128;
}

[[nodiscard]] inline int WidthForHeight(const QString &type, int height) {
	(void)type;
	return height;
}

inline bool Paint(
		QPainter &p,
		const QRect &rect,
		const QString &type,
		std::optional<bool> forceWhite = std::nullopt) {
	const auto white = forceWhite.value_or(IsDarkTheme());
	const auto renderer = Detail::RendererFor(type, white);
	if (!renderer) {
		return false;
	}
	Detail::Track(p, rect);
	Detail::PaintEffects(p, QRectF(rect), type);
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	renderer->render(&p, QRectF(rect));
	p.restore();
	return true;
}

[[nodiscard]] inline QString RoleTitle(const Entry &info) {
	const auto base = (info.type == u"dev"_q)
		? QString::fromUtf8("VRG Developer")
		: (info.type == u"luna"_q)
		? QString::fromUtf8("VRG Luna")
		: QString::fromUtf8("VRG Supporter");
	return (info.num > 0)
		? (base + u" #"_q + QString::number(info.num))
		: base;
}

[[nodiscard]] inline QString RoleDescription(const Entry &info) {
	if (info.type == u"dev"_q) {
		return QString::fromUtf8("Этот пользователь создал VRG Client.");
	} else if (info.type == u"luna"_q) {
		return QString::fromUtf8("Этот пользователь поддержал развитие VRG Client и может сам выбирать себе значок.");
	}
	return QString::fromUtf8("Этот пользователь поддержал развитие VRG Client.");
}

inline void ShowInfoBox(
		std::shared_ptr<Ui::Show> show,
		const Entry &info,
		const QString &peerName,
		Fn<void()> openSupport = nullptr,
		bool own = false,
		Fn<void(const QString&)> changeIcon = nullptr) {
	if (!show || !info.valid()) {
		return;
	}
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(RoleTitle(info)));

		const auto iconWrap = box->verticalLayout()->add(
			object_ptr<Ui::RpWidget>(box));
		iconWrap->resize(iconWrap->width(), 120);
		iconWrap->setMinimumHeight(120);
		iconWrap->paintRequest() | rpl::on_next([=] {
			QPainter p(iconWrap);
			const auto h = 68;
			const auto kind = KindOf(info);
			const auto w = WidthForHeight(kind, h);
			const auto x = (iconWrap->width() - w) / 2;
			const auto y = (iconWrap->height() - h) / 2;
			Paint(p, QRect(x, y, w, h), kind);
		}, iconWrap->lifetime());

		const auto desc = peerName.isEmpty()
			? RoleDescription(info)
			: (peerName + u"\n"_q + RoleDescription(info));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				desc,
				st::boxLabel),
			st::boxRowPadding);

		if (own && info.type == u"luna"_q && changeIcon) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					QString::fromUtf8("Твой значок: нажми на другой, чтобы поменять"),
					st::boxLabel),
				st::boxRowPadding + QMargins(0, 18, 0, 6));
			const auto current = std::make_shared<QString>(
				info.icon.isEmpty() ? u"crown"_q : info.icon);
			const auto picker = box->verticalLayout()->add(
				object_ptr<Ui::RpWidget>(box));
			picker->resize(picker->width(), 76);
			picker->setMinimumHeight(76);
			picker->setCursor(style::cur_pointer);
			const auto names = std::array<QString, 4>{
				u"crown"_q, u"moon"_q, u"bolt"_q, u"heart"_q };
			const auto cell = [=](int index) {
				const auto total = 4 * 56 + 3 * 18;
				const auto left = (picker->width() - total) / 2;
				return QRect(left + index * (56 + 18), 10, 56, 56);
			};
			picker->paintRequest() | rpl::on_next([=] {
				QPainter p(picker);
				p.setRenderHint(QPainter::Antialiasing);
				for (auto i = 0; i != 4; ++i) {
					const auto rect = cell(i);
					if (*current == names[i]) {
						p.setPen(QPen(st::windowSubTextFg->c, 2));
						p.setBrush(Qt::NoBrush);
						p.drawRoundedRect(rect.adjusted(-3, -3, 3, 3), 14, 14);
					}
					Paint(
						p,
						rect.adjusted(10, 10, -10, -10),
						u"supp2_"_q + names[i]);
				}
			}, picker->lifetime());
			picker->events() | rpl::on_next([=](not_null<QEvent*> e) {
				if (e->type() != QEvent::MouseButtonPress) {
					return;
				}
				const auto pos = static_cast<QMouseEvent*>(e.get())->pos();
				for (auto i = 0; i != 4; ++i) {
					if (cell(i).contains(pos) && *current != names[i]) {
						*current = names[i];
						picker->update();
						changeIcon(names[i]);
					}
				}
			}, picker->lifetime());
		}

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				QString::fromUtf8("Как получить такой же значок"),
				st::boxLabel),
			st::boxRowPadding + QMargins(0, 22, 0, 10));

		const auto tiers = box->verticalLayout()->add(
			object_ptr<Ui::RpWidget>(box));
		tiers->resize(tiers->width(), 196);
		tiers->setMinimumHeight(196);
		tiers->paintRequest() | rpl::on_next([=] {
			QPainter p(tiers);
			p.setRenderHint(QPainter::TextAntialiasing);
			const auto drawTier = [&](
					int top,
					const QStringList &kinds,
					const QString &title,
					const QString &text) {
				const auto left = st::boxRowPadding.left();
				auto x = left;
				for (const auto &kind : kinds) {
					Paint(p, QRect(x, top + 2, 28, 28), kind);
					x += 38;
				}
				p.setPen(st::windowFg);
				p.setFont(st::semiboldFont);
				p.drawText(left, top + 50, title);
				p.setPen(st::windowSubTextFg);
				p.setFont(st::normalFont);
				p.drawText(
					QRect(left, top + 56, tiers->width() - left * 2, 40),
					Qt::TextWordWrap,
					text);
			};
			drawTier(
				0,
				{ u"supporter"_q },
				QString::fromUtf8("Supporter — от 3 $"),
				QString::fromUtf8("Один значок навсегда, менять его нельзя."));
			drawTier(
				98,
				{ u"supp2_crown"_q, u"supp2_moon"_q, u"supp2_bolt"_q, u"supp2_heart"_q },
				QString::fromUtf8("Luna — от 50 $"),
				QString::fromUtf8("Можно менять значок: корона, луна, молния или сердце."));
		}, tiers->lifetime());

		if (openSupport) {
			box->addButton(
				rpl::single(QString::fromUtf8("Поддержать VRG")),
				[=] {
					box->closeBox();
					openSupport();
				});
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

}

