//
// Created by simone on 12/08/20.
//

#ifndef FILE_TRANSFER_TEST_MESSAGE_H
#define FILE_TRANSFER_TEST_MESSAGE_H

#include <filesystem>
#include <rapidxml/rapidxml.hpp>
#include <list>

namespace fs = std::filesystem;
namespace rx = rapidxml;

namespace Message {

    //Messaggi comuni
    bool SyncMessage(rx::xml_document<> &doc, const fs::path& path, const fs::path& rootPath, bool remove = false);

    bool SyncAckMessage(rx::xml_document<> &doc);

    //Messaggi del client

    bool CheckSyncMessage(rx::xml_document<> &doc, const fs::path& rootPath);

    bool AuthenticationMessage(rx::xml_document<> &doc, const std::string& user, const std::string& pass);

    bool SigningUpMessage(rx::xml_document<> &doc, const std::string& user, const std::string& pass);

    void RestoreMessage(rx::xml_document<> &doc);

    //Messaggi del server
    void CheckSyncResponseMessage(rx::xml_document<> &doc, const std::list<fs::path>& files, const fs::path& root_path);

    void AuthenticationResponseMessage(rx::xml_document<> &doc, bool success, const std::string& message);

    bool authenticated(rx::xml_document<> &doc);

    std::string getMessage(rx::xml_document<> &doc);

    std::list<fs::path>  checksync_list(rx::xml_document<> &doc);

    void RestoreEndMessage(rx::xml_document<> &doc);
};

#endif //FILE_TRANSFER_TEST_MESSAGE_H
