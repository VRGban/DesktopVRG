/*
This file is part of VRG, a fork of Telegram Desktop.
Licensed under GPLv3 with the same exceptions as the upstream project.
*/
#pragma once

#include "vrg/vrg_conf.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

#include <atomic>
#include <chrono>
#include <thread>

#ifdef Q_OS_UNIX
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace Vrg {

class Stall final {
public:
	static void Start() {
#ifdef Q_OS_UNIX
		if (!Conf::Bool("stall_log", false) || Started()) {
			return;
		}
		Started() = true;
		const auto path = (QDir::homePath() + "/.config/vrg/stall.log").toUtf8();
		LogFd() = ::open(path.constData(), O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (LogFd() < 0) {
			return;
		}
		MainThread() = pthread_self();

		void *warm[4];
		backtrace(warm, 4);

		struct sigaction action = {};
		action.sa_handler = &Stall::Handler;
		sigemptyset(&action.sa_mask);
		action.sa_flags = SA_RESTART;
		sigaction(SIGUSR2, &action, nullptr);

		Beat().store(Now());
		const auto timer = new QTimer(qApp);
		timer->setTimerType(Qt::PreciseTimer);
		timer->setInterval(8);
		QObject::connect(timer, &QTimer::timeout, [] {
			Beat().store(Now());
		});
		timer->start();

		std::thread([] {
			constexpr auto kLimitMs = 70;
			auto reportedAt = qint64(0);
			auto worst = qint64(0);
			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				const auto now = Now();
				const auto gap = now - Beat().load();
				if (gap >= kLimitMs) {
					worst = std::max(worst, gap);
					if (now - reportedAt >= 60) {
						reportedAt = now;
						Say("\n=== main thread busy for %lld ms so far\n", gap);
						pthread_kill(MainThread(), SIGUSR2);
					}
				} else if (worst > 0) {
					Say("=== stall ended, worst %lld ms\n", worst);
					worst = 0;
					reportedAt = 0;
				}
			}
		}).detach();
#endif
	}

private:
#ifdef Q_OS_UNIX
	[[nodiscard]] static qint64 Now() {
		using namespace std::chrono;
		return duration_cast<milliseconds>(
			steady_clock::now().time_since_epoch()).count();
	}
	static std::atomic<qint64> &Beat() {
		static std::atomic<qint64> value = 0;
		return value;
	}
	static bool &Started() {
		static auto value = false;
		return value;
	}
	static int &LogFd() {
		static auto fd = -1;
		return fd;
	}
	static pthread_t &MainThread() {
		static pthread_t thread;
		return thread;
	}
	static void Say(const char *format, qint64 value) {
		char line[128];
		const auto size = snprintf(line, sizeof(line), format, (long long)value);
		if (size > 0) {
			(void)!::write(LogFd(), line, size_t(size));
		}
	}
	static void Handler(int) {
		void *frames[40];
		const auto count = backtrace(frames, 40);
		backtrace_symbols_fd(frames, count, LogFd());
	}
#endif
};

}

