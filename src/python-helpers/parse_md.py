import re
import sys, os

link_line = re.compile(r"\!\[.*?\]\((.*?)\/api.*?\)")


def fix_file(file_path: str, new_link: str) -> str:
    with open(file_path, 'r', encoding='utf-8') as file:
        content = file.read()
    
    content = link_line.sub(lambda match: match.group(0).replace(match.group(1), new_link), content)
    
    # Write the changes back to the file
    with open(file_path, 'w', encoding='utf-8') as file:
        file.write(content)
    
    return file_path


if __name__ == "__main__":
    this_file_foler = os.path.dirname(os.path.abspath(__file__))
    new_link = sys.argv[1] if len(sys.argv) > 1 else "http:://localhost"
    target_foler = os.path.join(this_file_foler, "..", "pages")
    for root, dirs, files in os.walk(target_foler):
        for file in files:
            if file.endswith(".md"):
                file_path = os.path.join(root, file)
                print(f"Fixing {file_path}")
                fix_file(file_path, new_link)