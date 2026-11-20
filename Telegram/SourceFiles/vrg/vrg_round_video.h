/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <crl/crl.h>

namespace Vrg::RoundVideo {

inline void SendFromFile(
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	const auto weak = base::make_weak(controller.get());
	FileDialog::GetOpenPath(
		controller->widget().get(),
		QString::fromUtf8("Выбрать видео для кружочка"),
		u"Видео (*.mp4 *.mov *.mkv *.webm *.avi *.m4v)"_q,
		[=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty() || !weak) {
				return;
			}
			const auto ffmpeg = QStandardPaths::findExecutable(u"ffmpeg"_q);
			if (ffmpeg.isEmpty()) {
				weak->showToast(QString::fromUtf8(
					"Не найден ffmpeg в системе — установите ffmpeg"));
				return;
			}
			const auto inputPath = result.paths.front();
			weak->showToast(QString::fromUtf8(
				"Конвертирую видео в кружочек..."));

			crl::async([=] {
				const auto tmpPath = QDir::tempPath()
					+ u"/vrg_round_"_q
					+ QUuid::createUuid().toString(QUuid::WithoutBraces)
					+ u".mp4"_q;

				QProcess proc;
				proc.start(ffmpeg, {
					u"-y"_q,
					u"-i"_q, inputPath,
					u"-t"_q, u"60"_q,
					u"-vf"_q, u"crop='min(iw,ih)':'min(iw,ih)',scale=384:384"_q,
					u"-c:v"_q, u"libx264"_q,
					u"-preset"_q, u"fast"_q,
					u"-crf"_q, u"23"_q,
					u"-pix_fmt"_q, u"yuv420p"_q,
					u"-c:a"_q, u"aac"_q,
					u"-b:a"_q, u"64k"_q,
					u"-movflags"_q, u"+faststart"_q,
					tmpPath,
				});
				const auto finished = proc.waitForFinished(120000);
				if (!finished || proc.exitCode() != 0) {
					QFile::remove(tmpPath);
					crl::on_main([=] {
						if (weak) {
							weak->showToast(QString::fromUtf8(
								"Не удалось сконвертировать видео в кружок"));
						}
					});
					return;
				}

				crl::time durationMs = 10000;
				const auto ffprobe = QStandardPaths::findExecutable(u"ffprobe"_q);
				if (!ffprobe.isEmpty()) {
					QProcess probe;
					probe.start(ffprobe, {
						u"-v"_q, u"error"_q,
						u"-show_entries"_q, u"format=duration"_q,
						u"-of"_q, u"default=noprint_wrappers=1:nokey=1"_q,
						tmpPath,
					});
					if (probe.waitForFinished(10000) && probe.exitCode() == 0) {
						auto ok = false;
						const auto sec = QString::fromUtf8(
							probe.readAllStandardOutput()).trimmed().toDouble(&ok);
						if (ok && sec > 0) {
							durationMs = crl::time(sec * 1000.0);
						}
					}
				}

				QFile outFile(tmpPath);
				QByteArray bytes;
				if (outFile.open(QIODevice::ReadOnly)) {
					bytes = outFile.readAll();
				}
				QFile::remove(tmpPath);

				crl::on_main([=, bytes = std::move(bytes)]() mutable {
					if (!weak) {
						return;
					}
					if (bytes.isEmpty()) {
						weak->showToast(QString::fromUtf8(
							"Ошибка чтения сконвертированного кружочка"));
						return;
					}
					history->session().api().sendVoiceMessage(
						std::move(bytes),
						VoiceWaveform(),
						durationMs,
						true,
						Api::SendAction(history));
				});
			});
		});
}

}
