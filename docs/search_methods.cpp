#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <string>

namespace fs = std::filesystem;

void searchAndWrite(const std::string& directory, const std::string& regexPattern, const std::string& outputFilePath) {
    std::regex pattern(regexPattern);
    std::ofstream outputFile(outputFilePath);

    if (!outputFile.is_open()) {
        std::cerr << "Error: Could not open output file." << std::endl;
        return;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::ifstream inputFile(entry.path());

                if (!inputFile.is_open()) {
                    std::cerr << "Error: Could not open file " << entry.path() << std::endl;
                    continue;
                }

                std::string line;
                while (std::getline(inputFile, line)) {
                    std::smatch match;
                    if (std::regex_search(line, match, pattern) && match.size() > 2) {
                        outputFile << match[2] << std::endl;
                    }
                }

                inputFile.close();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    outputFile.close();
}

int main() {
    // Example usage:
    std::string directory = "/path/to/search"; // Replace with your directory
    std::string regexPattern = R"(some-pattern(group1)(group2))"; // Replace with your regex
    std::string outputFilePath = "/path/to/output.txt"; // Replace with your output file path

    searchAndWrite(directory, regexPattern, outputFilePath);

    return 0;
}
