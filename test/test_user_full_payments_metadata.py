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


def test_public_user_endpoint_is_authenticated_but_not_owner_admin_only():
    source = USER_DATA_CC.read_text()
    handler_start = source.index("void user_info(")
    handler_end = source.index("void full_user_info(", handler_start)
    handler = source[handler_start:handler_end]

    assert "auth::get_username(token, pool_ptr)" in handler
    assert "status_unauthorized" in handler
    assert "security::is_same_user_or_admin" not in handler
    assert "status_forbidden" not in handler


def test_public_user_query_does_not_select_private_money_fields():
    source = USER_DATA_CC.read_text()
    query_start = source.index("pqxx::result user_info(")
    query_end = source.index("pqxx::result full_user_info(", query_start)
    query = source[query_start:query_end]

    assert '"user".balance' not in query
    assert '"roles".payment' not in query
    assert "paycheck" not in query
    assert "last_incoming_payment" not in query
    assert "last_outgoing_payment" not in query


def test_private_user_endpoint_stays_owner_admin_only():
    source = USER_DATA_CC.read_text()
    handler_start = source.index("void full_user_info(")
    handler_end = source.index("void user_roles(", handler_start)
    handler = source[handler_start:handler_end]

    assert "security::is_same_user_or_admin(actor, username, pool_ptr)" in handler
    assert "status_forbidden" in handler
