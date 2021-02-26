#pragma once

#include <iostream>
#include "boost/asio.hpp"
#include <filesystem>
#include <spdlog/async.h>
#include <boost/bind/bind.hpp>
#include <fstream>
#include "./../../Headers/General/Utils.h"
#include <rapidxml/rapidxml.hpp>
#include "./../../Headers/General/Message.h"
#include "./../../Headers/Client/FileWatcher.h"

namespace fs = std::filesystem;
namespace nw = boost::asio;
namespace nwip = boost::asio::ip;
namespace rx = rapidxml;

#define CHUNK_SIZE 4096L

class ClientEngine {
    //Data
private:
    std::mutex stop_mutex;
    nw::io_service &io_service;
    nwip::tcp::socket tcp_socket;
    nwip::tcp::endpoint endpoint;
    nw::steady_timer deadlineTimer;
    bool connected = false, new_user = false;
    long long fileSize, toTransfer;
    FileWatcher fw;
    Utils::threadqueue<std::pair<fs::path, FileStatus>> q;
    std::pair<fs::path, FileStatus> pendingFile;
    rx::xml_document<> nextXML, responseXML;
    fs::path root;
    nw::streambuf response;
    std::atomic<bool> restoreEnded = false, checksyncEnded = false;
    std::string pw, user;
    std::ofstream file;
    //Methods
public:
    ClientEngine(nw::io_service &io_service, const fs::path &mainDirPath, const nwip::address &addr, int port);

    bool isNewUSer() const;

    void startSync(bool first = false);

    bool connect();

    void startWatch();

    void close();

    void checkSync();

    void restore();

    void resetFolder();

    bool test();

    void stop();

    bool getRestoreEnded();

    bool getChecksyncEnded();

    bool reConnect();

    void resetWatcherDirectory();
private:

    void sendFile(const std::pair<fs::path, FileStatus> &f);

    void sendFileHeader(const fs::path &path, FileStatus fileStatus);

    void sendFileData(const fs::path &f);

    void read_response();

    void async_read_response();

    bool connect_handler(const boost::system::error_code &error);

    void resetSocket();

    void deadline_handler();

    void async_read_response_handler(const boost::system::error_code &ec, size_t byte_transferred);

    void async_read_restore_response();

    void async_read_response_restore_handler(const boost::system::error_code &ec, size_t byte_transferred);

    void readChunk();

    void async_read_data();

    void readfile_handler(const boost::system::error_code &error);

    void deadline_handler_restore();

    void async_checksync_read_response();

    void async_checksync_read_response_handler(const boost::system::error_code &ec, size_t byte_transferred);

    void deadline_handler_checksync();
};

