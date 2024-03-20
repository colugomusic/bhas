#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "bhas.h"
#include "doctest.h"
#include <condition_variable>
#include <mutex>
#include <thread>

static constexpr auto NUM_OUTPUT_CHANNELS  = 2;
static constexpr auto START_STREAM_TIMEOUT = std::chrono::seconds(5);
static constexpr auto STOP_STREAM_TIMEOUT  = std::chrono::seconds(5);
static constexpr auto WAIT_TIME            = std::chrono::milliseconds(100);

auto default_report(bhas::error item) -> void   { FAIL_CHECK(item.value); }
auto default_report(bhas::info item) -> void    { MESSAGE(item.value); }
auto default_report(bhas::warning item) -> void { WARN(item.value.c_str()); }

auto default_report(bhas::log_item item) -> void {
	std::visit([](auto&& item) -> void { default_report(std::move(item)); }, std::move(item));
}

auto default_report(bhas::log log) -> void {
	for (auto&& item : log) { default_report(std::move(item)); }
}

auto make_default_audio_cb() -> bhas::audio_cb {
	return [](bhas::input_buffer input, bhas::output_buffer output, bhas::frame_count frame_count, bhas::sample_rate sample_rate, bhas::output_latency output_latency, const bhas::time_info* time_info) -> bhas::callback_result {
		for (uint32_t i = 0; i < frame_count.value; ++i) {
			for (auto j = 0; j < NUM_OUTPUT_CHANNELS; ++j) {
				output.buffer[j][i] = 0.0f;
			}
		}
		return bhas::callback_result::complete;
	};
}

auto make_default_report_cb() -> bhas::report_cb {
	return [](bhas::log log) -> void {
		default_report(std::move(log));
	};
}

struct Tracking {
	int stream_start_fail_count    = 0;
	int stream_start_success_count = 0;
	int stream_stop_count          = 0;
};

struct Critical {
	std::mutex mutex;
	Tracking tracking;
};

auto try_to_open_stream(bhas::stream_request request, Critical* critical) -> bool {
	std::unique_lock<std::mutex> lock{critical->mutex};
	auto old_state = critical->tracking;
	lock.unlock();
	bhas::request_stream(request);
	const auto start_time = std::chrono::system_clock::now();
	for (;;) {
		bhas::update();
		lock.lock();
		if (critical->tracking.stream_start_success_count > old_state.stream_start_success_count) {
			return true;
		}
		if (critical->tracking.stream_start_fail_count > old_state.stream_start_fail_count) {
			return false;
		}
		lock.unlock();
		const auto now = std::chrono::system_clock::now();
		if (now - start_time > START_STREAM_TIMEOUT) {
			FAIL("Timed out while waiting for the stream to start");
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

auto try_to_stop_stream(Critical* critical) -> bool {
	std::unique_lock<std::mutex> lock{critical->mutex};
	auto old_state = critical->tracking;
	lock.unlock();
	bhas::stop_stream();
	const auto start_time = std::chrono::system_clock::now();
	for (;;) {
		bhas::update();
		lock.lock();
		if (critical->tracking.stream_stop_count > old_state.stream_stop_count) {
			return true;
		}
		lock.unlock();
		const auto now = std::chrono::system_clock::now();
		if (now - start_time > STOP_STREAM_TIMEOUT) {
			FAIL("Timed out while waiting for the stream to stop");
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

TEST_CASE("start and stop the system default audio stream") {
	Critical critical;
	bhas::callbacks cb;
	cb.audio = make_default_audio_cb();
	cb.report = make_default_report_cb();
	cb.stream_starting = [](bhas::stream stream) -> void {
		MESSAGE("stream starting");
	};
	cb.stream_start_failure = [&critical]() -> void {
		std::unique_lock<std::mutex> lock{critical.mutex};
		critical.tracking.stream_start_fail_count++;
		MESSAGE("stream failed to start");
	};
	cb.stream_start_success = [&critical](bhas::stream stream) -> void {
		std::unique_lock<std::mutex> lock{critical.mutex};
		critical.tracking.stream_start_success_count++;
		MESSAGE("stream started successfully");
	};
	cb.stream_stopped = [&critical]() -> void {
		std::unique_lock<std::mutex> lock{critical.mutex};
		critical.tracking.stream_stop_count++;
		MESSAGE("stream stopped");
	};
	if (!bhas::init(std::move(cb))) {
		FAIL_CHECK("failed to initialize");
		return;
	}
	const auto& system = bhas::get_system();
	bhas::stream_request request;
	request.input_device  = system.default_input_device;
	request.output_device = system.default_output_device;
	request.sample_rate   = system.devices.at(request.output_device.value).default_sample_rate;
	if (!try_to_open_stream(request, &critical)) {
		FAIL_CHECK("failed to start an audio stream with the default settings");
		bhas::shutdown();
		return;
	}
	SUBCASE("try switching the sample rate a few times") {
		const bhas::sample_rate sample_rates[] = {22050, 44100, 48000, 96000};
		for (auto SR : sample_rates) {
			request.sample_rate = SR;
			if (!try_to_open_stream(request, &critical)) {
				FAIL_CHECK("failed to switch sample rate");
			}
		}
	}
	if (!try_to_stop_stream(&critical)) {
		FAIL("failed to stop the audio stream");
	}
	bhas::shutdown();
}
