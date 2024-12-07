#include "hash.h"

int EXPIRATION_TIME = 86400; 
std::unordered_map<std::string, std::pair<std::string, time_t>> hash_table;
namespace hashing {
    string hash_from_string(const string& input) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

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
}