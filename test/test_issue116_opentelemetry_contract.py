from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_collector_config_and_compose_are_wired():
    collector = read("docs/otel-collector-config.yaml")
    compose = read("docs/docker-compose.yaml")
    nginx = read("docs/nginx.conf")

    assert "receivers:" in collector
    assert "otlp:" in collector
    assert "protocols:" in collector
    assert "grpc:" in collector
    assert "http:" in collector
    assert "processors:" in collector
    assert "batch:" in collector
    assert "exporters:" in collector
    assert "otlphttp/jaeger:" in collector
    assert "endpoint: http://jaeger:4318" in collector
    assert "debug:" in collector
    assert "service:" in collector
    assert "pipelines:" in collector
    assert "traces:" in collector
    assert "logs:" in collector

    assert "jaeger:" in compose
    assert "jaegertracing/all-in-one:1.57" in compose
    assert "SPAN_STORAGE_TYPE: badger" in compose
    assert "BADGER_EPHEMERAL: \"false\"" in compose
    assert "127.0.0.1:16686:16686" in compose
    assert "jaeger-data:" in compose
    assert "otel-collector:" in compose
    assert "otel/opentelemetry-collector:0.155.0" in compose
    assert "          - jaeger" in compose
    assert "./otel-collector-config.yaml:/etc/otelcol/config.yaml:ro" in compose
    assert "127.0.0.1:4317:4317" in compose
    assert "127.0.0.1:4318:4318" in compose
    assert "OTEL_SERVICE_NAME: letovo-server" in compose
    assert "OTEL_SERVICE_NAME: letovo-registration-server" in compose
    assert "OTEL_RESOURCE_ATTRIBUTES: deployment.environment=production,service.namespace=letovocorp" in compose
    assert "OTEL_EXPORTER_OTLP_ENDPOINT: http://127.0.0.1:4318" in compose
    assert "OTEL_EXPORTER_OTLP_TRACES_ENDPOINT: http://127.0.0.1:4318/v1/traces" in compose
    assert 'NEXT_PUBLIC_OTEL_ENABLED: "true"' in compose
    assert "NEXT_PUBLIC_OTEL_EXPORTER_OTLP_TRACES_ENDPOINT: /otel/v1/traces" in compose
    assert "NEXT_PUBLIC_OTEL_DEPLOYMENT_ENVIRONMENT: production" in compose
    assert "NEXT_PUBLIC_OTEL_SERVICE_NAMESPACE: letovocorp" in compose
    assert 'NEXT_PUBLIC_OTEL_TRACES_SAMPLER_RATIO: "0.1"' in compose

    assert "location = /otel/v1/traces" in nginx
    assert "limit_except POST" in nginx
    assert "client_max_body_size 1m" in nginx
    assert "proxy_pass http://127.0.0.1:4318/v1/traces" in nginx
    assert "location /otel/" in nginx
    assert "return 404" in nginx


def test_backend_opentelemetry_dependency_and_request_wrapper_are_wired():
    cmake = read("src/CMakeLists.txt")
    dockerfile = read("src/Dockerfile")
    workflow = read(".github/workflows/docker-image.yml")
    header = read("src/basic/otel.h")
    source = read("src/basic/otel.cc")
    server = read("src/server.cpp")
    registration_server = read("src/registration_server.cpp")

    assert "opentelemetry-cpp" in cmake
    assert "GIT_TAG        v1.27.0" in cmake
    assert "WITH_OTLP_HTTP" in cmake
    assert "basic/otel.cc" in cmake
    assert "opentelemetry-cpp::otlp_http_exporter" in cmake
    assert "opentelemetry-cpp::trace" in cmake

    assert "libcurl4-openssl-dev" in dockerfile
    assert "libprotobuf-dev" in dockerfile
    assert "protobuf-compiler" in dockerfile
    assert "libcurl4" in dockerfile
    assert "libprotobuf" in dockerfile

    assert "basic/otel.cc" in workflow

    assert "struct Settings" in header
    assert "make_traced_handler" in header
    assert "TraceHeaderCarrier" in header
    assert "request_handling_status_t operator()" in header

    assert "OtlpHttpExporterFactory::Create" in source
    assert "BatchSpanProcessorFactory::Create" in source
    assert "HttpTraceContext" in source
    assert "OTEL_EXPORTER_OTLP_TRACES_ENDPOINT" in source
    assert "OTEL_SERVICE_NAME" in source
    assert "OTEL_RESOURCE_ATTRIBUTES" in source
    assert "OTEL_SDK_DISABLED" in source
    assert "OTEL_TRACES_EXPORTER" in source
    assert '"http://127.0.0.1:4318"' not in source
    assert "std::string traces_endpoint{}" in header
    assert "request.duration_ms" in source
    assert "http.response.status_code" in header
    assert "trace_id" in source
    assert "url.query" not in header
    assert '"query"' not in header

    assert '#include "./basic/otel.h"' in server
    assert "telemetry::init_tracing" in server
    assert "telemetry::make_traced_handler(std::move(router), logger_ptr)" in server
    assert "telemetry::shutdown_tracing" in server
    assert '#include "./basic/otel.h"' in registration_server
    assert 'telemetry::settings_from_env("letovo-registration-server")' in registration_server
    assert "telemetry::make_traced_handler(std::move(router), logger_ptr)" in registration_server
    assert "telemetry::shutdown_tracing" in registration_server


def test_frontend_opentelemetry_contract_is_wired():
    package_json = read("frontend/package.json")
    axios = read("frontend/src/shared/lib/ApiSPA/axios/axios.ts")
    browser = read("frontend/src/shared/lib/otel/browser.ts")
    bootstrap = read("frontend/src/shared/lib/otel/TelemetryBootstrap.tsx")
    layout = read("frontend/src/app/layout.tsx")
    analytics = read("frontend/src/shared/hooks/useActivityAnalytics.ts")
    dockerfile = read("frontend/dockerfile")

    assert '"@opentelemetry/api": "^1.9.1"' in package_json
    assert '"@opentelemetry/sdk-trace-web": "^2.9.0"' in package_json
    assert '"@opentelemetry/exporter-trace-otlp-http": "^0.220.0"' in package_json
    assert '"@opentelemetry/instrumentation-xml-http-request": "^0.220.0"' in package_json
    assert '"test:opentelemetry": "node scripts/test-opentelemetry.mjs"' in package_json

    assert "attachAxiosTelemetry(instance)" in axios
    assert "WebTracerProvider" in browser
    assert "OTLPTraceExporter" in browser
    assert "DocumentLoadInstrumentation" in browser
    assert "XMLHttpRequestInstrumentation" in browser
    assert "propagation.inject" in browser
    assert "traceparent" in browser
    assert "NEXT_PUBLIC_OTEL_EXPORTER_OTLP_TRACES_ENDPOINT" in browser
    assert "NEXT_PUBLIC_OTEL_DEPLOYMENT_ENVIRONMENT" in browser
    assert "NEXT_PUBLIC_OTEL_SERVICE_NAMESPACE" in browser
    assert "NEXT_PUBLIC_OTEL_TRACES_SAMPLER_RATIO" in browser
    assert "TraceIdRatioBasedSampler" in browser
    assert "!initialized" in browser
    assert "withFrontendSpan" in browser
    assert "initBrowserTelemetry" in bootstrap
    assert "<TelemetryBootstrap />" in layout
    assert "'analytics.activity_ping'" in analytics
    assert "ARG NEXT_PUBLIC_OTEL_ENABLED=" in dockerfile
    assert "ARG NEXT_PUBLIC_OTEL_EXPORTER_OTLP_TRACES_ENDPOINT=" in dockerfile
    assert "ARG NEXT_PUBLIC_OTEL_DEPLOYMENT_ENVIRONMENT=" in dockerfile
    assert "ARG NEXT_PUBLIC_OTEL_SERVICE_NAMESPACE=" in dockerfile
    assert "ARG NEXT_PUBLIC_OTEL_TRACES_SAMPLER_RATIO=" in dockerfile
