/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#include "vrg/music/vrg_mpris.h"

#include <QtCore/qglobal.h>

#ifdef Q_OS_LINUX

#include "vrg/music/vrg_music_player.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>

namespace Vrg::Music {
namespace {

const auto kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const auto kService = QStringLiteral("org.mpris.MediaPlayer2.vrg");

class MprisRoot final : public QDBusAbstractAdaptor {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
	Q_PROPERTY(bool CanQuit READ canQuit)
	Q_PROPERTY(bool CanRaise READ canRaise)
	Q_PROPERTY(bool HasTrackList READ hasTrackList)
	Q_PROPERTY(QString Identity READ identity)
	Q_PROPERTY(QString DesktopEntry READ desktopEntry)
	Q_PROPERTY(QStringList SupportedUriSchemes READ schemes)
	Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)

public:
	explicit MprisRoot(QObject *parent) : QDBusAbstractAdaptor(parent) {
	}

	bool canQuit() const { return false; }
	bool canRaise() const { return false; }
	bool hasTrackList() const { return false; }
	QString identity() const { return QStringLiteral("VRG Music"); }
	QString desktopEntry() const { return QStringLiteral("vrg"); }
	QStringList schemes() const { return {}; }
	QStringList mimeTypes() const { return {}; }

public Q_SLOTS:
	void Raise() {
	}
	void Quit() {
	}
};

class MprisPlayer final : public QDBusAbstractAdaptor {
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
	Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
	Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
	Q_PROPERTY(double Rate READ rate WRITE setRate)
	Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
	Q_PROPERTY(QVariantMap Metadata READ metadata)
	Q_PROPERTY(double Volume READ volume WRITE setVolume)
	Q_PROPERTY(qlonglong Position READ position)
	Q_PROPERTY(double MinimumRate READ rate)
	Q_PROPERTY(double MaximumRate READ rate)
	Q_PROPERTY(bool CanGoNext READ canControl)
	Q_PROPERTY(bool CanGoPrevious READ canControl)
	Q_PROPERTY(bool CanPlay READ canControl)
	Q_PROPERTY(bool CanPause READ canControl)
	Q_PROPERTY(bool CanSeek READ canControl)
	Q_PROPERTY(bool CanControl READ canControl)

public:
	explicit MprisPlayer(Player *player)
	: QDBusAbstractAdaptor(player)
	, _player(player) {
		connect(_player, &Player::currentTrackChanged, this, [=] {
			changed({ "Metadata", "PlaybackStatus" });
		});
		connect(_player, &Player::playbackStateChanged, this, [=] {
			changed({ "PlaybackStatus" });
		});
		connect(_player, &Player::shuffleChanged, this, [=] {
			changed({ "Shuffle" });
		});
		connect(_player, &Player::repeatModeChanged, this, [=] {
			changed({ "LoopStatus" });
		});
		connect(_player, &Player::volumeChanged, this, [=] {
			changed({ "Volume" });
		});
		connect(_player, &Player::durationChanged, this, [=] {
			changed({ "Metadata" });
		});
		QTimer::singleShot(0, this, [=] {
			registerIfNeeded();
		});
	}

	QString playbackStatus() const {
		switch (_player->state()) {
		case PlaybackState::Playing:
		case PlaybackState::Loading: return QStringLiteral("Playing");
		case PlaybackState::Paused: return QStringLiteral("Paused");
		default: return QStringLiteral("Stopped");
		}
	}
	QString loopStatus() const {
		switch (_player->repeatMode()) {
		case RepeatMode::All: return QStringLiteral("Playlist");
		case RepeatMode::One: return QStringLiteral("Track");
		default: return QStringLiteral("None");
		}
	}
	void setLoopStatus(const QString &value) {
		_player->setRepeatMode(value == QStringLiteral("Track")
			? RepeatMode::One
			: (value == QStringLiteral("Playlist"))
			? RepeatMode::All
			: RepeatMode::None);
	}
	double rate() const { return 1.0; }
	void setRate(double) {
	}
	bool shuffle() const { return _player->shuffle(); }
	void setShuffle(bool value) { _player->setShuffle(value); }
	double volume() const { return _player->volume(); }
	void setVolume(double value) {
		_player->setVolume(float(std::clamp(value, 0.0, 1.0)));
	}
	qlonglong position() const { return _player->position() * 1000; }
	bool canControl() const { return _player->currentTrack().valid(); }

	QVariantMap metadata() const {
		QVariantMap map;
		const auto t = _player->currentTrack();
		if (!t.valid()) {
			map.insert(
				QStringLiteral("mpris:trackid"),
				QVariant::fromValue(QDBusObjectPath(
					QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"))));
			return map;
		}
		auto safe = t.id;
		safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
		map.insert(
			QStringLiteral("mpris:trackid"),
			QVariant::fromValue(QDBusObjectPath(
				QStringLiteral("/org/vrg/track/") + safe)));
		const auto duration = _player->duration() > 0
			? _player->duration()
			: t.durationMs;
		map.insert(
			QStringLiteral("mpris:length"),
			QVariant::fromValue(qlonglong(duration) * 1000));
		map.insert(QStringLiteral("xesam:title"), t.title);
		map.insert(
			QStringLiteral("xesam:artist"),
			QStringList{ t.artist });
		if (!t.artworkUrl.isEmpty()) {
			map.insert(QStringLiteral("mpris:artUrl"), t.artworkUrl);
		}
		if (!t.permalinkUrl.isEmpty()) {
			map.insert(QStringLiteral("xesam:url"), t.permalinkUrl);
		}
		return map;
	}

public Q_SLOTS:
	void Next() { _player->next(); }
	void Previous() { _player->prev(); }
	void Pause() {
		if (_player->state() == PlaybackState::Playing) {
			_player->togglePlayPause();
		}
	}
	void PlayPause() { _player->togglePlayPause(); }
	void Stop() { _player->stop(); }
	void Play() {
		if (_player->state() != PlaybackState::Playing) {
			_player->togglePlayPause();
		}
	}
	void Seek(qlonglong offsetUs) {
		_player->seek(std::max<qint64>(
			_player->position() + offsetUs / 1000,
			0));
	}
	void SetPosition(const QDBusObjectPath &, qlonglong positionUs) {
		_player->seek(std::max<qint64>(positionUs / 1000, 0));
	}
	void OpenUri(const QString &) {
	}

Q_SIGNALS:
	void Seeked(qlonglong positionUs);

private:
	void registerIfNeeded() {
		if (!_registered && _player->currentTrack().valid()) {
			_registered = QDBusConnection::sessionBus().registerService(kService);
		}
	}

	void changed(const QStringList &names) {
		registerIfNeeded();
		if (!_registered) {
			return;
		}
		QVariantMap props;
		for (const auto &name : names) {
			props.insert(name, property(name.toUtf8().constData()));
		}
		auto message = QDBusMessage::createSignal(
			kPath,
			QStringLiteral("org.freedesktop.DBus.Properties"),
			QStringLiteral("PropertiesChanged"));
		message << QStringLiteral("org.mpris.MediaPlayer2.Player")
			<< props
			<< QStringList();
		QDBusConnection::sessionBus().send(message);
	}

	Player *_player = nullptr;
	bool _registered = false;

};

}

void InitMpris(Player *player) {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	auto bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		return;
	}
	new MprisRoot(player);
	new MprisPlayer(player);
	bus.registerObject(kPath, player, QDBusConnection::ExportAdaptors);
}

}

#include "vrg_mpris.moc"

#else

namespace Vrg::Music {

void InitMpris(Player *) {
}

}

#endif
