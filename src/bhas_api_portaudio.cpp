#include "bhas_api.h"
#include <fmt/format.h>
#include <mutex>
#include <numeric>
#include <portaudio.h>
#ifdef _WIN32
#include <pa_asio.h>
#include <pa_win_wasapi.h>
#endif

namespace bhas {
namespace api {

struct Callbacks {
	bhas::audio_cb audio;
	bhas::stream_stopped_cb stream_stopped;
};

struct CurrentStream {
	PaStream* pa_stream = nullptr;
	PaHostApiTypeId host_type;
	bhas::sample_rate sample_rate;
	bhas::output_latency output_latency;
};

struct PaModel {
	Callbacks cb;
	std::optional<CurrentStream> current_stream;
};

static PaModel model;

struct pa_stream_parameters {
	PaStreamParameters input_params  = {0};
	PaStreamParameters output_params = {0};
	PaStreamParameters* input_params_ptr  = nullptr;
	PaStreamParameters* output_params_ptr = nullptr;
	const PaDeviceInfo* output_device_info = nullptr;
};

[[nodiscard]] static
auto is_wasapi_loopback_device(PaDeviceIndex device) -> bool {
#	ifdef _WIN32
		return PaWasapi_IsLoopback(device);
#	else
		return false;
#	endif
}

[[nodiscard]] static
auto make_input_params(PaDeviceIndex device_index, const PaDeviceInfo& info) -> PaStreamParameters {
	PaStreamParameters params;
	params.device                    = device_index;
	params.hostApiSpecificStreamInfo = nullptr;
	params.sampleFormat              = paFloat32 | paNonInterleaved;
	params.channelCount              = info.maxInputChannels;
	params.suggestedLatency          = info.defaultLowInputLatency;
	return params;
}

[[nodiscard]] static
auto make_output_params(PaDeviceIndex device_index, const PaDeviceInfo& info) -> PaStreamParameters {
	PaStreamParameters params;
	params.device                    = device_index;
	params.hostApiSpecificStreamInfo = nullptr;
	params.sampleFormat              = paFloat32 | paNonInterleaved;
	params.channelCount              = 2;
	params.suggestedLatency          = info.defaultLowOutputLatency;
	return params;
}

static
auto make_pa_stream_parameters(const bhas::stream_request& request, pa_stream_parameters* params) -> void {
	if (request.input_device) {
		const auto input_device_pa_index = static_cast<PaDeviceIndex>(request.input_device->value);
		const auto input_device_info     = Pa_GetDeviceInfo(input_device_pa_index);
		params->input_params             = make_input_params(input_device_pa_index, *input_device_info);
		params->input_params_ptr		 = &params->input_params;
	}
	const auto output_device_pa_index = static_cast<PaDeviceIndex>(request.output_device.value);
	params->output_device_info        = Pa_GetDeviceInfo(output_device_pa_index);
	params->output_params             = make_output_params(output_device_pa_index, *params->output_device_info);
	params->output_params_ptr         = &params->output_params;
}

[[nodiscard]] static
auto callback_result_to_pa(bhas::callback_result result) -> int {
	switch (result) {
		case bhas::callback_result::continue_: return paContinue;
		case bhas::callback_result::abort: return paAbort;
		case bhas::callback_result::complete: return paComplete;
		default: return paAbort;
	}
}

static
auto stream_audio_callback(
	const void* input, 
	void* output, 
	unsigned long pa_frame_count, 
	const PaStreamCallbackTimeInfo* pa_time_info, 
	PaStreamCallbackFlags status_flags, 
	void* user_data) -> int
{
	const auto input_buffer   = bhas::input_buffer{reinterpret_cast<float const * const *>(input)};
	const auto output_buffer  = bhas::output_buffer{reinterpret_cast<float * const *>(output)};
	const auto frame_count    = bhas::frame_count{static_cast<uint32_t>(pa_frame_count)};
	const auto sample_rate    = model.current_stream->sample_rate;
	const auto output_latency = model.current_stream->output_latency;
	bhas::time_info time_info;
	time_info.current_time           = pa_time_info->currentTime;
	time_info.input_buffer_adc_time  = pa_time_info->inputBufferAdcTime;
	time_info.output_buffer_dac_time = pa_time_info->outputBufferDacTime;
	return callback_result_to_pa(
		model.cb.audio(
			input_buffer,
			output_buffer,
			frame_count,
			sample_rate,
			output_latency,
			&time_info));
}

static
auto stream_finished_callback(void*) -> void {
	if (model.cb.stream_stopped) {
		model.cb.stream_stopped();
	}
}

[[nodiscard]] static
auto err_stream_settings_not_supported() -> bhas::error {
	return {fmt::format("The requested stream settings are not supported.")};
}

[[nodiscard]] static
auto info_sample_rate_fallback_try(bhas::sample_rate sr) -> bhas::info {
	return {fmt::format("I'm going to try falling back to the default sample rate ({} Hz)", sr.value)};
}

[[nodiscard]] static
auto info_sample_rate_fallback_success() -> bhas::info {
	return {"That worked."};
}

[[nodiscard]] static
auto info_sample_rate_fallback_failure(const char* pa_error_text) -> bhas::info {
	return {fmt::format("It still doesn't work. ({})", pa_error_text)};
}

[[nodiscard]] static
auto warn_request_not_supported(const char* pa_error_text) -> bhas::warning {
	return {fmt::format("The requested stream settings are not supported. ({})", pa_error_text)};
}

auto check_if_supported_or_try_to_fall_back(bhas::stream_request request, bhas::log* log) -> std::optional<bhas::stream_request> {
	pa_stream_parameters params;
	make_pa_stream_parameters(request, &params);
	auto supported_check = Pa_IsFormatSupported(params.input_params_ptr, params.output_params_ptr, request.sample_rate.value);
	if (supported_check == paFormatIsSupported) {
		return request;
	}
	auto pa_error_text = Pa_GetErrorText(supported_check);
	log->push_back(warn_request_not_supported(pa_error_text));
	const auto default_SR     = params.output_device_info->defaultSampleRate;
	const auto default_SR_int = bhas::sample_rate{static_cast<uint32_t>(params.output_device_info->defaultSampleRate)};
	if (default_SR_int.value != request.sample_rate.value) {
		log->push_back(info_sample_rate_fallback_try(default_SR_int));
		supported_check = Pa_IsFormatSupported(params.input_params_ptr, params.output_params_ptr, default_SR);
		if (supported_check == paFormatIsSupported) {
			log->push_back(info_sample_rate_fallback_success());
			request.sample_rate = default_SR_int;
		}
		else {
			pa_error_text = Pa_GetErrorText(supported_check);
			log->push_back(info_sample_rate_fallback_failure(pa_error_text));
		}
	}
	if (supported_check == paFormatIsSupported) {
		return request;
	}
	log->push_back(err_stream_settings_not_supported());
	return std::nullopt;
}

auto get_cpu_load() -> cpu_load {
	if (!is_stream_active()) {
		return {0.0};
	}
	return {Pa_GetStreamCpuLoad(model.current_stream->pa_stream)};
}

auto get_output_latency() -> bhas::output_latency {
	if (!model.current_stream) {
		return {0.0};
	}
	return model.current_stream->output_latency;
}

auto get_stream_time() -> stream_time {
	if (!is_stream_active()) {
		return {0.0};
	}
	return {Pa_GetStreamTime(model.current_stream->pa_stream)};
}

auto is_stream_active() -> bool {
	if (!model.current_stream) {
		return false;
	}
	return Pa_IsStreamActive(model.current_stream->pa_stream) == 1;
}

auto init(bhas::log* log) -> bool {
	if (const auto err = Pa_Initialize(); err != paNoError) {
		log->push_back(bhas::error{fmt::format("Failed to initialize PortAudio. ({})", Pa_GetErrorText(err))});
		return false;
	}
	return true;
}

auto shutdown() -> void {
	Pa_Terminate();
}

auto rescan() -> bhas::system {
	bhas::system system;
	const auto api_count    = Pa_GetHostApiCount();
	const auto device_count = Pa_GetDeviceCount();
	system.hosts.resize(api_count);
	system.devices.resize(device_count);
	for (PaDeviceIndex i = 0; i < device_count; i++) {
		const auto info                  = Pa_GetDeviceInfo(i);
		auto& device                     = system.devices.at(i);
		device.index                     = bhas::device_index{static_cast<size_t>(i)};
		device.name.value                = info->name;
		device.num_channels.value        = info->maxInputChannels;
		device.default_sample_rate.value = static_cast<uint32_t>(info->defaultSampleRate);
		device.host.value                = info->hostApi;
		if (info->maxInputChannels > 0)   { device.flags.value |= bhas::device_flags::input; }
		if (info->maxOutputChannels > 0)  { device.flags.value |= bhas::device_flags::output; }
		if (is_wasapi_loopback_device(i)) { device.flags.value |= bhas::device_flags::wasapi_loopback; }
	}
	for (PaHostApiIndex i = 0; i < api_count; i++) {
		const auto info = Pa_GetHostApiInfo(i);
		auto& host      = system.hosts.at(i);
		host.index      = bhas::host_index{static_cast<size_t>(i)};
		host.name.value = info->name;
		if (info->defaultInputDevice != paNoDevice) {
			host.default_input_device = bhas::device_index{static_cast<size_t>(info->defaultInputDevice)};
		}
		if (info->defaultOutputDevice != paNoDevice) {
			host.default_output_device = bhas::device_index{static_cast<size_t>(info->defaultOutputDevice)};
		}
		if (info->type == paASIO) { host.flags.value |= bhas::host_flags::asio; }
		for (const auto& device : system.devices) {
			if (device.host.value == host.index.value) {
				host.devices.push_back(device.index);
			}
		}
	}
	system.default_host          = bhas::host_index{static_cast<size_t>(Pa_GetDefaultHostApi())};
	system.default_input_device  = bhas::device_index{static_cast<size_t>(Pa_GetDefaultInputDevice())};
	system.default_output_device = bhas::device_index{static_cast<size_t>(Pa_GetDefaultOutputDevice())};
	return system;
}

[[nodiscard]] static
auto try_to_open_pa_stream(bhas::stream_request request, const pa_stream_parameters& params, double sample_rate, PaStream** pa_stream) -> PaError {
	return Pa_OpenStream(
		pa_stream,
		params.input_params_ptr,
		params.output_params_ptr,
		sample_rate,
		paFramesPerBufferUnspecified,
		paNoFlag,
		stream_audio_callback,
		nullptr);
}

[[nodiscard]] static
auto err_stream_open_failed(PaError err) -> bhas::error {
	return {fmt::format("Failed to open the stream. ({})", Pa_GetErrorText(err))};
}

[[nodiscard]] static
auto warn_failed_to_open_stream_but_i_will_try_again() -> bhas::warning {
	return {
		"Failed to open the stream for some reason. "
		"I'm going to try a few more times because sometimes "
		"these audio drivers are just stupid and if you keep "
		"trying to open the stream then eventually it succeeds..."
	};
}

[[nodiscard]] static
auto info_open_stream_retry() -> bhas::info {
	return {"Retrying..."};
}

[[nodiscard]] static
auto info_open_stream_success() -> bhas::info {
	return {"Stream opened successfully."};
}

[[nodiscard]] static
auto warn_stream_already_open() -> bhas::warning {
	return {"A stream is already open so I'm ignoring this request."};
}

[[nodiscard]] static
auto err_failed_to_start_stream(const char* reason) -> bhas::error {
	return {fmt::format("Failed to start the stream. ({})", reason)};
}

[[nodiscard]] static
auto err_failed_to_close_stream(const char* reason) -> bhas::error {
	return {fmt::format("Failed to close the stream. ({})", reason)};
}

[[nodiscard]] static
auto err_failed_to_stop_stream(const char* reason) -> bhas::error {
	return {fmt::format("Failed to stop the stream. ({})", reason)};
}

[[nodiscard]] static
auto make_stream_info(bhas::stream_request request, PaStream* pa_stream) -> bhas::stream {
	bhas::stream info;
	info.sample_rate = bhas::sample_rate{static_cast<uint32_t>(Pa_GetStreamInfo(pa_stream)->sampleRate)};
	return info;
}

auto open_stream(bhas::stream_request request, bhas::log* log, bhas::channel_count* input_channel_count) -> bool {
	if (model.current_stream) {
		log->push_back(warn_stream_already_open());
		return false;
	}
	pa_stream_parameters params;
	make_pa_stream_parameters(request, &params);
	CurrentStream stream;
	const auto SR = static_cast<double>(request.sample_rate.value);
	auto err = try_to_open_pa_stream(request, params, SR, &stream.pa_stream);
	if (err != paNoError) {
		static constexpr auto MAX_RETRIES = 3;
		log->push_back(warn_failed_to_open_stream_but_i_will_try_again());
		for (int i = 0; i < MAX_RETRIES; i++) {
			log->push_back(info_open_stream_retry());
			err = try_to_open_pa_stream(request, params, SR, &stream.pa_stream);
			if (err == paNoError) {
				break;
			}
		}
	}
	if (err != paNoError) {
		log->push_back(err_stream_open_failed(err));
		return false;
	}
	log->push_back(info_open_stream_success());
	stream.host_type      = Pa_GetHostApiInfo(params.output_device_info->hostApi)->type;
	stream.output_latency = bhas::output_latency{Pa_GetStreamInfo(stream.pa_stream)->outputLatency};
	stream.sample_rate    = request.sample_rate;
	*input_channel_count  = bhas::channel_count{static_cast<uint32_t>(params.input_params.channelCount)};
	model.current_stream  = stream;
	return true;
}

auto start_stream(bhas::log* log) -> bool {
	if (!model.current_stream) {
		log->push_back(err_failed_to_start_stream("No stream is open."));
		return false;
	}
	PaError err;
	if (err = Pa_SetStreamFinishedCallback(model.current_stream->pa_stream, stream_finished_callback); err != paNoError) {
		log->push_back(err_failed_to_start_stream(Pa_GetErrorText(err)));
		return false;
	}
	if (err = Pa_StartStream(model.current_stream->pa_stream); err != paNoError) {
		log->push_back(err_failed_to_start_stream(Pa_GetErrorText(err)));
		return false;
	}
	return true;
}

auto close_stream() -> void {
	if (!model.current_stream) {
		return;
	}
	Pa_CloseStream(model.current_stream->pa_stream);
	model.current_stream = std::nullopt;
}

auto set(audio_cb cb) -> void {
	model.cb.audio = std::move(cb);
}

auto set(stream_stopped_cb cb) -> void {
	model.cb.stream_stopped = std::move(cb);
}

auto stop_stream(bhas::log* log) -> bool {
	if (!is_stream_active()) {
		model.cb.stream_stopped();
		return true;
	}
	PaError err;
	if (model.current_stream->host_type == paDirectSound) {
		// Can get stuck while waiting for the stream to stop
		// due to an unknown Windows or PortAudio bug i guess
		// So just abort instead
		if (err = Pa_AbortStream(model.current_stream->pa_stream); err != paNoError) {
			if (log) log->push_back(err_failed_to_stop_stream(Pa_GetErrorText(err)));
			return false;
		}
		return true;
	}
	if (model.current_stream->host_type == paMME) {
		// Likewise MME will always get stuck if you try to stop
		// cleanly AFAIK due to a PortAudio bug which I can't be
		// bothered to report
		if (err = Pa_AbortStream(model.current_stream->pa_stream); err != paNoError) {
			if (log) log->push_back(err_failed_to_stop_stream(Pa_GetErrorText(err)));
			return false;
		}
		return true;
	}
	if (err = Pa_StopStream(model.current_stream->pa_stream); err != paNoError) {
		if (log) log->push_back(err_failed_to_stop_stream(Pa_GetErrorText(err)));
		return false;
	}
	return true;
}

} // api
} // bhas
