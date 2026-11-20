/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "api/api_common.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"
#include "vrg/vrg_conf.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <map>

namespace Vrg {

class AutoReply final {
public:
	[[nodiscard]] static QString Unpack(const QString &value) {
		auto result = value;
		result.replace("\\n", "\n");
		return result;
	}
	[[nodiscard]] static QString Pack(const QString &value) {
		auto result = value;
		result.replace("\r", "").replace("\n", "\\n");
		return result;
	}

	[[nodiscard]] static QString RichPath() {
		return QDir::homePath() + QStringLiteral("/.config/vrg/autoreply_rich.json");
	}

	static void SaveRich(const QString &key, const TextWithTags &value) {
		QJsonObject root;
		if (QFile f(RichPath()); f.open(QIODevice::ReadOnly)) {
			root = QJsonDocument::fromJson(f.readAll()).object();
		}
		QJsonObject entry;
		entry.insert(QStringLiteral("text"), value.text);
		QJsonArray tags;
		for (const auto &tag : value.tags) {
			QJsonObject t;
			t.insert(QStringLiteral("offset"), tag.offset);
			t.insert(QStringLiteral("length"), tag.length);
			t.insert(QStringLiteral("id"), tag.id);
			tags.append(t);
		}
		entry.insert(QStringLiteral("tags"), tags);
		root.insert(key, entry);
		QDir().mkpath(QDir::homePath() + QStringLiteral("/.config/vrg"));
		QSaveFile out(RichPath());
		if (out.open(QIODevice::WriteOnly)) {
			out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
			out.commit();
		}
	}

	[[nodiscard]] static TextWithTags LoadRich(
			const QString &key,
			const QString &fallbackConfKey) {
		if (QFile f(RichPath()); f.open(QIODevice::ReadOnly)) {
			const auto root = QJsonDocument::fromJson(f.readAll()).object();
			if (root.contains(key)) {
				const auto entry = root.value(key).toObject();
				TextWithTags result;
				result.text = entry.value(QStringLiteral("text")).toString();
				const auto tags = entry.value(QStringLiteral("tags")).toArray();
				for (const auto &item : tags) {
					const auto obj = item.toObject();
					result.tags.push_back({
						obj.value(QStringLiteral("offset")).toInt(),
						obj.value(QStringLiteral("length")).toInt(),
						obj.value(QStringLiteral("id")).toString(),
					});
				}
				return result;
			}
		}
		return TextWithTags{ Unpack(Conf::Str(fallbackConfKey)) };
	}

	struct IgnoredEntry {
		uint64 peerId = 0;
		QString title;
	};

	[[nodiscard]] static bool IsIgnored(uint64 peerId) {
		if (QFile f(RichPath()); f.open(QIODevice::ReadOnly)) {
			const auto root = QJsonDocument::fromJson(f.readAll()).object();
			return root.value(QStringLiteral("ignored"))
				.toObject()
				.contains(QString::number(peerId));
		}
		return false;
	}

	static void AddIgnored(uint64 peerId, const QString &title) {
		QJsonObject root;
		if (QFile f(RichPath()); f.open(QIODevice::ReadOnly)) {
			root = QJsonDocument::fromJson(f.readAll()).object();
		}
		auto ignored = root.value(QStringLiteral("ignored")).toObject();
		ignored.insert(QString::number(peerId), title);
		root.insert(QStringLiteral("ignored"), ignored);
		QDir().mkpath(QDir::homePath() + QStringLiteral("/.config/vrg"));
		QSaveFile out(RichPath());
		if (out.open(QIODevice::WriteOnly)) {
			out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
			out.commit();
		}
	}

	static void RemoveIgnored(uint64 peerId) {
		QJsonObject root;
		if (QFile f(RichPath()); f.open(QIODevice::ReadOnly)) {
			root = QJsonDocument::fromJson(f.readAll()).object();
		}
		auto ignored = root.value(QStringLiteral("ignored")).toObject();
		ignored.remove(QString::number(peerId));
		root.insert(QStringLiteral("ignored"), ignored);
		QDir().mkpath(QDir::homePath() + QStringLiteral("/.config/vrg"));
		QSaveFile out(RichPath());
		if (out.open(QIODevice::WriteOnly)) {
			out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
			out.commit();
		}
	}

	[[nodiscard]] static std::vector<IgnoredEntry> ListIgnored() {
		auto result = std::vector<IgnoredEntry>();
		if (QFile f(RichPath()); f.open(QIODevice::ReadOnly)) {
			const auto ignored = QJsonDocument::fromJson(f.readAll())
				.object()
				.value(QStringLiteral("ignored"))
				.toObject();
			for (auto it = ignored.constBegin(); it != ignored.constEnd(); ++it) {
				const auto id = it.key().toULongLong();
				if (id) {
					result.push_back({ id, it.value().toString(it.key()) });
				}
			}
		}
		return result;
	}

	static void Handle(not_null<HistoryItem*> item) {
		if (!Conf::Bool("autoreply_on", false)
			|| item->out()
			|| !item->isRegular()
			|| item->isService()
			|| item->isScheduled()) {
			return;
		}
		const auto peer = item->history()->peer;
		if (!peer->isUser()
			|| peer->isSelf()
			|| peer->asUser()->isBot()
			|| peer->isServiceUser()
			|| IsIgnored(uint64(peer->id.value))) {
			return;
		}
		if (QDateTime::currentSecsSinceEpoch() - item->date() > 60) {
			return;
		}
		const auto text = item->originalText().text.trimmed();
		const auto now = QDateTime::currentSecsSinceEpoch();
		auto &last = Last()[uint64(peer->id.value)];

		auto richAnswer = TextWithTags();
		auto isCommand = false;
		if (text.startsWith('/')) {
			const auto word = text.section(QRegularExpression("\\s"), 0, 0)
				.toLower();
			const auto richCmds = LoadRich(QStringLiteral("cmds"), QStringLiteral("autoreply_cmds"));
			const auto lines = richCmds.text.split('\n');
			auto lineStart = 0;
			auto capturing = false;
			auto absStart = 0;
			auto absEnd = 0;
			for (const auto &line : lines) {
				const auto lineLen = int(line.size());
				const auto arrow = line.indexOf("->");
				const auto trimmed = line.trimmed();
				const auto isHeader = (arrow > 0 && trimmed.startsWith('/'));
				if (isHeader) {
					if (capturing) {
						break;
					}
					if (line.left(arrow).trimmed().toLower() == word) {
						capturing = true;
						isCommand = true;
						absStart = lineStart + arrow + 2;
						while (absStart < lineStart + lineLen
							&& richCmds.text[absStart].isSpace()) {
							++absStart;
						}
						absEnd = lineStart + lineLen;
					}
				} else if (capturing) {
					absEnd = lineStart + lineLen;
				}
				lineStart += lineLen + 1;
			}
			if (capturing && absEnd > absStart) {
				while (absEnd > absStart && richCmds.text[absEnd - 1].isSpace()) {
					--absEnd;
				}
				if (absEnd > absStart) {
					richAnswer.text = richCmds.text.mid(absStart, absEnd - absStart);
					for (const auto &tag : richCmds.tags) {
						if (tag.offset >= absStart && (tag.offset + tag.length) <= absEnd) {
							richAnswer.tags.push_back({
								int(tag.offset - absStart),
								tag.length,
								tag.id,
							});
						}
					}
				}
			}
		}
		if (richAnswer.text.trimmed().isEmpty()) {
			auto greeting = LoadRich(QStringLiteral("greeting"), QStringLiteral("autoreply_text"));
			if (greeting.text.trimmed().isEmpty()) {
				return;
			}
			const auto cooldown = std::max(
				Conf::Int("autoreply_cooldown", 60),
				1) * 60;
			if (now - last.greeting < cooldown) {
				return;
			}
			richAnswer = std::move(greeting);
			last.greeting = now;
		} else {
			const auto rawCmdCd = Conf::Int("autoreply_cmd_cooldown", 5);
			const auto cmdCooldown = (rawCmdCd < 0)
				? (std::max(Conf::Int("autoreply_cooldown", 60), 1) * 60)
				: std::max(rawCmdCd, 0);
			if (cmdCooldown > 0 && now - last.command < cmdCooldown) {
				return;
			}
		}
		if (isCommand) {
			last.command = now;
		}
		const auto history = item->history();
		crl::on_main(&history->session(), [history, richAnswer = std::move(richAnswer)]() mutable {
			auto message = Api::MessageToSend(Api::SendAction(history));
			message.textWithTags = std::move(richAnswer);
			history->session().api().sendMessage(std::move(message));
		});
	}

private:
	struct Stamp {
		qint64 greeting = 0;
		qint64 command = 0;
	};
	[[nodiscard]] static std::map<uint64, Stamp> &Last() {
		static std::map<uint64, Stamp> map;
		return map;
	}
};

}
