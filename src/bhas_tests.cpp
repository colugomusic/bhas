#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "bhas.h"
#include "doctest.h"

static constexpr auto NUM_OUTPUT_CHANNELS = 2;

auto make_empty_report_cb() -> bhas::report_cb {
	return [](bhas::log log) -> void {
	};
}

auto make_single_buffer_audio_cb() -> bhas::audio_cb {
	return [](bhas::input_buffer input, bhas::output_buffer output, bhas::frame_count frame_count, bhas::sample_rate sample_rate, bhas::output_latency output_latency, const bhas::time_info* time_info) -> bhas::callback_result {
		return bhas::callback_result::complete;
	};
}

auto default_report(bhas::error item) -> void { FAIL_CHECK(item.value); }
auto default_report(bhas::info item) -> void { MESSAGE(item.value); }
auto default_report(bhas::warning item) -> void { WARN(item.value.c_str()); }

auto default_report(bhas::log log) -> void {
	for (auto&& item : log) {
		std::visit([](auto&& item) -> void { default_report(std::move(item)); }, std::move(item));
	}
}

auto make_default_audio_cb() -> bhas::audio_cb {
	return [](bhas::input_buffer input, bhas::output_buffer output, bhas::frame_count frame_count, bhas::sample_rate sample_rate, bhas::output_latency output_latency, const bhas::time_info* time_info) -> bhas::callback_result {
		for (uint32_t i = 0; i < frame_count.value; ++i) {
			for (auto j = 0; j < NUM_OUTPUT_CHANNELS; ++j) {
				output.buffer[i][j] = 0.0f;
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

TEST_CASE("default audio stream test") {
	struct {
		std::mutex mutex;
		std::condition_variable cv;
		bool stream_start_failure = false;
		bool stream_start_success = false;
		bool stream_stopped       = false;
	} critical;
	bhas::callbacks cb;
	cb.audio = make_default_audio_cb();
	cb.report = make_default_report_cb();
	cb.stream_starting = [](bhas::stream stream) -> void {
		MESSAGE("stream starting");
	};
	cb.stream_start_failure = [&critical]() -> void {
		std::unique_lock<std::mutex> lock{critical.mutex};
		critical.stream_start_failure = true;
		critical.cv.notify_all();
		MESSAGE("stream failed to start");
	};
	cb.stream_start_success = [&critical](bhas::stream stream) -> void {
		std::unique_lock<std::mutex> lock{critical.mutex};
		critical.stream_start_success = true;
		critical.cv.notify_all();
		MESSAGE("stream started successfully");
	};
	cb.stream_stopped = [&critical]() -> void {
		std::unique_lock<std::mutex> lock{critical.mutex};
		critical.stream_stopped = true;
		critical.cv.notify_all();
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
	bhas::request_stream(request);
	std::unique_lock<std::mutex> lock{critical.mutex};
	critical.cv.wait_for(lock, std::chrono::seconds(5), [&critical]() -> bool { return critical.stream_start_success || critical.stream_start_failure; });
	if (!(critical.stream_start_success || critical.stream_start_failure)) {
		FAIL("Timed out while waiting for the stream to start");
	}
	if (critical.stream_start_failure) {
		lock.unlock();
		bhas::shutdown();
		return;
	}
	lock.unlock();
	bhas::stop_stream();
	lock.lock();
	critical.cv.wait_for(lock, std::chrono::seconds(5), [&critical]() -> bool { return critical.stream_stopped; });
	if (!critical.stream_stopped) {
		FAIL("Timed out while waiting for the stream to stop");
	}
	lock.unlock();
	bhas::shutdown();
}
