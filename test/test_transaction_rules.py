import subprocess
import textwrap
from pathlib import Path


def test_admin_negative_transaction_ignores_receiver_balance(tmp_path):
    """The market rules allow admins to prepare negative transactions."""
    repo_root = Path(__file__).resolve().parents[1]
    source = tmp_path / "transaction_rules_check.cpp"
    binary = tmp_path / "transaction_rules_check"

    source.write_text(
        textwrap.dedent(
            f"""
            #include "{repo_root / "src/market/transaction_rules.h"}"

            int main() {{
                if (!transactions::can_transfer_amount(10000, -1000000000, true)) {{
                    return 1;
                }}
                if (transactions::can_transfer_amount(10000, -1, false)) {{
                    return 2;
                }}
                return 0;
            }}
            """
        )
    )

    subprocess.run(
        ["g++", "-std=c++20", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)


def test_admin_bypasses_transfer_cooldown(tmp_path):
    repo_root = Path(__file__).resolve().parents[1]
    source = tmp_path / "transaction_cooldown_rules_check.cpp"
    binary = tmp_path / "transaction_cooldown_rules_check"

    source.write_text(
        textwrap.dedent(
            f"""
            #include "{repo_root / "src/market/transaction_rules.h"}"

            int main() {{
                if (transactions::is_transfer_cooldown_active(5, true)) {{
                    return 1;
                }}
                if (!transactions::is_transfer_cooldown_active(5, false)) {{
                    return 2;
                }}
                if (transactions::is_transfer_cooldown_active(0, false)) {{
                    return 3;
                }}
                return 0;
            }}
            """
        )
    )

    subprocess.run(
        ["g++", "-std=c++20", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)


def test_whireable_role_allows_either_transfer_participant(tmp_path):
    repo_root = Path(__file__).resolve().parents[1]
    source = tmp_path / "whireable_transfer_rules_check.cpp"
    binary = tmp_path / "whireable_transfer_rules_check"

    source.write_text(
        textwrap.dedent(
            f"""
            #include "{repo_root / "src/market/transaction_rules.h"}"

            int main() {{
                if (!transactions::has_whireable_participant(true, false)) {{
                    return 1;
                }}
                if (!transactions::has_whireable_participant(false, true)) {{
                    return 2;
                }}
                if (transactions::has_whireable_participant(false, false)) {{
                    return 3;
                }}
                return 0;
            }}
            """
        )
    )

    subprocess.run(
        ["g++", "-std=c++20", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)


def test_whireable_role_is_required_for_at_least_one_transfer_participant():
    repo_root = Path(__file__).resolve().parents[1]
    auth_header = (repo_root / "src/basic/auth.h").read_text()
    auth_source = (repo_root / "src/basic/auth.cc").read_text()
    transaction_source = (repo_root / "src/market/transactions.cc").read_text()
    transaction_header = (repo_root / "src/market/transactions.h").read_text()
    transaction_rules = (repo_root / "src/market/transaction_rules.h").read_text()
    schema = (repo_root / "docs/schema.sql").read_text()
    migration = (repo_root / "docs/whireable_role_migration.sql").read_text()

    assert '"whireable"' in auth_source
    assert "bool is_active" in auth_header
    assert "bool can_receive_transfer" in transaction_header
    assert "bool has_whireable_participant" in transaction_header
    assert 'auth::is_rights_by_username(username, pool_ptr, "whireable")' in transaction_source
    assert "return sender_whireable || receiver_whireable;" in transaction_rules
    assert "has_whireable_participant(sender_whireable, can_receive_transfer(receiver, pool_ptr))" in transaction_source
    assert "TransactionStatus::NotReceiver" in transaction_source
    assert "sender or receiver must be whireable" in transaction_source
    assert "whireable boolean DEFAULT false" in schema
    assert "ADD COLUMN IF NOT EXISTS whireable boolean DEFAULT false" in migration


def test_transactions_require_active_users():
    repo_root = Path(__file__).resolve().parents[1]
    transaction_source = (repo_root / "src/market/transactions.cc").read_text()
    transaction_header = (repo_root / "src/market/transactions.h").read_text()

    assert "bool can_use_transactions" in transaction_header
    assert "TransactionStatus::InactiveUser" in transaction_source
    assert "auth::is_active(username, pool_ptr)" in transaction_source
    assert 'set_body("user is not active")' in transaction_source
    assert transaction_source.count("transactions::can_use_transactions(username, pool_ptr)") >= 3
    assert "can_use_transactions(transaction->sender, pool_ptr)" in transaction_source
    assert "can_use_transactions(transaction->receiver, pool_ptr)" in transaction_source


def test_admin_sender_bypasses_transaction_recipient_restrictions():
    repo_root = Path(__file__).resolve().parents[1]
    transaction_source = (repo_root / "src/market/transactions.cc").read_text()

    assert "bool sender_is_admin = auth::is_rights_by_username(transaction->sender, pool_ptr);" in transaction_source
    assert "const bool sender_is_admin = auth::is_rights_by_username(sender, pool_ptr);" in transaction_source
    assert "if (!sender_is_admin &&\n            (!can_use_transactions(transaction->sender, pool_ptr) ||" in transaction_source
    assert "if (!sender_is_admin && !has_whireable_participant(transaction->sender, transaction->receiver, pool_ptr))" in transaction_source
    assert "if (!sender_is_admin && !can_use_transactions(sender, pool_ptr))" in transaction_source
    assert "if (!sender_is_admin && !can_use_transactions(reciver, pool_ptr))" in transaction_source
    assert "if (!sender_is_admin && !has_whireable_participant(sender, reciver, pool_ptr))" in transaction_source
    assert transaction_source.count("if (!sender_is_admin && !transactions::can_use_transactions(") >= 2


def test_transfer_cooldown_is_enforced_for_non_admin_senders():
    repo_root = Path(__file__).resolve().parents[1]
    transaction_source = (repo_root / "src/market/transactions.cc").read_text()
    transaction_header = (repo_root / "src/market/transactions.h").read_text()
    transaction_rules = (repo_root / "src/market/transaction_rules.h").read_text()

    assert "constexpr int kTransferCooldownSeconds = 5;" in transaction_rules
    assert "Cooldown," in transaction_header
    assert "int transfer_cooldown_seconds_remaining" in transaction_header
    assert 'FROM "transactions"' in transaction_source
    assert "WHERE sender = $1" in transaction_source
    assert "MAX(transactiontime)" in transaction_source
    assert "LOCALTIMESTAMP - MAX(transactiontime)" in transaction_source
    assert "transfer_cooldown_seconds_remaining(sender, pool_ptr)" in transaction_source
    assert "transfer_cooldown_seconds_remaining(transaction->sender, pool_ptr)" in transaction_source
    assert transaction_source.count("if (is_transfer_cooldown_active(") == 2
    assert transaction_source.count("sender_is_admin))") >= 2
    assert transaction_source.count("TransactionStatus::Cooldown") >= 3
    assert transaction_source.count("restinio::status_too_many_requests()") >= 2
    assert transaction_source.count('append_header("Retry-After", std::to_string(kTransferCooldownSeconds))') >= 2
    assert transaction_source.count('set_body("transfer cooldown active")') >= 2
