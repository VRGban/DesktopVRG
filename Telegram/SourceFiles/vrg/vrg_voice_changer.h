/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QProcess>
#include <QtCore/QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Vrg::VoiceChanger {

struct Preset {
	QString id;
	QString title;
	QString description;
	int pitch = 0;
	int bass = 0;
	int treble = 0;
	int tempo = 100;
	int echo = 0;
	bool robot = false;
	bool preserveFormants = false;
};

[[nodiscard]] inline const std::vector<Preset> &Presets() {
	static const auto kPresets = std::vector<Preset>{
		{
			u"male"_q,
			QString::fromUtf8("Мужской"),
			QString::fromUtf8("Более низкий и глубокий мужской голос"),
			-4, 6, 1, 100, 0, false, false,
		},
		{
			u"deep"_q,
			QString::fromUtf8("Демон / Бас"),
			QString::fromUtf8("Тяжёлый низкий голос с лёгким эхом"),
			-7, 9, -2, 95, 25, false, false,
		},
		{
			u"female"_q,
			QString::fromUtf8("Женский"),
			QString::fromUtf8("Выше тон и мягче верха"),
			5, -2, 3, 100, 0, false, false,
		},
		{
			u"chipmunk"_q,
			QString::fromUtf8("Бурундук"),
			QString::fromUtf8("Высокий писклявый и чуть ускоренный"),
			9, -4, 4, 112, 0, false, false,
		},
		{
			u"robot"_q,
			QString::fromUtf8("Робот / Рация"),
			QString::fromUtf8("Эффект рации с модуляцией"),
			-2, -3, 2, 100, 15, true, false,
		},
		{
			u"custom"_q,
			QString::fromUtf8("Свой (ползунки)"),
			QString::fromUtf8("Ручная настройка всех параметров ниже"),
			0, 0, 0, 100, 0, false, false,
		},
	};
	return kPresets;
}

[[nodiscard]] inline bool Enabled() {
	return Conf::Bool(u"voice_changer_enabled"_q, false);
}

inline void SetEnabled(bool enabled) {
	Conf::SetBool(u"voice_changer_enabled"_q, enabled);
}

[[nodiscard]] inline QString PresetId() {
	const auto v = Conf::Str(u"voice_changer_preset"_q);
	return v.isEmpty() ? u"male"_q : v;
}

inline void SetPresetId(const QString &id) {
	Conf::Set(u"voice_changer_preset"_q, id);
}

[[nodiscard]] inline int Pitch() {
	return std::clamp(Conf::Int(u"voice_pitch"_q, -4), -12, 12);
}

inline void SetPitch(int v) {
	Conf::SetInt(u"voice_pitch"_q, std::clamp(v, -12, 12));
}

[[nodiscard]] inline int Bass() {
	return std::clamp(Conf::Int(u"voice_bass"_q, 6), -6, 12);
}

inline void SetBass(int v) {
	Conf::SetInt(u"voice_bass"_q, std::clamp(v, -6, 12));
}

[[nodiscard]] inline int Treble() {
	return std::clamp(Conf::Int(u"voice_treble"_q, 1), -6, 12);
}

inline void SetTreble(int v) {
	Conf::SetInt(u"voice_treble"_q, std::clamp(v, -6, 12));
}

[[nodiscard]] inline int Tempo() {
	return std::clamp(Conf::Int(u"voice_tempo"_q, 100), 75, 135);
}

inline void SetTempo(int v) {
	Conf::SetInt(u"voice_tempo"_q, std::clamp(v, 75, 135));
}

[[nodiscard]] inline int Echo() {
	return std::clamp(Conf::Int(u"voice_echo"_q, 0), 0, 100);
}

inline void SetEcho(int v) {
	Conf::SetInt(u"voice_echo"_q, std::clamp(v, 0, 100));
}

[[nodiscard]] inline bool Robot() {
	return Conf::Bool(u"voice_robot"_q, false);
}

inline void SetRobot(bool v) {
	Conf::SetBool(u"voice_robot"_q, v);
}

[[nodiscard]] inline bool PreserveFormants() {
	return Conf::Bool(u"voice_preserve_formants"_q, false);
}

inline void SetPreserveFormants(bool v) {
	Conf::SetBool(u"voice_preserve_formants"_q, v);
}

inline void ApplyPreset(const QString &id) {
	SetPresetId(id);
	if (id == u"custom"_q) {
		return;
	}
	for (const auto &p : Presets()) {
		if (p.id == id) {
			SetPitch(p.pitch);
			SetBass(p.bass);
			SetTreble(p.treble);
			SetTempo(p.tempo);
			SetEcho(p.echo);
			SetRobot(p.robot);
			SetPreserveFormants(p.preserveFormants);
			return;
		}
	}
}

[[nodiscard]] inline QString PresetTitle(const QString &id) {
	for (const auto &p : Presets()) {
		if (p.id == id) {
			return p.title;
		}
	}
	return Presets().front().title;
}

[[nodiscard]] inline QString BuildFilterGraph() {
	const auto pitch = Pitch();
	const auto bass = Bass();
	const auto treble = Treble();
	const auto tempo = Tempo();
	const auto echo = Echo();
	const auto robot = Robot();
	const auto preserveFormants = PreserveFormants();

	QStringList filters;
	if (pitch != 0 || tempo != 100) {
		const auto pitchScale = std::pow(2.0, double(pitch) / 12.0);
		const auto tempoScale = double(tempo) / 100.0;
		filters.append(QString(
			"rubberband=pitch=%1:tempo=%2:formant=%3:pitchq=quality:transients=smooth")
			.arg(pitchScale, 0, 'f', 5)
			.arg(tempoScale, 0, 'f', 3)
			.arg(preserveFormants ? QString("preserved") : QString("shifted")));
	}
	if (robot) {
		filters.append(
			u"highpass=f=350,lowpass=f=2800,"
			"flanger=delay=4:depth=6:regen=50:speed=4,acompressor"_q);
	}
	if (bass != 0) {
		filters.append(QString("bass=g=%1:f=120:w=0.7").arg(bass));
	}
	if (treble != 0) {
		filters.append(QString("treble=g=%1:f=3500:w=0.7").arg(treble));
	}
	if (echo > 0) {
		const auto delay = 35 + (echo * 85) / 100;
		const auto decay = 0.15 + (double(echo) / 100.0) * 0.45;
		filters.append(QString("aecho=0.8:0.88:%1:%2")
			.arg(delay)
			.arg(decay, 0, 'f', 2));
	}
	if (filters.isEmpty()) {
		return QString();
	}
	filters.append(u"alimiter=limit=0.95"_q);
	return filters.join(',');
}

[[nodiscard]] inline crl::time AdjustDuration(crl::time duration) {
	if (!Enabled()) {
		return duration;
	}
	const auto tempo = Tempo();
	if (tempo == 100 || tempo <= 0) {
		return duration;
	}
	return crl::time(std::max<double>(
		100.0,
		double(duration) * 100.0 / double(tempo)));
}

[[nodiscard]] inline QByteArray ProcessVoiceBytes(const QByteArray &input) {
	if (input.isEmpty() || !Enabled()) {
		return input;
	}
	const auto graph = BuildFilterGraph();
	if (graph.isEmpty()) {
		return input;
	}
	QProcess proc;
	proc.start(u"ffmpeg"_q, {
		u"-hide_banner"_q,
		u"-loglevel"_q, u"error"_q,
		u"-f"_q, u"ogg"_q,
		u"-i"_q, u"pipe:0"_q,
		u"-af"_q, graph,
		u"-c:a"_q, u"libopus"_q,
		u"-b:a"_q, u"32k"_q,
		u"-ar"_q, u"48000"_q,
		u"-ac"_q, u"1"_q,
		u"-f"_q, u"ogg"_q,
		u"pipe:1"_q,
	});
	if (!proc.waitForStarted(3000)) {
		return input;
	}
	proc.write(input);
	proc.closeWriteChannel();
	if (!proc.waitForFinished(15000)) {
		proc.kill();
		proc.waitForFinished(1000);
		return input;
	}
	if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
		return input;
	}
	const auto out = proc.readAllStandardOutput();
	return out.isEmpty() ? input : out;
}

}
