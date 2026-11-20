/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/music/vrg_youtube.h"
#include "vrg/vrg_conf.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <memory>
#include <QtCore/QDir>
#include <QtCore/QSharedPointer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Vrg::Music {
namespace {

#ifdef Q_OS_WIN
const auto kBinaryName = QStringLiteral("yt-dlp.exe");
#else
const auto kBinaryName = QStringLiteral("yt-dlp");
#endif

const auto kDownloadUrl = QStringLiteral(
	"https://github.com/yt-dlp/yt-dlp/releases/latest/download/")
	+ kBinaryName;

}

static QString FriendlyError(const QString &error) {
	if (error.isEmpty()) {
		return QStringLiteral("YouTube: не получена ссылка");
	}
	const auto lower = error.toLower();
	const auto browser = Vrg::Conf::Str("yt_cookies_browser");
	const auto cookiesFailed = lower.contains(QStringLiteral("cookies"))
		&& (lower.contains(QStringLiteral("could not find"))
			|| lower.contains(QStringLiteral("failed"))
			|| lower.contains(QStringLiteral("unable"))
			|| lower.contains(QStringLiteral("decrypt")))
		&& !lower.contains(QStringLiteral("sign in"));
	if (cookiesFailed) {
		return QStringLiteral(
			"Не удалось прочитать куки браузера «%1». Проверь выбор в настройках.")
			.arg(browser);
	}
	if (lower.contains(QStringLiteral("sign in"))
		|| lower.contains(QStringLiteral("not a bot"))
		|| lower.contains(QStringLiteral("--cookies"))
		|| lower.contains(QStringLiteral("authentication"))) {
		return browser.isEmpty()
			? QStringLiteral(
				"Для YouTube включи вход через браузер в настройках.")
			: QStringLiteral(
				"В браузере «%1» нет входа в Google. Открой youtube.com в нём "
				"и войди в аккаунт.").arg(browser);
	}
	if (lower.contains(QStringLiteral("unavailable"))
		|| lower.contains(QStringLiteral("private"))
		|| lower.contains(QStringLiteral("removed"))) {
		return QStringLiteral("Видео недоступно на YouTube");
	}
	return QStringLiteral("YouTube: ") + error.left(160);
}

YoutubeProvider::YoutubeProvider(QObject *parent)
: QObject(parent)
, _network(new QNetworkAccessManager(this)) {
	_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QString YoutubeProvider::VideoId(const QString &trackId) {
	return trackId.startsWith(QStringLiteral("yt:"))
		? trackId.mid(3)
		: QString();
}

QString YoutubeProvider::LocalBinaryPath() {
	return QDir::homePath() + QStringLiteral("/.config/vrg/") + kBinaryName;
}

QString YoutubeProvider::FindBinary() {
	const auto local = LocalBinaryPath();
	if (QFileInfo::exists(local)) {
		return local;
	}
	const auto bundled = QCoreApplication::applicationDirPath()
		+ QChar('/')
		+ kBinaryName;
	if (QFileInfo::exists(bundled)) {
		QDir().mkpath(QFileInfo(local).absolutePath());
		if (QFile::copy(bundled, local)) {
			QFile::setPermissions(local, QFile::permissions(local)
				| QFileDevice::ExeOwner
				| QFileDevice::ExeGroup
				| QFileDevice::ExeOther);
			return local;
		}
		return bundled;
	}
	return QStandardPaths::findExecutable(kBinaryName);
}

void YoutubeProvider::run(
		const QStringList &args,
		int timeoutMs,
		std::function<void(QByteArray, QString)> callback,
		bool urgent) {
	Job job{ args, timeoutMs, std::move(callback) };
	if (urgent) {
		_jobs.push_front(std::move(job));
	} else {
		_jobs.push_back(std::move(job));
	}
	pump();
}

void YoutubeProvider::pump() {
	if (_busy || _jobs.empty()) {
		return;
	}
	_busy = true;
	auto job = std::move(_jobs.front());
	_jobs.pop_front();
	runNow(job.args, job.timeoutMs, [=, this](QByteArray out, QString error) {
		_busy = false;
		job.callback(std::move(out), std::move(error));
		pump();
	});
}

void YoutubeProvider::runNow(
		const QStringList &args,
		int timeoutMs,
		std::function<void(QByteArray, QString)> callback) {
	withBinary([=, this](QString path, QString error) {
		if (path.isEmpty()) {
			callback({}, error);
			return;
		}
		const auto process = new QProcess(this);
		process->setProcessChannelMode(QProcess::SeparateChannels);
		const auto done = QSharedPointer<bool>::create(false);
		const auto finish = [=](QString err) {
			if (*done) {
				return;
			}
			*done = true;
			const auto out = process->readAllStandardOutput();
			auto text = err;
			if (text.isEmpty() && out.trimmed().isEmpty()) {
				text = QString::fromUtf8(process->readAllStandardError())
					.trimmed()
					.right(300);
				if (text.isEmpty()) {
					text = QStringLiteral("yt-dlp не вернул данных");
				}
			}
			process->deleteLater();
			callback(out, text);
		};
		connect(process, &QProcess::finished, this, [=] {
			finish(QString());
		});
		connect(process, &QProcess::errorOccurred, this, [=](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				finish(QStringLiteral("Не удалось запустить yt-dlp"));
			}
		});
		QTimer::singleShot(timeoutMs, process, [=] {
			if (!*done) {
				process->kill();
				finish(QStringLiteral("yt-dlp: таймаут"));
			}
		});
		auto full = args;
		for (const auto &runtime : { QStringLiteral("deno"), QStringLiteral("node") }) {
			if (!QStandardPaths::findExecutable(runtime).isEmpty()) {
				full << QStringLiteral("--js-runtimes") << runtime;
			}
		}
		const auto browser = Vrg::Conf::Str("yt_cookies_browser");
		if (!browser.isEmpty()) {
			full << QStringLiteral("--cookies-from-browser") << browser;
		}
		process->start(path, full);
	});
}

void YoutubeProvider::withBinary(
		std::function<void(QString, QString)> callback) {
	const auto found = FindBinary();
	if (!found.isEmpty()) {
		if (!_updateChecked && found == LocalBinaryPath()) {
			_updateChecked = true;
			const auto info = QFileInfo(found);
			if (info.lastModified().daysTo(QDateTime::currentDateTime()) >= 7) {
				const auto updater = new QProcess(this);
				connect(updater, &QProcess::finished, updater, &QObject::deleteLater);
				updater->start(found, { QStringLiteral("-U") });
			}
		}
		callback(found, QString());
		return;
	}
	downloadBinary(std::move(callback));
}

void YoutubeProvider::downloadBinary(
		std::function<void(QString, QString)> callback) {
	_waiting.push_back(std::move(callback));
	if (_downloading) {
		return;
	}
	_downloading = true;
	auto request = QNetworkRequest(QUrl(kDownloadUrl));
	const auto reply = _network->get(request);
	connect(reply, &QNetworkReply::finished, this, [=, this] {
		reply->deleteLater();
		_downloading = false;
		QString path;
		QString error;
		if (reply->error() != QNetworkReply::NoError) {
			error = QStringLiteral("Не удалось скачать yt-dlp: ")
				+ reply->errorString();
		} else {
			const auto bytes = reply->readAll();
			QDir().mkpath(QFileInfo(LocalBinaryPath()).absolutePath());
			QFile file(LocalBinaryPath());
			if (bytes.size() < 1024 * 1024
				|| !file.open(QIODevice::WriteOnly)) {
				error = QStringLiteral("Не удалось сохранить yt-dlp");
			} else {
				file.write(bytes);
				file.close();
				file.setPermissions(file.permissions()
					| QFileDevice::ExeOwner
					| QFileDevice::ExeGroup
					| QFileDevice::ExeOther);
				path = LocalBinaryPath();
			}
		}
		auto waiting = std::move(_waiting);
		_waiting.clear();
		for (const auto &cb : waiting) {
			cb(path, error);
		}
	});
}

static QVector<TrackInfo> ParseFlatLines(const QByteArray &out, double maxDuration) {
	QVector<TrackInfo> result;
	for (const auto &line : out.split('\n')) {
		if (line.trimmed().isEmpty()) {
			continue;
		}
		const auto object = QJsonDocument::fromJson(line).object();
		const auto id = object.value(QStringLiteral("id")).toString();
		const auto title = object.value(QStringLiteral("title")).toString();
		if (id.size() != 11 || title.isEmpty()) {
			continue;
		}
		const auto duration = object.value(QStringLiteral("duration")).toDouble();
		if (duration <= 0 || duration > maxDuration) {
			continue;
		}
		auto channel = object.value(QStringLiteral("channel")).toString();
		if (channel.isEmpty()) {
			channel = object.value(QStringLiteral("uploader")).toString();
		}
		channel.remove(QStringLiteral(" - Topic"));
		TrackInfo track;
		track.id = QStringLiteral("yt:") + id;
		track.title = title;
		track.artist = channel;
		track.durationMs = qint64(duration * 1000.);
		track.artworkUrl = QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(id);
		track.streamApiUrl = QStringLiteral("yt:") + id;
		track.permalinkUrl = QStringLiteral("https://www.youtube.com/watch?v=") + id;
		result.push_back(track);
	}
	return result;
}

void YoutubeProvider::search(
		const QString &query,
		int limit,
		std::function<void(QVector<TrackInfo>)> callback) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty()) {
		callback({});
		return;
	}
	run({
		QStringLiteral("ytsearch%1:%2").arg(limit).arg(trimmed),
		QStringLiteral("--flat-playlist"),
		QStringLiteral("--dump-json"),
		QStringLiteral("--no-warnings"),
		QStringLiteral("--skip-download"),
	}, 25000, [=](QByteArray out, QString) {
		callback(ParseFlatLines(out, 20 * 60));
	});
}

void YoutubeProvider::playlist(
		const QString &url,
		std::function<void(QVector<TrackInfo>, QString)> callback) {
	run({
		url.trimmed(),
		QStringLiteral("--flat-playlist"),
		QStringLiteral("--dump-json"),
		QStringLiteral("--no-warnings"),
		QStringLiteral("--skip-download"),
	}, 120000, [=](QByteArray out, QString error) {
		const auto tracks = ParseFlatLines(out, 40 * 60);
		callback(tracks, tracks.isEmpty() ? FriendlyError(error) : QString());
	});
}

void YoutubeProvider::resolve(
		const QString &trackId,
		std::function<void(QString, QString)> callback) {
	const auto id = VideoId(trackId);
	if (id.isEmpty()) {
		callback(QString(), QStringLiteral("Не YouTube-трек"));
		return;
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	const auto cached = _urlCache.constFind(id);
	if (cached != _urlCache.constEnd()
		&& now - cached->second < 3LL * 3600 * 1000) {
		callback(cached->first, QString());
		return;
	}
	const auto attempt = std::make_shared<int>(0);
	const auto args = QStringList{
		QStringLiteral("-f"),
		QStringLiteral("bestaudio[ext=m4a]/bestaudio"),
		QStringLiteral("-g"),
		QStringLiteral("--no-playlist"),
		QStringLiteral("--no-warnings"),
		QStringLiteral("https://www.youtube.com/watch?v=") + id,
	};
	const auto step = std::make_shared<std::function<void()>>();
	*step = [=, this] {
		++*attempt;
		run(args, 40000, [=, this](QByteArray out, QString error) {
			const auto url = QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts).value(0).trimmed();
			if (url.startsWith(QStringLiteral("http"))) {
				_urlCache.insert(id, { url, QDateTime::currentMSecsSinceEpoch() });
				callback(url, QString());
				return;
			}
			const auto lower = error.toLower();
			const auto hard = lower.contains(QStringLiteral("sign in"))
				|| lower.contains(QStringLiteral("not a bot"))
				|| lower.contains(QStringLiteral("unavailable"))
				|| lower.contains(QStringLiteral("private"));
			if (*attempt < 2 && !hard) {
				(*step)();
				return;
			}
			callback(QString(), FriendlyError(error));
		}, true);
	};
	(*step)();
}

}
