#include "hash.h"

int EXPIRATION_TIME = 86400;
std::unordered_map<std::string, std::pair<std::string, time_t>> hash_table;
std::unordered_set<std::string> new_users;

namespace hashing {
    string hash_from_string(const string& input) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        std::string str_to_hash;
        if (Config::giveMe().server_config.update_hashes) {
            str_to_hash = input + to_string(
                chrono::duration_cast<chrono::seconds>(chrono::system_clock::now()
                .time_since_epoch())
                .count()
            );
        } else {
            str_to_hash = input;
        }
        SHA256(reinterpret_cast<const unsigned char*>(str_to_hash.c_str()), input.size(), hash);

        // Convert hash to hex string
        stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
        }

        string hashString = ss.str();

        // Store the hash and current time
        hash_table[hashString] = make_pair(input, time(nullptr));

        return hashString;
    }

    // Function to retrieve the original string from the hash
    string string_from_hash(const string& hash) {
        auto it = hash_table.find(hash);
        if (it != hash_table.end()) {
            // Check if the hash is expired
            if (time(nullptr) - it->second.second <= EXPIRATION_TIME) {
                return it->second.first; // Return original string
            } else {
                hash_table.erase(it); // Remove expired hash
                return "Hash expired";
            }
        }
        return "Hash not found";
    }

    bool defele_from_hash(const std::string& hash) {
        auto it = hash_table.find(hash);
        if (it != hash_table.end()) {
            hash_table.erase(it);
            return true;
        }
        return false;
    }

    bool check_new_user(const std::string& hash) {
        bool exists = new_users.find(hash) != new_users.end();
        if (exists) {
            new_users.erase(hash);
        }
        return exists;
    }

    void add_new_user(const std::string& input) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

        // Convert hash to hex string
        stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
        }

        string hashString = ss.str();

        new_users.insert(hashString);
    }
} // namespace hashing
