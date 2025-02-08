#include "assist_funcs.h"
#include <iostream>
namespace assist{
    std::unordered_map<std::string, std::any> parse_page_content(rapidjson::Document& new_body) {
        std::unordered_map<std::string, std::any> new_map;
        if(new_body.HasMember("author")) {
            std::string author = new_body["author"].GetString();
            new_map["author"] = std::any(author);
        } else {
            new_map["author"] = std::any("");
        }
        if(new_body.HasMember("is_secret") && new_body["is_secret"].GetBool()) {
            new_map["is_secret"] = std::any(true);
        } else {
            new_map["is_secret"] = std::any(false);
        }
        if(new_body.HasMember("is_published") && new_body["is_published"].GetBool()) {
            new_map["is_published"] = std::any(true);
        } else {
            new_map["is_published"] = std::any(false);
        }
        if(new_body.HasMember("post_path")) {
            std::string post_path = new_body["post_path"].GetString();
            new_map["post_path"] = std::any(post_path);
        } else {
            return {};
        }
        if(new_body.HasMember("likes")) {
            int likes = new_body["likes"].GetInt();
            new_map["likes"] = std::any(likes);
        } else {
            new_map["likes"] = std::any(0);
        }
        if(new_body.HasMember("text")) {
            std::string text = new_body["text"].GetString();
            new_map["text"] = std::any(text);
        } else {
            return {};
        }
        if(new_body.HasMember("title")) {
            std::string title = new_body["title"].GetString();
            new_map["title"] = std::any(title);
        } else {
            new_map["title"] = std::any("");
        }
        return new_map;
    }

    void add_id_to_page(const std::string& filePath, int number, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        if (!std::filesystem::exists(filePath)) {
            logger_ptr->error( [filePath]{return fmt::format("File does not exist: {}", filePath);});
            return;
        }
        std::ifstream inFile(filePath);
        if (!inFile) {
            logger_ptr->error( [filePath]{return fmt::format("Failed to open file for reading: {}", filePath);});
            return;
        }
        std::string firstLine;
        std::getline(inFile, firstLine); 

        std::string remainingContent;
        if (inFile) {
            remainingContent.assign((std::istreambuf_iterator<char>(inFile)),
                                    std::istreambuf_iterator<char>());
        }
        inFile.close();

        firstLine += " " + std::to_string(number);

        std::ofstream outFile(filePath);
        if (!outFile) {
            logger_ptr->error( [filePath]{return fmt::format("Failed to open file for writing: {}", filePath);});
            return;
        }

        outFile << firstLine << '\n' << remainingContent;
        outFile.close();

        logger_ptr->info( [filePath]{return fmt::format("Added id to file: {}", filePath);});
    }

    void create_file(const std::string &filePath, const std::string &content, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        std::ofstream outFile(filePath);
        if (!outFile) {
            logger_ptr->error( [filePath]{return fmt::format("Failed to open file for writing: {}", filePath);});
            return;
        }

        outFile << content;
        outFile.close();

        logger_ptr->info( [filePath]{return fmt::format("Created file: {}", filePath);});
    }

    void create_file(const std::string &filePath, std::unique_ptr<const std::string> content) {
        std::ofstream outFile(filePath);
        if (!outFile) {
            std::cerr << "Failed to open file for writing: " << filePath << std::endl;
            return;
        }

        outFile << content.get();
        outFile.close();
        content.reset();
    }

    void create_mdx_from_template_file(const std::string &filePath, const std::string &title, const std::string & post_id, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        std::string mdx_template_text = "import { useState, useEffect } from \"react\";\n\n#" + title + "\n\nexport const PokemonData = () => {\n  const [pokemon, setPokemon] = useState(null);\n  useEffect(() => {\n    const fetchPokemon = () => {\n        fetch('/api/post/" + post_id + "', {\n                    method: 'GET',\n                    headers: {\n                        'Content-Type': 'application/json'\n                    }\n                })\n        .then((result) => result.json())\n        .then((data) => setPokemon(data));\n    };\n    fetchPokemon();\n  }, []);\n  if (!pokemon) {\n    return <h1>Загрузка...</h1>;\n  }\n  return <pre>{JSON.stringify(pokemon[\"result\"][0][\"text\"], null, 2).slice(1, -1)}</pre>;\n};\n\n<PokemonData />\n";

        create_file(filePath, mdx_template_text, logger_ptr);
    }

    void create_mdx_from_template_file(const std::string &filePath, const std::string &title, const std::string &author_id, const std::string & post_id, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        std::string mdx_template_text = "import { useState, useEffect } from \"react\";\nimport { AuthorCard } from '../src/author_card';\n\n# " + title + "\n\nexport const AuthorCardElement = () => {\n  const [pokemon, setPokemon] = useState(null);\n  useEffect(() => {\n    const fetchPokemon = () => {\n        fetch('/api/post/author/" + author_id + "', {\n                    method: 'GET',\n                    headers: {\n                        'Content-Type': 'application/json'\n                    }\n                })\n        .then((result) => result.json())\n        .then((data) => setPokemon(data));\n    };\n    fetchPokemon();\n  }, []);\n  if (!pokemon) {\n    return <h1>Загрузка...</h1>;\n  }\n    return <AuthorCard \n    name={JSON.stringify(pokemon[\"result\"][0][\"username\"]).slice(1, -1)}\n   ava={JSON.stringify(pokemon[\"result\"][0][\"avatar_pic\"]).slice(1, -1)}\n    authorLink={\"/user/page/\" + JSON.stringify(pokemon[\"result\"][0][\"username\"]).slice(1, -1)} \n    />\n};\n\nexport const PokemonData = () => {\n  const [pokemon, setPokemon] = useState(null);\n  useEffect(() => {\n    const fetchPokemon = () => {\n        fetch('/api/post/" + post_id + "', {\n                    method: 'GET',\n                    headers: {\n                        'Content-Type': 'application/json'\n                    }\n                })\n        .then((result) => result.json())\n        .then((data) => setPokemon(data));\n    };\n    fetchPokemon();\n  }, []);\n  if (!pokemon) {\n    return <h1>Загрузка...</h1>;\n  }\n  return <pre>{JSON.stringify(pokemon[\"result\"][0][\"text\"], null, 2).slice(1, -1)}</pre>;\n};\n\n<AuthorCardElement />\n\n<PokemonData />\n";

        create_file(filePath, mdx_template_text, logger_ptr);
    }
}