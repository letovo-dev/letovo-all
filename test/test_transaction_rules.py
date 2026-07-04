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


def test_whireable_role_is_required_for_transfer_recipient():
    repo_root = Path(__file__).resolve().parents[1]
    auth_source = (repo_root / "src/basic/auth.cc").read_text()
    transaction_source = (repo_root / "src/market/transactions.cc").read_text()
    transaction_header = (repo_root / "src/market/transactions.h").read_text()
    schema = (repo_root / "docs/schema.sql").read_text()
    migration = (repo_root / "docs/whireable_role_migration.sql").read_text()

    assert '"whireable"' in auth_source
    assert "bool can_receive_transfer" in transaction_header
    assert 'auth::is_rights_by_username(username, pool_ptr, "whireable")' in transaction_source
    assert "TransactionStatus::NotReceiver" in transaction_source
    assert "receiver is not whireable" in transaction_source
    assert "whireable boolean DEFAULT false" in schema
    assert "ADD COLUMN IF NOT EXISTS whireable boolean DEFAULT false" in migration
