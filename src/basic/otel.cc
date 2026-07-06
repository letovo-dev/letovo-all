#include "otel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

namespace telemetry {

namespace {

namespace otlp = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace internal_log = opentelemetry::sdk::common::internal_log;
namespace propagation = opentelemetry::context::propagation;

std::shared_ptr<trace_sdk::TracerProvider> provider;

const char *domain_event_level_name(DomainEventLevel level) {
  switch (level) {
    case DomainEventLevel::kWarn:
      return "warn";
    case DomainEventLevel::kError:
      return "error";
    case DomainEventLevel::kInfo:
      return "info";
  }
  return "info";
}

std::string env_or(const char *name, const std::string &fallback) {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  return value;
}

std::string lower_copy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return lowered;
}

bool env_bool(const char *name, bool fallback) {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }

  std::string raw = lower_copy(value);
  return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
}

bool env_is(const char *name, const std::string &expected) {
  const char *value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  std::string raw = lower_copy(value);
  return raw == expected;
}

std::vector<std::pair<std::string, std::string>> parse_resource_attributes(
    const std::string &raw) {
  std::vector<std::pair<std::string, std::string>> attributes;
  std::size_t start = 0;
  while (start < raw.size()) {
    const std::size_t comma = raw.find(',', start);
    const std::string item =
        raw.substr(start, comma == std::string::npos ? std::string::npos
                                                     : comma - start);
    const std::size_t equals = item.find('=');
    if (equals != std::string::npos && equals > 0 &&
        equals + 1 < item.size()) {
      attributes.emplace_back(item.substr(0, equals), item.substr(equals + 1));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return attributes;
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

std::string attributes_json(
    const std::vector<std::pair<std::string, std::string>> &attributes) {
  std::string result = "{";
  bool first = true;
  for (const auto &[key, value] : attributes) {
    if (!first) {
      result += ",";
    }
    first = false;
    result += fmt::format(R"("{}":"{}")", json_escape(key), json_escape(value));
  }
  result += "}";
  return result;
}

void write_domain_event_log(
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    DomainEventLevel level,
    std::string payload) {
  if (!logger_ptr) {
    return;
  }

  switch (level) {
    case DomainEventLevel::kError:
      logger_ptr->error([payload = std::move(payload)] { return payload; });
      break;
    case DomainEventLevel::kWarn:
      logger_ptr->warn([payload = std::move(payload)] { return payload; });
      break;
    case DomainEventLevel::kInfo:
      logger_ptr->info([payload = std::move(payload)] { return payload; });
      break;
  }
}

}  // namespace

Settings settings_from_env(const std::string &fallback_service_name) {
  Settings settings;
  const std::string traces_endpoint =
      env_or("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT", "");
  const std::string otlp_endpoint = env_or("OTEL_EXPORTER_OTLP_ENDPOINT", "");
  settings.traces_endpoint =
      traces_endpoint.empty() && !otlp_endpoint.empty()
          ? otlp_endpoint + "/v1/traces"
          : traces_endpoint;
  settings.enabled = env_bool("OTEL_SDK_ENABLED", true) &&
                     !env_bool("OTEL_SDK_DISABLED", false) &&
                     !env_is("OTEL_TRACES_EXPORTER", "none") &&
                     !settings.traces_endpoint.empty();
  settings.service_name = env_or("OTEL_SERVICE_NAME", fallback_service_name);
  settings.resource_attributes = env_or("OTEL_RESOURCE_ATTRIBUTES", "");
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
        return "OpenTelemetry tracing disabled by configuration";
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
  auto resource_attributes = parse_resource_attributes(settings.resource_attributes);
  resource_attributes.emplace_back("service.name", settings.service_name);
  resource::ResourceAttributes attributes;
  for (const auto &[key, value] : resource_attributes) {
    attributes[key] = value;
  }
  provider = std::shared_ptr<trace_sdk::TracerProvider>(
      trace_sdk::TracerProviderFactory::Create(
          std::move(processor),
          resource::Resource::Create(attributes)));

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

std::string stable_attribute_hash(std::string_view value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char c : value) {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  return fmt::format("{:016x}", hash);
}

bool is_safe_domain_event_attribute(std::string_view key) {
  const std::string lowered = lower_copy(key);
  static const std::vector<std::string> blocked = {
      "password", "passwd", "token", "cookie", "authorization", "body",
      "query"};
  return std::none_of(blocked.begin(), blocked.end(),
                      [&lowered](const auto &term) {
                        return lowered.find(term) != std::string::npos;
                      });
}

void record_domain_event(
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::string_view name,
    DomainEventLevel level,
    std::string_view reason,
    std::initializer_list<DomainEventAttribute> attributes) {
  namespace trace = opentelemetry::trace;
  namespace common = opentelemetry::common;

  std::vector<std::pair<std::string, std::string>> safe_attributes;
  safe_attributes.emplace_back("event.severity", domain_event_level_name(level));
  safe_attributes.emplace_back("event.reason", reason);
  for (const auto &attribute : attributes) {
    if (!is_safe_domain_event_attribute(attribute.key)) {
      continue;
    }
    safe_attributes.emplace_back(attribute.key, attribute.value);
  }

  std::vector<std::pair<opentelemetry::nostd::string_view,
                        common::AttributeValue>>
      span_attributes;
  span_attributes.reserve(safe_attributes.size());
  for (const auto &[key, value] : safe_attributes) {
    span_attributes.emplace_back(
        opentelemetry::nostd::string_view(key.data(), key.size()),
        opentelemetry::nostd::string_view(value.data(), value.size()));
  }

  auto span =
      trace::GetSpan(opentelemetry::context::RuntimeContext::GetCurrent());
  span->AddEvent(opentelemetry::nostd::string_view(name.data(), name.size()),
                 span_attributes);

  const auto context = span->GetContext();
  write_domain_event_log(
      logger_ptr,
      level,
      fmt::format(
          R"({{"event":"domain_event","name":"{}","level":"{}","reason":"{}","trace_id":"{}","span_id":"{}","attributes":{}}})",
          json_escape(name),
          domain_event_level_name(level),
          json_escape(reason),
          trace_id_for_log(context),
          span_id_for_log(context),
          attributes_json(safe_attributes)));
}

}  // namespace telemetry
