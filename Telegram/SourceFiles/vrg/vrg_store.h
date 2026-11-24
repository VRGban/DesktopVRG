/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include <QtCore/QDateTime>

#include "history/history.h"
#include "main/main_session.h"
#include "mtproto/core_types.h"
#include "vrg/vrg_deleted.h"

#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QSaveFile>
#include <QtCore/QString>
#include <algorithm>
#include <map>
#include <optional>
#include <memory>
#include <vector>

namespace Vrg {

class Store final {
public:
	static void Save(
			not_null<Main::Session*> session,
			PeerId peerId,
			MsgId id,
			const MTPMessage &message) {
		if (!Deleted::Enabled() || !Storable(message)) {
			return;
		}
		auto &account = For(session);
		const auto key = Key{ uint64(peerId.value), int64(id.bare) };
		const auto edit = EditDate(message);
		const auto i = account.index.constFind(key);
		if (i != account.index.constEnd() && i.value().edit == edit) {
			return;
		}
		account.append(key, edit, message);
	}

	static void MarkDeleted(not_null<HistoryItem*> item) {
		Log(QString("markDeleted peer=%1 id=%2")
			.arg(uint64(item->history()->peer->id.value))
			.arg(int64(item->id.bare)));
		auto &account = For(&item->history()->session());
		const auto key = Key{
			uint64(item->history()->peer->id.value),
			int64(item->id.bare),
		};
		account.markDeleted(key);
	}

	static void Erase(not_null<HistoryItem*> item) {
		auto &account = For(&item->history()->session());
		const auto key = Key{
			uint64(item->history()->peer->id.value),
			int64(item->id.bare),
		};
		account.erase(key);
	}

	static void Erase(not_null<Main::Session*> session, uint64 peer, int64 id) {
		auto &account = For(session);
		account.erase(Key{ peer, id });
	}

	static void ClearAll(not_null<Main::Session*> session) {
		For(session).clearAll();
	}

	static void ErasePeer(not_null<Main::Session*> session, uint64 peer) {
		auto &account = For(session);
		account.erasePeer(peer);
	}

	[[nodiscard]] static QVector<MTPMessage> Merge(
			not_null<History*> history,
			const QVector<MTPMessage> &slice,
			bool older) {
		if (!Deleted::Enabled()) {
			return slice;
		}
		auto &account = For(&history->session());
		const auto peer = uint64(history->peer->id.value);
		const auto found = account.deleted.constFind(peer);
		if (found == account.deleted.constEnd() || found.value().empty()) {
			Log(QString("merge peer=%1 older=%2 slice=%3: nothing stored")
				.arg(peer).arg(older).arg(slice.size()));
			return slice;
		}
		auto ids = std::vector<int64>();
		ids.reserve(slice.size());
		for (const auto &message : slice) {
			ids.push_back(int64(IdFromMessage(message).bare));
		}
		const auto have = [&](int64 id) {
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		};
		const auto unbounded = int64(1) << 40;
		const auto sliceMax = ids.empty()
			? int64(0)
			: *std::max_element(ids.begin(), ids.end());
		auto latest = !older || history->isEmpty();
		if (history->lastServerMessageKnown()) {
			const auto last = history->lastServerMessage();
			latest = !last || int64(last->id.bare) <= sliceMax;
		}
		auto lower = int64(0);
		auto upper = int64(0);
		if (older) {
			if (slice.isEmpty()) {
				return slice;
			}
			lower = *std::min_element(ids.begin(), ids.end());
			upper = latest
				? unbounded
				: !history->isEmpty()
				? int64(history->minMsgId().bare)
				: (sliceMax + 1);
		} else {
			lower = history->isEmpty()
				? 0
				: (int64(history->maxMsgId().bare) + 1);
			upper = (slice.isEmpty() || latest)
				? unbounded
				: (sliceMax + 1);
		}
		Log(QString("merge peer=%1 older=%2 slice=%3 sliceMax=%4 histEmpty=%5 latest=%6 lower=%7 upper=%8 stored=%9")
			.arg(peer).arg(older).arg(slice.size()).arg(sliceMax)
			.arg(history->isEmpty()).arg(latest).arg(lower).arg(upper)
			.arg(found.value().size()));
		auto extra = std::vector<std::pair<int64, MTPMessage>>();
		const auto &map = found.value();
		for (auto i = map.lower_bound(lower); i != map.end(); ++i) {
			if (i->first >= upper) {
				break;
			}
			if (have(i->first)
				|| history->owner().message(
					history->peer->id,
					MsgId(i->first))) {
				continue;
			}
			if (auto message = account.read(i->second)) {
				extra.emplace_back(i->first, std::move(*message));
			}
		}
		Log(QString("merge extra=%1").arg(extra.size()));
		if (extra.empty()) {
			return slice;
		}
		auto result = QVector<MTPMessage>();
		result.reserve(slice.size() + int(extra.size()));
		auto all = std::vector<std::pair<int64, MTPMessage>>();
		for (const auto &message : slice) {
			all.emplace_back(int64(IdFromMessage(message).bare), message);
		}
		for (auto &entry : extra) {
			all.push_back(std::move(entry));
		}
		std::stable_sort(all.begin(), all.end(), [](
				const auto &a,
				const auto &b) {
			return a.first > b.first;
		});
		for (auto &entry : all) {
			result.push_back(std::move(entry.second));
		}
		return result;
	}

	[[nodiscard]] static QVector<MTPMessage> Extra(
			not_null<PeerData*> peer,
			int64 from,
			int64 till,
			const std::vector<MsgId> &have) {
		auto result = QVector<MTPMessage>();
		if (!Deleted::Enabled()) {
			return result;
		}
		auto &account = For(&peer->session());
		const auto found = account.deleted.constFind(uint64(peer->id.value));
		if (found == account.deleted.constEnd()) {
			return result;
		}
		const auto &map = found.value();
		for (auto i = map.lower_bound(from); i != map.end(); ++i) {
			if (i->first > till) {
				break;
			}
			if (std::find_if(have.begin(), have.end(), [&](MsgId id) {
				return int64(id.bare) == i->first;
			}) != have.end()) {
				continue;
			}
			if (auto message = account.read(i->second)) {
				result.push_back(std::move(*message));
			}
		}
		Log(QString("extra peer=%1 range=%2..%3 have=%4 added=%5")
			.arg(uint64(peer->id.value)).arg(from).arg(till)
			.arg(have.size()).arg(result.size()));
		return result;
	}

	static void Log(const QString &text) {
		QFile file(QDir::homePath() + "/.config/vrg/store.log");
		if (file.open(QIODevice::Append | QIODevice::Text)) {
			file.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
				+ " " + text + "\n").toUtf8());
		}
	}

private:
	using Key = std::pair<uint64, int64>;

	struct KeyHash {
		size_t operator()(const Key &key) const {
			return size_t(key.first * 1000003ULL) ^ size_t(key.second);
		}
	};

	struct Rec {
		qint64 offset = 0;
		qint32 edit = 0;
		qint32 bytes = 0;
	};

	static constexpr quint32 kMagic = 0x4d475256;
	static constexpr qint64 kCap = 400LL * 1024 * 1024;
	static constexpr qint64 kTarget = 250LL * 1024 * 1024;

	struct Account {
		QString dir;
		QHash<Key, Rec> index;
		QHash<uint64, std::map<int64, Rec>> deleted;
		std::unique_ptr<QFile> data;
		std::unique_ptr<QFile> marks;

		void open() {
			QDir().mkpath(dir);
			const auto owner = QFileDevice::ReadOwner
				| QFileDevice::WriteOwner
				| QFileDevice::ExeOwner;
			QFile::setPermissions(dir, owner);
			QFile::setPermissions(QDir::homePath() + "/.local/share/vrg", owner);
			data = std::make_unique<QFile>(dir + "/msgs.dat");
			marks = std::make_unique<QFile>(dir + "/deleted.dat");
			load();
			if (data->size() > kCap) {
				compact();
			}
			data->open(QIODevice::ReadWrite);
			data->seek(data->size());
			marks->open(QIODevice::ReadWrite);
			marks->seek(marks->size());
			const auto files = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
			data->setPermissions(files);
			marks->setPermissions(files);
		}

		void load() {
			index.clear();
			deleted.clear();
			if (data->open(QIODevice::ReadOnly)) {
				const auto size = data->size();
				auto pos = qint64(0);
				while (pos + 28 <= size) {
					data->seek(pos);
					auto head = QByteArray(data->read(28));
					if (head.size() < 28) {
						break;
					}
					auto stream = QDataStream(&head, QIODevice::ReadOnly);
					stream.setByteOrder(QDataStream::LittleEndian);
					auto magic = quint32(0);
					auto peer = quint64(0);
					auto id = qint64(0);
					auto edit = qint32(0);
					auto bytes = qint32(0);
					stream >> magic >> peer >> id >> edit >> bytes;
					if (magic != kMagic
						|| bytes <= 0
						|| pos + 28 + bytes > size) {
						break;
					}
					index.insert(Key{ peer, id }, Rec{ pos, edit, bytes });
					pos += 28 + bytes;
				}
				data->close();
			}
			if (marks->open(QIODevice::ReadOnly)) {
				while (marks->size() - marks->pos() >= 16) {
					auto raw = QByteArray(marks->read(16));
					auto stream = QDataStream(&raw, QIODevice::ReadOnly);
					stream.setByteOrder(QDataStream::LittleEndian);
					auto peer = quint64(0);
					auto id = qint64(0);
					stream >> peer >> id;
					const auto i = index.constFind(Key{ peer, id });
					if (i != index.constEnd()) {
						deleted[peer][id] = i.value();
						Deleted::Remember(peer, id);
					}
				}
				marks->close();
			}
		}

		void append(const Key &key, qint32 edit, const MTPMessage &message) {
			auto buffer = mtpBuffer();
			message.write(buffer);
			const auto bytes = qint32(buffer.size() * sizeof(mtpPrime));
			if (bytes <= 0) {
				return;
			}
			auto head = QByteArray();
			{
				auto stream = QDataStream(&head, QIODevice::WriteOnly);
				stream.setByteOrder(QDataStream::LittleEndian);
				stream << kMagic << quint64(key.first) << qint64(key.second)
					<< edit << bytes;
			}
			const auto offset = data->pos();
			data->write(head);
			data->write(
				reinterpret_cast<const char*>(buffer.constData()),
				bytes);
			data->flush();
			index.insert(key, Rec{ offset, edit, bytes });
		}

		void markDeleted(const Key &key) {
			const auto i = index.constFind(key);
			if (i == index.constEnd()) {
				return;
			}
			auto &entry = deleted[key.first];
			if (entry.contains(key.second)) {
				return;
			}
			entry[key.second] = i.value();
			auto raw = QByteArray();
			{
				auto stream = QDataStream(&raw, QIODevice::WriteOnly);
				stream.setByteOrder(QDataStream::LittleEndian);
				stream << quint64(key.first) << qint64(key.second);
			}
			marks->write(raw);
			marks->flush();
		}

		void erase(const Key &key) {
			auto d = deleted.find(key.first);
			if (d != deleted.end()) {
				d.value().erase(key.second);
				if (d.value().empty()) {
					deleted.erase(d);
				}
			}
			index.remove(key);
			rewriteMarks();
		}

		void clearAll() {
			deleted.clear();
			index.clear();
			if (data) {
				data->close();
				data->open(QIODevice::ReadWrite | QIODevice::Truncate);
				data->seek(0);
			}
			rewriteMarks();
		}

		void erasePeer(uint64 peer) {
			deleted.remove(peer);
			for (auto it = index.begin(); it != index.end(); ) {
				if (it.key().first == peer) {
					it = index.erase(it);
				} else {
					++it;
				}
			}
			rewriteMarks();
		}

		void rewriteMarks() {
			if (!marks) {
				return;
			}
			marks->close();
			auto save = QSaveFile(dir + "/deleted.dat");
			if (save.open(QIODevice::WriteOnly)) {
				for (auto it = deleted.cbegin(); it != deleted.cend(); ++it) {
					const auto peer = it.key();
					for (const auto &[id, rec] : it.value()) {
						auto raw = QByteArray();
						{
							auto stream = QDataStream(&raw, QIODevice::WriteOnly);
							stream.setByteOrder(QDataStream::LittleEndian);
							stream << quint64(peer) << qint64(id);
						}
						save.write(raw);
					}
				}
				save.commit();
			}
			marks->open(QIODevice::ReadWrite);
			marks->seek(marks->size());
			const auto files = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
			marks->setPermissions(files);
		}

		[[nodiscard]] std::optional<MTPMessage> read(const Rec &rec) {
			auto bytes = QByteArray();
			{
				auto file = QFile(dir + "/msgs.dat");
				if (!file.open(QIODevice::ReadOnly)
					|| !file.seek(rec.offset + 28)) {
					return std::nullopt;
				}
				bytes = file.read(rec.bytes);
			}
			if (bytes.size() != rec.bytes || (bytes.size() % 4)) {
				return std::nullopt;
			}
			auto result = MTPMessage();
			auto from = reinterpret_cast<const mtpPrime*>(bytes.constData());
			const auto end = from + bytes.size() / sizeof(mtpPrime);
			if (!result.read(from, end)) {
				return std::nullopt;
			}
			return result;
		}

		void compact() {
			auto keep = std::vector<std::pair<Key, Rec>>();
			auto kept = qint64(0);
			auto all = std::vector<std::pair<Key, Rec>>();
			for (auto i = index.cbegin(); i != index.cend(); ++i) {
				all.emplace_back(i.key(), i.value());
			}
			std::sort(all.begin(), all.end(), [](
					const auto &a,
					const auto &b) {
				return a.second.offset > b.second.offset;
			});
			for (const auto &entry : all) {
				const auto isDeleted = deleted.contains(entry.first.first)
					&& deleted[entry.first.first].count(entry.first.second);
				if (isDeleted || kept < kTarget) {
					keep.push_back(entry);
					kept += 28 + entry.second.bytes;
				}
			}
			std::sort(keep.begin(), keep.end(), [](
					const auto &a,
					const auto &b) {
				return a.second.offset < b.second.offset;
			});
			auto source = QFile(dir + "/msgs.dat");
			auto output = QSaveFile(dir + "/msgs.dat.new");
			if (!source.open(QIODevice::ReadOnly)
				|| !output.open(QIODevice::WriteOnly)) {
				return;
			}
			auto fresh = QHash<Key, Rec>();
			for (const auto &entry : keep) {
				source.seek(entry.second.offset);
				const auto record = source.read(28 + entry.second.bytes);
				fresh.insert(entry.first, Rec{
					output.pos(),
					entry.second.edit,
					entry.second.bytes,
				});
				output.write(record);
			}
			source.close();
			if (!output.commit()) {
				return;
			}
			QFile::remove(dir + "/msgs.dat");
			QFile::rename(dir + "/msgs.dat.new", dir + "/msgs.dat");
			index = std::move(fresh);
			for (auto d = deleted.begin(); d != deleted.end(); ++d) {
				const auto peer = d.key();
				for (auto &entry : d.value()) {
					const auto i = index.constFind(Key{ peer, entry.first });
					if (i != index.constEnd()) {
						entry.second = i.value();
					}
				}
			}
		}
	};

	[[nodiscard]] static Account &For(not_null<Main::Session*> session) {
		static auto accounts = std::map<uint64, std::unique_ptr<Account>>();
		const auto id = uint64(session->userId().bare);
		auto &slot = accounts[id];
		if (!slot) {
			slot = std::make_unique<Account>();
			slot->dir = QDir::homePath()
				+ "/.local/share/vrg/store/"
				+ QString::number(id);
			slot->open();
		}
		return *slot;
	}

	[[nodiscard]] static qint32 EditDate(const MTPMessage &message) {
		if (message.type() != mtpc_message) {
			return 0;
		}
		const auto &data = message.c_message();
		return data.vedit_date() ? data.vedit_date()->v : 0;
	}

	[[nodiscard]] static bool Storable(const MTPMessage &message) {
		if (message.type() != mtpc_message) {
			return false;
		}
		if (!Conf::Bool("keep_ttl", true)) {
			const auto &data = message.c_message();
			if (data.vttl_period()) {
				return false;
			}
			if (const auto media = data.vmedia()) {
				if (media->type() == mtpc_messageMediaPhoto
					&& media->c_messageMediaPhoto().vttl_seconds()) {
					return false;
				}
				if (media->type() == mtpc_messageMediaDocument
					&& media->c_messageMediaDocument().vttl_seconds()) {
					return false;
				}
			}
		}
		return true;
	}
};

}

