//
// Created by simone on 12/08/20.
//

#include <fstream>
#include "../../Headers/General/Message.h"
#include "../../Headers/General/Utils.h"

using rapidxml::xml_document;
using rapidxml::xml_attribute;
using rapidxml::xml_node;

/** Create a node containing the information of a file
 * */
void createFileNode(xml_document<> &doc, xml_node<> *parent, const fs::path &file_path, bool hash_required) {
    auto *file = doc.allocate_node(rapidxml::node_element, "file");
    file->value(doc.allocate_string(file_path.filename().c_str()));
    xml_attribute<> *size = doc.allocate_attribute("size",
                                                   doc.allocate_string(std::to_string(
                                                           fs::file_size(file_path)).c_str()));
    file->append_attribute(size);
    std::ifstream is{file_path};
    if (hash_required) {
        std::optional<std::string> hash_code = Utils::SimpleSHA512(&is);

        if (!hash_code) {
            Utils::getLog(Utils::Error)->error("File {} is missing", file_path.string());
            return;
        }

        xml_attribute<> *hash = doc.allocate_attribute("hash", doc.allocate_string(hash_code->c_str()));
        file->append_attribute(hash);
    }
    parent->append_node(file);
}

/** Create a node containing the information of a directory
 * */
void createDirNode(xml_document<> &doc, xml_node<> *parent, const fs::path &dir_path, bool hash_required) {
    xml_node<> *dir = doc.allocate_node(rapidxml::node_element, "directory");
    xml_attribute<> *attr = doc.allocate_attribute("name", doc.allocate_string(dir_path.filename().c_str()));
    dir->append_attribute(attr);
    parent->append_node(dir);

    xml_node<> *directories = doc.allocate_node(rapidxml::node_element, "directories");
    xml_node<> *files = doc.allocate_node(rapidxml::node_element, "files");
    dir->append_node(directories);
    dir->append_node(files);
    for (auto &p: fs::directory_iterator(dir_path)) {
        if (fs::is_directory(p))
            createDirNode(doc, directories, p, hash_required);
        if (fs::is_regular_file(p))
            createFileNode(doc, files, p, hash_required);
    }
}

/** Create the header for a file system xml
 * */
void createFSXML(rapidxml::xml_document<> &doc, const std::filesystem::path &dir_path, bool hash_required = false) {
    auto data = doc.first_node("message")->first_node("data");
    if (fs::exists(dir_path) && fs::is_directory(dir_path))
        createDirNode(doc, data, dir_path, hash_required);
    else if (fs::exists(dir_path) && fs::is_regular_file(dir_path))
        createFileNode(doc, data, dir_path, hash_required);
}
/** Create a generic message header
 * */
xml_node<> *createHeaderXML(rx::xml_document<> &doc, const std::string &serviceName) {
    auto root = doc.allocate_node(rx::node_element, "message");
    doc.append_node(root);

    auto serviceNode = doc.allocate_node(rapidxml::node_element, "service");
    serviceNode->value(doc.allocate_string(serviceName.c_str()));
    root->append_node(serviceNode);
    auto dataNode = doc.allocate_node(rapidxml::node_element, "data");
    root->append_node(dataNode);
    return dataNode;
}

bool Message::SyncAckMessage(xml_document<> &doc) {
    createHeaderXML(doc, "syncack");
    return true;
}

bool Message::SyncMessage(rx::xml_document<> &doc, const fs::path &path, const fs::path &rootPath, bool remove) {
    if (path.empty() || rootPath.empty()) {
        std::stringstream error;
        error << (path.empty() ? "\n\t- path value empty" : "");
        error << (rootPath.empty() ? "\n\t- root path value empty" : "");
        Utils::getLog(Utils::Error)->error("Wrong parameters for service Sync: {}", error.str());
        return false;
    }

    auto data_node = createHeaderXML(doc, "sync");

    fs::path relative;
    try {
        relative = fs::relative(path, rootPath);
    } catch (fs::filesystem_error &error) {
        EXCLOG->error(error.what());
        return false;
    }
    if (fs::is_directory(path)) {
        auto dir_node = doc.allocate_node(rx::node_element, "file");
        data_node->append_node(dir_node);
        dir_node->value(doc.allocate_string(relative.c_str()));
        auto size_attr = doc.allocate_attribute("size", doc.allocate_string("-1"));
        dir_node->append_attribute(size_attr);
    } else if (fs::is_regular_file(path) && !remove) {
        auto file_node = doc.allocate_node(rx::node_element, "file");
        data_node->append_node(file_node);
        file_node->value(doc.allocate_string(relative.c_str()));
        auto size_attr = doc.allocate_attribute("size",
                                                doc.allocate_string(std::to_string(fs::file_size(path)).c_str()));
        file_node->append_attribute(size_attr);
    } else if (remove) {
        auto file_node = doc.allocate_node(rx::node_element, "file");
        data_node->append_node(file_node);
        file_node->value(doc.allocate_string(relative.c_str()));
        auto size_attr = doc.allocate_attribute("size", doc.allocate_string("-2"));
        file_node->append_attribute(size_attr);
    }
    else return false;
    return true;
}

bool Message::CheckSyncMessage(xml_document<> &doc, const fs::path &rootPath) {
    if (rootPath.empty() || !fs::exists(rootPath)) {
        ERRLOG->error("Wrong parameters for service Sync: root path doesn't exists");
        return false;
    }
    createHeaderXML(doc, "checksync");
    STDLOG->info("Starting CheckSync protocol");
    createFSXML(doc, rootPath, true);
    STDLOG->info("Ending CheckSync message creation");
    return true;
}

void Message::AuthenticationResponseMessage(xml_document<> &doc, bool success, const std::string &message) {
    auto dataNode = createHeaderXML(doc, "authentication_response");

    auto success_node = doc.allocate_node(rx::node_element, "success");
    auto message_attr = doc.allocate_attribute("message", doc.allocate_string(message.c_str()));
    success_node->append_attribute(message_attr);
    success_node->value(doc.allocate_string((success ? "true" : "false")));
    dataNode->append_node(success_node);
}

bool Message::AuthenticationMessage(xml_document<> &doc, const std::string &user, const std::string &pass) {
    if (pass.empty() || user.empty()) {
        std::stringstream error;
        error << (pass.empty() ? "\n\t- pass value empty" : "");
        error << (user.empty() ? "\n\t- user value empty" : "");
        Utils::getLog(Utils::Error)->error("Wrong parameters for service Sync: {}", error.str());
        return false;
    }
    auto dataNode = createHeaderXML(doc, "authentication");

    auto user_node = doc.allocate_node(rx::node_element, "user");
    user_node->value(doc.allocate_string(user.c_str()));
    dataNode->append_node(user_node);

    auto pass_node = doc.allocate_node(rx::node_element, "pass");
    pass_node->value(doc.allocate_string(pass.c_str()));
    dataNode->append_node(pass_node);

    return true;
}

bool Message::SigningUpMessage(xml_document<> &doc, const std::string &user, const std::string &pass) {
    if (pass.empty() || user.empty()) {
        std::stringstream error;
        error << (pass.empty() ? "\n\t- pass value empty" : "");
        error << (user.empty() ? "\n\t- user value empty" : "");
        Utils::getLog(Utils::Error)->error("Wrong parameters for service SignUp: {}", error.str());
        return false;
    }

    auto dataNode = createHeaderXML(doc, "signup");

    auto user_node = doc.allocate_node(rx::node_element, "user");
    user_node->value(doc.allocate_string(user.c_str()));
    dataNode->append_node(user_node);

    auto pass_node = doc.allocate_node(rx::node_element, "pass");
    pass_node->value(doc.allocate_string(pass.c_str()));
    dataNode->append_node(pass_node);

    return true;
}

void
Message::CheckSyncResponseMessage(xml_document<> &doc, const std::list<fs::path> &files, const fs::path &root_path) {
    createHeaderXML(doc, "checksyncresponse");
    auto files_node = doc.allocate_node(rx::node_element, "files");
    doc.first_node()->first_node("data")->append_node(files_node);
    for (auto &file : files) {
        fs::path relative;
        try {
            relative = fs::relative(file, root_path);
            auto file_node = doc.allocate_node(rx::node_element, "file");
            file_node->value(doc.allocate_string(relative.c_str()));
            files_node->append_node(file_node);
        } catch (fs::filesystem_error &error) {
            EXCLOG->error(error.what());
        }

    }
}

bool Message::authenticated(rx::xml_document<> &doc) {
    auto success_node = doc.first_node()->first_node("data")->first_node("success");
    STDLOG->info("Received authentication response: {}", success_node->first_attribute("message")->value());
    return strcmp(success_node->value(), "true") == 0;
}

std::string Message::getMessage(rx::xml_document<> &doc){
    return std::string(doc.first_node()->first_node("data")->first_node("success")->first_attribute("message")->value());
}

std::list<fs::path> Message::checksync_list(xml_document<> &doc) {
    std::list<fs::path> files_list;
    STDLOG->info("Parsing checksync response xml");
    VERBLOG->info("Checksync response xml \n{}", Utils::XMLDebugToString(doc));
    auto files_node = doc.first_node("message")->first_node("data")->first_node("files");
    for (auto node = files_node->first_node("file"); node; node = node->next_sibling("file")) {
        fs::path file{node->value()};
        files_list.push_back(file);
    }
    return files_list;
}

void Message::RestoreMessage(xml_document<> &doc) {
    createHeaderXML(doc, "restore");
}

void Message::RestoreEndMessage(xml_document<> &doc) {
    createHeaderXML(doc, "restoreend");
}

