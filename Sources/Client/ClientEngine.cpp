#include "../../Headers/Client/ClientEngine.h"


ClientEngine::ClientEngine(nw::io_service &io_service, const fs::path &mainDirPath, const nwip::address &addr, int port)
        : io_service{io_service}, tcp_socket{io_service},
          fw{mainDirPath, q},
          root{mainDirPath},
          deadlineTimer{io_service} {
    endpoint.address(addr);
    endpoint.port(port);
}

/**
 * Try to connect until te connection is established
 */
bool ClientEngine::connect() {
    while (true) {
        try {
            boost::system::error_code ec;
            tcp_socket.connect(endpoint, ec);
            return connect_handler(ec);
        }
        catch (Utils::ConnectionException &ex) {
            EXCLOG->error(ex.what());
            resetSocket();
        }
    }
}

/**
 * Timer expire handler for not restore operations
 * */
void ClientEngine::deadline_handler() {

    if (!connected) return;
    //check if timer is realy expired
    if (deadlineTimer.expiry() <= nw::steady_timer::clock_type::now()) {
        //not responding server, reset connection and handler
        resetSocket();
        deadlineTimer.expires_at(nw::steady_timer::time_point::max());
    }

    deadlineTimer.async_wait(boost::bind(&ClientEngine::deadline_handler, this));
}

bool ClientEngine::getRestoreEnded() {
    return restoreEnded;
}

/**
 * Timer expire handler for restore operations
 * */
void ClientEngine::deadline_handler_restore() {
    if (!connected) return;

    if (deadlineTimer.expiry() <= nw::steady_timer::clock_type::now()) {
        connected = false;
    } else if (!restoreEnded) // if restore is ended, new wait not requested
        deadlineTimer.async_wait(boost::bind(&ClientEngine::deadline_handler_restore, this));

}


/**
 * Timer expire handler for restore operations
 * */
void ClientEngine::deadline_handler_checksync() {
    if (!connected) return;

    if (deadlineTimer.expiry() <= nw::steady_timer::clock_type::now()) {
        connected = false;
    } else if (!checksyncEnded) // if restore is ended, new wait not requested
        deadlineTimer.async_wait(boost::bind(&ClientEngine::deadline_handler_checksync, this));

}

void ClientEngine::close() {
    boost::system::error_code ec;
    tcp_socket.close(ec);
}

/**Close the socket, create a new one and
*clear the data buffer*/
void ClientEngine::resetSocket() {

    boost::system::error_code ec;
    response.consume(response.size());
    if (test()) this->tcp_socket.close(ec);

    std::unique_lock lk{stop_mutex};
    connected = false;
    this->tcp_socket = nwip::tcp::socket(io_service);
}

/** Wait on queue until a value is given
 * @param first indicate if the call is the first in the async stack
 * */
void ClientEngine::startSync(bool first) {
    STDLOG->info("Starting sync method");
    deadlineTimer.async_wait(boost::bind(&ClientEngine::deadline_handler, this));
    bool retry = false;
    while (true) {
        try {
            if (test()) {
                std::optional<std::pair<fs::path, FileStatus>> res;
                if (retry) {
                    res = pendingFile;
                    retry = false;
                } else {
                    while (true) {
                        res = q.pop();
                        // if res is an accepted value continue the algorithm otherwise wait for a new value on the queue
                        if (!res || fs::exists(res->first) || res->second == FileStatus::erased) break;
                    }
                }
                //if res is not empty send the appropriate information to server
                if (res) {
                    pendingFile = *res;
                    sendFile(*res);
                    if (first) io_service.run();
                    break;
                } // if res is empty kill the io_service (exit condition)
                else break;
            } else throw Utils::ConnectionException{"Error while sending header to server"};
        }
            // check for exception during execution, in case restart the connection
        catch (Utils::ConnectionException &ex) {
            EXCLOG->error(ex.what());
            resetSocket();
            while (!test()) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (reConnect()) {
                    std::unique_lock lk_reconnect{stop_mutex};
                    connected = true;
                }
            }
            retry = true;
        }
        catch (std::filesystem::filesystem_error &ex) {
            //file or directory deleted while sending data to server
            resetSocket();
            while (!test()) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (reConnect()) {
                    std::unique_lock lk_reconnect{stop_mutex};
                    connected = true;
                }
            }
        }
        catch (Utils::UtilsException &ex) {
            //file or directory deleted while sending data to server
            resetSocket();
            while (!test()) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (reConnect()) {
                    std::unique_lock lk_reconnect{stop_mutex};
                    connected = true;
                }
            }
        }
    }
    STDLOG->info("Ending sync method");
}

/** Async read handler
 * @param ec boost::system::error_code checking for communication error
 * @param byte_transferred number of byte received during communication
 * */
void ClientEngine::async_read_response_handler(const boost::system::error_code &ec, size_t byte_transferred) {
    if (!ec) {
        // parsing in xml the reading buffer
        auto response_data = response.data();
        std::string result(nw::buffers_begin(response_data), nw::buffers_begin(response_data) + byte_transferred);
        response.consume(byte_transferred);
        auto result_cstr = responseXML.allocate_string(result.c_str());
        responseXML.parse<0>(result_cstr);

        VERBLOG->info(Utils::XMLDebugToString(responseXML));
        // case switching for different system request
        if (strcmp(responseXML.first_node()->first_node("service")->value(), "syncack") == 0) {
            startSync();
        }
    } else {
        resetSocket();
        startSync();
    }
}

/** Allocate async read handler*/
void ClientEngine::async_checksync_read_response() {

    responseXML.clear();
    deadlineTimer.expires_after(boost::asio::chrono::seconds(300));

    nw::async_read_until(tcp_socket, response, "</message>",
                         boost::bind(&ClientEngine::async_checksync_read_response_handler, this,
                                     nw::placeholders::error,
                                     nw::placeholders::bytes_transferred));
}

/** Async read handler
 * @param ec boost::system::error_code checking for communication error
 * @param byte_transferred number of byte received during communication
 * */
void ClientEngine::async_checksync_read_response_handler(const boost::system::error_code &ec, size_t byte_transferred) {
    if (!ec) {
        // parsing in xml the reading buffer
        auto response_data = response.data();
        std::string result(nw::buffers_begin(response_data), nw::buffers_begin(response_data) + byte_transferred);
        response.consume(byte_transferred);
        auto result_cstr = responseXML.allocate_string(result.c_str());
        responseXML.parse<0>(result_cstr);

        VERBLOG->info(Utils::XMLDebugToString(responseXML));
        if (strcmp(responseXML.first_node()->first_node("service")->value(), "checksyncresponse") == 0) {
            auto files_list = Message::checksync_list(responseXML);
            for (const auto &it : files_list) {
                q.push(std::pair(root / it, FileStatus::created));
            }
            checksyncEnded = true;
            deadlineTimer.cancel();
        }
    } else {
        resetSocket();
        deadlineTimer.cancel();

    }
}

/** Allocate async read handler*/
void ClientEngine::async_read_response() {

    responseXML.clear();
    deadlineTimer.expires_after(boost::asio::chrono::seconds(30));

    nw::async_read_until(tcp_socket, response, "</message>",
                         boost::bind(&ClientEngine::async_read_response_handler, this, nw::placeholders::error,
                                     nw::placeholders::bytes_transferred));
}

/**
 * @param f pair composing of path and status (erased or created)
 * */
void ClientEngine::sendFile(const std::pair<fs::path, FileStatus> &f) {

    sendFileHeader(f.first, f.second);
    if (f.second != FileStatus::erased && !fs::is_directory(f.first)) {
        sendFileData(f.first);
    }
    async_read_response();

}

/**
 * @param f pair composing of path and status (erased or created)
 * */
void ClientEngine::sendFileHeader(const fs::path &path, FileStatus fileStatus) {
    bool remove = fileStatus == FileStatus::erased;
    nextXML.clear();
    STDLOG->info("Sending thing with path: {}", path.string());
    if (!Message::SyncMessage(nextXML, path, root, remove)) throw Utils::UtilsException{"Error While Creating Header"};

    boost::system::error_code ec;
    std::string sendThing = Utils::XMLToString(nextXML);
    VERBLOG->info("Sending xml: \n{}", Utils::XMLDebugToString(nextXML));
    nw::write(tcp_socket, nw::buffer(sendThing, sendThing.size()), ec);
    if (ec) throw Utils::ConnectionException{"Error while sending header to server"};
}

/** Send single file data to server
 * @param f path to file or directory
 * */
void ClientEngine::sendFileData(const fs::path &f) {
    int fileSizeS;

    fileSizeS = fs::file_size(f);
    int chunkCount = fileSizeS / CHUNK_SIZE + 1;

    std::ifstream fileS(f, std::ios::binary);

    while (chunkCount--) {
        long long sizeToTransfer = chunkCount > 0 ? CHUNK_SIZE : fileSizeS % CHUNK_SIZE;
        std::vector<unsigned char> bufferFile(sizeToTransfer);
        fileS.read((char *) bufferFile.data(), sizeToTransfer);
        boost::system::error_code ec;
        nw::write(tcp_socket, nw::buffer((char *) bufferFile.data(), sizeToTransfer), ec);
        if (ec)
            throw Utils::ConnectionException{"Error while sending fileS to server"};
    }

}

bool ClientEngine::reConnect() {
    STDLOG->info("Trying connection");
    boost::system::error_code ec;
    tcp_socket.connect(endpoint, ec);
    if (ec) {
        ERRLOG->info("Reconnection error");
        return false;
    }

    nextXML.clear();
    Message::AuthenticationMessage(nextXML, this->user, this->pw);
    std::string sendThing = Utils::XMLToString(nextXML);
    nw::write(tcp_socket, nw::buffer(sendThing, sendThing.size()), ec);
    if (ec) {
        ERRLOG->info("Reconnection error");
        return false;
    }

    read_response();
    bool return_value = Message::authenticated(responseXML);
    STDLOG->info(return_value ? "Connection opened" : "Reconnect failed");
    return return_value;
}

/** Handler for connect operation
 * @param error boost error_code for communication error
 * */
bool ClientEngine::connect_handler(const boost::system::error_code &error) {
    if (!error) {
        connected = false;
        int a;
        while (!connected) {
            while (a != 0 && a != 1) {
                std::cout << "To authenticate insert 0, to signup insert 1: ";
                std::cin >> a;
                if (std::cin.fail()) {
                    std::cin.clear();
                    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                    a = -1;
                }
            }

            std::string tmp_user, pass;
            if (a == 0) {
                nextXML.clear();
                while (!connected) {
                    new_user = false;
                    nextXML.clear();
                    std::cout << "Username: ";
                    std::cin >> tmp_user;
                    std::cout << "Password: ";
                    std::cin >> pass;
                    //invio una richiesta di login con i dati di accesso
                    Message::AuthenticationMessage(nextXML, tmp_user, pass);
                    boost::system::error_code ec;
                    std::string sendThing = Utils::XMLToString(nextXML);
                    nw::write(tcp_socket, nw::buffer(sendThing, sendThing.size()), ec);
                    //la connessione risulta chiusa, lancio un'eccezione per terminare il processo
                    if (ec)
                        throw Utils::ConnectionException(ec.message());
                    read_response();
                    connected = Message::authenticated(responseXML);
                    std::cout << Message::getMessage(responseXML) << std::endl;
                }
                STDLOG->info("Connected to remote server");
                this->user = tmp_user;
                this->pw = pass;
                return true;
            } else if (a == 1) {
                new_user = true;
                while (!connected) {
                    nextXML.clear();
                    std::string confirmed_pass;
                    std::cout << "Username: ";
                    std::cin >> tmp_user;
                    do {
                        std::cout << "Password: ";
                        std::cin >> pass;
                        std::cout << "Confirm password: ";
                        std::cin >> confirmed_pass;
                        if (pass != confirmed_pass)
                            std::cout << "Error on password, retry" << std::endl;
                    } while (confirmed_pass != pass || pass.empty());
                    boost::system::error_code ec;
                    Message::SigningUpMessage(nextXML, tmp_user, pass);
                    std::string sendThing = Utils::XMLToString(nextXML);
                    nw::write(tcp_socket, nw::buffer(sendThing, sendThing.size()), ec);
                    //la connessione risulta chiusa, lancio un'eccezione per terminare il processo
                    if (ec)
                        throw Utils::ConnectionException(ec.message());
                    read_response();
                    connected = Message::authenticated(responseXML);
                    std::cout << Message::getMessage(responseXML) << std::endl;
                }
                try {
                    for (const auto &it : fs::recursive_directory_iterator(root)) {
                        q.push(std::pair{it, FileStatus::created});
                    }
                } catch (fs::filesystem_error &ex) {
                    ERRLOG->error("File deleted while reading info");
                    EXCLOG->error(ex.what());
                }
                this->user = tmp_user;
                this->pw = pass;
                return true;
            }
        }
    }
    return false;
}

void ClientEngine::startWatch() {
    fw.start();
}

void ClientEngine::read_response() {
    responseXML.clear();
    deadlineTimer.expires_after(boost::asio::chrono::seconds(30));

    boost::system::error_code ec;
    size_t byte_transferred = nw::read_until(tcp_socket, response, "</message>", ec);
    if (ec) {
        throw Utils::ConnectionException(ec.message());
    }

    auto response_data = response.data();
    std::string result(nw::buffers_begin(response_data), nw::buffers_begin(response_data) + byte_transferred);
    response.consume(byte_transferred);

    auto result_cstr = responseXML.allocate_string(result.c_str());
    responseXML.parse<0>(result_cstr);
}

/**
 * Calculate the SHA512 value for each file inside the synced directory and send data to server
 * */
void ClientEngine::checkSync() {
    while (!test()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (reConnect()) {
            std::unique_lock lk_reconnect{stop_mutex};
            connected = true;
        }
    }
    nextXML.clear();
    Message::CheckSyncMessage(nextXML, root);
    boost::system::error_code ec;
    deadlineTimer.cancel();
    deadlineTimer.async_wait(boost::bind(&ClientEngine::deadline_handler_checksync, this));
    std::string sendThing = Utils::XMLToString(nextXML);
    nw::write(tcp_socket, nw::buffer(sendThing, sendThing.size()), ec);
    //la connessione risulta chiusa, lancio un'eccezione per terminare il processo
    if (ec)
        throw Utils::ConnectionException(ec.message());
    //waiting for checksync response
    async_checksync_read_response();
}

//clear folder
void ClientEngine::resetFolder() {
    for (const auto &entry : std::filesystem::directory_iterator(root))
        std::filesystem::remove_all(entry.path());
}

void ClientEngine::restore() {
    while (!test()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (reConnect()) {
            std::unique_lock lk_reconnect{stop_mutex};
            connected = true;
        }
    }
    nextXML.clear();
    deadlineTimer.cancel();
    deadlineTimer.async_wait(boost::bind(&ClientEngine::deadline_handler_restore, this));
    Message::RestoreMessage(nextXML);
    boost::system::error_code ec;
    std::string sendThing = Utils::XMLToString(nextXML);
    nw::write(tcp_socket, nw::buffer(sendThing, sendThing.size()), ec);
    if (ec)
        throw Utils::ConnectionException(ec.message());
    async_read_restore_response();
}

/** Handler for  file chunk reading
 * */
void ClientEngine::readfile_handler(const boost::system::error_code &error) {
    if (error) {
        int num = deadlineTimer.cancel();
        STDLOG->info("Removed {} handler", num);
        return;
    }
    readChunk();
}

inline void ClientEngine::async_read_data() {
    deadlineTimer.expires_after(boost::asio::chrono::seconds(30));
    nw::async_read(tcp_socket, response, nw::transfer_exactly(toTransfer),
                   [capture0 = this](boost::system::error_code ec, size_t size) {
                       capture0->readfile_handler(ec);
                   });
}

/** Read all data present on the buffer until fileSize is reached
 * */
void ClientEngine::readChunk() {
    long long readed = std::min(fileSize, (long long) response.size());
    fileSize -= readed;
    std::vector<unsigned char> fileChunk(nw::buffers_begin(response.data()),
                                         nw::buffers_begin(response.data()) + readed);
    response.consume(readed);
    file.write((char *) fileChunk.data(), readed);
    file.flush();
    toTransfer = std::min((long long) CHUNK_SIZE, fileSize);
    if (fileSize != 0) {
        async_read_data();
    } else {
        async_read_restore_response();
    }
}

/** Restore response handler
 * */
void ClientEngine::async_read_response_restore_handler(const boost::system::error_code &ec, size_t byte_transferred) {
    if (!ec) {
        auto response_data = response.data();
        std::string result(nw::buffers_begin(response_data), nw::buffers_begin(response_data) + byte_transferred);
        response.consume(byte_transferred);
        responseXML.clear();
        auto result_cstr = responseXML.allocate_string(result.c_str());
        responseXML.parse<0>(result_cstr);
        std::string service{responseXML.first_node()->first_node("service")->value()};
        // if sync message preparing for file receving
        if (service == "sync") {
            fs::path file_path{responseXML.first_node()->first_node("data")->first_node("file")->value()};
            fileSize = std::stoll(
                    responseXML.first_node()->first_node("data")->first_node("file")->first_attribute("size")->value(),
                    nullptr, 10);
            if (fileSize == -1) {
                fs::create_directories(root / file_path);
                async_read_restore_response();
            } else if (fileSize >= 0) {
                if (!fs::exists((root / file_path).parent_path()))
                    fs::create_directories((root / file_path).parent_path());
                std::ofstream foo = std::ofstream{root / file_path, std::ios::binary};
                file.swap(foo);
                readChunk();
            }
        } else {
            restoreEnded = true;
            deadlineTimer.cancel();
        }
    } else {
        deadlineTimer.cancel();
        resetSocket();
    }
}

void ClientEngine::async_read_restore_response() {
    responseXML.clear();
    deadlineTimer.expires_after(boost::asio::chrono::seconds(30));

    nw::async_read_until(tcp_socket, response, "</message>",
                         boost::bind(&ClientEngine::async_read_response_restore_handler, this,
                                     nw::placeholders::error,
                                     nw::placeholders::bytes_transferred));
}

/** Stop filewatcher and notify last element to queue
 * */
void ClientEngine::stop() {
    fw.stop();
    std::unique_lock lk{stop_mutex};
    connected = false;
    lk.unlock();
    q.notify_last();
    deadlineTimer.cancel();
}

/** Test socket for active connection
 * */
bool ClientEngine::test() {
    std::unique_lock lk{stop_mutex};
    bool res = connected;
    lk.unlock();
    return res;
}

/** Return user status (new or not)
 * */
bool ClientEngine::isNewUSer() const {
    return new_user;
}

bool ClientEngine::getChecksyncEnded() {
    return checksyncEnded;
}

void ClientEngine::resetWatcherDirectory() {
    fw.resetDirectory();
}

