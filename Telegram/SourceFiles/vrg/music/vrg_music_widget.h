/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/music/vrg_music_types.h"

#include <QtCore/QModelIndex>
#include <QtGui/QColor>
#include <QtGui/QPixmap>
#include <QtWidgets/QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QToolButton;

namespace Window {
class SessionController;
}

namespace Ui {
class PopupMenu;
}

namespace Vrg::Music {

class Player;
class SoundCloudClient;
class TrackListModel;
class TrackDelegate;
class MarqueeLabel;
class SmoothSlider;
class SmoothLyricsWidget;

class VRGMiniPlayerBar final : public QWidget {
	Q_OBJECT

public:
	VRGMiniPlayerBar(
		Player *player,
		SoundCloudClient *client,
		Window::SessionController *controller,
		QWidget *parent = nullptr);

Q_SIGNALS:
	void lyricsToggled();
	void eqRequested();
	void queueRequested();
	void playlistRequested(TrackInfo track, QPoint globalPos);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateTrack(const TrackInfo &track);
	void updateState(PlaybackState state);
	void updatePosition(qint64 positionMs);
	void updateDuration(qint64 durationMs);
	void updateShuffle(bool enabled);
	void updateRepeat(RepeatMode mode);
	void updateLibraryState();
	void applyVisualSettings();
	void setArtwork(const QPixmap &pixmap);

	Player * const _player;
	SoundCloudClient * const _client;
	Window::SessionController * const _controller;

	QLabel *_artwork = nullptr;
	MarqueeLabel *_title = nullptr;
	QToolButton *_like = nullptr;
	QToolButton *_shuffle = nullptr;
	QToolButton *_prev = nullptr;
	QToolButton *_play = nullptr;
	QToolButton *_next = nullptr;
	QToolButton *_repeat = nullptr;
	QToolButton *_lyricsBtn = nullptr;
	QToolButton *_eqBtn = nullptr;
	QToolButton *_queueBtn = nullptr;
	QLabel *_volumeIcon = nullptr;
	SmoothSlider *_volume = nullptr;
	QLabel *_volumePct = nullptr;
	QToolButton *_addPlaylistBtn = nullptr;
	QLabel *_elapsed = nullptr;
	SmoothSlider *_seek = nullptr;
	QLabel *_total = nullptr;

	QString _artworkUrl;
	QColor _accentColor = QColor(0x14, 0x28, 0x30);
	qint64 _posMs = 0;
	qint64 _durMs = 0;

};

class VRGMusicWidget final : public QWidget {
	Q_OBJECT

public:
	explicit VRGMusicWidget(
		Window::SessionController *controller = nullptr,
		QWidget *parent = nullptr);
	~VRGMusicWidget() override;

	[[nodiscard]] Player *player() const;
	[[nodiscard]] SoundCloudClient *client() const;
	[[nodiscard]] bool lyricsOpen() const {
		return _lyricsOpen;
	}
	[[nodiscard]] Window::SessionController *controller() const {
		return _controller;
	}

Q_SIGNALS:
	void fullscreenLyricsChanged(bool active);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	enum class Tab {
		Search,
		Library,
		Playlists,
	};

	void setupUi();
	void setTab(Tab tab);
	void runSearch();
	void applyLocalFilter();
	void refreshStatus();
	void refreshCounters();
	void refreshPlaylists();
	void openPlaylist(const QString &playlistId);
	void closePlaylist();
	void createPlaylistPrompt(std::optional<TrackInfo> autoAddTrack = std::nullopt);
	void showPlaylistMenuForTrack(const TrackInfo &track, const QPoint &globalPos, bool withQueue = false);
	void sendTrackToChat(const TrackInfo &track);
	void toggleLyricsPanel();
	void showEqBox();
	void showQueueBox();
	void updateLyricsDisplay(qint64 positionMs);
	void showError(const QString &message);
	void showImportBox(const QString &playlistId);
	void playFromList(const QModelIndex &index);
	void toggleLibrary(const QModelIndex &index);
	void toggleDownload(const QModelIndex &index);
	[[nodiscard]] TrackListModel *currentModel() const;

	Window::SessionController * const _controller;
	SoundCloudClient *_client = nullptr;
	Player *_player = nullptr;

	TrackListModel *_searchModel = nullptr;
	TrackListModel *_libraryModel = nullptr;
	TrackListModel *_playlistTracksModel = nullptr;
	TrackDelegate *_delegate = nullptr;

	QLineEdit *_search = nullptr;
	QPushButton *_searchTab = nullptr;
	QPushButton *_libraryTab = nullptr;
	QPushButton *_playlistsTab = nullptr;

	QWidget *_libraryBar = nullptr;
	QLabel *_libraryCounterLabel = nullptr;
	QPushButton *_libraryImportBtn = nullptr;
	QPushButton *_libraryDownloadAllBtn = nullptr;

	QWidget *_playlistBar = nullptr;
	QPushButton *_playlistBackBtn = nullptr;
	QLabel *_playlistTitleLabel = nullptr;
	QPushButton *_playlistPlayAllBtn = nullptr;
	QPushButton *_playlistImportBtn = nullptr;
	QPushButton *_playlistDownloadAllBtn = nullptr;

	QPushButton *_newPlaylistBtn = nullptr;
	QWidget *_playlistsContainer = nullptr;
	QLabel *_error = nullptr;
	QStackedWidget *_pages = nullptr;
	QListView *_list = nullptr;
	QLabel *_status = nullptr;

	QWidget *_headerWidget = nullptr;
	QWidget *_lyricsPage = nullptr;
	QPixmap _lyricsBg;
	QPixmap _lyricsBgScaled;
	QSize _lyricsBgScaledSize;
	QLabel *_lyricsWatermark = nullptr;
	QLabel *_lyricsHeader = nullptr;
	SmoothLyricsWidget *_lyricsView = nullptr;
	QPushButton *_lyricsCloseBtn = nullptr;
	QString _lyricsTrackId;
	bool _lyricsOpen = false;

	VRGMiniPlayerBar *_bar = nullptr;
	QPointer<Ui::PopupMenu> _popupMenu;

	Tab _tab = Tab::Search;
	QString _activePlaylistId;
	bool _searching = false;
	bool _searchedOnce = false;
	int _errorSerial = 0;

};

[[nodiscard]] VRGMusicWidget *AcquireMusicWidget(
	Window::SessionController *controller,
	QWidget *parent);
void ReleaseMusicWidget(VRGMusicWidget *widget);

[[nodiscard]] QWidget *CreateChatsMiniBar(
	QWidget *parent,
	Window::SessionController *controller);

}

