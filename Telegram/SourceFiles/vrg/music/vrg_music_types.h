/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace Vrg::Music {

struct TrackInfo {
	QString id;
	QString title;
	QString artist;
	qint64 durationMs = 0;
	QString artworkUrl;
	QString streamApiUrl;
	QString permalinkUrl;

	[[nodiscard]] bool valid() const {
		return !id.isEmpty() && !streamApiUrl.isEmpty();
	}

	[[nodiscard]] bool isYoutube() const {
		return id.startsWith(QStringLiteral("yt:"));
	}

	friend inline bool operator==(
			const TrackInfo &a,
			const TrackInfo &b) {
		return a.id == b.id;
	}
};

struct WantedTrack {
	QString artist;
	QString title;
	qint64 durationMs = 0;
};

struct Playlist {
	QString id;
	QString name;
	QVector<TrackInfo> tracks;
};

enum class RepeatMode {
	None,
	One,
	All,
};

enum class PlaybackState {
	Stopped,
	Loading,
	Playing,
	Paused,
};

}

Q_DECLARE_METATYPE(Vrg::Music::TrackInfo)

