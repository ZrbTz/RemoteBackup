//
// Created by simone on 09/07/20.
//

#pragma once

#define CHUNK_SIZE 4096L

#include <cstddef>
#include <list>
#include <fstream>
#include "./../../Headers/General/Utils.h"
#include "Database.h"
#include <boost/asio.hpp>
#include <set>
#include <fstream>


namespace nw = boost::asio;
namespace nwip = boost::asio::ip;


class ServerEngine {
    // enable_shared_from_this needed to ensure object exist until at least one pending async operation
    class tcp_connection : public std::enable_shared_from_this<tcp_connection> {
        friend class ServerEngine;
//data
    public:
        typedef std::shared_ptr<tcp_connection> pointer;
    private:
        std::mutex root_mutex, write_mutex, data_mutex;
        nwip::tcp::socket socket_;
        nw::streambuf data_;

        std::atomic<bool> needToKill = false;

        std::ofstream file;
        long long fileSize;
        long long toTransfer;

        bool authenticated = false;
        fs::path root;
        std::string currentUser;
        std::string userIP;
        nw::steady_timer deadlineTimer;
        ServerEngine *server;
//methods
    public:
        static pointer create(boost::asio::io_service &io_service, ServerEngine *server);

        nwip::tcp::socket &socket();

        void start();

        long long execCommand(rx::xml_document<> &doc, const std::string &document_string);

        ~tcp_connection();

        void write(std::string message);

        void writeRestore(std::vector<unsigned char>& message, long long size);

        fs::path getRoot();

        std::string getConnectionData();

    private:
        tcp_connection(boost::asio::io_service &io_service, ServerEngine *server);

        void authenticate(rx::xml_node<> *data);

        void signingUp(rx::xml_node<> *data);

        void read_handler(const boost::system::error_code &error, size_t byte_transferred);

        void readfile_handler(const boost::system::error_code &error);

        void readChunk();

        void deadline_handler(boost::system::error_code&);

        inline void async_read_header();

        inline void async_read_data();
    };
    //data
private:
    nwip::tcp::acceptor acceptor_;
    nw::io_context &io_context_;
    std::atomic<int> connection_count;
    int connection_limit;
    Utils::threadSetString currentlyConnectedUsers;
    fs::path mainFolder;

    //methods
public:
    ServerEngine(nw::io_context &io_service, int connection_limit, fs::path mainFolder, int port);

private:
    void start_accept();

    void handle_accept(const tcp_connection::pointer &new_connection,
                       const boost::system::error_code &error);
};
