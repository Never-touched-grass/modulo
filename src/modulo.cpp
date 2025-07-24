#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include "ext/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

std::vector<std::string> find_include_dirs(const std::string& base_path) {
    std::vector<std::string> includes;
    for (auto& p : fs::recursive_directory_iterator(base_path)) {
        if (p.is_directory() && p.path().filename() == "include") {
            includes.push_back(p.path().string());
        }
    }
    return includes;
}

std::vector<std::string> find_lib_dirs(const std::string& base_path) {
    std::vector<std::string> libs;
    for (auto& p : fs::recursive_directory_iterator(base_path)) {
        if (p.is_directory() && p.path().filename() == "lib") {
            libs.push_back(p.path().string());
        }
    }
    return libs;
}

std::vector<std::string> find_dll_dirs(const std::string& base_path) {
    std::vector<std::string> dlls;
    for (auto& p : fs::recursive_directory_iterator(base_path)) {
        if (p.is_directory() && p.path().filename() == "bin") {
            dlls.push_back(p.path().string());
        }
    }
    return dlls;
}

std::string get_url_from_registry(const std::string& pkgName) {
    std::ifstream in("registry.json");
    if (!in) {
        std::cerr << "Failed to open registry.json\n";
        return "";
    }

    json registry;
    in >> registry;

    if (!registry.contains(pkgName)) {
        std::cerr << "Package not found in registry: " << pkgName << "\n";
        return "";
    }

    return registry[pkgName];
}

void install(const std::string& pkg) {
    std::string url = get_url_from_registry(pkg);
    if (url.empty()) {
        std::cerr << "Package '" << pkg << "' not found in registry.\n";
        return;
    }
    std::string zip = pkg + ".zip";
    std::string cmd = "curl -L -o \"" + zip + "\" \"" + url + "\"";
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Download failed for: " << pkg << "\n";
        return;
    }

    std::ifstream f(zip, std::ios::binary | std::ios::ate);
    if (!f || f.tellg() < 100) {
        std::cerr << "Downloaded file is too small — possibly invalid ZIP.\n";
        return;
    }

    std::string dest = std::getenv("USERPROFILE");
    dest += "\\modulo\\packages\\" + pkg;
    std::string unzip = "powershell -Command \"Expand-Archive -Path '" + zip + "' -DestinationPath '" + dest + "' -Force\"";

    if (system(unzip.c_str()) != 0) {
        std::cerr << "Unzip failed for: " << pkg << "\n";
        return;
    }
    auto dlls = find_dll_dirs(dest);
    std::string targetBin = std::getenv("USERPROFILE");
    targetBin += "\\modulo\\bin";
    for (const auto& dllDir : dlls) {
        for (const auto& entry : fs::directory_iterator(dllDir)) {
            if (entry.path().extension() == ".dll") {
                fs::copy(entry.path(), targetBin + "/" + entry.path().filename().string(), fs::copy_options::overwrite_existing);
            }
        }
    }
    std::cout << "Installed " << pkg << " successfully!\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: modulo <command> [args...]\nType modulo help for more info.\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "init") {
        if (argc < 3) {
            std::cout << "Usage: modulo init <name>\n";
            return 1;
        }
        std::ofstream cpkg("cpkg.json", std::ios::trunc);
        if (!cpkg) {
            std::cerr << "Failed to create cpkg.json\n";
            return 1;
        }
        std::string name = argv[2];
        json j;
        j["name"] = name;
        j["dependencies"] = json::array();
        cpkg << j.dump(4);
        cpkg.close();
        std::cout << "Initialized cpkg.json for project: " << name << "\n";
    } else if (command == "install") {
        if (argc < 3) {
            std::cout << "Usage: cpkg install <name>\n";
            return 1;
        }
        install(argv[2]);

    } else if (command == "add") {
        if (argc < 3) {
            std::cout << "Usage: modulo add <name>\nINFO: You need to install the package via cpkg install first.\n";
            return 1;
        }
        std::ifstream in("cpkg.json");
        if (!in) {
            std::cout << "cpkg.json not found. Try running cpkg init.\n";
            return 1;
        }
        json config;
        in >> config;
        in.close();
        config["dependencies"].push_back(argv[2]);

        std::ofstream out("cpkg.json");
        out << config.dump(4);
        std::cout << "Added " << argv[2] << " to dependencies.\n";
        out.close();
    } else if (command == "compile") {
        if (argc <= 2) {
            std::cout << "Usage: modulo compile <compiler(e.g. msvc|g++)> [compiler args...]\nINFO: This command assumes you have either compiler already installed on your system.\n";
            return 1;
        }

        std::string compiler = argv[2];
        std::string cmd;

        if (compiler == "msvc") {
            cmd += "cl ";
        } else if (compiler == "g++") {
            cmd += "g++ ";
        } else {
            std::cerr << "Unsupported compiler. Supported compilers are msvc and g++.\n";
            return 1;
        }

        std::ifstream in("cpkg.json");
        if (!in) {
            std::cerr << "cpkg.json not found. Try running cpkg init.\n";
            return 1;
        }

        json config;
        in >> config;

        if (!config.contains("dependencies")) {
            std::cerr << "Missing 'dependencies' in cpkg.json.\n";
            return 1;
        }

        std::string base = std::getenv("USERPROFILE");
        base += "\\cpkg\\packages\\";
        std::string name = "/Fe:";
        name += config["name"];
        cmd += name;
        for (const auto& dep : config["dependencies"]) {
            std::string path = base + dep.get<std::string>();
            auto includes = find_include_dirs(path);
            for (const auto& inc : includes) {
                if (compiler == "msvc") {
                    cmd += "/I\"" + inc + "\" ";
                } else {
                    cmd += "-I\"" + inc + "\" ";
                }
            }
            auto libs = find_lib_dirs(path);
            for (const auto& inc : libs) {
                if (compiler == "msvc") {
                    cmd += "/LIBPATH:\"" + inc + "\" ";
                } else {
                    cmd += "-L\"" + inc + "\" ";
                }
            }
        }
        for (int i = 3; i < argc; ++i) {
            cmd += std::string(argv[i]) + " ";
        }
        return system(cmd.c_str());
    } else if (command == "list") {
        std::ifstream in("cpkg.json");
        if(!in) {
            std::cerr << "cpkg.json not found. Try running cpkg init.\n";
            return 1;
        }
        json j;
        in >> j;
        if(!j.contains("dependencies")) {
            std::cerr << "Missing 'dependencies' in cpkg.json.\n";
            return 1;
        }
        std::cout << "Dependencies:\n";
        for(const auto& dep : j["dependencies"]) {
            std::cout << dep << "\n";
        }
    } else if (command == "remove") {
        if (argc < 3) {
            std::cout << "Usage: modulo remove <dependency name>\n";
            return 1;
        }

        std::ifstream in("cpkg.json");
        if (!in) {
            std::cerr << "cpkg.json not found. Try running cpkg init.\n";
            return 1;
        }

        json j;
        in >> j;
        in.close();

        if (!j.contains("dependencies") || !j["dependencies"].is_array()) {
            std::cerr << "Missing or invalid 'dependencies' in cpkg.json.\n";
            return 1;
        }

        auto& deps = j["dependencies"];
        auto it = std::find(deps.begin(), deps.end(), argv[2]);
        if (it != deps.end()) {
            deps.erase(it);
            std::ofstream out("cpkg.json");
            out << j.dump(4); 
            std::cout << "Removed dependency: " << argv[2] << "\n";
        } else {
            std::cout << "Dependency not found: " << argv[2] << "\n";
        }
    } else if (command == "help") {
        std::cout << "Available commands:\n";
        std::cout << "modulo init: initializes cpkg.json.\n";
        std::cout << "modulo install <libname>: installs a library on the system as long as it is on the registry.\n";
        std::cout << "modulo add <dependency>: adds a dependency to cpkg.json as long as it is installed on the system.\n";
        std::cout << "modulo compile <compilername (options are currently msvc and g++)> [compiler args...]: links static libraries and includes relevant paths.\n";
        std::cout << "modulo list: lists the current project dependencies.\n";
        std::cout << "modulo remove <dependency name>: removes a dependency from the current project.\n";

        std::ifstream regFile("registry.json");
        if (!regFile) {
            std::cerr << "Could not open registry.json to list available packages.\n";
            return 1;
        }

        json registry;
        regFile >> registry;

        std::cout << "Registry packages:\n";
        for (const auto& [name, url] : registry.items()) {
            std::cout << "- " << name << "\n";
        }
        } else {
            std::cerr << "Unkown command " << command << ".\n";
        }
    return 0;
}
