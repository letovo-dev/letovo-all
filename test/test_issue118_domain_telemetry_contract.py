from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_domain_event_helper_records_span_events_and_correlated_logs():
    header = read("src/basic/otel.h")
    source = read("src/basic/otel.cc")

    assert "enum class DomainEventLevel" in header
    assert "struct DomainEventAttribute" in header
    assert "record_domain_event(" in header
    assert "stable_attribute_hash(std::string_view value)" in header
    assert "is_safe_domain_event_attribute(std::string_view key)" in header

    assert "trace::GetSpan(opentelemetry::context::RuntimeContext::GetCurrent())" in source
    assert "span->AddEvent" in source
    assert '"event":"domain_event"' in source
    assert '"trace_id"' in source
    assert '"span_id"' in source
    assert '"event.severity"' in source
    assert '"event.reason"' in source


def test_domain_event_helper_filters_sensitive_attribute_keys():
    source = read("src/basic/otel.cc")

    for blocked in [
        '"password"',
        '"passwd"',
        '"token"',
        '"cookie"',
        '"authorization"',
        '"body"',
        '"query"',
    ]:
        assert blocked in source

    assert "is_safe_domain_event_attribute(attribute.key)" in source
    assert "continue;" in source


def test_login_handler_emits_branch_events_without_sensitive_values():
    auth = read("src/basic/auth.cc")

    assert '#include "otel.h"' in auth
    assert '"auth.login.started"' in auth
    assert '"auth.login.success"' in auth
    assert '"auth.login_failed"' in auth
    assert '"auth.login_error"' in auth

    assert '"request_received"' in auth
    assert '"user_not_found"' in auth
    assert '"bad_credentials"' in auth
    assert '"user_inactive"' in auth
    assert '"missing_credentials"' in auth
    assert '"db_error"' in auth
    assert '"unexpected_exception"' in auth
    assert '"session_schema_missing"' in auth

    assert "auth::is_user(loginHeader, pool_ptr)" in auth
    assert "auth::auth(loginHeader, passwordHeader, pool_ptr)" in auth
    assert "auth::is_active(loginHeader, pool_ptr)" in auth
    assert "telemetry::stable_attribute_hash(loginHeader)" in auth
    assert '{"user.hash", user_hash}' in auth

    assert '{"password"' not in auth
    assert '{"token"' not in auth
    assert '{"cookie"' not in auth
    assert '{"body"' not in auth
