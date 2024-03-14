#pragma once

#include "bhas.h"
#include <optional>

namespace bhas {
namespace api {

[[nodiscard]] auto check_if_supported_or_try_to_fall_back(bhas::stream_request request, bhas::log* log) -> std::optional<bhas::stream_request>;
[[nodiscard]] auto get_cpu_load() -> cpu_load;
[[nodiscard]] auto get_output_latency() -> bhas::output_latency;
[[nodiscard]] auto get_stream_time() -> stream_time;
[[nodiscard]] auto is_stream_active() -> bool;
[[nodiscard]] auto rescan() -> bhas::system;
[[nodiscard]] auto open_stream(bhas::stream_request request, bhas::log* log, bhas::channel_count* num_input_channels) -> bool;
[[nodiscard]] auto start_stream(bhas::log* log) -> bool;
auto close_stream() -> void;
auto set(audio_cb cb) -> void;
auto set(stream_stopped_cb cb) -> void;
auto stop_stream(bhas::log* log) -> bool;

} // api
} // bhas