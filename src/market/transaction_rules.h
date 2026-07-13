#pragma once

namespace transactions {

constexpr int kAdminBalanceLimit = 999999999;
constexpr int kTransferCooldownSeconds = 5;

inline bool can_transfer_amount(int sender_balance, int amount, bool sender_is_admin) {
    if (!sender_is_admin && amount < 0) {
        return false;
    }
    int effective_sender_balance = sender_is_admin ? kAdminBalanceLimit : sender_balance;
    return effective_sender_balance >= amount;
}

inline bool has_whireable_participant(bool sender_whireable, bool receiver_whireable) {
    return sender_whireable || receiver_whireable;
}

} // namespace transactions
