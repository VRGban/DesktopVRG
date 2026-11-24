/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/music/vrg_music_types.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <deque>
#include <functional>
#include <QtCore/QHash>
#include <QtCore/QStringList>

class QNetworkAccessManager;

namespace Vrg::Music {

class YoutubeProvider final : public QObject {
public:
	explicit YoutubeProvider(QObject *parent = nullptr);

	void search(
		const QString &query,
		int limit,
		std::function<void(QVector<TrackInfo>)> callback);

	void playlist(
		const QString &url,
		std::function<void(QVector<TrackInfo>, QString error)> callback);

	void resolve(
		const QString &trackId,
		std::function<void(QString url, QString error)> callback);

	[[nodiscard]] static QString VideoId(const QString &trackId);

private:
	void withBinary(std::function<void(QString path, QString error)> callback);
	void downloadBinary(std::function<void(QString path, QString error)> callback);
	[[nodiscard]] static QString LocalBinaryPath();
	[[nodiscard]] static QString FindBinary();
	void run(
		const QStringList &args,
		int timeoutMs,
		std::function<void(QByteArray out, QString error)> callback,
		bool urgent = false);
	void runNow(
		const QStringList &args,
		int timeoutMs,
		std::function<void(QByteArray out, QString error)> callback);
	void pump();

	struct Job {
		QStringList args;
		int timeoutMs = 0;
		std::function<void(QByteArray, QString)> callback;
	};
	std::deque<Job> _jobs;
	bool _busy = false;
	QHash<QString, QPair<QString, qint64>> _urlCache;

	QNetworkAccessManager *_network = nullptr;
	bool _updateChecked = false;
	bool _downloading = false;
	QVector<std::function<void(QString, QString)>> _waiting;

};

}

