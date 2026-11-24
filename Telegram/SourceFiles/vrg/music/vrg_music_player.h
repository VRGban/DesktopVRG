/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/music/vrg_music_types.h"
#include "vrg/music/vrg_music_dsp.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QVector>

#ifdef Q_OS_WIN
#include <QtCore/QByteArray>
#include <QtCore/QUrl>
#include <QtMultimedia/QMediaPlayer>
#include <functional>
#include <vector>
#else
class QMediaPlayer;
#endif
class QAudioOutput;
class QAudioBufferOutput;
class QAudioSink;
class QBuffer;
class QIODevice;
class QNetworkAccessManager;
class QTimer;

namespace Vrg::Music {

class AudioRingBufferDevice;
class SoundCloudClient;

#ifdef Q_OS_WIN
class WinMediaPlayer final : public QObject {
	Q_OBJECT

public:
	explicit WinMediaPlayer(QObject *parent = nullptr);
	~WinMediaPlayer();

	void setAudioOutput(QAudioOutput *output);
	void setAudioBufferOutput(QAudioBufferOutput *output);
	void setProcessor(std::function<void(float*, int)> processor);
	void setSource(const QUrl &url);
	void setSourceDevice(QIODevice *device, const QUrl &url = QUrl());
	[[nodiscard]] QUrl source() const;
	[[nodiscard]] QIODevice *sourceDevice() const;
	[[nodiscard]] QMediaPlayer::PlaybackState playbackState() const;
	[[nodiscard]] QMediaPlayer::MediaStatus mediaStatus() const;
	[[nodiscard]] qint64 position() const;
	[[nodiscard]] qint64 duration() const;
	[[nodiscard]] bool isSeekable() const;

public Q_SLOTS:
	void play();
	void pause();
	void stop();
	void setPosition(qint64 positionMs);

Q_SIGNALS:
	void positionChanged(qint64 position);
	void durationChanged(qint64 duration);
	void seekableChanged(bool seekable);
	void playbackStateChanged(QMediaPlayer::PlaybackState state);
	void mediaStatusChanged(QMediaPlayer::MediaStatus status);
	void errorOccurred(QMediaPlayer::Error error, const QString &message);

private:
	static constexpr int kBuffers = 8;

	bool loadWav(const QByteArray &data);
	void clearMedia();
	void setStatus(QMediaPlayer::MediaStatus status);
	void setPlaybackState(QMediaPlayer::PlaybackState state);
	bool openDevice();
	void closeDevice();
	void pump();
	void fillBuffer(int index);
	void finishPlayback();
	[[nodiscard]] qint64 framesToMs(qint64 frames) const;
	[[nodiscard]] qint64 playedFrames() const;

	QByteArray _pcm;
	qint64 _frames = 0;
	qint64 _baseFrame = 0;
	qint64 _writeFrame = 0;
	QUrl _source;
	QIODevice *_device = nullptr;
	QAudioOutput *_output = nullptr;
	bool _processorActive = false;
	std::function<void(float*, int)> _processor;
	float _gain = 1.f;
	QMediaPlayer::PlaybackState _state = QMediaPlayer::StoppedState;
	QMediaPlayer::MediaStatus _status = QMediaPlayer::NoMedia;
	void *_handle = nullptr;
	void *_headers = nullptr;
	std::vector<QByteArray> _blocks;
	std::vector<char> _inFlight;
	QTimer *_pumpTimer = nullptr;
	QTimer *_positionTimer = nullptr;
	std::vector<float> _scratch;

};

using MediaBackend = WinMediaPlayer;
#else
using MediaBackend = QMediaPlayer;
#endif

class Player final : public QObject {
	Q_OBJECT

public:
	explicit Player(
		SoundCloudClient *client,
		QObject *parent = nullptr);
	~Player() override;

	void setQueue(const QVector<TrackInfo> &tracks, int startIndex);
	void playTrack(const TrackInfo &track);
	void adoptQueueKeepPlaying(const QVector<TrackInfo> &tracks);
	void togglePlayPause();
	void setVoiceActive(bool active);
	void stop();
	void next();
	void prev();
	void enqueueNext(const TrackInfo &track);
	void enqueueLast(const TrackInfo &track);
	[[nodiscard]] QVector<TrackInfo> upcoming() const;
	[[nodiscard]] int manualAhead() const;
	void removeUpcoming(int index);
	void moveUpcoming(int from, int to);
	void clearUpcoming();
	void playUpcoming(int index);
	void seek(qint64 positionMs);
	void setVolume(float volume);
	void setBoost(float boost);
	[[nodiscard]] float boost() const;
	void setShuffle(bool enabled);
	void setRepeatMode(RepeatMode mode);

	void setEqEnabled(bool enabled);
	void setEqBand(int band, float gainDb);
	void setEqGains(const std::array<float, 5> &gainsDb);
	void notifyVisualSettingsChanged() {
		Q_EMIT visualSettingsChanged();
	}
	[[nodiscard]] bool eqEnabled() const;
	[[nodiscard]] std::array<float, 5> eqGains() const;

	void fetchLyrics(const TrackInfo &track);

	[[nodiscard]] const QVector<TrackInfo> &queue() const;
	[[nodiscard]] int currentIndex() const;
	[[nodiscard]] TrackInfo currentTrack() const;
	[[nodiscard]] PlaybackState state() const;
	[[nodiscard]] qint64 position() const;
	[[nodiscard]] qint64 duration() const;
	[[nodiscard]] float volume() const;
	[[nodiscard]] bool shuffle() const;
	[[nodiscard]] RepeatMode repeatMode() const;

	[[nodiscard]] const QVector<TrackInfo> &library() const;
	[[nodiscard]] bool isInLibrary(const QString &trackId) const;
	void addToLibrary(const TrackInfo &track);
	void removeFromLibrary(const QString &trackId);
	void toggleLibrary(const TrackInfo &track);
	[[nodiscard]] static QString LibraryPath();

	[[nodiscard]] const QVector<Playlist> &playlists() const;
	[[nodiscard]] const Playlist *findPlaylist(const QString &playlistId) const;
	QString createPlaylist(const QString &name);
	void deletePlaylist(const QString &playlistId);
	void renamePlaylist(const QString &playlistId, const QString &name);
	void addTrackToPlaylist(const QString &playlistId, const TrackInfo &track);
	void removeTrackFromPlaylist(const QString &playlistId, const QString &trackId);
	void addTrackToPlaylistAtEnd(const QString &playlistId, const TrackInfo &track);
	void moveTrackInPlaylist(const QString &playlistId, const QString &trackId, int delta);
	void reorderTrack(const QString &playlistId, const QString &movedId, const QString &anchorId, bool after);

	[[nodiscard]] static QString CacheDir();
	[[nodiscard]] static QString CachedTrackPath(const QString &trackId);
	[[nodiscard]] static bool WriteCachedBytes(const QString &trackId, const QByteArray &bytes);
	[[nodiscard]] static QByteArray ReadCachedBytes(const QString &trackId);
	[[nodiscard]] bool isTrackCached(const QString &trackId) const;
	[[nodiscard]] bool isTrackDownloading(const QString &trackId) const;
	void cacheTrack(const TrackInfo &track);
	void cachePlaylist(const QString &playlistId);

Q_SIGNALS:
	void currentTrackChanged(Vrg::Music::TrackInfo track);
	void playbackStateChanged(Vrg::Music::PlaybackState state);
	void positionChanged(qint64 positionMs);
	void durationChanged(qint64 durationMs);
	void libraryChanged();
	void playlistsChanged();
	void trackCached(QString trackId);
	void queueChanged();
	void shuffleChanged(bool enabled);
	void repeatModeChanged(Vrg::Music::RepeatMode mode);
	void volumeChanged(float volume);
	void eqChanged();
	void visualSettingsChanged();
	void lyricsLoaded(QString trackId, QString plainLyrics, QString syncedLyrics);
	void errorOccurred(QString message);

private:
	void startIndex(int index, qint64 resumeMs = 0);
	void startPlayback();
	void setResumeMuted(bool muted);
	void finishResume();
	void verifyResume(int serial);
	bool _resumeMuted = false;
	QElapsedTimer _loadClock;
	bool _silentRetry = false;
	int _silentRetries = 0;
	void prefetchNext();
	[[nodiscard]] QString freshStreamUrl(const QString &trackId) const;
	void advance(bool userAction);
	void rebuildOrder();
	[[nodiscard]] int orderPosition() const;
	void setState(PlaybackState state);
	void applyStream(const QString &trackId, const QString &url);
	void playStreamUrl(const QString &trackId, const QString &url);
	void playBytes(const QByteArray &bytes);
	void handleEndOfMedia();
	void handleStreamFailure(const QString &trackId);

	void loadLibrary();
	void saveLibrary() const;
	[[nodiscard]] static QString StatePath();
	[[nodiscard]] static QString StreamUrlsPath();
	void loadState();
	void saveState() const;
	void loadStreamUrls();
	void saveStreamUrls() const;
	void startDownloadWithUrl(const QString &trackId, const QString &url);
	void ensureAudioSink(int sampleRate, int channels);
	void resetAudioSink();

	SoundCloudClient * const _client;
	MediaBackend *_media = nullptr;
	QAudioOutput *_output = nullptr;
	QAudioBufferOutput *_bufferOutput = nullptr;
	QAudioSink *_audioSink = nullptr;
	AudioRingBufferDevice *_ringDevice = nullptr;
	QBuffer *_cacheBuffer = nullptr;
	int _sinkSampleRate = 0;
	int _sinkChannels = 0;
	EqualizerDsp _dsp;
	QNetworkAccessManager *_downloader = nullptr;
	QTimer *_positionTimer = nullptr;
	QTimer *_stateSaveTimer = nullptr;
	QTimer *_saveDebounceTimer = nullptr;
	qint64 _pendingSeekMs = 0;
	qint64 _lastSeekAttemptMs = 0;
	int _seekAttempts = 0;

	void trySeekPending();
	void updateAudioRoute();
	int _audioRoute = -1;
	bool _bufferSeen = false;
	bool _noBufferOutput = false;
	QString _tempTrackPath;
	int _tempTrackCounter = 0;
	bool _stopByUs = false;
	int _autoResumes = 0;
	[[nodiscard]] bool playFromTempFile(const QByteArray &data, const QString &extension);
	QSet<QString> _prefetching;
	QHash<QString, QByteArray> _ram;
	QStringList _ramOrder;
	QSet<QString> _ramFetching;
	QSet<QString> _ramWanted;
	void fetchToRam(const QString &trackId, const QString &url);
	void touchRam(const QString &trackId);
	[[nodiscard]] QByteArray ramBytes(const QString &trackId) const;
	QHash<QString, QPair<QString, qint64>> _streamUrls;
	bool _playAfterSeek = false;
	int _seekSerial = 0;
	void scheduleSaveState();

	QVector<TrackInfo> _queue;
	int _index = -1;
	QVector<int> _order;
	int _manualAhead = 0;
	QString _restartTrackId;
	int _restartCount = 0;
	QElapsedTimer _restartClock;
	void removeQueueIndex(int queueIndex);

	QVector<TrackInfo> _library;
	QVector<Playlist> _playlists;
	QSet<QString> _downloading;
	QSet<QString> _pendingDownloadResolves;

	PlaybackState _state = PlaybackState::Stopped;
	bool _pausedByVoice = false;
	bool _shuffle = false;
	RepeatMode _repeat = RepeatMode::None;
	float _volume = 0.8f;
	int _failuresInRow = 0;
	bool _failureHandled = false;

};

[[nodiscard]] SoundCloudClient *SharedClient();
[[nodiscard]] Player *SharedPlayer();

}

