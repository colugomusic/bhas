#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bhas {

struct time_info {
	double current_time;
	double input_buffer_adc_time;
	double output_buffer_dac_time;
};

enum class callback_result {
	continue_,
	complete,
	abort,
};

struct device_index    { size_t value; };
struct device_name     { std::string value; };
struct device_name_view{ std::string_view value; };
struct channel_count   { uint32_t value; };
struct cpu_load        { double value; };
struct error           { std::string value; };
struct frame_count     { uint32_t value; };
struct host_index      { size_t value; };
struct host_name       { std::string value; };
struct host_name_view  { std::string_view value; };
struct info            { std::string value; };
struct input_buffer    { float const * const * buffer; };
struct notify          { bool value = false; };
struct output_buffer   { float       * const * buffer; };
struct output_latency  { double value; };
struct sample_rate     { uint32_t value; };
struct stream_time     { double value; };
struct system_rescan   {};
struct warning         { std::string value; };

using log_item = std::variant<error, info, warning>;
using log      = std::vector<log_item>;

struct device_flags {
	enum e {
		input           = 1 << 0,
		output          = 1 << 1,
		wasapi_loopback = 1 << 2,
	};
	int value = 0;
};

struct host_flags {
	enum e {
		asio = 1 << 0,
	};
	int value = 0;
};

struct device {
	device_index index;
	host_index host;
	device_name_view name;
	device_flags flags;
	channel_count num_channels;
	sample_rate default_sample_rate;
};

struct host {
	host_index index;
	host_name_view name;
	host_flags flags;
	std::vector<device_index> devices;
	std::optional<device_index> default_input_device;
	device_index default_output_device;
};

struct system {
	std::vector<device> devices;
	std::vector<host> hosts;
	host_index default_host;
	device_index default_input_device;
	device_index default_output_device;
};

struct stream {
	bhas::channel_count num_input_channels;
	bhas::channel_count num_output_channels;
	bhas::device_index output_device;
	bhas::host_index host;
	bhas::output_latency output_latency;
	bhas::sample_rate sample_rate;
	std::optional<bhas::device_index> input_device;
};

using audio_cb =
	std::function<callback_result(
		bhas::input_buffer input,
		bhas::output_buffer output,
		bhas::frame_count frame_count,
		bhas::sample_rate sample_rate,
		bhas::output_latency output_latency,
		const bhas::time_info* time_info)>;

using report_cb               = std::function<void(bhas::log log)>;
using stream_start_failure_cb = std::function<void()>;
using stream_start_success_cb = std::function<void(bhas::stream stream)>;
using stream_starting_cb      = std::function<void(bhas::stream stream)>;
using stream_stopped_cb       = std::function<void()>;

struct stream_request {
	std::optional<bhas::device_index> input_device;
	bhas::device_index output_device;
	bhas::sample_rate sample_rate;
};

struct user_config {
	bhas::host_name host_name;
	bhas::device_name input_device_name;
	bhas::device_name output_device_name;
	bhas::sample_rate sample_rate;
};

struct callbacks {
	audio_cb audio;
	report_cb report;
	stream_start_failure_cb stream_start_failure;
	stream_start_success_cb stream_start_success;
	stream_starting_cb stream_starting;
	stream_stopped_cb stream_stopped;
};

// Call this before anything else.
// Every callback needs to be set.
// Only the audio callback is called in the audio thread.
// Everything else is called in the main thread.
auto init(callbacks cb) noexcept -> void;

// Call this to shut down the audio system.
// If a stream is currently active, this will block until it has finished.
// The stream_stopped callback will NOT be called.
auto shutdown() noexcept -> void;

// Asynchronously request a stream with the given settings.
// This will return immediately.
// If a stream is currently active, it is stopped automatically and the
// new stream request will be queued until the old one has finished.
auto request_stream(bhas::stream_request request) noexcept -> void;

// Asynchronously stop the stream.
// This will return immediately, but the stream will take some time to stop.
// The stream_stopped callback will be called in the main thread when
// the stream has finished (during the next call to update().)
auto stop_stream() noexcept -> void;

// Keep calling this at regular intervals, in your main thread.
// If there is a pending stream_stopped callback to call, this is
// where that will happen.
// If there is a pending stream request, this is where that will
// be done.
// Otherwise does nothing.
auto update() noexcept -> void;

// Check if the given stream settings are supported by the system.
// If they're not, we will try various fallback mechanisms and return
// the updated settings.
// Information about what we tried is reported via the report callback.
[[nodiscard]] auto check_if_supported_or_try_to_fall_back(bhas::stream_request request) noexcept -> std::optional<bhas::stream_request>;

// Get a reference to the system information. If the system
// has not been scanned for audio devices yet, it will happen
// here automatically.
[[nodiscard]] auto get_system() noexcept -> const bhas::system&;

// Get a reference to the system information.
// Forces a rescan of all available audio devices.
[[nodiscard]] auto get_system(bhas::system_rescan) noexcept -> const bhas::system&;

// Tries to generate a stream_request from the give user_config.
// This searches for devices matching the given names.
[[nodiscard]] auto make_request_from_user_config(const bhas::user_config& config) noexcept -> std::optional<bhas::stream_request>;

// Get the current CPU load.
[[nodiscard]] auto get_cpu_load() noexcept -> cpu_load;

// Get the current stream if there is one.
[[nodiscard]] auto get_current_stream() noexcept -> std::optional<bhas::stream>;

// Get the current stream time.
[[nodiscard]] auto get_stream_time() noexcept -> stream_time;

// Utilities
[[nodiscard]] inline auto is_flag_set(device_flags mask, device_flags::e flag) noexcept -> bool { return (mask.value & flag) == flag; }
[[nodiscard]] inline auto is_flag_set(host_flags mask, host_flags::e flag) noexcept -> bool     { return (mask.value & flag) == flag; }

} // bhas