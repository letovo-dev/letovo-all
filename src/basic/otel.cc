#include "otel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

namespace telemetry {

namespace {

namespace otlp = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace internal_log = opentelemetry::sdk::common::internal_log;
namespace propagation = opentelemetry::context::propagation;

std::shared_ptr<trace_sdk::TracerProvider> provider;

std::string env_or(const char *name, const std::string &fallback) {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  return value;
}

bool env_bool(const char *name, bool fallback) {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }

  std::string raw = value;
  std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
}

bool env_is(const char *name, const std::string &expected) {
  const char *value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  std::string raw = value;
  std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return raw == expected;
}

internal_log::LogLevel parse_log_level(const std::string &level) {
  if (level == "debug") {
    return internal_log::LogLevel::Debug;
  }
  if (level == "warn" || level == "warning") {
    return internal_log::LogLevel::Warning;
  }
  if (level == "error") {
    return internal_log::LogLevel::Error;
  }
  return internal_log::LogLevel::Info;
}

}  // namespace

Settings settings_from_env(const std::string &fallback_service_name) {
  Settings settings;
  settings.enabled = env_bool("OTEL_SDK_ENABLED", true) &&
                     !env_bool("OTEL_SDK_DISABLED", false) &&
                     !env_is("OTEL_TRACES_EXPORTER", "none");
  settings.service_name = env_or("OTEL_SERVICE_NAME", fallback_service_name);
  settings.traces_endpoint = env_or(
      "OTEL_EXPORTER_OTLP_TRACES_ENDPOINT",
      env_or("OTEL_EXPORTER_OTLP_ENDPOINT", "http://127.0.0.1:4318") +
          "/v1/traces");
  settings.log_level = env_or("OTEL_LOG_LEVEL", "info");
  return settings;
}

void init_tracing(
    const Settings &settings,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  auto http_trace_context =
      std::make_shared<opentelemetry::trace::propagation::HttpTraceContext>();
  std::shared_ptr<propagation::TextMapPropagator> propagator =
      http_trace_context;
  propagation::GlobalTextMapPropagator::SetGlobalPropagator(
      opentelemetry::nostd::shared_ptr<propagation::TextMapPropagator>(
          propagator));

  if (!settings.enabled) {
    if (logger_ptr) {
      logger_ptr->info([] {
        return "OpenTelemetry tracing disabled by OTEL_SDK_ENABLED";
      });
    }
    return;
  }

  internal_log::GlobalLogHandler::SetLogLevel(
      parse_log_level(settings.log_level));

  otlp::OtlpHttpExporterOptions exporter_options;
  exporter_options.url = settings.traces_endpoint;
  exporter_options.console_debug = settings.log_level == "debug";

  auto exporter = otlp::OtlpHttpExporterFactory::Create(exporter_options);

  trace_sdk::BatchSpanProcessorOptions processor_options;
  processor_options.schedule_delay_millis = std::chrono::milliseconds(5000);
  processor_options.max_export_batch_size = 512;

  auto processor = trace_sdk::BatchSpanProcessorFactory::Create(
      std::move(exporter), processor_options);
  provider = std::shared_ptr<trace_sdk::TracerProvider>(
      trace_sdk::TracerProviderFactory::Create(
          std::move(processor),
          resource::Resource::Create({{"service.name", settings.service_name}})));

  std::shared_ptr<opentelemetry::trace::TracerProvider> api_provider_std =
      provider;
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>
      api_provider(api_provider_std);
  trace_sdk::Provider::SetTracerProvider(api_provider);

  if (logger_ptr) {
    logger_ptr->info([settings] {
      return fmt::format(
          R"({{"event":"otel_initialized","service_name":"{}","traces_endpoint":"{}"}})",
          json_escape(settings.service_name),
          json_escape(settings.traces_endpoint));
    });
  }
}

void shutdown_tracing() {
  if (provider) {
    provider->ForceFlush();
    provider->Shutdown();
  }

  provider.reset();
  opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> none;
  trace_sdk::Provider::SetTracerProvider(none);
}

std::string trace_id_for_log(const opentelemetry::trace::SpanContext &context) {
  char buffer[2 * opentelemetry::trace::TraceId::kSize] = {};
  context.trace_id().ToLowerBase16(buffer);
  return std::string(buffer, sizeof(buffer));
}

std::string span_id_for_log(const opentelemetry::trace::SpanContext &context) {
  char buffer[2 * opentelemetry::trace::SpanId::kSize] = {};
  context.span_id().ToLowerBase16(buffer);
  return std::string(buffer, sizeof(buffer));
}

const char *request_duration_ms_attribute() {
  return "request.duration_ms";
}

std::string handling_status_name(restinio::request_handling_status_t status) {
  switch (status) {
    case restinio::request_handling_status_t::accepted:
      return "accepted";
    case restinio::request_handling_status_t::rejected:
      return "rejected";
    case restinio::request_handling_status_t::not_handled:
      return "not_handled";
  }
  return "unknown";
}

}  // namespace telemetry
