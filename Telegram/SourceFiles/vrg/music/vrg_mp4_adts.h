#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <vector>

namespace Vrg::Music::Mp4 {
namespace details {

struct Box {
	quint32 type = 0;
	qint64 begin = 0;
	qint64 body = 0;
	qint64 end = 0;
};

[[nodiscard]] constexpr quint32 Fourcc(const char (&s)[5]) {
	return (quint32(uchar(s[0])) << 24)
		| (quint32(uchar(s[1])) << 16)
		| (quint32(uchar(s[2])) << 8)
		| quint32(uchar(s[3]));
}

[[nodiscard]] inline quint32 ReadU32(const QByteArray &d, qint64 i) {
	const auto p = reinterpret_cast<const uchar*>(d.constData()) + i;
	return (quint32(p[0]) << 24)
		| (quint32(p[1]) << 16)
		| (quint32(p[2]) << 8)
		| quint32(p[3]);
}

[[nodiscard]] inline quint64 ReadU64(const QByteArray &d, qint64 i) {
	return (quint64(ReadU32(d, i)) << 32) | quint64(ReadU32(d, i + 4));
}

[[nodiscard]] inline std::vector<Box> ListBoxes(
		const QByteArray &d,
		qint64 start,
		qint64 end) {
	auto result = std::vector<Box>();
	auto i = start;
	while (i + 8 <= end) {
		auto size = qint64(ReadU32(d, i));
		const auto type = ReadU32(d, i + 4);
		auto header = qint64(8);
		if (size == 1) {
			if (i + 16 > end) {
				break;
			}
			size = qint64(ReadU64(d, i + 8));
			header = 16;
		} else if (size == 0) {
			size = end - i;
		}
		if (size < header || i + size > end) {
			break;
		}
		result.push_back({ type, i, i + header, i + size });
		i += size;
	}
	return result;
}

[[nodiscard]] inline bool FindChild(
		const QByteArray &d,
		const Box &parent,
		qint64 skip,
		quint32 type,
		Box &out) {
	for (const auto &box : ListBoxes(d, parent.body + skip, parent.end)) {
		if (box.type == type) {
			out = box;
			return true;
		}
	}
	return false;
}

[[nodiscard]] inline QByteArray AudioSpecificConfig(const QByteArray &d) {
	auto box = Box{ 0, 0, 0, d.size() };
	const quint32 path[] = {
		Fourcc("moov"), Fourcc("trak"), Fourcc("mdia"),
		Fourcc("minf"), Fourcc("stbl"),
	};
	for (const auto type : path) {
		auto next = Box();
		if (!FindChild(d, box, 0, type, next)) {
			return {};
		}
		box = next;
	}
	auto stsd = Box();
	if (!FindChild(d, box, 0, Fourcc("stsd"), stsd)) {
		return {};
	}
	auto mp4a = Box();
	{
		auto found = false;
		for (const auto &entry : ListBoxes(d, stsd.body + 8, stsd.end)) {
			if (entry.type == Fourcc("mp4a")) {
				mp4a = entry;
				found = true;
				break;
			}
		}
		if (!found) {
			return {};
		}
	}
	auto esds = Box();
	if (mp4a.body + 28 > mp4a.end) {
		return {};
	}
	auto foundEsds = false;
	for (const auto &child : ListBoxes(d, mp4a.body + 28, mp4a.end)) {
		if (child.type == Fourcc("esds")) {
			esds = child;
			foundEsds = true;
			break;
		}
	}
	if (!foundEsds) {
		return {};
	}
	auto i = esds.body + 4;
	const auto end = esds.end;
	while (i < end) {
		const auto tag = int(uchar(d[i++]));
		auto length = qint64(0);
		for (auto k = 0; k != 4 && i < end; ++k) {
			const auto c = int(uchar(d[i++]));
			length = (length << 7) | (c & 0x7F);
			if (!(c & 0x80)) {
				break;
			}
		}
		if (tag == 0x05) {
			if (i + length > end || length < 2) {
				return {};
			}
			return d.mid(int(i), int(length));
		} else if (tag == 0x03) {
			i += 3;
		} else if (tag == 0x04) {
			i += 13;
		} else {
			i += length;
		}
	}
	return {};
}

}

[[nodiscard]] inline QByteArray FragmentedMp4ToAdts(
		const QByteArray &d,
		QString *why = nullptr) {
	using namespace details;
	const auto fail = [&](const char *reason) {
		if (why) {
			*why = QString::fromLatin1(reason);
		}
		return QByteArray();
	};
	if (d.size() < 32) {
		return fail("data too short");
	}
	const auto asc = AudioSpecificConfig(d);
	if (asc.size() < 2) {
		return fail("AudioSpecificConfig not found in init segment");
	}
	const auto b0 = int(uchar(asc[0]));
	const auto b1 = int(uchar(asc[1]));
	const auto objectType = b0 >> 3;
	const auto freqIndex = ((b0 & 7) << 1) | (b1 >> 7);
	const auto channels = (b1 >> 3) & 15;
	if (objectType != 2 || freqIndex > 12 || channels < 1 || channels > 7) {
		if (why) {
			*why = QStringLiteral("unsupported audio config: object type %1, freq index %2, channels %3")
				.arg(objectType).arg(freqIndex).arg(channels);
		}
		return QByteArray();
	}

	auto out = QByteArray();
	out.reserve(d.size() + d.size() / 50);
	auto frames = 0;
	for (const auto &moof : ListBoxes(d, 0, d.size())) {
		if (moof.type != Fourcc("moof")) {
			continue;
		}
		for (const auto &traf : ListBoxes(d, moof.body, moof.end)) {
			if (traf.type != Fourcc("traf")) {
				continue;
			}
			auto defaultSize = qint64(0);
			auto base = moof.begin;
			for (const auto &box : ListBoxes(d, traf.body, traf.end)) {
				if (box.type == Fourcc("tfhd")) {
					const auto flags = qint64(ReadU32(d, box.body) & 0xFFFFFF);
					auto q = box.body + 8;
					if (flags & 0x01) {
						base = qint64(ReadU64(d, q));
						q += 8;
					}
					if (flags & 0x02) {
						q += 4;
					}
					if (flags & 0x08) {
						q += 4;
					}
					if (flags & 0x10) {
						defaultSize = qint64(ReadU32(d, q));
						q += 4;
					}
				} else if (box.type == Fourcc("trun")) {
					const auto flags = qint64(ReadU32(d, box.body) & 0xFFFFFF);
					const auto count = qint64(ReadU32(d, box.body + 4));
					auto q = box.body + 8;
					if (!(flags & 0x01)) {
						return fail("trun has no data offset");
					}
					const auto offset = qint64(qint32(ReadU32(d, q)));
					q += 4;
					if (flags & 0x04) {
						q += 4;
					}
					auto pos = base + offset;
					for (auto i = qint64(0); i != count; ++i) {
						if (flags & 0x100) {
							q += 4;
						}
						auto size = defaultSize;
						if (flags & 0x200) {
							size = qint64(ReadU32(d, q));
							q += 4;
						}
						if (flags & 0x400) {
							q += 4;
						}
						if (flags & 0x800) {
							q += 4;
						}
						if (size <= 0
							|| size > 8191 - 7
							|| pos < 0
							|| pos + size > d.size()
							|| q > box.end) {
							return fail("bad sample size or position in trun");
						}
						const auto n = int(size) + 7;
						char h[7];
						h[0] = char(0xFF);
						h[1] = char(0xF1);
						h[2] = char(((objectType - 1) << 6)
							| (freqIndex << 2)
							| (channels >> 2));
						h[3] = char(((channels & 3) << 6) | (n >> 11));
						h[4] = char((n >> 3) & 0xFF);
						h[5] = char(((n & 7) << 5) | 0x1F);
						h[6] = char(0xFC);
						out.append(h, 7);
						out.append(d.constData() + pos, int(size));
						pos += size;
						++frames;
					}
				}
			}
		}
	}
	return frames > 0 ? out : fail("no fragments with audio frames found");
}

}

