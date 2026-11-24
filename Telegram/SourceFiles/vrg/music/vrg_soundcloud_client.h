/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/music/vrg_music_types.h"

#include <QtCore/QCache>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtGui/QPixmap>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

namespace Vrg::Music {

class YoutubeProvider;

class SoundCloudClient final : public QObject {
	Q_OBJECT

public:
	explicit SoundCloudClient(QObject *parent = nullptr);
	~SoundCloudClient() override;

	void searchTracks(const QString &query);

	void searchOneTrack(
		const QString &query,
		std::function<void(std::optional<TrackInfo>)> callback,
		bool retried = false);

	void searchBestTrack(
		const WantedTrack &wanted,
		std::function<void(std::optional<TrackInfo>)> callback);

	void fetchYoutubePlaylist(
		const QString &url,
		std::function<void(QVector<TrackInfo>, QString)> callback);

	void fetchSpotifyTracks(
		const QString &url,
		std::function<void(QVector<WantedTrack>, QString)> callback);

	void resolvePlaylistUrl(
		const QString &url,
		std::function<void(QVector<TrackInfo>, QString)> callback,
		bool retried = false);

	void fetchRelatedTracks(
		const QString &trackId,
		std::function<void(QVector<TrackInfo>)> callback,
		bool retried = false);

	void resolveStreamUrl(const TrackInfo &track, bool silent = false);

	void downloadBytes(
		const QString &url,
		std::function<void(QByteArray)> callback);

	void fetchArtwork(const QString &url);

	[[nodiscard]] QPixmap cachedArtwork(const QString &url) const;

	[[nodiscard]] bool hasClientId() const;

	void setForegroundTrack(const QString &id) {
		_foregroundId = id;
	}
	[[nodiscard]] bool youtubeBlocked() const {
		return _youtubeBlocked;
	}

Q_SIGNALS:
	void tracksFound(QVector<Vrg::Music::TrackInfo> tracks);
	void streamResolved(QString trackId, QString streamUrl);
	void streamFailed(QString trackId);
	void artworkLoaded(QString url, QPixmap pixmap);
	void errorOccurred(QString message);

private:
	struct Job {
		std::function<void()> run;
		std::function<void()> fail;
	};

	void flushSearch(int serial);

	void searchCandidates(
		const QString &query,
		int limit,
		std::function<void(QVector<TrackInfo>)> callback,
		bool retried = false);

	[[nodiscard]] QNetworkReply *get(const QUrl &url, bool json);
	void withClientId(
		std::function<void()> run,
		std::function<void()> fail = nullptr);
	void requestClientId();
	void parseHomePage(QNetworkReply *reply);
	void tryNextScript();
	void finishClientIdRequest(const QString &clientId);
	[[nodiscard]] bool rejectsClientId(QNetworkReply *reply) const;

	void doSearch(const QString &query, int serial, bool retried);
	void doResolve(const TrackInfo &track, bool retried);
	void refreshTrack(const TrackInfo &track);

	[[nodiscard]] static TrackInfo ParseTrack(const QJsonObject &object);
	[[nodiscard]] static QString PickTranscoding(const QJsonObject &track);
	[[nodiscard]] static QString LargeArtwork(QString url);
	[[nodiscard]] static QString ExtractClientId(const QString &text);

	QNetworkAccessManager *_network = nullptr;
	YoutubeProvider *_youtube = nullptr;

	QString _clientId;
	bool _requestingClientId = false;
	std::vector<Job> _waitingForClientId;
	QStringList _scriptUrls;

	bool _youtubeBlocked = false;
	QString _foregroundId;
	int _searchSerial = 0;
	std::pair<int, QVector<TrackInfo>> _pendingSoundcloud;
	std::pair<int, QVector<TrackInfo>> _pendingYoutube;

	QCache<QString, QPixmap> _artwork;
	QSet<QString> _artworkLoading;

};

}

