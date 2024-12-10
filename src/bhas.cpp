#include "bhas.h"
#include "bhas_api.h"
#include <condition_variable>
#include <format>
#include <mutex>

namespace bhas {
namespace impl {

struct Callbacks {
	bhas::report_cb report;
	bhas::stream_starting_cb stream_starting;
	bhas::stream_stopped_cb stream_stopped;
	bhas::stream_start_failure_cb stream_start_failure;
	bhas::stream_start_success_cb stream_start_success;
};

struct Critical {
	std::mutex mutex;
	bhas::stream_stopped_cb stream_stopped_cb;
	bool just_stopped = false;
};

struct Model {
	Callbacks cb;
	Critical critical;
	std::optional<bhas::stream_request> pending_stream_request;
	std::optional<bhas::system> system;
	std::optional<bhas::stream> current_stream;
	bool init = false;
};

static Model model;

struct function_name { const char* value; };

[[nodiscard]] static
auto err_exception_caught(function_name func_name, const char* what) -> bhas::error {
	return {std::format("Caught exception in {}: {}", func_name.value, what)};
}

[[nodiscard]] static
auto err_exception_caught(function_name func_name) -> bhas::error {
	return {std::format("Caught an unknown exception in {}", func_name.value)};
}

[[nodiscard]] static
auto info__couldnt_find_user_input_device(bhas::device_name name) -> bhas::info {
	return {std::format("Couldnt' find your saved input device: '{}' so I'm going to try to fall back to the host default.", name.value)};
}

[[nodiscard]] static
auto info__couldnt_find_user_output_device(bhas::device_name name) -> bhas::info {
	return {std::format("Couldnt' find your saved output device: '{}' so I'm going to try to fall back to the host default.", name.value)};
}

[[nodiscard]] static
auto info__no_default_output_device() -> bhas::info {
	return {"There isn't one!"};
}

[[nodiscard]] static
auto info__couldnt_find_user_host(bhas::host_name name) -> bhas::info {
	return {std::format("Couldnt' find your saved device host: '{}' so I'm going to try to fall back to the system defaults.", name.value)};
}

[[nodiscard]] static
auto info_requesting_stream(const bhas::stream_request& request) -> bhas::info {
	const auto& system            = get_system();
	const auto input_device_name  = request.input_device ? system.devices.at(request.input_device->value).name : device_name_view{"none"};
	const auto output_device_name = system.devices.at(request.output_device.value).name;
	return {
		std::format(
			"Requesting stream: input_device: {}, output_device: {}, sample_rate: {}",
			input_device_name.value,
			output_device_name.value,
			request.sample_rate.value)};
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
	return []() -> void {
		std::unique_lock<std::mutex> lock{model.critical.mutex};
		auto stopped_cb = model.critical.stream_stopped_cb;
		model.critical.stream_stopped_cb = {};
		model.critical.just_stopped = true;
		lock.unlock();
		if (stopped_cb) {
			stopped_cb();
		}
	};
};

static
auto stop_stream_and_request_a_new_one(bhas::stream_request request) -> void {
	model.pending_stream_request = request;
	stop_stream();
}

[[nodiscard]] static
auto get_cpu_load() -> cpu_load {
	return api::get_cpu_load();
}

[[nodiscard]] static
auto get_current_stream() -> std::optional<bhas::stream> {
	return model.current_stream;
}

[[nodiscard]] static
auto get_stream_time() -> stream_time {
	return api::get_stream_time();
}

[[nodiscard]] static
auto get_system() -> const bhas::system& {
	if (!model.system) {
		model.system = api::rescan();
	}
	return *model.system;
}

[[nodiscard]] static
auto get_system(bhas::system_rescan) -> const bhas::system& {
	model.system = api::rescan();
	return *model.system;
}

[[nodiscard]] static
auto did_stream_just_stop() -> bool {
	if (!model.init) {
		return false;
	}
	auto lock = std::unique_lock<std::mutex>(model.critical.mutex);
	return model.critical.just_stopped;
}

static
auto init(callbacks cb) -> bool {
	model.cb.report = std::move(cb.report);
	bhas::log log;
	if (!api::init(&log)) {
		model.cb.report(std::move(log));
		return false;
	}
	api::set(make_stream_stopped_cb());
	api::set(std::move(cb.audio));
	model.cb.stream_starting      = std::move(cb.stream_starting);
	model.cb.stream_stopped       = std::move(cb.stream_stopped);
	model.cb.stream_start_failure = std::move(cb.stream_start_failure);
	model.cb.stream_start_success = std::move(cb.stream_start_success);
	model.init = true;
	return true;
}

static
auto request_stream(bhas::stream_request request) -> void {
	if (model.current_stream) {
		stop_stream_and_request_a_new_one(request);
		return;
	}
	const auto& system = get_system();
	bhas::stream stream;
	bhas::log log;
	log.push_back(info_requesting_stream(request));
	if (!api::open_stream(request, &log, &stream.num_input_channels)) {
		model.cb.report(std::move(log));
		model.cb.stream_start_failure();
		return;
	}
	stream.host                = system.devices.at(request.output_device.value).host;
	stream.input_device        = request.input_device;
	stream.num_output_channels = {2};
	stream.output_device       = request.output_device;
	stream.output_latency      = api::get_output_latency();
	stream.sample_rate         = request.sample_rate;
	model.current_stream       = stream;
	model.cb.stream_starting(stream);
	if (!api::start_stream(&log)) {
		model.cb.report(std::move(log));
		model.cb.stream_start_failure();
		return;
	}
	model.cb.report(std::move(log));
	model.cb.stream_start_success(std::move(stream));
}

static
auto stop_stream() -> void {
	std::unique_lock<std::mutex> lock{model.critical.mutex};
	model.critical.stream_stopped_cb = model.cb.stream_stopped;
	lock.unlock();
	bhas::log log;
	api::stop_stream(&log);
	model.cb.report(std::move(log));
}

static
auto shutdown() -> void {
	if (!model.current_stream) {
		api::shutdown();
		return;
	}
	if (!api::is_stream_active()) {
		api::shutdown();
		return;
	}
	struct stop_info {
		bool done = false;
		std::mutex mutex;
		std::condition_variable cv;
	} info;
	bhas::stream_stopped_cb cb;
	cb = [&info]() {
		std::unique_lock<std::mutex> lock{info.mutex};
		info.done = true;
		info.cv.notify_all();
	};
	std::unique_lock<std::mutex> lock{info.mutex};
	model.critical.stream_stopped_cb = std::move(cb);
	lock.unlock();
	api::stop_stream(nullptr);
	lock.lock();
	info.cv.wait(lock, [&] { return info.done; });
	api::shutdown();
}

static
auto update() -> void {
	if (!model.current_stream) {
		return;
	}
	std::unique_lock<std::mutex> lock{model.critical.mutex};
	if (model.critical.just_stopped) {
		model.critical.just_stopped = false;
		// If the stream was just stopped, call the stream_stopped callbacks
		auto stopped_cb = model.critical.stream_stopped_cb;
		model.critical.stream_stopped_cb = {};
		lock.unlock();
		if (stopped_cb) {
			stopped_cb();
		}
		// Close the stream
		api::close_stream();
		model.current_stream = std::nullopt;
		if (model.pending_stream_request) {
			// If another stream request is pending, request the stream
			impl::request_stream(*model.pending_stream_request);
			model.pending_stream_request = std::nullopt;
		}
	}
}

[[nodiscard]] static
auto check_if_supported_or_try_to_fall_back(bhas::stream_request request) -> std::optional<bhas::stream_request> {
	bhas::log log;
	auto supported_request = api::check_if_supported_or_try_to_fall_back(request, &log);
	model.cb.report(std::move(log));
	return supported_request;
}

[[nodiscard]] static
auto make_request_from_user_config(const bhas::user_config& config) -> std::optional<bhas::stream_request> {
	const auto& system = get_system();
	const auto user_host_index = find_host(system, config.host_name);
	bhas::stream_request request;
	bhas::log log;
	if (!user_host_index) {
		log.push_back(info__couldnt_find_user_host(config.host_name));
		request.input_device  = system.default_input_device;
		request.output_device = system.default_output_device;
		request.sample_rate   = system.devices.at(request.output_device.value).default_sample_rate;
		model.cb.report(std::move(log));
		return request;
	}
	const auto user_host                = system.hosts.at(user_host_index->value);
	const auto user_input_device_index  = find_input_device(system, *user_host_index, config.input_device_name);
	const auto user_output_device_index = find_output_device(system, *user_host_index, config.output_device_name);
	if (!user_input_device_index) {
		log.push_back(info__couldnt_find_user_input_device(config.input_device_name));
		request.input_device = user_host.default_input_device;
	}
	else {
		request.input_device = *user_input_device_index;
	}
	if (!user_output_device_index) {
		log.push_back(info__couldnt_find_user_output_device(config.output_device_name));
		if (!user_host.default_output_device) {
			log.push_back(info__no_default_output_device());
			return std::nullopt;
		}
		request.output_device = *user_host.default_output_device;
	}
	else {
		request.output_device = *user_output_device_index;
	}
	request.sample_rate = config.sample_rate;
	model.cb.report(std::move(log));
	return bhas::check_if_supported_or_try_to_fall_back(request);
}

} // impl

auto get_cpu_load() -> cpu_load {
	try {
		return impl::get_cpu_load();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return {0};
}

auto get_current_stream() -> std::optional<bhas::stream> {
	try {
		return impl::get_current_stream();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return std::nullopt;
}

auto get_stream_time() -> stream_time {
	try {
		return impl::get_stream_time();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return {0};
}

auto get_system() -> const bhas::system& {
	try {
		return impl::get_system();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	static const auto NULL_SYSTEM = bhas::system{};
	return NULL_SYSTEM;
}

auto get_system(bhas::system_rescan) -> const bhas::system& {
	try {
		return impl::get_system(bhas::system_rescan{});
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	static const auto NULL_SYSTEM = bhas::system{};
	return NULL_SYSTEM;
}

auto did_stream_just_stop() -> bool {
	try {
		return impl::did_stream_just_stop();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return false;
}

auto init(callbacks cb) -> bool {
	try {
		impl::init(std::move(cb));
		return true;
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return false;
}

auto request_stream(bhas::stream_request request) -> void {
	try {
		impl::request_stream(std::move(request));
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
}

auto stop_stream() -> void {
	try {
		impl::stop_stream();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
}

auto shutdown() -> void {
	try {
		impl::shutdown();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
}

auto update() -> void {
	try {
		impl::update();
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
}

auto check_if_supported_or_try_to_fall_back(bhas::stream_request request) -> std::optional<bhas::stream_request> {
	try {
		return impl::check_if_supported_or_try_to_fall_back(std::move(request));
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return std::nullopt;
}

auto make_request_from_user_config(const bhas::user_config& config) -> std::optional<bhas::stream_request> {
	try {
		return impl::make_request_from_user_config(config);
	}
	catch (const std::exception& e) { impl::model.cb.report({impl::err_exception_caught({__func__}, e.what())}); }
	catch (...)                     { impl::model.cb.report({impl::err_exception_caught({__func__})}); }
	return std::nullopt;
}

namespace jack {

auto set_client_name(std::string_view name) -> void {
	api::jack::set_client_name(name);
}

} // jack

} // bhas