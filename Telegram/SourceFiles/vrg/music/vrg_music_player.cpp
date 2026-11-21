/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/music/vrg_music_player.h"
#include "vrg/music/vrg_mpris.h"

#include "vrg/music/vrg_soundcloud_client.h"
#include "vrg/vrg_conf.h"

#include <QtCore/QBuffer>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>
#include <QtMultimedia/QAudioBuffer>
#include <QtMultimedia/QAudioBufferOutput>
#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QMediaPlayer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace Vrg::Music {
namespace {

constexpr auto kLibraryVersion = 1;
constexpr auto kRestartThresholdMs = qint64(3000);

[[nodiscard]] QJsonObject Serialize(const TrackInfo &track) {
	return QJsonObject{
		{ QStringLiteral("id"), track.id },
		{ QStringLiteral("title"), track.title },
		{ QStringLiteral("artist"), track.artist },
		{ QStringLiteral("durationMs"), double(track.durationMs) },
		{ QStringLiteral("artworkUrl"), track.artworkUrl },
		{ QStringLiteral("streamApiUrl"), track.streamApiUrl },
		{ QStringLiteral("permalinkUrl"), track.permalinkUrl },
	};
}

[[nodiscard]] TrackInfo Deserialize(const QJsonObject &object) {
	auto result = TrackInfo();
	result.id = object.value(QStringLiteral("id")).toString();
	result.title = object.value(QStringLiteral("title")).toString();
	result.artist = object.value(QStringLiteral("artist")).toString();
	result.durationMs = qint64(
		object.value(QStringLiteral("durationMs")).toDouble());
	result.artworkUrl = object.value(QStringLiteral("artworkUrl")).toString();
	result.streamApiUrl = object.value(
		QStringLiteral("streamApiUrl")).toString();
	result.permalinkUrl = object.value(
		QStringLiteral("permalinkUrl")).toString();
	return result;
}

}

class AudioRingBufferDevice final : public QIODevice {
public:
	explicit AudioRingBufferDevice(QObject *parent = nullptr)
	: QIODevice(parent)
	, _buf(kCapacity, 0) {
		open(QIODevice::ReadOnly);
	}

	bool isSequential() const override {
		return true;
	}

	qint64 bytesAvailable() const override {
		QMutexLocker lock(&_mutex);
		return qint64(_size) + QIODevice::bytesAvailable();
	}

	void push(const char *data, qint64 len) {
		if (!data || len <= 0) {
			return;
		}
		bool wasEmpty = false;
		{
			QMutexLocker lock(&_mutex);
			wasEmpty = (_size == 0);
			if (std::size_t(len) >= kCapacity) {
				data += (len - qint64(kCapacity));
				len = qint64(kCapacity);
				_readPos = 0;
				_writePos = 0;
				_size = 0;
			} else if (_size + std::size_t(len) > kCapacity) {
				const auto drop = (_size + std::size_t(len)) - kCapacity;
				_readPos = (_readPos + drop) % kCapacity;
				_size -= drop;
			}
			const auto first = std::min(std::size_t(len), kCapacity - _writePos);
			std::memcpy(_buf.data() + _writePos, data, first);
			if (std::size_t(len) > first) {
				std::memcpy(_buf.data(), data + first, std::size_t(len) - first);
			}
			_writePos = (_writePos + std::size_t(len)) % kCapacity;
			_size += std::size_t(len);
		}
		if (wasEmpty) {
			Q_EMIT readyRead();
		}
	}

	void clear() {
		QMutexLocker lock(&_mutex);
		_readPos = 0;
		_writePos = 0;
		_size = 0;
	}

protected:
	qint64 readData(char *data, qint64 maxlen) override {
		if (!data || maxlen <= 0) {
			return 0;
		}
		QMutexLocker lock(&_mutex);
		const auto toRead = std::min(std::size_t(maxlen), _size);
		if (toRead == 0) {
			return 0;
		}
		const auto first = std::min(toRead, kCapacity - _readPos);
		std::memcpy(data, _buf.data() + _readPos, first);
		if (toRead > first) {
			std::memcpy(data + first, _buf.data(), toRead - first);
		}
		_readPos = (_readPos + toRead) % kCapacity;
		_size -= toRead;
		return qint64(toRead);
	}

	qint64 writeData(const char *, qint64) override {
		return -1;
	}

private:
	static constexpr std::size_t kCapacity = 2 * 1024 * 1024;
	mutable QMutex _mutex;
	std::vector<char> _buf;
	std::size_t _readPos = 0;
	std::size_t _writePos = 0;
	std::size_t _size = 0;
};

namespace {

void MusicLog(const QString &text) {
	static const auto enabled = Vrg::Conf::Bool("music_log", false);
	if (!enabled) {
		return;
	}
	static auto clock = QElapsedTimer();
	if (!clock.isValid()) {
		clock.start();
	}
	auto file = QFile(QDir::homePath() + QStringLiteral("/.config/vrg/music.log"));
	if (file.open(QIODevice::Append | QIODevice::Text)) {
		file.write(QStringLiteral("%1 ms  %2\n").arg(clock.elapsed(), 7).arg(text).toUtf8());
	}
}

}

Player::Player(SoundCloudClient *client, QObject *parent)
: QObject(parent)
, _client(client)
, _media(new QMediaPlayer(this))
, _output(new QAudioOutput(this))
, _downloader(new QNetworkAccessManager(this)) {
	Q_ASSERT(_client != nullptr);

	_media->setAudioOutput(_output);
	_output->setVolume(0.f);
	_dsp.setVolumeGain(_volume);

	QAudioFormat fmt;
	fmt.setSampleRate(44100);
	fmt.setChannelCount(2);
	fmt.setSampleFormat(QAudioFormat::Float);
	_bufferOutput = new QAudioBufferOutput(fmt, this);
	_media->setAudioBufferOutput(_bufferOutput);

	connect(
		_bufferOutput,
		&QAudioBufferOutput::audioBufferReceived,
		this,
		[this](const QAudioBuffer &buffer) {
			if (!buffer.isValid() || buffer.frameCount() <= 0) {
				return;
			}
			const auto format = buffer.format();
			if (format.sampleFormat() != QAudioFormat::Float
				|| format.channelCount() != 2) {
				return;
			}
			ensureAudioSink(format.sampleRate(), format.channelCount());
			if (!_ringDevice) {
				return;
			}
			const auto sampleCount = buffer.sampleCount();
			if (sampleCount <= 0 || buffer.frameCount() <= 0) {
				return;
			}
			std::vector<float> samples(sampleCount);
			std::memcpy(
				samples.data(),
				buffer.constData<float>(),
				size_t(sampleCount) * sizeof(float));
			_dsp.processStereoFloat(samples.data(), buffer.frameCount());
			_ringDevice->push(
				reinterpret_cast<const char*>(samples.data()),
				qint64(sampleCount * sizeof(float)));
		});

	connect(
		_client,
		&SoundCloudClient::streamResolved,
		this,
		&Player::applyStream);
	connect(
		_client,
		&SoundCloudClient::streamFailed,
		this,
		&Player::handleStreamFailure);

	connect(_media, &QMediaPlayer::positionChanged, this, [=, this](
			qint64 position) {
		MusicLog(QStringLiteral("media position %1, pending %2, muted %3").arg(position).arg(_pendingSeekMs).arg(_resumeMuted));
		Q_EMIT positionChanged(this->position());
	});
	connect(_media, &QMediaPlayer::seekableChanged, this, [=, this] {
		trySeekPending();
	});
	connect(_media, &QMediaPlayer::durationChanged, this, [=, this] {
		trySeekPending();
		Q_EMIT durationChanged(duration());
	});
	connect(_media, &QMediaPlayer::playbackStateChanged, this, [=, this](
			QMediaPlayer::PlaybackState state) {
		switch (state) {
		case QMediaPlayer::PlayingState:
			MusicLog(QStringLiteral("playing after %1 ms").arg(_loadClock.isValid() ? _loadClock.elapsed() : -1));
			_failuresInRow = 0;
			setState(PlaybackState::Playing);
			if (_positionTimer && !_positionTimer->isActive()) {
				_positionTimer->start();
			}
			trySeekPending();
			if (const auto cur = currentTrack();
				cur.valid()
				&& !_cacheBuffer
				&& ramBytes(cur.id).isEmpty()
				&& !_ramFetching.contains(cur.id)) {
				if (const auto url = freshStreamUrl(cur.id); !url.isEmpty()) {
					fetchToRam(cur.id, url);
				}
			}
			prefetchNext();
			break;
		case QMediaPlayer::PausedState:
			if (_positionTimer) {
				_positionTimer->stop();
			}
			setState(PlaybackState::Paused);
			break;
		case QMediaPlayer::StoppedState:
			if (_positionTimer) {
				_positionTimer->stop();
			}
			if (_state != PlaybackState::Loading) {
				setState(PlaybackState::Stopped);
			}
			break;
		}
	});
	connect(_media, &QMediaPlayer::mediaStatusChanged, this, [=, this](
			QMediaPlayer::MediaStatus status) {
		if (status == QMediaPlayer::LoadedMedia
			|| status == QMediaPlayer::BufferedMedia) {
			MusicLog(QStringLiteral("media status %1 after %2 ms, media duration %3 ms, api duration %4 ms")
				.arg(int(status))
				.arg(_loadClock.isValid() ? _loadClock.elapsed() : -1)
				.arg(_media->duration())
				.arg(currentTrack().durationMs));
			trySeekPending();
		} else if (status == QMediaPlayer::EndOfMedia) {
			handleEndOfMedia();
		} else if (status == QMediaPlayer::InvalidMedia) {
			if (_silentRetry) {
				return;
			}
			if (const auto track = currentTrack(); track.valid()) {
				handleStreamFailure(track.id);
			}
		}
	});
	connect(_media, &QMediaPlayer::errorOccurred, this, [=, this](
			QMediaPlayer::Error error,
			const QString &message) {
		if (error == QMediaPlayer::NoError) {
			return;
		}
		if (const auto track = currentTrack(); track.valid()) {
			if (_cacheBuffer || !freshStreamUrl(track.id).isEmpty()) {
				_media->stop();
				_media->setSource(QUrl());
				if (_cacheBuffer) {
					_cacheBuffer->close();
					delete _cacheBuffer;
					_cacheBuffer = nullptr;
				}
				_streamUrls.remove(track.id);
				saveStreamUrls();
				_silentRetry = true;
				setState(PlaybackState::Loading);
				_client->resolveStreamUrl(track);
				return;
			}
		}
		Q_EMIT errorOccurred(message.isEmpty()
			? QStringLiteral("Ошибка воспроизведения")
			: message);
		if (const auto track = currentTrack(); track.valid()) {
			handleStreamFailure(track.id);
		}
	});

	_positionTimer = new QTimer(this);
	_positionTimer->setInterval(50);
	connect(_positionTimer, &QTimer::timeout, this, [this] {
		if (_state == PlaybackState::Playing) {
			Q_EMIT positionChanged(position());
		}
	});

	_stateSaveTimer = new QTimer(this);
	_stateSaveTimer->setInterval(5000);
	connect(_stateSaveTimer, &QTimer::timeout, this, [this] {
		if (_state == PlaybackState::Playing) {
			saveState();
		}
	});
	_stateSaveTimer->start();

	_saveDebounceTimer = new QTimer(this);
	_saveDebounceTimer->setInterval(300);
	_saveDebounceTimer->setSingleShot(true);
	connect(_saveDebounceTimer, &QTimer::timeout, this, &Player::saveState);

	if (qApp) {
		connect(qApp, &QCoreApplication::aboutToQuit, this, &Player::saveState);
	}

	loadLibrary();
	loadState();
	loadStreamUrls();
	updateAudioRoute();
	if (const auto track = currentTrack();
		track.valid()
			&& !isTrackCached(track.id)
			&& freshStreamUrl(track.id).isEmpty()) {
		_prefetching.insert(track.id);
		_client->resolveStreamUrl(track);
	}
}

Player::~Player() {
	saveState();
	_media->stop();
	QDir(QDir::tempPath() + QStringLiteral("/vrg-send")).removeRecursively();
}

void Player::setQueue(const QVector<TrackInfo> &tracks, int startIndex) {
	_queue = tracks;
	_index = (startIndex >= 0 && startIndex < _queue.size())
		? startIndex
		: (_queue.isEmpty() ? -1 : 0);
	_failuresInRow = 0;
	rebuildOrder();
	Q_EMIT queueChanged();
	if (_index >= 0) {
		this->startIndex(_index);
	} else {
		stop();
	}
}

void Player::adoptQueueKeepPlaying(const QVector<TrackInfo> &tracks) {
	if (_index < 0 || _index >= _queue.size()) {
		return;
	}
	const auto current = _queue[_index];
	const auto position = int(tracks.indexOf(current));
	if (position < 0) {
		return;
	}
	_queue = tracks;
	_index = position;
	rebuildOrder();
	Q_EMIT queueChanged();
	scheduleSaveState();
}

void Player::playTrack(const TrackInfo &track) {
	if (!track.valid()) {
		return;
	}
	auto index = int(_queue.indexOf(track));
	if (index < 0) {
		_queue.push_back(track);
		index = int(_queue.size()) - 1;
		if (_shuffle) {
			const auto position = orderPosition();
			_order.insert(position + 1, index);
		} else {
			_order.push_back(index);
		}
		Q_EMIT queueChanged();
	}
	_failuresInRow = 0;
	startIndex(index);
}

void Player::enqueueNext(const TrackInfo &track) {
	if (!track.valid()) {
		return;
	}
	if (_queue.isEmpty() || _index < 0) {
		setQueue({ track }, 0);
		return;
	}
	const auto index = int(_queue.size());
	_queue.push_back(track);
	const auto at = std::min(
		orderPosition() + 1 + _manualAhead,
		int(_order.size()));
	_order.insert(at, index);
	++_manualAhead;
	Q_EMIT queueChanged();
	scheduleSaveState();
}

void Player::enqueueLast(const TrackInfo &track) {
	if (!track.valid()) {
		return;
	}
	if (_queue.isEmpty() || _index < 0) {
		setQueue({ track }, 0);
		return;
	}
	_order.push_back(int(_queue.size()));
	_queue.push_back(track);
	Q_EMIT queueChanged();
	scheduleSaveState();
}

QVector<TrackInfo> Player::upcoming() const {
	auto result = QVector<TrackInfo>();
	if (_index < 0) {
		return result;
	}
	for (auto i = orderPosition() + 1; i < _order.size(); ++i) {
		result.push_back(_queue[_order[i]]);
	}
	return result;
}

int Player::manualAhead() const {
	return _manualAhead;
}

void Player::removeQueueIndex(int queueIndex) {
	if (queueIndex < 0 || queueIndex >= _queue.size()) {
		return;
	}
	_order.removeAll(queueIndex);
	for (auto &value : _order) {
		if (value > queueIndex) {
			--value;
		}
	}
	_queue.remove(queueIndex);
	if (_index > queueIndex) {
		--_index;
	}
}

void Player::removeUpcoming(int index) {
	const auto at = orderPosition() + 1 + index;
	if (index < 0 || at >= _order.size()) {
		return;
	}
	if (index < _manualAhead) {
		--_manualAhead;
	}
	removeQueueIndex(_order[at]);
	Q_EMIT queueChanged();
	scheduleSaveState();
}

void Player::moveUpcoming(int from, int to) {
	const auto base = orderPosition() + 1;
	const auto count = int(_order.size()) - base;
	if (from < 0 || to < 0 || from >= count || to >= count || from == to) {
		return;
	}
	_order.move(base + from, base + to);
	_manualAhead = std::min(_manualAhead, count);
	Q_EMIT queueChanged();
	scheduleSaveState();
}

void Player::clearUpcoming() {
	if (_index < 0) {
		return;
	}
	auto indices = QVector<int>();
	for (auto i = orderPosition() + 1; i < _order.size(); ++i) {
		indices.push_back(_order[i]);
	}
	if (indices.isEmpty()) {
		return;
	}
	std::sort(indices.begin(), indices.end(), std::greater<int>());
	for (const auto queueIndex : indices) {
		removeQueueIndex(queueIndex);
	}
	_manualAhead = 0;
	Q_EMIT queueChanged();
	scheduleSaveState();
}

void Player::playUpcoming(int index) {
	const auto at = orderPosition() + 1 + index;
	if (index < 0 || at >= _order.size()) {
		return;
	}
	_manualAhead = (index < _manualAhead) ? (_manualAhead - index - 1) : 0;
	_failuresInRow = 0;
	startIndex(_order[at]);
}

void Player::togglePlayPause() {
	switch (_state) {
	case PlaybackState::Playing:
		_media->pause();
		resetAudioSink();
		saveState();
		break;
	case PlaybackState::Paused:
		_media->play();
		break;
	case PlaybackState::Stopped:
		if (_index >= 0 && _index < _queue.size()) {
			if (_media->mediaStatus() == QMediaPlayer::NoMedia
				|| _media->mediaStatus() == QMediaPlayer::InvalidMedia
				|| _media->mediaStatus() == QMediaPlayer::EndOfMedia) {
				startIndex(_index, _pendingSeekMs);
			} else {
				_media->play();
			}
		} else if (!_order.isEmpty()) {
			startIndex(_order.front());
		}
		break;
	case PlaybackState::Loading:
		break;
	}
}

void Player::setVoiceActive(bool active) {
	if (active) {
		if (_state == PlaybackState::Playing) {
			_pausedByVoice = true;
			togglePlayPause();
		}
	} else if (_pausedByVoice) {
		_pausedByVoice = false;
		if (_state == PlaybackState::Paused) {
			togglePlayPause();
		}
	}
}

void Player::stop() {
	_media->stop();
	resetAudioSink();
	setState(PlaybackState::Stopped);
}

void Player::next() {
	_failuresInRow = 0;
	advance(true);
}

void Player::prev() {
	if (_queue.isEmpty()) {
		return;
	}
	if (_state != PlaybackState::Loading
		&& _media->position() > kRestartThresholdMs) {
		seek(0);
		return;
	}
	const auto position = orderPosition();
	if (position > 0) {
		_failuresInRow = 0;
		_manualAhead = 0;
		startIndex(_order[position - 1]);
	} else if (_repeat == RepeatMode::All && _order.size() > 1) {
		_failuresInRow = 0;
		startIndex(_order.back());
	} else {
		seek(0);
	}
}

void Player::seek(qint64 positionMs) {
	if (_state == PlaybackState::Loading) {
		return;
	}
	const auto clamped = std::max(positionMs, qint64(0));
	if (_state == PlaybackState::Stopped
		&& _media->mediaStatus() == QMediaPlayer::NoMedia) {
		_pendingSeekMs = clamped;
		Q_EMIT positionChanged(_pendingSeekMs);
		scheduleSaveState();
		return;
	}
	if (!_cacheBuffer && _index >= 0 && _index < _queue.size()
		&& !ramBytes(_queue[_index].id).isEmpty()) {
		const auto wasPaused = (_state == PlaybackState::Paused);
		startIndex(_index, clamped);
		if (wasPaused) {
			_playAfterSeek = false;
		}
		return;
	}
	_pendingSeekMs = 0;
	setResumeMuted(false);
	resetAudioSink();
	_media->setPosition(clamped);
	scheduleSaveState();
}

void Player::setVolume(float volume) {
	volume = std::clamp(volume, 0.f, 1.f);
	if (qFuzzyCompare(volume + 1.f, _volume + 1.f)) {
		return;
	}
	_volume = volume;
	_dsp.setVolumeGain(_resumeMuted ? 0.f : _volume);
	if (_audioRoute == 0) {
		_output->setVolume(_resumeMuted ? 0.f : _volume);
	}
	Q_EMIT volumeChanged(_volume);
	scheduleSaveState();
}

void Player::setBoost(float boost) {
	_dsp.setBoostGain(boost);
	updateAudioRoute();
	Q_EMIT eqChanged();
	scheduleSaveState();
}

float Player::boost() const {
	return _dsp.boostGain();
}

void Player::setShuffle(bool enabled) {
	if (_shuffle == enabled) {
		return;
	}
	_shuffle = enabled;
	rebuildOrder();
	Q_EMIT shuffleChanged(_shuffle);
	scheduleSaveState();
}

void Player::setRepeatMode(RepeatMode mode) {
	if (_repeat == mode) {
		return;
	}
	_repeat = mode;
	Q_EMIT repeatModeChanged(_repeat);
	scheduleSaveState();
}

const QVector<TrackInfo> &Player::queue() const {
	return _queue;
}

int Player::currentIndex() const {
	return _index;
}

TrackInfo Player::currentTrack() const {
	return (_index >= 0 && _index < _queue.size())
		? _queue[_index]
		: TrackInfo();
}

PlaybackState Player::state() const {
	return _state;
}

qint64 Player::position() const {
	if (_state == PlaybackState::Loading) {
		return (_pendingSeekMs > 0) ? _pendingSeekMs : 0;
	}
	if (_pendingSeekMs > 0
		&& (_state == PlaybackState::Stopped
			|| _media->position() + 1500 < _pendingSeekMs)) {
		return _pendingSeekMs;
	}
	return _media->position();
}

void Player::trySeekPending() {
	if (_pendingSeekMs <= 0
		|| (_media->source().isEmpty() && !_media->sourceDevice())
		|| !_media->isSeekable()
		|| _media->duration() <= 0) {
		return;
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	if (now - _lastSeekAttemptMs < 250) {
		return;
	}
	if (_seekAttempts >= 12) {
		if (_playAfterSeek) {
			_playAfterSeek = false;
			_media->play();
		}
		finishResume();
		return;
	}
	_lastSeekAttemptMs = now;
	++_seekAttempts;
	const auto limit = std::max<qint64>(_media->duration() - 500, 0);
	MusicLog(QStringLiteral("setPosition %1 (attempt %2)").arg(std::min(_pendingSeekMs, limit)).arg(_seekAttempts));
	_media->setPosition(std::min(_pendingSeekMs, limit));
	if (_playAfterSeek) {
		_playAfterSeek = false;
		MusicLog(QStringLiteral("play() after seek"));
		_media->play();
	}
	const auto serial = _seekSerial;
	QTimer::singleShot(350, this, [this, serial] {
		verifyResume(serial);
	});
}

qint64 Player::duration() const {
	const auto media = (_state == PlaybackState::Loading)
		? qint64(0)
		: _media->duration();
	const auto api = currentTrack().durationMs;
	if (media <= 0) {
		return api;
	}
	if (!currentTrack().isYoutube() && api > media && api - media <= 10000) {
		return api;
	}
	return media;
}

float Player::volume() const {
	return _volume;
}

bool Player::shuffle() const {
	return _shuffle;
}

RepeatMode Player::repeatMode() const {
	return _repeat;
}

void Player::startIndex(int index, qint64 resumeMs) {
	if (index < 0 || index >= _queue.size()) {
		return;
	}
	_pendingSeekMs = std::max<qint64>(resumeMs, 0);
	_playAfterSeek = (_pendingSeekMs > 0);
	setResumeMuted(_pendingSeekMs > 0);
	++_seekSerial;
	_seekAttempts = 0;
	_lastSeekAttemptMs = 0;
	_index = index;
	_failureHandled = false;
	_silentRetry = false;
	const auto track = _queue[_index];
	setState(PlaybackState::Loading);
	_media->stop();
	_media->setSource(QUrl());
	if (_cacheBuffer) {
		_cacheBuffer->close();
		delete _cacheBuffer;
		_cacheBuffer = nullptr;
	}
	_loadClock.start();
	MusicLog(QStringLiteral("load start: %1 (api duration %2 ms)").arg(track.id).arg(track.durationMs));
	Q_EMIT currentTrackChanged(track);
	Q_EMIT positionChanged(_pendingSeekMs);
	Q_EMIT durationChanged(track.durationMs);
	auto cachedBytes = ramBytes(track.id);
	if (cachedBytes.isEmpty() && isTrackCached(track.id)) {
		cachedBytes = ReadCachedBytes(track.id);
	}
	if (!cachedBytes.isEmpty()) {
		const auto &bytes = cachedBytes;
		{
			_cacheBuffer = new QBuffer(this);
			_cacheBuffer->setData(bytes);
			_cacheBuffer->open(QIODevice::ReadOnly);
			_media->setSourceDevice(
				_cacheBuffer,
				QUrl(QStringLiteral("track.mp3")));
			startPlayback();
			return;
		}
	}
	if (const auto url = freshStreamUrl(track.id); !url.isEmpty()) {
		_media->setSource(QUrl(url));
		startPlayback();
		return;
	}
	_client->resolveStreamUrl(track);
}

void Player::setResumeMuted(bool muted) {
	MusicLog(QStringLiteral("mute %1").arg(muted));
	_resumeMuted = muted;
	const auto volume = muted ? 0.f : _volume;
	_dsp.setVolumeGain(volume);
	if (_audioRoute == 0) {
		_output->setVolume(volume);
	}
}

void Player::finishResume() {
	_pendingSeekMs = 0;
	setResumeMuted(false);
}

void Player::verifyResume(int serial) {
	if (serial != _seekSerial || _pendingSeekMs <= 0) {
		return;
	}
	MusicLog(QStringLiteral("verify: media position %1, target %2").arg(_media->position()).arg(_pendingSeekMs));
	if (_media->position() + 800 >= _pendingSeekMs) {
		finishResume();
		Q_EMIT positionChanged(position());
		return;
	}
	_lastSeekAttemptMs = 0;
	trySeekPending();
}

void Player::startPlayback() {
	MusicLog(QStringLiteral("startPlayback pending %1 playAfterSeek %2").arg(_pendingSeekMs).arg(_playAfterSeek));
	if (_pendingSeekMs > 0) {
		const auto guard = _seekSerial;
		QTimer::singleShot(6000, this, [this, guard] {
			if (guard == _seekSerial && _pendingSeekMs > 0) {
				if (_playAfterSeek) {
					_playAfterSeek = false;
					_media->play();
				}
				finishResume();
			}
		});
	}
	if (!_playAfterSeek) {
		_media->play();
		return;
	}
	const auto serial = _seekSerial;
	QTimer::singleShot(2500, this, [this, serial] {
		if (serial == _seekSerial && _playAfterSeek) {
			_playAfterSeek = false;
			_media->play();
		}
	});
}

QString Player::freshStreamUrl(const QString &trackId) const {
	const auto i = _streamUrls.constFind(trackId);
	if (i == _streamUrls.constEnd()
		|| QDateTime::currentMSecsSinceEpoch() - i->second
			>= 8 * 60 * 1000) {
		return QString();
	}
	return i->first;
}

void Player::prefetchNext() {
	if (_queue.isEmpty()) {
		return;
	}
	const auto position = orderPosition();
	const auto next = (position + 1 < _order.size())
		? _order[position + 1]
		: (_repeat == RepeatMode::All && !_order.isEmpty())
		? _order.front()
		: -1;
	if (next < 0 || next >= _queue.size() || next == _index) {
		return;
	}
	const auto &track = _queue[next];
	if (!track.valid()
		|| isTrackCached(track.id)
		|| !ramBytes(track.id).isEmpty()
		|| _ramFetching.contains(track.id)
		|| _prefetching.contains(track.id)) {
		return;
	}
	if (const auto url = freshStreamUrl(track.id); !url.isEmpty()) {
		fetchToRam(track.id, url);
		return;
	}
	_prefetching.insert(track.id);
	_ramWanted.insert(track.id);
	_client->resolveStreamUrl(track);
}

void Player::advance(bool userAction) {
	if (_queue.isEmpty()) {
		return;
	}
	if (!userAction && _repeat == RepeatMode::One && _index >= 0) {
		_media->setPosition(0);
		_media->play();
		return;
	}
	const auto position = orderPosition();
	if (position + 1 < _order.size()) {
		if (_manualAhead > 0) {
			--_manualAhead;
		}
		startIndex(_order[position + 1]);
		return;
	}
	if (_repeat == RepeatMode::All || userAction) {
		if (_shuffle) {
			rebuildOrder();
		}
		startIndex(_order.front());
	} else if (Vrg::Conf::Bool("player_radio", false) && currentTrack().valid()) {
		const auto seed = currentTrack();
		setState(PlaybackState::Loading);
		_client->fetchRelatedTracks(seed.id, [this, seedId = seed.id](QVector<TrackInfo> related) {
			if (currentTrack().id != seedId) {
				return;
			}
			QSet<QString> seen;
			for (const auto &t : _queue) {
				seen.insert(t.id);
			}
			int firstAdded = -1;
			for (const auto &t : related) {
				if (!seen.contains(t.id)) {
					seen.insert(t.id);
					if (firstAdded < 0) {
						firstAdded = _queue.size();
					}
					_order.push_back(_queue.size());
					_queue.push_back(t);
				}
			}
			if (firstAdded >= 0) {
				startIndex(firstAdded);
			} else {
				stop();
			}
		});
	} else {
		stop();
	}
}

void Player::rebuildOrder() {
	_manualAhead = 0;
	_order.resize(_queue.size());
	std::iota(_order.begin(), _order.end(), 0);
	if (!_shuffle || _order.size() < 2) {
		return;
	}
	std::shuffle(_order.begin(), _order.end(), *QRandomGenerator::global());
	if (_index >= 0) {
		const auto i = std::find(_order.begin(), _order.end(), _index);
		std::iter_swap(_order.begin(), i);
	}
	for (int i = 1; i < int(_order.size()); ++i) {
		const auto &prevArtist = _queue[_order[i - 1]].artist;
		const auto &currArtist = _queue[_order[i]].artist;
		if (!prevArtist.isEmpty() && currArtist == prevArtist) {
			for (int j = i + 1; j < int(_order.size()); ++j) {
				if (_queue[_order[j]].artist != prevArtist) {
					std::swap(_order[i], _order[j]);
					break;
				}
			}
		}
	}
}

int Player::orderPosition() const {
	const auto position = int(_order.indexOf(_index));
	return std::max(position, 0);
}

void Player::setState(PlaybackState state) {
	if (_state == state) {
		return;
	}
	_state = state;
	Q_EMIT playbackStateChanged(_state);
}

void Player::applyStream(const QString &trackId, const QString &url) {
	if (currentTrack().id == trackId) {
		_silentRetry = false;
	}
	MusicLog(QStringLiteral("stream url resolved after %1 ms").arg(_loadClock.isValid() ? _loadClock.elapsed() : -1));
	if (_pendingDownloadResolves.remove(trackId)) {
		startDownloadWithUrl(trackId, url);
	}
	_prefetching.remove(trackId);
	_streamUrls.insert(
		trackId,
		{ url, QDateTime::currentMSecsSinceEpoch() });
	saveStreamUrls();
	if (_ramWanted.remove(trackId)) {
		fetchToRam(trackId, url);
	}
	if (_state != PlaybackState::Loading || currentTrack().id != trackId) {
		return;
	}
	_media->setSource(QUrl(url));
	startPlayback();
}

void Player::handleEndOfMedia() {
	advance(false);
}

void Player::handleStreamFailure(const QString &trackId) {
	if (_prefetching.remove(trackId)
		&& (_state != PlaybackState::Loading
			|| currentTrack().id != trackId)) {
		return;
	}
	if (_pendingDownloadResolves.remove(trackId)) {
		_downloading.remove(trackId);
		Q_EMIT trackCached(trackId);
	}
	if (currentTrack().id != trackId || _failureHandled) {
		return;
	}
	if (currentTrack().isYoutube() && _client->youtubeBlocked()) {
		_failuresInRow = 0;
		stop();
		return;
	}
	_failureHandled = true;
	_failuresInRow = 0;
	const auto failed = currentTrack();
	stop();
	Q_EMIT errorOccurred(QStringLiteral("Не удалось загрузить трек «%1». Нажми на него ещё раз или выбери другой.")
		.arg(failed.title));
}

const QVector<TrackInfo> &Player::library() const {
	return _library;
}

bool Player::isInLibrary(const QString &trackId) const {
	return std::any_of(_library.begin(), _library.end(), [&](
			const TrackInfo &track) {
		return track.id == trackId;
	});
}

void Player::addToLibrary(const TrackInfo &track) {
	if (!track.valid() || isInLibrary(track.id)) {
		return;
	}
	_library.prepend(track);
	saveLibrary();
	Q_EMIT libraryChanged();
}

void Player::removeFromLibrary(const QString &trackId) {
	const auto removed = _library.removeIf([&](const TrackInfo &track) {
		return track.id == trackId;
	});
	if (!removed) {
		return;
	}
	saveLibrary();
	Q_EMIT libraryChanged();
}

void Player::toggleLibrary(const TrackInfo &track) {
	if (isInLibrary(track.id)) {
		removeFromLibrary(track.id);
	} else {
		addToLibrary(track);
	}
}

QString Player::LibraryPath() {
	return QDir::homePath() + QStringLiteral("/.config/vrg/music_library.json");
}

void Player::loadLibrary() {
	_library.clear();
	_playlists.clear();
	auto file = QFile(LibraryPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	const auto root = document.object();
	const auto tracks = document.isArray()
		? document.array()
		: root.value(QStringLiteral("tracks")).toArray();
	for (const auto &entry : tracks) {
		const auto track = Deserialize(entry.toObject());
		if (track.valid() && !isInLibrary(track.id)) {
			_library.push_back(track);
		}
	}
	const auto playlists = root.value(QStringLiteral("playlists")).toArray();
	for (const auto &entry : playlists) {
		const auto obj = entry.toObject();
		auto pl = Playlist();
		pl.id = obj.value(QStringLiteral("id")).toString();
		pl.name = obj.value(QStringLiteral("name")).toString();
		if (pl.id.isEmpty() || pl.name.isEmpty()) {
			continue;
		}
		const auto plTracks = obj.value(QStringLiteral("tracks")).toArray();
		for (const auto &tEntry : plTracks) {
			const auto t = Deserialize(tEntry.toObject());
			if (t.valid()) {
				pl.tracks.push_back(t);
			}
		}
		_playlists.push_back(std::move(pl));
	}
}

void Player::saveLibrary() const {
	const auto path = LibraryPath();
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return;
	}
	auto tracks = QJsonArray();
	for (const auto &track : _library) {
		tracks.push_back(Serialize(track));
	}
	auto playlists = QJsonArray();
	for (const auto &pl : _playlists) {
		auto plTracks = QJsonArray();
		for (const auto &t : pl.tracks) {
			plTracks.push_back(Serialize(t));
		}
		playlists.push_back(QJsonObject{
			{ QStringLiteral("id"), pl.id },
			{ QStringLiteral("name"), pl.name },
			{ QStringLiteral("tracks"), plTracks },
		});
	}
	const auto document = QJsonDocument(QJsonObject{
		{ QStringLiteral("version"), kLibraryVersion },
		{ QStringLiteral("tracks"), tracks },
		{ QStringLiteral("playlists"), playlists },
	});
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(document.toJson(QJsonDocument::Indented));
	file.commit();
}

const QVector<Playlist> &Player::playlists() const {
	return _playlists;
}

const Playlist *Player::findPlaylist(const QString &playlistId) const {
	for (const auto &pl : _playlists) {
		if (pl.id == playlistId) {
			return &pl;
		}
	}
	return nullptr;
}

QString Player::createPlaylist(const QString &name) {
	const auto trimmed = name.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	auto pl = Playlist();
	pl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	pl.name = trimmed;
	_playlists.push_back(pl);
	saveLibrary();
	Q_EMIT playlistsChanged();
	return pl.id;
}

void Player::deletePlaylist(const QString &playlistId) {
	const auto removed = _playlists.removeIf([&](const Playlist &pl) {
		return pl.id == playlistId;
	});
	if (removed) {
		saveLibrary();
		Q_EMIT playlistsChanged();
	}
}

void Player::renamePlaylist(const QString &playlistId, const QString &name) {
	const auto trimmed = name.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	for (auto &pl : _playlists) {
		if (pl.id == playlistId) {
			pl.name = trimmed;
			saveLibrary();
			Q_EMIT playlistsChanged();
			return;
		}
	}
}

void Player::addTrackToPlaylist(
		const QString &playlistId,
		const TrackInfo &track) {
	if (!track.valid()) {
		return;
	}
	for (auto &pl : _playlists) {
		if (pl.id == playlistId) {
			if (!pl.tracks.contains(track)) {
				pl.tracks.push_front(track);
				saveLibrary();
				Q_EMIT playlistsChanged();
			}
			return;
		}
	}
}

void Player::addTrackToPlaylistAtEnd(
		const QString &playlistId,
		const TrackInfo &track) {
	if (!track.valid()) {
		return;
	}
	for (auto &pl : _playlists) {
		if (pl.id == playlistId) {
			if (!pl.tracks.contains(track)) {
				pl.tracks.push_back(track);
				saveLibrary();
				Q_EMIT playlistsChanged();
			}
			return;
		}
	}
}

void Player::moveTrackInPlaylist(
		const QString &playlistId,
		const QString &trackId,
		int delta) {
	for (auto &pl : _playlists) {
		if (pl.id != playlistId) {
			continue;
		}
		auto index = -1;
		for (auto i = 0; i != pl.tracks.size(); ++i) {
			if (pl.tracks[i].id == trackId) {
				index = i;
				break;
			}
		}
		const auto target = index + delta;
		if (index < 0 || target < 0 || target >= pl.tracks.size()) {
			return;
		}
		pl.tracks.swapItemsAt(index, target);
		saveLibrary();
		Q_EMIT playlistsChanged();
		return;
	}
}

void Player::reorderTrack(
		const QString &playlistId,
		const QString &movedId,
		const QString &anchorId,
		bool after) {
	if (movedId == anchorId) {
		return;
	}
	auto *list = static_cast<QVector<TrackInfo>*>(nullptr);
	if (playlistId.isEmpty()) {
		list = &_library;
	} else {
		for (auto &pl : _playlists) {
			if (pl.id == playlistId) {
				list = &pl.tracks;
				break;
			}
		}
	}
	if (!list) {
		return;
	}
	auto from = -1;
	for (auto i = 0; i != list->size(); ++i) {
		if ((*list)[i].id == movedId) {
			from = i;
			break;
		}
	}
	if (from < 0) {
		return;
	}
	const auto moved = (*list)[from];
	list->remove(from);
	auto to = -1;
	for (auto i = 0; i != list->size(); ++i) {
		if ((*list)[i].id == anchorId) {
			to = i;
			break;
		}
	}
	if (to < 0) {
		list->insert(from, moved);
		return;
	}
	list->insert(to + (after ? 1 : 0), moved);
	saveLibrary();
	if (playlistId.isEmpty()) {
		Q_EMIT libraryChanged();
	} else {
		Q_EMIT playlistsChanged();
	}
}

void Player::removeTrackFromPlaylist(
		const QString &playlistId,
		const QString &trackId) {
	for (auto &pl : _playlists) {
		if (pl.id == playlistId) {
			const auto removed = pl.tracks.removeIf([&](const TrackInfo &t) {
				return t.id == trackId;
			});
			if (removed) {
				saveLibrary();
				Q_EMIT playlistsChanged();
			}
			return;
		}
	}
}

namespace {

void ScrambleMusicBytes(QByteArray &bytes) {
	static constexpr char kKey[] = "VRGMusicVaultKey_2026_Obfuscated!";
	constexpr int kKeyLen = int(sizeof(kKey) - 1);
	auto *data = bytes.data();
	const int n = int(bytes.size());
	for (int i = 0; i < n; ++i) {
		data[i] ^= kKey[i % kKeyLen] ^ char((i * 13 + 7) & 0xff);
	}
}

[[nodiscard]] QString LegacyMp3Path(const QString &trackId) {
	return Player::CacheDir() + QLatin1Char('/') + trackId + QStringLiteral(".mp3");
}

}

QString Player::CacheDir() {
	return QDir::homePath() + QStringLiteral("/.local/share/vrg/music_cache");
}

QString Player::CachedTrackPath(const QString &trackId) {
	const auto hash = QString::fromLatin1(
		QCryptographicHash::hash(trackId.toUtf8(), QCryptographicHash::Sha256)
			.toHex()
			.left(24));
	return CacheDir() + QLatin1Char('/') + hash + QStringLiteral(".vrg");
}

bool Player::WriteCachedBytes(const QString &trackId, const QByteArray &bytes) {
	if (trackId.isEmpty() || bytes.isEmpty()) {
		return false;
	}
	QDir().mkpath(CacheDir());
	auto payload = bytes;
	ScrambleMusicBytes(payload);
	auto file = QSaveFile(CachedTrackPath(trackId));
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.write("VRGM", 4);
	file.write(payload);
	return file.commit();
}

QByteArray Player::ReadCachedBytes(const QString &trackId) {
	if (trackId.isEmpty()) {
		return {};
	}
	QFile vrg(CachedTrackPath(trackId));
	if (vrg.open(QIODevice::ReadOnly)) {
		const auto raw = vrg.readAll();
		if (raw.size() > 4 && raw.startsWith("VRGM")) {
			auto payload = raw.mid(4);
			ScrambleMusicBytes(payload);
			return payload;
		}
	}
	const auto legacyPath = LegacyMp3Path(trackId);
	QFile legacy(legacyPath);
	if (legacy.open(QIODevice::ReadOnly)) {
		const auto raw = legacy.readAll();
		legacy.close();
		if (!raw.isEmpty() && WriteCachedBytes(trackId, raw)) {
			QFile::remove(legacyPath);
		}
		return raw;
	}
	return {};
}

bool Player::isTrackCached(const QString &trackId) const {
	if (trackId.isEmpty()) {
		return false;
	}
	const auto info = QFileInfo(CachedTrackPath(trackId));
	if (info.exists() && info.size() > 1024) {
		return true;
	}
	const auto legacy = QFileInfo(LegacyMp3Path(trackId));
	return legacy.exists() && legacy.size() > 1024;
}

bool Player::isTrackDownloading(const QString &trackId) const {
	return _downloading.contains(trackId);
}

void Player::cacheTrack(const TrackInfo &track) {
	if (!track.valid() || isTrackCached(track.id) || isTrackDownloading(track.id)) {
		return;
	}
	_downloading.insert(track.id);
	_pendingDownloadResolves.insert(track.id);
	Q_EMIT trackCached(track.id);
	_client->resolveStreamUrl(track);
}

void Player::cachePlaylist(const QString &playlistId) {
	const auto pl = findPlaylist(playlistId);
	if (!pl) {
		return;
	}
	for (const auto &track : pl->tracks) {
		cacheTrack(track);
	}
}

void Player::startDownloadWithUrl(const QString &trackId, const QString &url) {
	QDir().mkpath(CacheDir());
	_client->downloadBytes(url, [this, trackId](QByteArray bytes) {
		_downloading.remove(trackId);
		if (!bytes.isEmpty()) {
			(void)WriteCachedBytes(trackId, bytes);
		}
		Q_EMIT trackCached(trackId);
	});
}

QByteArray Player::ramBytes(const QString &trackId) const {
	return _ram.value(trackId);
}

void Player::touchRam(const QString &trackId) {
	_ramOrder.removeAll(trackId);
	_ramOrder.push_back(trackId);
	while (_ramOrder.size() > 4) {
		const auto old = _ramOrder.takeFirst();
		_ram.remove(old);
	}
}

void Player::fetchToRam(const QString &trackId, const QString &url) {
	if (_ram.contains(trackId) || _ramFetching.contains(trackId)) {
		return;
	}
	_ramFetching.insert(trackId);
	const auto start = QDateTime::currentMSecsSinceEpoch();
	_client->downloadBytes(url, [this, trackId, start](QByteArray bytes) {
		_ramFetching.remove(trackId);
		MusicLog(QStringLiteral("ram fetch %1: %2 bytes in %3 ms")
			.arg(trackId)
			.arg(bytes.size())
			.arg(QDateTime::currentMSecsSinceEpoch() - start));
		if (bytes.size() > 100 * 1024) {
			_ram.insert(trackId, bytes);
			touchRam(trackId);
		}
	});
}

void Player::ensureAudioSink(int sampleRate, int channels) {
	if (_audioSink && _sinkSampleRate == sampleRate && _sinkChannels == channels) {
		if (!_ringDevice) {
			_ringDevice = new AudioRingBufferDevice(this);
		}
		if (_audioSink->state() == QAudio::StoppedState) {
			_audioSink->start(_ringDevice);
		} else if (_audioSink->state() == QAudio::SuspendedState) {
			_audioSink->resume();
		}
		return;
	}
	resetAudioSink();
	delete _audioSink;
	_audioSink = nullptr;
	delete _ringDevice;
	_ringDevice = nullptr;

	QAudioFormat fmt;
	fmt.setSampleRate(sampleRate);
	fmt.setChannelCount(channels);
	fmt.setSampleFormat(QAudioFormat::Float);
	const auto device = QMediaDevices::defaultAudioOutput();
	_audioSink = new QAudioSink(device, fmt, this);
	_audioSink->setBufferSize(sampleRate * channels * sizeof(float) / 2);
	_sinkSampleRate = sampleRate;
	_sinkChannels = channels;
	_dsp.setSampleRate(sampleRate);
	_ringDevice = new AudioRingBufferDevice(this);
	_audioSink->start(_ringDevice);
}

void Player::updateAudioRoute() {
	const auto custom = _dsp.enabled() || _dsp.boostGain() > 1.001f;
	const auto route = custom ? 1 : 0;
	if (route == _audioRoute) {
		return;
	}
	_audioRoute = route;
	if (custom) {
		_output->setVolume(0.f);
		_media->setAudioBufferOutput(_bufferOutput);
	} else {
		_media->setAudioBufferOutput(nullptr);
		if (_audioSink) {
			_audioSink->stop();
		}
		if (_ringDevice) {
			_ringDevice->clear();
		}
		_dsp.resetState();
		_output->setVolume(_resumeMuted ? 0.f : _volume);
	}
}

void Player::resetAudioSink() {
	if (_ringDevice) {
		_ringDevice->clear();
	}
	if (_audioSink) {
		_audioSink->reset();
	}
	_dsp.resetState();
}

void Player::setEqEnabled(bool enabled) {
	_dsp.setEnabled(enabled);
	updateAudioRoute();
	Q_EMIT eqChanged();
	scheduleSaveState();
}

void Player::setEqBand(int band, float gainDb) {
	_dsp.setBandGain(band, gainDb);
	Q_EMIT eqChanged();
	scheduleSaveState();
}

void Player::setEqGains(const std::array<float, 5> &gainsDb) {
	_dsp.setAllGains(gainsDb);
	Q_EMIT eqChanged();
	scheduleSaveState();
}

bool Player::eqEnabled() const {
	return _dsp.enabled();
}

std::array<float, 5> Player::eqGains() const {
	return _dsp.gains();
}

void Player::fetchLyrics(const TrackInfo &track) {
	if (!track.valid()) {
		return;
	}
	QUrl url(QStringLiteral("https://lrclib.net/api/search"));
	QUrlQuery query;
	const auto q = track.artist.isEmpty()
		? track.title
		: (track.artist + QLatin1Char(' ') + track.title);
	query.addQueryItem(QStringLiteral("q"), q);
	url.setQuery(query);

	QNetworkRequest req(url);
	req.setHeader(
		QNetworkRequest::UserAgentHeader,
		QStringLiteral("VRGMusic/1.0"));
	auto reply = _downloader->get(req);
	const auto trackId = track.id;
	connect(reply, &QNetworkReply::finished, this, [this, reply, trackId] {
		reply->deleteLater();
		QString plain;
		QString synced;
		if (reply->error() == QNetworkReply::NoError) {
			const auto doc = QJsonDocument::fromJson(reply->readAll());
			const auto arr = doc.array();
			if (!arr.isEmpty()) {
				const auto obj = arr.first().toObject();
				plain = obj.value(QStringLiteral("plainLyrics")).toString();
				synced = obj.value(QStringLiteral("syncedLyrics")).toString();
			}
		}
		Q_EMIT lyricsLoaded(trackId, plain, synced);
	});
}

void Player::scheduleSaveState() {
	if (_saveDebounceTimer) {
		_saveDebounceTimer->start();
	}
}

QString Player::StatePath() {
	return QDir::homePath() + QStringLiteral("/.config/vrg/music_state.json");
}

QString Player::StreamUrlsPath() {
	return QDir::homePath() + QStringLiteral("/.config/vrg/music_stream_urls.json");
}

void Player::loadStreamUrls() {
	QFile f(StreamUrlsPath());
	if (!f.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		return;
	}
	const auto now = QDateTime::currentMSecsSinceEpoch();
	const auto obj = doc.object();
	for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
		const auto entry = it.value().toObject();
		const auto url = entry.value(QStringLiteral("u")).toString();
		const auto ts = qint64(entry.value(QStringLiteral("t")).toDouble(0));
		if (!url.isEmpty() && (now - ts) < 8 * 60 * 1000) {
			_streamUrls.insert(it.key(), { url, ts });
		}
	}
}

void Player::saveStreamUrls() const {
	QDir().mkpath(QFileInfo(StreamUrlsPath()).absolutePath());
	const auto now = QDateTime::currentMSecsSinceEpoch();
	QJsonObject obj;
	for (auto it = _streamUrls.constBegin(); it != _streamUrls.constEnd(); ++it) {
		if ((now - it.value().second) < 8 * 60 * 1000 && !it.value().first.isEmpty()) {
			QJsonObject entry;
			entry.insert(QStringLiteral("u"), it.value().first);
			entry.insert(QStringLiteral("t"), double(it.value().second));
			obj.insert(it.key(), entry);
		}
	}
	QSaveFile f(StreamUrlsPath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
		f.commit();
	}
}

void Player::saveState() const {
	const auto path = StatePath();
	QDir().mkpath(QFileInfo(path).absolutePath());

	QJsonArray queueArray;
	for (const auto &track : _queue) {
		queueArray.append(Serialize(track));
	}
	QJsonArray eqArray;
	for (const auto gain : _dsp.gains()) {
		eqArray.append(double(gain));
	}

	QJsonObject root;
	root.insert(QStringLiteral("queue"), queueArray);
	root.insert(QStringLiteral("index"), _index);
	root.insert(QStringLiteral("positionMs"), position());
	root.insert(QStringLiteral("volume"), double(_volume));
	root.insert(QStringLiteral("boost"), double(boost()));
	root.insert(QStringLiteral("shuffle"), _shuffle);
	root.insert(QStringLiteral("repeat"), int(_repeat));
	root.insert(QStringLiteral("eqEnabled"), _dsp.enabled());
	root.insert(QStringLiteral("eqGains"), eqArray);

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	file.commit();
}

void Player::loadState() {
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto root = QJsonDocument::fromJson(file.readAll()).object();
	if (root.isEmpty()) {
		return;
	}

	if (root.contains(QStringLiteral("volume"))) {
		setVolume(float(root.value(QStringLiteral("volume")).toDouble(0.85)));
	}
	if (root.contains(QStringLiteral("boost"))) {
		setBoost(float(root.value(QStringLiteral("boost")).toDouble(1.0)));
	}
	if (root.contains(QStringLiteral("shuffle"))) {
		_shuffle = root.value(QStringLiteral("shuffle")).toBool(false);
	}
	if (root.contains(QStringLiteral("repeat"))) {
		const auto rep = root.value(QStringLiteral("repeat")).toInt(0);
		if (rep >= 0 && rep <= int(RepeatMode::One)) {
			_repeat = RepeatMode(rep);
		}
	}
	if (root.contains(QStringLiteral("eqEnabled"))) {
		_dsp.setEnabled(root.value(QStringLiteral("eqEnabled")).toBool(false));
	}
	const auto eqArray = root.value(QStringLiteral("eqGains")).toArray();
	if (eqArray.size() == 5) {
		std::array<float, 5> gains{};
		for (int i = 0; i < 5; ++i) {
			gains[i] = float(eqArray.at(i).toDouble(0.0));
		}
		_dsp.setAllGains(gains);
	}
	Q_EMIT eqChanged();

	QVector<TrackInfo> loadedQueue;
	const auto queueArray = root.value(QStringLiteral("queue")).toArray();
	loadedQueue.reserve(queueArray.size());
	for (const auto &entry : queueArray) {
		const auto track = Deserialize(entry.toObject());
		if (track.valid()) {
			loadedQueue.push_back(track);
		}
	}
	if (!loadedQueue.isEmpty()) {
		_queue = std::move(loadedQueue);
		const auto savedIndex = root.value(QStringLiteral("index")).toInt(0);
		_index = (savedIndex >= 0 && savedIndex < _queue.size())
			? savedIndex
			: 0;
		rebuildOrder();
		_pendingSeekMs = std::max(
			qint64(0),
			root.value(QStringLiteral("positionMs")).toVariant().toLongLong());
		Q_EMIT queueChanged();
		Q_EMIT currentTrackChanged(currentTrack());
		Q_EMIT durationChanged(duration());
		Q_EMIT positionChanged(_pendingSeekMs);
	}
}

SoundCloudClient *SharedClient() {
	static auto *instance = new SoundCloudClient();
	return instance;
}

Player *SharedPlayer() {
	if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES")) {
		qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray());
	}
	static auto *instance = [] {
		const auto player = new Player(SharedClient());
		InitMpris(player);
		return player;
	}();
	return instance;
}

}
