import os
import re

def search_and_write(directory, regex_pattern, output_file):
    """
    Searches for a regex pattern in files within a directory recursively
    and writes matches from group 2 to an output file.

    :param directory: Directory to search files in.
    :param regex_pattern: Regular expression pattern to search for.
    :param output_file: Path to the output file.
    """
    regex = re.compile(regex_pattern)

    with open(output_file, 'w') as output:
        for root, _, files in os.walk(directory):
            for file in files:
                file_path = os.path.join(root, file)
                try:
                    with open(file_path, 'r', encoding='utf-8') as f:
                        for line in f:
                            match = regex.search(line)
                            if match and len(match.groups()) >= 2:
                                output.write(match.group(2) + '\n')
                except Exception as e:
                    print(f"Error reading file {file_path}: {e}")

if __name__ == "__main__":
    # Example usage:
    search_directory = "/path/to/search"  # Replace with your directory
    regex_to_search = r'(some-pattern)(group2)'  # Replace with your regex pattern
    output_file_path = "/path/to/output.txt"  # Replace with your output file path

    search_and_write(search_directory, regex_to_search, output_file_path)
