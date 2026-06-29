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
