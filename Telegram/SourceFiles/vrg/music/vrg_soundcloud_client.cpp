/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/music/vrg_soundcloud_client.h"
#include "vrg/music/vrg_youtube.h"
#include "vrg/vrg_conf.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QSharedPointer>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <limits>

namespace Vrg::Music {
namespace {

constexpr auto kSearchLimit = 25;
constexpr auto kArtworkCacheSize = 400;

const auto kHomeUrl = QStringLiteral("https://soundcloud.com/");
const auto kSearchUrl = QStringLiteral(
	"https://api-v2.soundcloud.com/search/tracks");
const auto kTrackUrl = QStringLiteral(
	"https://api-v2.soundcloud.com/tracks/%1");
const auto kUserAgent = QByteArrayLiteral(
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
	"(KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");

[[nodiscard]] QString TransliterateChar(QChar c) {
	static const QHash<QChar, QString> map = {
		{ u'а', "a" }, { u'б', "b" }, { u'в', "v" }, { u'г', "g" },
		{ u'д', "d" }, { u'е', "e" }, { u'ё', "e" }, { u'ж', "zh" },
		{ u'з', "z" }, { u'и', "i" }, { u'й', "y" }, { u'к', "k" },
		{ u'л', "l" }, { u'м', "m" }, { u'н', "n" }, { u'о', "o" },
		{ u'п', "p" }, { u'р', "r" }, { u'с', "s" }, { u'т', "t" },
		{ u'у', "u" }, { u'ф', "f" }, { u'х', "h" }, { u'ц', "c" },
		{ u'ч', "ch" }, { u'ш', "sh" }, { u'щ', "sh" }, { u'ъ', "" },
		{ u'ы', "y" }, { u'ь', "" }, { u'э', "e" }, { u'ю', "u" },
		{ u'я', "ya" },
	};
	const auto i = map.constFind(c);
	return (i == map.constEnd()) ? QString(c) : i.value();
}

[[nodiscard]] QStringList FoldTokens(const QString &text) {
	const auto decomposed = text.toLower().normalized(QString::NormalizationForm_D);
	QStringList tokens;
	QString current;
	const auto flush = [&] {
		if (!current.isEmpty()) {
			tokens.append(current);
			current.clear();
		}
	};
	for (const auto ch : decomposed) {
		if (ch.category() == QChar::Mark_NonSpacing) {
			continue;
		}
		if (ch.isLetterOrNumber()) {
			current += TransliterateChar(ch);
		} else {
			flush();
		}
	}
	flush();
	return tokens;
}

[[nodiscard]] QString CleanTitle(QString title) {
	static const QRegularExpression brackets(
		QStringLiteral("[\\(\\[\\{][^\\)\\]\\}]*[\\)\\]\\}]"));
	static const QRegularExpression feat(
		QStringLiteral("\\s(feat\\.?|ft\\.?|featuring|with)\\s.*$"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression suffix(
		QStringLiteral("\\s[-\u2013\u2014]\\s(.*(remaster|version|edit|mix|live|mono|stereo|"
			"single|deluxe|bonus|\\d{4}).*)$"),
		QRegularExpression::CaseInsensitiveOption);
	title.remove(brackets);
	title.remove(suffix);
	title.remove(feat);
	return title.simplified();
}

[[nodiscard]] QString MainArtist(const QString &artist) {
	static const QRegularExpression splitter(
		QStringLiteral("\\s*(,|&|;|\\sx\\s|\\sfeat\\.?\\s|\\sft\\.?\\s|\\sand\\s)\\s*"),
		QRegularExpression::CaseInsensitiveOption);
	return artist.split(splitter, Qt::SkipEmptyParts).value(0).trimmed();
}

[[nodiscard]] QVector<QStringList> ArtistTokenSets(const QString &artist) {
	static const QRegularExpression splitter(
		QStringLiteral("\\s*(,|&|;|\\sx\\s|\\sfeat\\.?\\s|\\sft\\.?\\s|\\sand\\s)\\s*"),
		QRegularExpression::CaseInsensitiveOption);
	QVector<QStringList> result;
	for (const auto &part : artist.split(splitter, Qt::SkipEmptyParts)) {
		const auto tokens = FoldTokens(part);
		if (!tokens.isEmpty()) {
			result.push_back(tokens);
		}
	}
	return result;
}

[[nodiscard]] double MatchScore(
		const WantedTrack &want,
		const TrackInfo &candidate) {
	const auto titleTokens = FoldTokens(CleanTitle(want.title));
	if (titleTokens.isEmpty()) {
		return 0.;
	}
	const auto haystack = FoldTokens(candidate.artist + QChar(' ') + candidate.title);
	const auto set = QSet<QString>(haystack.begin(), haystack.end());
	auto hits = 0;
	for (const auto &t : titleTokens) {
		if (set.contains(t)) {
			++hits;
		}
	}
	const auto titleHit = double(hits) / titleTokens.size();

	auto artistHit = 1.;
	const auto sets = ArtistTokenSets(want.artist);
	if (!sets.isEmpty()) {
		artistHit = 0.;
		for (const auto &tokens : sets) {
			auto h = 0;
			for (const auto &t : tokens) {
				if (set.contains(t)) {
					++h;
				}
			}
			artistHit = std::max(artistHit, double(h) / tokens.size());
		}
	}

	if (!sets.isEmpty() && artistHit < 0.5) {
		return 0.2;
	}

	auto score = 0.65 * titleHit + 0.35 * artistHit;

	static const QStringList bad = {
		QStringLiteral("remix"), QStringLiteral("cover"),
		QStringLiteral("slowed"), QStringLiteral("sped"),
		QStringLiteral("nightcore"), QStringLiteral("reverb"),
		QStringLiteral("karaoke"), QStringLiteral("instrumental"),
		QStringLiteral("8d"), QStringLiteral("mashup"),
		QStringLiteral("bootleg"), QStringLiteral("edit"),
	};
	const auto wantedAll = FoldTokens(want.artist + QChar(' ') + want.title);
	const auto wantedSet = QSet<QString>(wantedAll.begin(), wantedAll.end());
	for (const auto &word : bad) {
		if (set.contains(word) && !wantedSet.contains(word)) {
			score -= 0.25;
		}
	}

	if (want.durationMs > 0 && candidate.durationMs > 0) {
		const auto diff = std::abs(want.durationMs - candidate.durationMs);
		if (diff <= 4000) {
			score += 0.15;
		} else if (diff > 20000) {
			score -= 0.25;
		}
	}
	return score;
}

[[nodiscard]] bool WantSoundcloud() {
	return Vrg::Conf::Str("search_sources") != QStringLiteral("yt");
}

[[nodiscard]] bool WantYoutube() {
	return Vrg::Conf::Str("search_sources") != QStringLiteral("sc");
}

[[nodiscard]] QVector<TrackInfo> Interleave(
		const QVector<TrackInfo> &a,
		const QVector<TrackInfo> &b) {
	QVector<TrackInfo> result;
	result.reserve(a.size() + b.size());
	for (auto i = 0; i < std::max(a.size(), b.size()); ++i) {
		if (i < a.size()) {
			result.push_back(a[i]);
		}
		if (i < b.size()) {
			result.push_back(b[i]);
		}
	}
	return result;
}

[[nodiscard]] int HttpStatus(QNetworkReply *reply) {
	return reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

[[nodiscard]] QString ErrorText(QNetworkReply *reply) {
	const auto status = HttpStatus(reply);
	return status
		? QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString())
		: reply->errorString();
}

[[nodiscard]] QUrl WithClientId(QUrl url, const QString &clientId) {
	auto query = QUrlQuery(url);
	query.removeAllQueryItems(QStringLiteral("client_id"));
	query.addQueryItem(QStringLiteral("client_id"), clientId);
	url.setQuery(query);
	return url;
}

}

SoundCloudClient::SoundCloudClient(QObject *parent)
: QObject(parent)
, _network(new QNetworkAccessManager(this))
, _youtube(new YoutubeProvider(this))
, _artwork(kArtworkCacheSize) {
}

SoundCloudClient::~SoundCloudClient() = default;

bool SoundCloudClient::hasClientId() const {
	return !_clientId.isEmpty();
}

QNetworkReply *SoundCloudClient::get(const QUrl &url, bool json) {
	auto request = QNetworkRequest(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	if (json) {
		request.setRawHeader("Accept", "application/json");
	}
	return _network->get(request);
}

void SoundCloudClient::withClientId(
		std::function<void()> run,
		std::function<void()> fail) {
	if (!_clientId.isEmpty()) {
		run();
		return;
	}
	_waitingForClientId.push_back({ std::move(run), std::move(fail) });
	requestClientId();
}

void SoundCloudClient::requestClientId() {
	if (_requestingClientId) {
		return;
	}
	_requestingClientId = true;
	const auto reply = get(QUrl(kHomeUrl), false);
	connect(reply, &QNetworkReply::finished, this, [=, this] {
		parseHomePage(reply);
	});
}

void SoundCloudClient::parseHomePage(QNetworkReply *reply) {
	reply->deleteLater();
	if (reply->error() != QNetworkReply::NoError) {
		Q_EMIT errorOccurred(
			QStringLiteral("SoundCloud недоступен: ") + ErrorText(reply));
		finishClientIdRequest(QString());
		return;
	}
	const auto page = QString::fromUtf8(reply->readAll());

	static const auto hydrated = QRegularExpression(
		QStringLiteral(R"re("apiClient"\s*,\s*"data"\s*:\s*\{\s*"id"\s*:\s*"([A-Za-z0-9]{32})")re"));
	if (const auto match = hydrated.match(page); match.hasMatch()) {
		finishClientIdRequest(match.captured(1));
		return;
	}

	static const auto script = QRegularExpression(
		QStringLiteral(R"re(<script[^>]+src="(https://a-v2\.sndcdn\.com/assets/[^"]+\.js)")re"));
	_scriptUrls.clear();
	auto i = script.globalMatch(page);
	while (i.hasNext()) {
		_scriptUrls.prepend(i.next().captured(1));
	}
	tryNextScript();
}

void SoundCloudClient::tryNextScript() {
	if (_scriptUrls.isEmpty()) {
		Q_EMIT errorOccurred(
			QStringLiteral("Не удалось получить client_id SoundCloud"));
		finishClientIdRequest(QString());
		return;
	}
	const auto reply = get(QUrl(_scriptUrls.takeFirst()), false);
	connect(reply, &QNetworkReply::finished, this, [=, this] {
		reply->deleteLater();
		if (reply->error() == QNetworkReply::NoError) {
			const auto clientId = ExtractClientId(
				QString::fromUtf8(reply->readAll()));
			if (!clientId.isEmpty()) {
				_scriptUrls.clear();
				finishClientIdRequest(clientId);
				return;
			}
		}
		tryNextScript();
	});
}

void SoundCloudClient::finishClientIdRequest(const QString &clientId) {
	_requestingClientId = false;
	_clientId = clientId;
	auto jobs = std::exchange(_waitingForClientId, {});
	for (auto &job : jobs) {
		if (!clientId.isEmpty()) {
			job.run();
		} else if (job.fail) {
			job.fail();
		}
	}
}

bool SoundCloudClient::rejectsClientId(QNetworkReply *reply) const {
	const auto status = HttpStatus(reply);
	return (status == 401) || (status == 403);
}

QString SoundCloudClient::ExtractClientId(const QString &text) {
	static const auto pattern = QRegularExpression(
		QStringLiteral(R"re(client_id\s*[:=]\s*["']?([A-Za-z0-9]{32})\b)re"));
	const auto match = pattern.match(text);
	return match.hasMatch() ? match.captured(1) : QString();
}

void SoundCloudClient::searchTracks(const QString &query) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	doSearch(trimmed, ++_searchSerial, false);
}

void SoundCloudClient::doSearch(
		const QString &query,
		int serial,
		bool retried) {
	if (!WantSoundcloud()) {
		_youtube->search(query, 20, [=, this](QVector<TrackInfo> found) {
			if (serial == _searchSerial) {
				Q_EMIT tracksFound(found);
			}
		});
		return;
	}
	if (WantYoutube() && !retried) {
		_youtube->search(query, 12, [=, this](QVector<TrackInfo> found) {
			_pendingYoutube = { serial, found };
			flushSearch(serial);
		});
	}
	withClientId([=, this] {
		auto url = QUrl(kSearchUrl);
		auto params = QUrlQuery();
		params.addQueryItem(QStringLiteral("q"), query);
		params.addQueryItem(QStringLiteral("client_id"), _clientId);
		params.addQueryItem(
			QStringLiteral("limit"),
			QString::number(kSearchLimit));
		params.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
		url.setQuery(params);

		const auto reply = get(url, true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			if (serial != _searchSerial) {
				return;
			}
			if (rejectsClientId(reply) && !retried) {
				_clientId.clear();
				doSearch(query, serial, true);
				return;
			}
			if (reply->error() != QNetworkReply::NoError) {
				if (WantYoutube()) {
					_pendingSoundcloud = { serial, {} };
					flushSearch(serial);
					return;
				}
				Q_EMIT errorOccurred(
					QStringLiteral("Ошибка поиска: ") + ErrorText(reply));
				return;
			}
			const auto document = QJsonDocument::fromJson(reply->readAll());
			const auto collection = document.object().value(
				QStringLiteral("collection")).toArray();
			auto tracks = QVector<TrackInfo>();
			tracks.reserve(collection.size());
			for (const auto &entry : collection) {
				const auto track = ParseTrack(entry.toObject());
				if (track.valid()) {
					tracks.push_back(track);
				}
			}
			if (WantYoutube()) {
				_pendingSoundcloud = { serial, tracks };
				flushSearch(serial);
				return;
			}
			Q_EMIT tracksFound(tracks);
		});
	});
}

void SoundCloudClient::flushSearch(int serial) {
	if (serial != _searchSerial
		|| _pendingSoundcloud.first != serial
		|| _pendingYoutube.first != serial) {
		return;
	}
	const auto combined = Interleave(
		_pendingSoundcloud.second,
		_pendingYoutube.second);
	_pendingSoundcloud = { 0, {} };
	_pendingYoutube = { 0, {} };
	Q_EMIT tracksFound(combined);
}

void SoundCloudClient::searchOneTrack(
		const QString &query,
		std::function<void(std::optional<TrackInfo>)> callback,
		bool retried) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty()) {
		if (callback) {
			callback(std::nullopt);
		}
		return;
	}
	withClientId([=, this] {
		auto url = QUrl(kSearchUrl);
		auto params = QUrlQuery();
		params.addQueryItem(QStringLiteral("q"), trimmed);
		params.addQueryItem(QStringLiteral("client_id"), _clientId);
		params.addQueryItem(QStringLiteral("limit"), QStringLiteral("3"));
		params.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
		url.setQuery(params);

		const auto reply = get(url, true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			if (rejectsClientId(reply) && !retried) {
				_clientId.clear();
				searchOneTrack(trimmed, callback, true);
				return;
			}
			if (reply->error() != QNetworkReply::NoError) {
				if (callback) {
					callback(std::nullopt);
				}
				return;
			}
			const auto document = QJsonDocument::fromJson(reply->readAll());
			const auto collection = document.object().value(
				QStringLiteral("collection")).toArray();
			std::optional<TrackInfo> best;
			for (const auto &entry : collection) {
				const auto track = ParseTrack(entry.toObject());
				if (track.valid()) {
					best = track;
					if (track.durationMs > 0) {
						break;
					}
				}
			}
			if (callback) {
				callback(best);
			}
		});
	});
}

void SoundCloudClient::searchCandidates(
		const QString &query,
		int limit,
		std::function<void(QVector<TrackInfo>)> callback,
		bool retried) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty()) {
		if (callback) {
			callback({});
		}
		return;
	}
	withClientId([=, this] {
		auto url = QUrl(kSearchUrl);
		auto params = QUrlQuery();
		params.addQueryItem(QStringLiteral("q"), trimmed);
		params.addQueryItem(QStringLiteral("client_id"), _clientId);
		params.addQueryItem(QStringLiteral("limit"), QString::number(limit));
		params.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
		url.setQuery(params);

		const auto reply = get(url, true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			if (rejectsClientId(reply) && !retried) {
				_clientId.clear();
				searchCandidates(trimmed, limit, callback, true);
				return;
			}
			QVector<TrackInfo> result;
			if (reply->error() == QNetworkReply::NoError) {
				const auto document = QJsonDocument::fromJson(reply->readAll());
				const auto collection = document.object().value(
					QStringLiteral("collection")).toArray();
				for (const auto &entry : collection) {
					const auto track = ParseTrack(entry.toObject());
					if (track.valid()) {
						result.push_back(track);
					}
				}
			}
			if (callback) {
				callback(result);
			}
		});
	}, [=] {
		if (callback) {
			callback({});
		}
	});
}

void SoundCloudClient::searchBestTrack(
		const WantedTrack &wanted,
		std::function<void(std::optional<TrackInfo>)> callback) {
	const auto cleanTitle = CleanTitle(wanted.title);
	const auto mainArtist = MainArtist(wanted.artist);
	QStringList variants;
	const auto add = [&](const QString &q) {
		const auto v = q.simplified();
		if (!v.isEmpty() && !variants.contains(v, Qt::CaseInsensitive)) {
			variants.append(v);
		}
	};
	add(mainArtist + QChar(' ') + cleanTitle);
	add(wanted.artist + QChar(' ') + wanted.title);
	add(cleanTitle);

	struct State {
		QStringList variants;
		int index = 0;
		std::optional<TrackInfo> best;
		double bestScore = -1.;
	};
	const auto state = QSharedPointer<State>::create();
	state->variants = variants;
	const auto next = QSharedPointer<std::function<void()>>::create();
	const QPointer<SoundCloudClient> self(this);
	*next = [=] {
		if (!self) {
			return;
		}
		constexpr auto kAccept = 0.6;
		constexpr auto kStrong = 0.85;
		if (state->index >= state->variants.size()
			|| state->bestScore >= kStrong) {
			if (state->bestScore < kAccept && WantYoutube()) {
				const auto q = (mainArtist + QChar(' ') + cleanTitle).simplified();
				self->_youtube->search(q, 6, [=](QVector<TrackInfo> found) {
					for (const auto &track : found) {
						const auto score = MatchScore(wanted, track);
						if (score > state->bestScore) {
							state->bestScore = score;
							state->best = track;
						}
					}
					if (callback) {
						callback(state->bestScore >= kAccept
							? state->best
							: std::optional<TrackInfo>());
					}
				});
				return;
			}
			if (callback) {
				callback(state->bestScore >= kAccept
					? state->best
					: std::optional<TrackInfo>());
			}
			return;
		}
		const auto query = state->variants[state->index++];
		self->searchCandidates(query, 10, [=](QVector<TrackInfo> found) {
			for (const auto &track : found) {
				const auto score = MatchScore(wanted, track);
				if (score > state->bestScore) {
					state->bestScore = score;
					state->best = track;
				}
			}
			(*next)();
		});
	};
	(*next)();
}

void SoundCloudClient::fetchYoutubePlaylist(
		const QString &url,
		std::function<void(QVector<TrackInfo>, QString)> callback) {
	_youtube->playlist(url, std::move(callback));
}

void SoundCloudClient::fetchSpotifyTracks(
		const QString &rawUrl,
		std::function<void(QVector<WantedTrack>, QString)> callback) {
	static const QRegularExpression rx(
		QStringLiteral("open\\.spotify\\.com/(?:intl-[a-z-]+/)?(playlist|album|track)/([A-Za-z0-9]+)"));
	const auto m = rx.match(rawUrl.trimmed());
	if (!m.hasMatch()) {
		if (callback) {
			callback({}, QStringLiteral("Неверная ссылка Spotify"));
		}
		return;
	}
	const auto kind = m.captured(1);
	const auto id = m.captured(2);
	const auto embedUrl = QUrl(
		QStringLiteral("https://open.spotify.com/embed/%1/%2").arg(kind, id));
	const auto reply = get(embedUrl, false);
	connect(reply, &QNetworkReply::finished, this, [=, this] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			if (callback) {
				callback({}, QStringLiteral("Не удалось загрузить Spotify: ") + ErrorText(reply));
			}
			return;
		}
		const auto html = QString::fromUtf8(reply->readAll());
		static const QRegularExpression nextDataRx(
			QStringLiteral("<script[^>]*id=\"__NEXT_DATA__\"[^>]*>(.*?)</script>"),
			QRegularExpression::DotMatchesEverythingOption);
		const auto jsonMatch = nextDataRx.match(html);
		if (!jsonMatch.hasMatch()) {
			if (callback) {
				callback({}, QStringLiteral("Не удалось разобрать страницу Spotify"));
			}
			return;
		}
		const auto doc = QJsonDocument::fromJson(jsonMatch.captured(1).toUtf8());
		const auto entity = doc.object()
			.value(QStringLiteral("props")).toObject()
			.value(QStringLiteral("pageProps")).toObject()
			.value(QStringLiteral("state")).toObject()
			.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("entity")).toObject();

		QVector<WantedTrack> queries;
		const auto trackList = entity.value(QStringLiteral("trackList")).toArray();
		for (const auto &item : trackList) {
			const auto obj = item.toObject();
			const auto title = obj.value(QStringLiteral("title")).toString().trimmed();
			const auto artist = obj.value(QStringLiteral("subtitle")).toString().trimmed();
			if (!title.isEmpty()) {
				queries.push_back({
					artist,
					title,
					qint64(obj.value(QStringLiteral("duration")).toDouble()),
				});
			}
		}
		if (queries.isEmpty()) {
			const auto title = entity.value(QStringLiteral("title")).toString().trimmed();
			QStringList artists;
			for (const auto &a : entity.value(QStringLiteral("artists")).toArray()) {
				const auto name = a.toObject().value(QStringLiteral("name")).toString().trimmed();
				if (!name.isEmpty()) {
					artists.append(name);
				}
			}
			if (!title.isEmpty()) {
				queries.push_back({
					artists.join(QStringLiteral(", ")),
					title,
					qint64(entity.value(QStringLiteral("duration")).toDouble()),
				});
			}
		}
		if (queries.isEmpty()) {
			if (callback) {
				callback({}, QStringLiteral("Плейлист пуст или приватный"));
			}
			return;
		}
		if (callback) {
			callback(queries, QString());
		}
	});
}

void SoundCloudClient::resolvePlaylistUrl(
		const QString &rawUrl,
		std::function<void(QVector<TrackInfo>, QString)> callback,
		bool retried) {
	const auto cleaned = rawUrl.trimmed();
	withClientId([=, this] {
		QUrl url(QStringLiteral("https://api-v2.soundcloud.com/resolve"));
		QUrlQuery params;
		params.addQueryItem(QStringLiteral("url"), cleaned);
		params.addQueryItem(QStringLiteral("client_id"), _clientId);
		url.setQuery(params);

		const auto reply = get(url, true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			if (rejectsClientId(reply) && !retried) {
				_clientId.clear();
				resolvePlaylistUrl(cleaned, callback, true);
				return;
			}
			if (reply->error() != QNetworkReply::NoError) {
				if (callback) {
					callback({}, QStringLiteral("Ошибка SoundCloud: ") + ErrorText(reply));
				}
				return;
			}
			const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
			const auto kind = obj.value(QStringLiteral("kind")).toString();
			QVector<TrackInfo> out;
			if (kind == QStringLiteral("track")) {
				const auto t = ParseTrack(obj);
				if (t.valid()) {
					out.push_back(t);
				}
			} else if (kind == QStringLiteral("playlist")) {
				const auto arr = obj.value(QStringLiteral("tracks")).toArray();
				for (const auto &entry : arr) {
					const auto t = ParseTrack(entry.toObject());
					if (t.valid()) {
						out.push_back(t);
					}
				}
			}
			if (callback) {
				callback(out, out.isEmpty() ? QStringLiteral("Треки не найдены") : QString());
			}
		});
	});
}

void SoundCloudClient::fetchRelatedTracks(
		const QString &trackId,
		std::function<void(QVector<TrackInfo>)> callback,
		bool retried) {
	const auto id = trackId.trimmed();
	if (id.isEmpty() || id.startsWith(QStringLiteral("yt:"))) {
		if (callback) {
			callback({});
		}
		return;
	}
	withClientId([=, this] {
		QUrl url(QStringLiteral("https://api-v2.soundcloud.com/tracks/%1/related").arg(id));
		QUrlQuery params;
		params.addQueryItem(QStringLiteral("client_id"), _clientId);
		params.addQueryItem(QStringLiteral("limit"), QStringLiteral("12"));
		url.setQuery(params);

		const auto reply = get(url, true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			if (rejectsClientId(reply) && !retried) {
				_clientId.clear();
				fetchRelatedTracks(id, callback, true);
				return;
			}
			QVector<TrackInfo> out;
			if (reply->error() == QNetworkReply::NoError) {
				const auto collection = QJsonDocument::fromJson(reply->readAll())
					.object().value(QStringLiteral("collection")).toArray();
				for (const auto &entry : collection) {
					const auto t = ParseTrack(entry.toObject());
					if (t.valid()) {
						out.push_back(t);
					}
				}
			}
			if (callback) {
				callback(out);
			}
		});
	});
}

void SoundCloudClient::resolveStreamUrl(const TrackInfo &track) {
	if (!track.valid()) {
		Q_EMIT errorOccurred(QStringLiteral("Трек нельзя воспроизвести"));
		Q_EMIT streamFailed(track.id);
		return;
	}
	if (track.isYoutube()) {
		_youtube->resolve(track.id, [=, this](QString url, QString error) {
			if (url.isEmpty()) {
				_youtubeBlocked = error.contains(QStringLiteral("в настройках"))
					|| error.contains(QStringLiteral("нет входа в Google"));
				Q_EMIT errorOccurred(error);
				Q_EMIT streamFailed(track.id);
			} else {
				_youtubeBlocked = false;
				Q_EMIT streamResolved(track.id, url);
			}
		});
		return;
	}
	doResolve(track, false);
}

void SoundCloudClient::doResolve(const TrackInfo &track, bool retried) {
	const auto fail = [=, this] {
		Q_EMIT streamFailed(track.id);
	};
	withClientId([=, this] {
		const auto reply = get(
			WithClientId(QUrl(track.streamApiUrl), _clientId),
			true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			const auto url = (reply->error() == QNetworkReply::NoError)
				? QJsonDocument::fromJson(reply->readAll()).object().value(
					QStringLiteral("url")).toString()
				: QString();
			if (url.isEmpty() && !retried) {
				if (rejectsClientId(reply)) {
					_clientId.clear();
				}
				refreshTrack(track);
				return;
			} else if (url.isEmpty()) {
				Q_EMIT errorOccurred(
					QStringLiteral("Не удалось получить поток «%1»: %2").arg(
						track.title,
						(reply->error() != QNetworkReply::NoError)
							? ErrorText(reply)
							: QStringLiteral("пустой ответ")));
				Q_EMIT streamFailed(track.id);
				return;
			}
			Q_EMIT streamResolved(track.id, url);
		});
	}, fail);
}

void SoundCloudClient::refreshTrack(const TrackInfo &track) {
	const auto fail = [=, this] {
		Q_EMIT streamFailed(track.id);
	};
	withClientId([=, this] {
		const auto reply = get(
			WithClientId(QUrl(kTrackUrl.arg(track.id)), _clientId),
			true);
		connect(reply, &QNetworkReply::finished, this, [=, this] {
			reply->deleteLater();
			auto fresh = (reply->error() == QNetworkReply::NoError)
				? ParseTrack(QJsonDocument::fromJson(reply->readAll()).object())
				: TrackInfo();
			if (!fresh.valid()) {
				Q_EMIT errorOccurred(
					QStringLiteral("Трек «%1» больше недоступен").arg(
						track.title));
				Q_EMIT streamFailed(track.id);
				return;
			}
			fresh.id = track.id;
			doResolve(fresh, true);
		});
	}, fail);
}

void SoundCloudClient::fetchArtwork(const QString &url) {
	if (url.isEmpty()) {
		return;
	}
	if (const auto cached = _artwork.object(url)) {
		Q_EMIT artworkLoaded(url, *cached);
		return;
	}
	if (_artworkLoading.contains(url)) {
		return;
	}
	_artworkLoading.insert(url);
	const auto reply = get(QUrl(url), false);
	connect(reply, &QNetworkReply::finished, this, [=, this] {
		reply->deleteLater();
		_artworkLoading.remove(url);
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		auto pixmap = QPixmap();
		if (!pixmap.loadFromData(reply->readAll()) || pixmap.isNull()) {
			return;
		}
		_artwork.insert(url, new QPixmap(pixmap));
		Q_EMIT artworkLoaded(url, pixmap);
	});
}

void SoundCloudClient::downloadBytes(
		const QString &url,
		std::function<void(QByteArray)> callback) {
	const auto reply = get(QUrl(url), false);
	connect(reply, &QNetworkReply::finished, this, [=, this] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			callback({});
			return;
		}
		const auto body = reply->readAll();
		if (!body.startsWith("#EXTM3U")) {
			callback(body);
			return;
		}
		const auto base = reply->url();
		const auto text = QString::fromUtf8(body);
		if (text.contains(QStringLiteral("#EXT-X-KEY"))
			&& !text.contains(QStringLiteral("METHOD=NONE"))) {
			callback({});
			return;
		}
		QUrl initUrl;
		QVector<QUrl> parts;
		QUrl variant;
		for (auto line : text.split('\n')) {
			line = line.trimmed();
			if (line.startsWith(QStringLiteral("#EXT-X-MAP"))) {
				static const QRegularExpression rx(QStringLiteral("URI=\"([^\"]+)\""));
				const auto m = rx.match(line);
				if (m.hasMatch()) {
					initUrl = base.resolved(QUrl(m.captured(1)));
				}
			} else if (line.startsWith(QStringLiteral("#EXT-X-STREAM-INF"))) {
				variant = QUrl(QStringLiteral("-"));
			} else if (!line.isEmpty() && !line.startsWith('#')) {
				const auto resolved = base.resolved(QUrl(line));
				if (variant.isValid() && variant.toString() == QStringLiteral("-")) {
					variant = resolved;
					break;
				}
				parts.push_back(resolved);
			}
		}
		if (variant.isValid() && variant.toString() != QStringLiteral("-")) {
			downloadBytes(variant.toString(), callback);
			return;
		}
		if (!initUrl.isEmpty()) {
			parts.push_front(initUrl);
		}
		if (parts.isEmpty()) {
			callback({});
			return;
		}
		struct Job {
			QVector<QByteArray> chunks;
			int remaining = 0;
			bool failed = false;
		};
		const auto job = QSharedPointer<Job>::create();
		job->chunks.resize(parts.size());
		job->remaining = parts.size();
		for (auto i = 0; i < parts.size(); ++i) {
			const auto part = get(parts[i], false);
			connect(part, &QNetworkReply::finished, this, [=] {
				part->deleteLater();
				if (part->error() != QNetworkReply::NoError) {
					job->failed = true;
				} else {
					job->chunks[i] = part->readAll();
				}
				if (--job->remaining > 0) {
					return;
				}
				if (job->failed) {
					callback({});
					return;
				}
				QByteArray all;
				for (const auto &c : job->chunks) {
					all.append(c);
				}
				callback(all);
			});
		}
	});
}

QPixmap SoundCloudClient::cachedArtwork(const QString &url) const {
	const auto cached = _artwork.object(url);
	return cached ? *cached : QPixmap();
}

TrackInfo SoundCloudClient::ParseTrack(const QJsonObject &object) {
	if (object.value(QStringLiteral("kind")).toString()
			!= QStringLiteral("track")) {
		return {};
	}
	const auto policy = object.value(QStringLiteral("policy")).toString();
	if (policy == QStringLiteral("BLOCK") || policy == QStringLiteral("SNIP")) {
		return {};
	}
	const auto user = object.value(QStringLiteral("user")).toObject();
	const auto publisher = object.value(
		QStringLiteral("publisher_metadata")).toObject();
	const auto publisherArtist = publisher.value(
		QStringLiteral("artist")).toString();

	auto result = TrackInfo();
	result.id = QString::number(
		object.value(QStringLiteral("id")).toVariant().toLongLong());
	result.title = object.value(QStringLiteral("title")).toString();
	result.artist = publisherArtist.isEmpty()
		? user.value(QStringLiteral("username")).toString()
		: publisherArtist;
	const auto full = object.value(QStringLiteral("full_duration")).toDouble();
	result.durationMs = qint64(full > 0.
		? full
		: object.value(QStringLiteral("duration")).toDouble());
	const auto artwork = object.value(QStringLiteral("artwork_url")).toString();
	result.artworkUrl = LargeArtwork(artwork.isEmpty()
		? user.value(QStringLiteral("avatar_url")).toString()
		: artwork);
	result.permalinkUrl = object.value(
		QStringLiteral("permalink_url")).toString();
	result.streamApiUrl = PickTranscoding(object);
	if (result.id == QStringLiteral("0")) {
		result.id.clear();
	}
	return result;
}

QString SoundCloudClient::PickTranscoding(const QJsonObject &track) {
	const auto rank = [](const QString &protocol, const QString &mime,
			const QString &preset) {
		const auto progressive = (protocol == QStringLiteral("progressive"));
		const auto hls = (protocol == QStringLiteral("hls"));
		if (mime == QStringLiteral("audio/mpeg")) {
			return progressive ? 0 : hls ? 1 : -1;
		} else if (!hls) {
			return -1;
		} else if (mime == QStringLiteral("audio/mp4")) {
			return preset.contains(QStringLiteral("160")) ? 2 : 3;
		} else if (mime == QStringLiteral("audio/ogg")) {
			return 4;
		}
		return -1;
	};
	const auto transcodings = track.value(QStringLiteral("media")).toObject()
		.value(QStringLiteral("transcodings")).toArray();
	auto chosen = QString();
	auto chosenRank = std::numeric_limits<int>::max();
	for (const auto &entry : transcodings) {
		const auto transcoding = entry.toObject();
		if (transcoding.value(QStringLiteral("snipped")).toBool()) {
			continue;
		}
		const auto url = transcoding.value(QStringLiteral("url")).toString();
		const auto format = transcoding.value(
			QStringLiteral("format")).toObject();
		const auto mime = format.value(
			QStringLiteral("mime_type")).toString().section(';', 0, 0).trimmed();
		const auto current = rank(
			format.value(QStringLiteral("protocol")).toString(),
			mime,
			transcoding.value(QStringLiteral("preset")).toString());
		if (!url.isEmpty() && current >= 0 && current < chosenRank) {
			chosen = url;
			chosenRank = current;
		}
	}
	if (chosen.isEmpty()) {
		return QString();
	}
	const auto authorization = track.value(
		QStringLiteral("track_authorization")).toString();
	if (authorization.isEmpty()) {
		return chosen;
	}
	auto url = QUrl(chosen);
	auto query = QUrlQuery(url);
	query.addQueryItem(QStringLiteral("track_authorization"), authorization);
	url.setQuery(query);
	return url.toString();
}

QString SoundCloudClient::LargeArtwork(QString url) {
	url.replace(QStringLiteral("-large."), QStringLiteral("-t200x200."));
	return url;
}

}
