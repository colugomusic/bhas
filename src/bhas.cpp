#include "bhas.h"
#include "bhas_api.h"
#include <condition_variable>
#include <fmt/format.h>
#include <mutex>

namespace bhas {

struct Callbacks {
	bhas::stream_starting_cb stream_starting;
	bhas::stream_stopped_cb stream_stopped;
	bhas::stream_start_failure_cb stream_start_failure;
	bhas::stream_start_success_cb stream_start_success;
};

struct Critical {
	std::mutex mutex;
	bhas::stream_stopped_cb stream_stopped_cb;
};

struct Model {
	Callbacks cb;
	Critical critical;
	std::optional<bhas::stream_request> pending_stream_request;
	std::optional<bhas::system> system;
	std::optional<bhas::stream> current_stream;
};

static Model model;

[[nodiscard]] static
auto info__couldnt_find_user_input_device(bhas::device_name name) -> bhas::info {
	return {fmt::format("Couldnt' find your saved input device: '{}' so I'm going to try to fall back to the host default.", name.value)};
}

[[nodiscard]] static
auto info__couldnt_find_user_output_device(bhas::device_name name) -> bhas::info {
	return {fmt::format("Couldnt' find your saved output device: '{}' so I'm going to try to fall back to the host default.", name.value)};
}

[[nodiscard]] static
auto info__couldnt_find_user_host(bhas::host_name name) -> bhas::info {
	return {fmt::format("Couldnt' find your saved device host: '{}' so I'm going to try to fall back to the system defaults.", name.value)};
}

template <typename Container, typename PredFn> [[nodiscard]] static
auto find_item(const Container& container, PredFn&& pred) -> std::optional<size_t> {
	const auto pos = std::find_if(std::cbegin(container), std::cend(container), pred);
	if (pos == std::cend(container)) {
		return std::nullopt;
	}
	return static_cast<size_t>(std::distance(std::cbegin(container), pos));
}

[[nodiscard]]
auto find_host(const bhas::system& system, const host_name& name) -> std::optional<host_index> {
	const auto match = [&name](const bhas::host& host) { return host.name.value == name.value; };
	if (const auto index = find_item(system.hosts, match)) {
		return host_index{*index};
	}
	return std::nullopt;
}

[[nodiscard]]
auto find_input_device(const bhas::system& system, const device_name& name) -> std::optional<device_index> {
	const auto match = [&name](const bhas::device& device) {
		return is_flag_set(device.flags, device_flags::input) && device.name.value == name.value;
	};
	if (const auto index = find_item(system.devices, match)) {
		return device_index{*index};
	}
	return std::nullopt;
}

[[nodiscard]]
auto find_input_device(const bhas::system& system, host_index host, const device_name& name) -> std::optional<device_index> {
	const auto match = [host, &name](const bhas::device& device) {
		return is_flag_set(device.flags, device_flags::input)
			&& device.name.value == name.value
			&& device.host.value == host.value;
	};
	if (const auto index = find_item(system.devices, match)) {
		return device_index{*index};
	}
	return std::nullopt;
}

[[nodiscard]]
auto find_output_device(const bhas::system& system, const device_name& name) -> std::optional<device_index> {
	const auto match = [&name](const bhas::device& device) {
		return is_flag_set(device.flags, device_flags::output) && device.name.value == name.value;
	};
	if (const auto index = find_item(system.devices, match)) {
		return device_index{*index};
	}
	return std::nullopt;
}

[[nodiscard]]
auto find_output_device(const bhas::system& system, host_index host, const device_name& name) -> std::optional<device_index> {
	const auto match = [host, &name](const bhas::device& device) {
		return is_flag_set(device.flags, device_flags::output)
			&& device.name.value == name.value
			&& device.host.value == host.value;
	};
	if (const auto index = find_item(system.devices, match)) {
		return device_index{*index};
	}
	return std::nullopt;
}

[[nodiscard]] static
auto make_stream_stopped_cb() -> bhas::stream_stopped_cb {
	bhas::stream_stopped_cb cb;
	cb.fn = [](std::any user) -> void {
		std::unique_lock<std::mutex> lock{model.critical.mutex};
		auto stopped_cb = model.critical.stream_stopped_cb;
		model.critical.stream_stopped_cb = {};
		lock.unlock();
		if (stopped_cb.fn) {
			stopped_cb.fn(stopped_cb.user);
		}
	};
	return cb;
};

static
auto stop_stream_and_request_a_new_one(bhas::stream_request request, bhas::log* log) -> void {
	model.pending_stream_request = request;
	stop_stream(log);
}

auto get_cpu_load() -> cpu_load {
	return api::get_cpu_load();
}

auto get_current_stream() -> std::optional<bhas::stream> {
	return model.current_stream;
}

auto get_stream_time() -> stream_time {
	return api::get_stream_time();
}

auto get_system() -> const bhas::system& {
	if (!model.system) {
		model.system = api::rescan();
	}
	return *model.system;
}

auto get_system(bhas::system_rescan) -> const bhas::system& {
	model.system = api::rescan();
	return *model.system;
}

auto init(callbacks cb) -> void {
	api::init();
	api::set(make_stream_stopped_cb());
	api::set(std::move(cb.audio));
	model.cb.stream_starting      = std::move(cb.stream_starting);
	model.cb.stream_stopped       = std::move(cb.stream_stopped);
	model.cb.stream_start_failure = std::move(cb.stream_start_failure);
	model.cb.stream_start_success = std::move(cb.stream_start_success);
}

auto request_stream(bhas::stream_request request, bhas::log* log) -> void {
	if (model.current_stream) {
		stop_stream_and_request_a_new_one(request, log);
		return;
	}
	const auto& system = get_system();
	bhas::stream stream;
	bhas::log local_log;
	if (!api::open_stream(request, &local_log, &stream.num_input_channels)) {
		model.cb.stream_start_failure.fn(model.cb.stream_start_failure.user, std::move(local_log));
		return;
	}
	stream.host                = system.devices.at(request.output_device.value).host;
	stream.input_device        = request.input_device;
	stream.num_output_channels = {2};
	stream.output_device       = request.output_device;
	stream.output_latency      = api::get_output_latency();
	stream.sample_rate         = request.sample_rate;
	model.current_stream       = stream;
	model.cb.stream_starting.fn(model.cb.stream_starting.user, stream);
	if (!api::start_stream(&local_log)) {
		model.cb.stream_start_failure.fn(model.cb.stream_start_failure.user, std::move(local_log));
		return;
	}
	model.cb.stream_start_success.fn(model.cb.stream_start_success.user, std::move(local_log), std::move(stream));
}

auto stop_stream(bhas::log* log) -> void {
	api::stop_stream(log);
}

// Stop current stream and block until finished
auto shutdown() -> void {
	if (!model.current_stream) {
		api::shutdown();
		return;
	}
	struct stop_info {
		bool done = false;
		std::mutex mutex;
		std::condition_variable cv;
	} info;
	bhas::stream_stopped_cb cb;
	cb.user = &info;
	cb.fn   = [](std::any user) {
		const auto info = std::any_cast<stop_info*>(user);
		std::unique_lock<std::mutex> lock{info->mutex};
		info->done = true;
		info->cv.notify_all();
	};
	std::unique_lock<std::mutex> lock{info.mutex};
	model.critical.stream_stopped_cb = std::move(cb);
	lock.unlock();
	api::stop_stream(nullptr);
	lock.lock();
	info.cv.wait(lock, [&] { return info.done; });
	api::shutdown();
}

auto update(bhas::log* log) -> void {
	if (!api::is_stream_active()) {
		// If the stream was just stopped, call the stream_stopped callbacks
		std::unique_lock<std::mutex> lock{model.critical.mutex};
		auto stopped_cb = model.critical.stream_stopped_cb;
		model.critical.stream_stopped_cb = {};
		lock.unlock();
		if (stopped_cb.fn) {
			stopped_cb.fn(stopped_cb.user);
		}
		// Close the stream if it's not already closed
		api::close_stream();
		model.current_stream = std::nullopt;
		if (model.pending_stream_request) {
			// If another stream request is pending, request the stream
			request_stream(*model.pending_stream_request, log);
			model.pending_stream_request = std::nullopt;
		}
	}
}

auto check_if_supported_or_try_to_fall_back(bhas::stream_request request, bhas::log* log) -> std::optional<bhas::stream_request> {
	return api::check_if_supported_or_try_to_fall_back(request, log);
}

auto make_request_from_user_config(const bhas::user_config& config, bhas::log* log) -> std::optional<bhas::stream_request> {
	const auto& system = get_system();
	const auto user_host_index = bhas::find_host(system, config.host_name);
	bhas::stream_request request;
	if (!user_host_index) {
		log->push_back(info__couldnt_find_user_host(config.host_name));
		request.input_device  = system.default_input_device;
		request.output_device = system.default_output_device;
		request.sample_rate   = system.devices.at(request.output_device.value).default_sample_rate;
		return request;
	}
	const auto user_host                = system.hosts.at(user_host_index->value);
	const auto user_input_device_index  = bhas::find_input_device(system, *user_host_index, config.input_device_name);
	const auto user_output_device_index = bhas::find_output_device(system, *user_host_index, config.output_device_name);
	if (!user_input_device_index) {
		log->push_back(info__couldnt_find_user_input_device(config.input_device_name));
		request.input_device = user_host.default_input_device;
	}
	else {
		request.input_device = *user_input_device_index;
	}
	if (!user_output_device_index) {
		log->push_back(info__couldnt_find_user_output_device(config.output_device_name));
		request.output_device = user_host.default_output_device;
	}
	else {
		request.output_device = *user_output_device_index;
	}
	request.sample_rate = config.sample_rate;
	return check_if_supported_or_try_to_fall_back(request, log);
}

} // bhas