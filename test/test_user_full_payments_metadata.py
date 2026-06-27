from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
USER_DATA_CC = REPO_ROOT / "src" / "basic" / "user_data.cc"
TRANSACTIONS_CC = REPO_ROOT / "src" / "market" / "transactions.cc"


def test_user_full_endpoint_includes_last_payment_metadata():
    """The profile refresh endpoint should expose the same payment metadata as login."""
    source = USER_DATA_CC.read_text()
    handler_start = source.index("void full_user_info(")
    handler_end = source.index("void user_roles(", handler_start)
    handler = source[handler_start:handler_end]

    assert "append_user_payments_fields" in handler
    assert "transactions::last_incoming_outgoing_payments_json(username, pool_ptr)" in handler
    assert ".set_body(response_body)" in handler

    transactions_source = TRANSACTIONS_CC.read_text()
    assert "'last_incoming_payment'" in transactions_source
    assert "'last_outgoing_payment'" in transactions_source
