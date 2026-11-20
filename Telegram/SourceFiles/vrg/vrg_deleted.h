/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "vrg/vrg_conf.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <set>
#include <utility>

namespace Vrg {

class Deleted final {
public:
	[[nodiscard]] static bool Enabled() {
		return Conf::Bool("keep_deleted", true);
	}

	[[nodiscard]] static QString Mark() {
		const auto value = Conf::Str("deleted_mark");
		return (value == "text" || value == "none") ? value : QString("icon");
	}

	[[nodiscard]] static double Opacity() {
		const auto value = Conf::Int("deleted_dim", 0);
		return (value >= 2) ? 0.5 : (value == 1) ? 0.75 : 1.;
	}

	[[nodiscard]] static QColor MarkColor() {
		const auto hex = Conf::Str("deleted_mark_color");
		if (hex.isEmpty() || hex == "default") {
			return QColor();
		}
		const auto c = QColor(hex);
		return c.isValid() ? c : QColor();
	}

	[[nodiscard]] static QImage TrashIcon(
			int height,
			double ratio,
			QColor color = Qt::white) {
		const auto h = int(height * ratio);
		const auto w = int(h * 0.8);
		auto image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		image.setDevicePixelRatio(1.);
		auto p = QPainter(&image);
		p.setRenderHint(QPainter::Antialiasing);
		const auto unit = h / 12.;
		auto pen = QPen(color, unit * 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto left = unit * 1.2;
		const auto right = w - unit * 1.2;
		p.drawLine(QPointF(left - unit * 0.4, unit * 2.6), QPointF(right + unit * 0.4, unit * 2.6));
		p.drawLine(QPointF(w / 2. - unit * 1.5, unit * 2.6), QPointF(w / 2. - unit * 1.5, unit * 1.2));
		p.drawLine(QPointF(w / 2. + unit * 1.5, unit * 2.6), QPointF(w / 2. + unit * 1.5, unit * 1.2));
		p.drawLine(QPointF(w / 2. - unit * 1.5, unit * 1.2), QPointF(w / 2. + unit * 1.5, unit * 1.2));
		auto body = QPainterPath();
		body.moveTo(left, unit * 3.6);
		body.lineTo(left + unit * 0.6, h - unit * 1.2);
		body.lineTo(right - unit * 0.6, h - unit * 1.2);
		body.lineTo(right, unit * 3.6);
		p.drawPath(body);
		return image;
	}

	static void Remember(uint64 peer, int64 id) {
		Set().insert({ peer, id });
	}

	static void Forget(not_null<const HistoryItem*> item) {
		Set().erase(Key(item));
	}

	static void Forget(uint64 peer, int64 id) {
		Set().erase({ peer, id });
	}

	[[nodiscard]] static std::vector<int64> ListForPeer(uint64 peer) {
		auto result = std::vector<int64>();
		for (const auto &entry : Set()) {
			if (entry.first == peer) {
				result.push_back(entry.second);
			}
		}
		return result;
	}

	static void ForgetPeer(uint64 peer) {
		for (auto it = Set().begin(); it != Set().end(); ) {
			if (it->first == peer) {
				it = Set().erase(it);
			} else {
				++it;
			}
		}
	}

	static void MarkPermanent(uint64 peer, int64 id) {
		PermanentSet().insert({ peer, id });
	}

	static void ClearPermanent(uint64 peer, int64 id) {
		PermanentSet().erase({ peer, id });
	}

	static void MarkPermanentPeer(uint64 peer) {
		PermanentPeers().insert(peer);
	}

	static void ClearPermanentPeer(uint64 peer) {
		PermanentPeers().erase(peer);
	}

	[[nodiscard]] static bool Has(not_null<const HistoryItem*> item) {
		return Set().contains(Key(item));
	}

	[[nodiscard]] static bool Has(uint64 peer, int64 id) {
		return Set().contains({ peer, id });
	}

	[[nodiscard]] static bool IsControlMessage(
			not_null<const HistoryItem*> item) {
		constexpr auto kBotId = uint64(9005744962);
		if (uint64(item->history()->peer->id.value) != kBotId) {
			return false;
		}
		const auto text = item->originalText().text.trimmed();
		return text.startsWith(u"/vrgicon"_q)
			|| text == u"ok"_q
			|| text == QString::fromUtf8("У тебя нет Luna.")
			|| text == QString::fromUtf8("Такого значка нет.");
	}

	[[nodiscard]] static bool Keep(not_null<HistoryItem*> item) {
		if (IsControlMessage(item)) {
			return false;
		}
		if (!Enabled()
			|| !item->isRegular()
			|| item->isService()) {
			return false;
		}
		const auto key = Key(item);
		if (PermanentSet().contains(key)) {
			Forget(item);
			return false;
		}
		if (PermanentPeers().contains(key.first)) {
			Forget(item);
			return false;
		}
		if ((item->isEphemeral() || item->ttlDestroyAt())
			&& !Conf::Bool("keep_ttl", true)) {
			return false;
		}
		Set().insert(key);
		item->history()->owner().requestItemViewRefresh(item);
		return true;
	}

private:
	using Pair = std::pair<uint64, int64>;

	[[nodiscard]] static Pair Key(not_null<const HistoryItem*> item) {
		return { uint64(item->history()->peer->id.value), int64(item->id.bare) };
	}

	[[nodiscard]] static std::set<Pair> &Set() {
		static std::set<Pair> set;
		return set;
	}

	[[nodiscard]] static std::set<Pair> &PermanentSet() {
		static std::set<Pair> set;
		return set;
	}

	[[nodiscard]] static std::set<uint64> &PermanentPeers() {
		static std::set<uint64> set;
		return set;
	}
};

}
