#pragma once

#include <any>
#include <cstdint>
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

struct audio_cb {
	using fn_t = auto (*)(
		std::any user,
		bhas::input_buffer input,
		bhas::output_buffer output,
		bhas::frame_count frame_count,
		bhas::sample_rate sample_rate,
		bhas::output_latency output_latency,
		const bhas::time_info* time_info) -> callback_result;
	fn_t fn;
	std::any user;
};

struct cb { std::any user; };

struct stream_start_failure_cb : cb { auto (*fn)(std::any user, bhas::log log) -> void = nullptr; };
struct stream_start_success_cb : cb { auto (*fn)(std::any user, bhas::log log, bhas::stream stream) -> void = nullptr; };
struct stream_stopped_cb : cb       { auto (*fn)(std::any user) -> void = nullptr; };
struct stream_starting_cb : cb      { auto (*fn)(std::any user, bhas::stream stream) -> void = nullptr; };

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
	stream_start_failure_cb stream_start_failure;
	stream_start_success_cb stream_start_success;
	stream_starting_cb stream_starting;
	stream_stopped_cb stream_stopped;
};

[[nodiscard]] inline auto is_flag_set(device_flags mask, device_flags::e flag) -> bool { return (mask.value & flag) == flag; }
[[nodiscard]] inline auto is_flag_set(host_flags mask, host_flags::e flag) -> bool     { return (mask.value & flag) == flag; }

[[nodiscard]] auto check_if_supported_or_try_to_fall_back(bhas::stream_request request, bhas::log* log) -> std::optional<bhas::stream_request>;
[[nodiscard]] auto get_cpu_load() -> cpu_load;
[[nodiscard]] auto get_current_stream() -> std::optional<bhas::stream>;
[[nodiscard]] auto get_stream_time() -> stream_time;
[[nodiscard]] auto get_system() -> const bhas::system&;
[[nodiscard]] auto get_system(bhas::system_rescan) -> const bhas::system&;
[[nodiscard]] auto make_request_from_user_config(const bhas::user_config& config, bhas::log* log) -> std::optional<bhas::stream_request>;
auto init(callbacks cb) -> void;
auto request_stream(bhas::stream_request request, bhas::log* log) -> void;
auto shutdown() -> void;
auto stop_stream(bhas::log* log) -> void;
auto update(bhas::log* log) -> void;

} // bhas