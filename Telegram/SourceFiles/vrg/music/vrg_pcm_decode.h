#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <cstring>
#include <memory>

namespace Vrg::Music::Pcm {

namespace details {

struct MemoryReader {
	const uint8_t *data = nullptr;
	int64_t size = 0;
	int64_t pos = 0;
};

inline int ReadPacket(void *opaque, uint8_t *buf, int size) {
	auto *r = static_cast<MemoryReader*>(opaque);
	const auto left = r->size - r->pos;
	if (left <= 0) {
		return AVERROR_EOF;
	}
	const auto count = int(std::min<int64_t>(left, size));
	std::memcpy(buf, r->data + r->pos, size_t(count));
	r->pos += count;
	return count;
}

inline int64_t SeekPacket(void *opaque, int64_t offset, int whence) {
	auto *r = static_cast<MemoryReader*>(opaque);
	if (whence & AVSEEK_SIZE) {
		return r->size;
	}
	whence &= ~AVSEEK_FORCE;
	auto target = int64_t(0);
	if (whence == SEEK_SET) {
		target = offset;
	} else if (whence == SEEK_CUR) {
		target = r->pos + offset;
	} else if (whence == SEEK_END) {
		target = r->size + offset;
	} else {
		return -1;
	}
	if (target < 0 || target > r->size) {
		return -1;
	}
	r->pos = target;
	return target;
}

inline void AppendLe16(QByteArray &out, quint16 v) {
	out.append(char(v & 0xFF));
	out.append(char((v >> 8) & 0xFF));
}

inline void AppendLe32(QByteArray &out, quint32 v) {
	out.append(char(v & 0xFF));
	out.append(char((v >> 8) & 0xFF));
	out.append(char((v >> 16) & 0xFF));
	out.append(char((v >> 24) & 0xFF));
}

}

constexpr auto kSampleRate = 44100;
constexpr auto kChannels = 2;

[[nodiscard]] inline QByteArray DecodeToWav(
		const QByteArray &input,
		QString *why = nullptr) {
	const auto fail = [&](const QString &text) {
		if (why) {
			*why = text;
		}
		return QByteArray();
	};
	if (input.isEmpty()) {
		return fail(QStringLiteral("empty input"));
	}

	auto reader = details::MemoryReader{
		reinterpret_cast<const uint8_t*>(input.constData()),
		int64_t(input.size()),
		0
	};
	const auto ioSize = 32768;
	auto *ioBuffer = static_cast<uint8_t*>(av_malloc(ioSize));
	if (!ioBuffer) {
		return fail(QStringLiteral("alloc io"));
	}
	auto *io = avio_alloc_context(
		ioBuffer,
		ioSize,
		0,
		&reader,
		&details::ReadPacket,
		nullptr,
		&details::SeekPacket);
	if (!io) {
		av_free(ioBuffer);
		return fail(QStringLiteral("avio_alloc_context"));
	}

	auto *format = avformat_alloc_context();
	if (!format) {
		av_freep(&io->buffer);
		avio_context_free(&io);
		return fail(QStringLiteral("avformat_alloc_context"));
	}
	format->pb = io;
	format->flags |= AVFMT_FLAG_CUSTOM_IO;

	auto result = QByteArray();
	AVCodecContext *codec = nullptr;
	SwrContext *swr = nullptr;
	AVPacket *packet = nullptr;
	AVFrame *frame = nullptr;
	auto reason = QString();

	do {
		if (avformat_open_input(&format, nullptr, nullptr, nullptr) < 0) {
			reason = QStringLiteral("avformat_open_input");
			break;
		}
		if (avformat_find_stream_info(format, nullptr) < 0) {
			reason = QStringLiteral("avformat_find_stream_info");
			break;
		}
		const auto streamIndex = av_find_best_stream(
			format,
			AVMEDIA_TYPE_AUDIO,
			-1,
			-1,
			nullptr,
			0);
		if (streamIndex < 0) {
			reason = QStringLiteral("no audio stream");
			break;
		}
		auto *stream = format->streams[streamIndex];
		const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!decoder) {
			reason = QStringLiteral("decoder not found (codec %1)")
				.arg(int(stream->codecpar->codec_id));
			break;
		}
		codec = avcodec_alloc_context3(decoder);
		if (!codec
			|| avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
			reason = QStringLiteral("codec context");
			break;
		}
		if (avcodec_open2(codec, decoder, nullptr) < 0) {
			reason = QStringLiteral("avcodec_open2");
			break;
		}

		auto outLayout = AVChannelLayout();
		av_channel_layout_default(&outLayout, kChannels);

		packet = av_packet_alloc();
		frame = av_frame_alloc();
		if (!packet || !frame) {
			av_channel_layout_uninit(&outLayout);
			reason = QStringLiteral("alloc packet/frame");
			break;
		}

		auto pcm = QByteArray();
		pcm.reserve(int(std::min<int64_t>(
			int64_t(input.size()) * 12,
			int64_t(400) * 1024 * 1024)));

		const auto convert = [&](AVFrame *source) {
			if (!swr) {
				if (swr_alloc_set_opts2(
						&swr,
						&outLayout,
						AV_SAMPLE_FMT_S16,
						kSampleRate,
						&source->ch_layout,
						AVSampleFormat(source->format),
						source->sample_rate,
						0,
						nullptr) < 0
					|| !swr
					|| swr_init(swr) < 0) {
					return false;
				}
			}
			const auto capacity = int(swr_get_delay(swr, source->sample_rate)
				* kSampleRate / source->sample_rate)
				+ int(int64_t(source->nb_samples) * kSampleRate
					/ source->sample_rate)
				+ 32;
			const auto bytesPerFrame = kChannels * 2;
			const auto old = pcm.size();
			pcm.resize(old + capacity * bytesPerFrame);
			auto *outPtr = reinterpret_cast<uint8_t*>(pcm.data() + old);
			const auto produced = swr_convert(
				swr,
				&outPtr,
				capacity,
				const_cast<const uint8_t**>(source->extended_data),
				source->nb_samples);
			if (produced < 0) {
				pcm.resize(old);
				return false;
			}
			pcm.resize(old + produced * bytesPerFrame);
			return true;
		};

		auto convertFailed = false;
		const auto drain = [&] {
			while (true) {
				const auto got = avcodec_receive_frame(codec, frame);
				if (got < 0) {
					break;
				}
				if (!convert(frame)) {
					convertFailed = true;
					av_frame_unref(frame);
					break;
				}
				av_frame_unref(frame);
			}
		};

		while (!convertFailed && av_read_frame(format, packet) >= 0) {
			if (packet->stream_index == streamIndex) {
				if (avcodec_send_packet(codec, packet) >= 0) {
					drain();
				}
			}
			av_packet_unref(packet);
		}
		if (!convertFailed) {
			avcodec_send_packet(codec, nullptr);
			drain();
		}
		av_channel_layout_uninit(&outLayout);
		if (convertFailed) {
			reason = QStringLiteral("resample failed");
			break;
		}
		if (pcm.isEmpty()) {
			reason = QStringLiteral("no samples decoded");
			break;
		}

		const auto dataSize = quint32(pcm.size());
		result.reserve(int(dataSize) + 44);
		result.append("RIFF", 4);
		details::AppendLe32(result, 36 + dataSize);
		result.append("WAVEfmt ", 8);
		details::AppendLe32(result, 16);
		details::AppendLe16(result, 1);
		details::AppendLe16(result, kChannels);
		details::AppendLe32(result, kSampleRate);
		details::AppendLe32(result, kSampleRate * kChannels * 2);
		details::AppendLe16(result, kChannels * 2);
		details::AppendLe16(result, 16);
		result.append("data", 4);
		details::AppendLe32(result, dataSize);
		result.append(pcm);
	} while (false);

	if (swr) {
		swr_free(&swr);
	}
	if (frame) {
		av_frame_free(&frame);
	}
	if (packet) {
		av_packet_free(&packet);
	}
	if (codec) {
		avcodec_free_context(&codec);
	}
	if (format) {
		avformat_close_input(&format);
	}
	av_freep(&io->buffer);
	avio_context_free(&io);

	if (result.isEmpty() && why) {
		*why = reason;
	}
	return result;
}

}

