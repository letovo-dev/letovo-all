from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TRANSACTIONS = (ROOT / "src/market/transactions.cc").read_text()
HEADER = (ROOT / "src/market/transactions.h").read_text()
SERVER = (ROOT / "src/server.cpp").read_text()
SCHEMA = (ROOT / "docs/schema.sql").read_text()


def test_admin_only_endpoint_is_registered():
    assert '"/transactions/department-payout"' in TRANSACTIONS
    assert "auth::is_admin(token, pool_ptr)" in TRANSACTIONS
    assert "transactions::server::department_payout(router" in SERVER


def test_recipient_policy_is_server_side_and_bounded_to_child_roles():
    assert "r.roleid BETWEEN 1 AND 28" in TRANSACTIONS
    assert "u.active = true" in TRANSACTIONS
    assert "u.registered = true" in TRANSACTIONS
    assert "recipient" not in HEADER.split("struct DepartmentPayoutResult", 1)[0]


def test_preview_and_apply_share_count_contract():
    assert "preview_department_payout" in HEADER
    assert "expected_recipient_count" in HEADER
    assert "totals.recipient_count = $5::integer" in TRANSACTIONS
    assert "DepartmentPayoutStatus::PreviewChanged" in TRANSACTIONS


def test_apply_is_atomic_auditable_and_idempotent():
    assert "pg_advisory_xact_lock(hashtext($4))" in TRANSACTIONS
    assert 'UPDATE "user" u' in TRANSACTIONS
    assert 'INSERT INTO "transactions" (sender, receiver, amount, reason)' in TRANSACTIONS
    assert "department payout: " in TRANSACTIONS
    assert "; issued by " in TRANSACTIONS
    assert "; request " in TRANSACTIONS
    assert "prior.recipient_count = 0" in TRANSACTIONS
    assert "BOOL_AND(u.balance <= 2147483647 - $2::integer)" in TRANSACTIONS
    assert "params, true" in TRANSACTIONS
    assert "reason character varying DEFAULT 'wire transfer'" in SCHEMA


def test_amount_and_request_id_are_validated():
    assert "amount <= 0" in TRANSACTIONS
    assert "valid_payout_request_id" in TRANSACTIONS
    assert "expected_recipient_count" in TRANSACTIONS
