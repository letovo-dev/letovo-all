#pragma once

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

#include <fmt/format.h>
#include <restinio/all.hpp>

#include "opentelemetry/context/context.h"
#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/tracer.h"

namespace telemetry {

struct Settings {
  bool enabled{true};
  std::string service_name{"letovo-server"};
  std::string traces_endpoint{"http://127.0.0.1:4318/v1/traces"};
  std::string log_level{"info"};
};

Settings settings_from_env(const std::string &fallback_service_name);
void init_tracing(const Settings &settings,
                  std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
void shutdown_tracing();

class TraceHeaderCarrier final
    : public opentelemetry::context::propagation::TextMapCarrier {
 public:
  explicit TraceHeaderCarrier(const restinio::http_request_header_t &header)
      : header_(header) {}

  opentelemetry::nostd::string_view Get(
      opentelemetry::nostd::string_view key) const noexcept override {
    key_buffer_.assign(key.data(), key.size());
    if (!header_.has_field(key_buffer_)) {
      return "";
    }
    value_buffer_ = header_.get_field(key_buffer_);
    return value_buffer_;
  }

  void Set(opentelemetry::nostd::string_view,
           opentelemetry::nostd::string_view) noexcept override {}

 private:
  const restinio::http_request_header_t &header_;
  mutable std::string key_buffer_;
  mutable std::string value_buffer_;
};

std::string trace_id_for_log(const opentelemetry::trace::SpanContext &context);
std::string span_id_for_log(const opentelemetry::trace::SpanContext &context);
std::string handling_status_name(restinio::request_handling_status_t status);
const char *request_duration_ms_attribute();

enum class DomainEventLevel {
  kInfo,
  kWarn,
  kError,
};

struct DomainEventAttribute {
  std::string key;
  std::string value;
};

std::string stable_attribute_hash(std::string_view value);
bool is_safe_domain_event_attribute(std::string_view key);
void record_domain_event(
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::string_view name,
    DomainEventLevel level,
    std::string_view reason,
    std::initializer_list<DomainEventAttribute> attributes = {});

inline std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const unsigned char c : value) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (c < 0x20) {
          char encoded[7] = {};
          std::snprintf(encoded, sizeof(encoded), "\\u%04x", c);
          escaped += encoded;
        } else {
          escaped += static_cast<char>(c);
        }
        break;
    }
  }
  return escaped;
}

inline std::int64_t elapsed_ms(std::chrono::steady_clock::time_point started_at) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - started_at)
      .count();
}

inline void record_request_log(
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::string method,
    std::string path,
    std::string handling_status,
    std::int64_t duration_ms,
    opentelemetry::trace::SpanContext context) {
  if (!logger_ptr) {
    return;
  }

  logger_ptr->info([method = std::move(method),
                    path = std::move(path),
                    handling_status = std::move(handling_status),
                    duration_ms,
                    context] {
    return fmt::format(
        R"({{"event":"http_request","method":"{}","path":"{}","handling_status":"{}","duration_ms":{},"trace_id":"{}","span_id":"{}"}})",
        json_escape(method),
        json_escape(path),
        json_escape(handling_status),
        duration_ms,
        trace_id_for_log(context),
        span_id_for_log(context));
  });
}

template <typename Handler>
class TracedRequestHandler {
 public:
  TracedRequestHandler(Handler handler,
                       std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr)
      : handler_(std::move(handler)), logger_ptr_(std::move(logger_ptr)) {}

  restinio::request_handling_status_t operator()(restinio::request_handle_t req) {
    namespace trace = opentelemetry::trace;

    const auto &header = req->header();
    const std::string method = header.method().c_str();
    const std::string path(header.path());
    const std::string span_name = method + " " + path;
    auto tracer = trace::Provider::GetTracerProvider()->GetTracer(
        "letovo.backend.http", "1.0.0");

    TraceHeaderCarrier carrier(header);
    auto propagator =
        opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto current_context = opentelemetry::context::RuntimeContext::GetCurrent();
    auto parent_context = propagator->Extract(carrier, current_context);

    trace::StartSpanOptions options;
    options.kind = trace::SpanKind::kServer;
    options.parent = trace::GetSpan(parent_context)->GetContext();

    auto span = tracer->StartSpan(
        span_name,
        {
            {"http.request.method", method},
            {"url.path", path},
        },
        options);
    auto scope = tracer->WithActiveSpan(span);
    const auto started_at = std::chrono::steady_clock::now();

    try {
      const auto result = (*handler_)(std::move(req));
      const auto duration_ms = elapsed_ms(started_at);
      const auto handling_status = handling_status_name(result);

      span->SetAttribute("restinio.request_handling_status",
                         handling_status);
      span->SetAttribute(request_duration_ms_attribute(),
                         static_cast<std::int64_t>(duration_ms));
      span->End();

      record_request_log(
          logger_ptr_, method, path, handling_status, duration_ms, span->GetContext());

      return result;
    } catch (const std::exception &error) {
      const auto duration_ms = elapsed_ms(started_at);
      span->SetStatus(trace::StatusCode::kError, error.what());
      span->SetAttribute("exception.type", std::string(typeid(error).name()));
      span->SetAttribute("exception.message", std::string(error.what()));
      span->SetAttribute("http.response.status_code", static_cast<std::int64_t>(500));
      span->SetAttribute(request_duration_ms_attribute(),
                         static_cast<std::int64_t>(duration_ms));
      record_request_log(
          logger_ptr_, method, path, "exception", duration_ms, span->GetContext());
      span->End();
      throw;
    } catch (...) {
      const auto duration_ms = elapsed_ms(started_at);
      span->SetStatus(trace::StatusCode::kError, "unknown exception");
      span->SetAttribute("http.response.status_code", static_cast<std::int64_t>(500));
      span->SetAttribute(request_duration_ms_attribute(),
                         static_cast<std::int64_t>(duration_ms));
      record_request_log(
          logger_ptr_, method, path, "exception", duration_ms, span->GetContext());
      span->End();
      throw;
    }
  }

 private:
  Handler handler_;
  std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr_;
};

template <typename Handler>
auto make_traced_handler(
    std::unique_ptr<Handler> handler,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  return TracedRequestHandler<std::shared_ptr<Handler>>(
      std::shared_ptr<Handler>(std::move(handler)), std::move(logger_ptr));
}

}  // namespace telemetry
