#include "./../../Headers/Server/ServerEngine.h"
#include "./../../Headers/General/Message.h"
#include <boost/asio.hpp>
#include <fstream>
#include <map>
#include <utility>
#include <algorithm>

/** Check if file checksum is equal to received hash
 * @param path pth to current file
 * @param files_list reference to the list containing  not correspond files
 * @param hash of the client file
 * @param size size of the client file
 **/
void evaulateFiles(const fs::path &path, std::list<fs::path> &files_list, const std::string &hash, long size) {
    std::ifstream file{path};
    if (fs::is_directory(path) || hash != Utils::SimpleSHA512(&file) || fs::file_size(path) != size) {
        files_list.push_back(path);
        fs::remove_all(path);
        STDLOG->info("File {} not synced with client version", path.c_str());
    }
}

/** Recursive method that explore current dir to check children status
 * @param path pth to current file
 * @param files_list reference to the list containing  not correspond files
 * @param exist signal if the current explored dir exist
 * */
void exploreDir(const fs::path &path, rx::xml_node<> *node, std::list<fs::path> &files_list, bool exist = true) {
    // Controlla se esista la cartella corrente e il suo parent
    bool status = exist && fs::exists(path);
    //    se non esiste la crea
    if (!status) {
        try {
            fs::create_directories(path);
            STDLOG->info("Directory {} created on server", path.c_str());
        } catch (std::filesystem::filesystem_error &er) {
            EXCLOG->error("Cannot create dir {}, error: {}", path.string(), er.what());
        }
    }

    //inserisco in una mappa tutti i file contenuti nella cartella in analisi e in un'altra tutte le directory
    std::map<std::string, fs::path> file_map, dir_map;
    for (const auto &it : fs::directory_iterator(path)) {
        if (fs::is_regular_file(it.path()))
            file_map[it.path().filename().string()] = it.path();
        else if (fs::is_directory(it.path()))
            dir_map[it.path().filename().string()] = it.path();
    }
    //richiama lo stesso metodo ricorsivamente su tutti i figli directory che esistono nell'xml
    for (auto dir_node = node->first_node(
            "directories")->first_node(); dir_node; dir_node = dir_node->next_sibling()) {
        //Si evitano possibili xml contraffatti per andare a interagire con file non concessi all'interno del server
        if (strcmp(dir_node->first_attribute("name")->value(), "..") != 0 &&
            strcmp(dir_node->first_attribute("name")->value(), ".") != 0) {
            fs::path dir = path / dir_node->first_attribute("name")->value();
            //escludo dalla mappa tutte le directory trovate anche nell'xml
            dir_map.erase(dir.filename().string());
            exploreDir(dir, dir_node, files_list, status);
        }
    }

    //effettua i controlli sui file elencati nella checksync
    for (auto file_node = node->first_node("files")->first_node(
            "file"); file_node; file_node = file_node->next_sibling()) {
        fs::path file = path / file_node->value();
        //se il file esiste ne lancio il controllo e lo cancello dalla mappa dei file, altrimenti finisce direttamente nella lista dei file da sincronizzare
        if (status && fs::exists(file)) {
            evaulateFiles(file, files_list, file_node->first_attribute("hash")->value(),
                          strtol(file_node->first_attribute("size")->value(), nullptr, 10));
            file_map.erase(file.filename().string());
        } else
            files_list.push_back(file);
    }
    // elimino tutti i file chhe risultano di troppo dopo la lettura del checkfile
    for (auto &it : file_map) {
        fs::remove_all(it.second);
        STDLOG->info("File {} removed from server", it.second.c_str());
    }
    // elimino tutte le directory che risultano di troppo dopo la lettura del checkfile
    for (auto &it : dir_map) {
        fs::remove_all(it.second);
        STDLOG->info("Directory {} removed from server", it.second.c_str());
    }
}

/** Method that start checksync analysis on filesystem
 */
std::list<fs::path> checkSync(const fs::path &path, const std::string &document_string) {
    rx::xml_document<> document;
    auto doc_c_string = document.allocate_string(document_string.c_str());
    try {
        document.parse<0>(doc_c_string);
    } catch (rapidxml::parse_error &er) {
        ERRLOG->error("Error while parsing CheckSync message, string in verbose.log");
        VERBLOG->error("Wrong CheckSync string: {}", document_string);
    }
    std::list<fs::path> files_list;
    auto dir_node = document.first_node()->first_node("data")->first_node("directory");
    exploreDir(path, dir_node, files_list);
    return files_list;
}


////TCP CONNECTION
/// Object that contains all data structures referring to a single client->server connection
nwip::tcp::socket &ServerEngine::tcp_connection::socket() {
    return socket_;
}

/**Start communication with client
 * */
void ServerEngine::tcp_connection::start() {
    deadlineTimer.async_wait(
            [capture0 = shared_from_this()](boost::system::error_code ec) { capture0->deadline_handler(ec); });

    std::stringstream ss;
    ss << this->socket_.remote_endpoint().address().to_string() << ":" << this->socket_.remote_endpoint().port();
    this->userIP = ss.str();
    async_read_header();
}

ServerEngine::tcp_connection::tcp_connection(boost::asio::io_service &io_service, ServerEngine *server) : socket_{
        io_service}, server{server}, fileSize{0}, toTransfer{0}, deadlineTimer{io_service} {
    data_.prepare(5000);
}


inline void ServerEngine::tcp_connection::async_read_header() {
    deadlineTimer.expires_after(boost::asio::chrono::seconds(60));
    boost::asio::async_read_until(
            socket_, data_, "</message>", [capture0 = shared_from_this()](boost::system::error_code ec, size_t size) {
                capture0->read_handler(ec, size);
            });
}

inline void ServerEngine::tcp_connection::async_read_data() {

    deadlineTimer.expires_after(boost::asio::chrono::seconds(60));
    nw::async_read(socket_, data_, nw::transfer_exactly(toTransfer),
                   [capture0 = shared_from_this()](boost::system::error_code ec, size_t size) {
                       capture0->readfile_handler(ec);
                   });
}

void ServerEngine::tcp_connection::readfile_handler(const boost::system::error_code &error) {
    if (error) {
        needToKill = true;
        int num = deadlineTimer.cancel();
        STDLOG->info("Removed {} handler", num);
        return;
    }
    readChunk();
}

/** Read all data of the file remaining in the socket buffer (at least CHUNK_SIZE)
 * */
void ServerEngine::tcp_connection::readChunk() {
    long long readed = std::min(fileSize, (long long) data_.size());
    fileSize -= readed;
    std::vector<unsigned char> fileChunk(nw::buffers_begin(data_.data()),
                                         nw::buffers_begin(data_.data()) + readed);
    data_.consume(readed);
    file.write((char *) fileChunk.data(), readed);
    file.flush();
    toTransfer = std::min((long long) CHUNK_SIZE, fileSize);
    if (fileSize != 0) {
        // No more data in the buffer, instantiate a new async_read
        async_read_data();
    } else {
        // Confirm to client file is received, wait for a new message
        rx::xml_document ack;
        Message::SyncAckMessage(ack);
        write(Utils::XMLToString(ack));
        async_read_header();
    }
}

/** Parse the message received from the client and update server status based on message request
 *
 */
void ServerEngine::tcp_connection::read_handler(const boost::system::error_code &error, size_t byte_transferred) {
    if (!error) {
        auto data = data_.data();
        std::string result(nw::buffers_begin(data), nw::buffers_begin(data) + byte_transferred);
        data_.consume(byte_transferred);
        rx::xml_document<> doc;
        auto doc_str = doc.allocate_string(result.c_str());
        try {
            doc.parse<0>(doc_str);
        } catch (rapidxml::parse_error &er) {
            ERRLOG->error("Error while parsing message, string in verbose.log");
            VERBLOG->error("Wrong  string: {}", result);
            return;
        }
        VERBLOG->info(Utils::XMLDebugToString(doc));
        fileSize = execCommand(doc, result);
        if (fileSize >= -2) {
            fs::path p{doc.first_node()->first_node("data")->first_node("file")->value()};
            if (fileSize == -1) {
                fs::create_directories(root / p);
                rx::xml_document ack;
                Message::SyncAckMessage(ack);
                write(Utils::XMLToString(ack));
                async_read_header();
            } else if (fileSize == -2) {
                rx::xml_document ack;
                fs::remove_all(root / p);
                Message::SyncAckMessage(ack);
                write(Utils::XMLToString(ack));
                async_read_header();
            } else if (fileSize >= 0) {
                fs::create_directories((root / p).parent_path());
                std::ofstream bar(root / p, std::ios::binary);
                file.swap(bar);

                readChunk();
            }
        } else if (fileSize == -4) {
            return; //metodi che instanziano una async_read autonomamente da un altro thread
        } else async_read_header();
    } else {
        ERRLOG->error("End of file received, closing connection to {}", this->userIP);
        if (socket_.is_open())
            socket_.close();

        needToKill = true;
        int num = deadlineTimer.cancel();

        STDLOG->info("Removed {} handler", num);
    }
}

/** Extract file size information from sync message
 * */
long long sync(rx::xml_node<> *data) {
    return std::stoll(data->first_node("file")->first_attribute("size")->value(), nullptr, 10);
}

/**Parse the authentication  message and ask database for password confirm
 * */
void ServerEngine::tcp_connection::authenticate(rx::xml_node<> *data) {
    std::string user{data->first_node("user")->value()};
    std::string pass{data->first_node("pass")->value()};
    bool authentication = Database::isUserRegistered(user, pass);
    // check if user already connected
    bool canLogIn = server->currentlyConnectedUsers.insert(user);
    authenticated = canLogIn && authentication;
    if (authenticated) {
        // if authenticated create a new directory if not present
        root = server->mainFolder / user;
        currentUser = user;
        if (!fs::exists(root)) {
            ERRLOG->error("Directory {} not found", root.string());
            fs::create_directory(root);
        }
    } else if (canLogIn) server->currentlyConnectedUsers.remove(user);
    //Comunicate the result to client
    rx::xml_document<> response_xml;
    if (canLogIn)
        Message::AuthenticationResponseMessage(response_xml, authenticated, (authentication ? "User authenticated"
                                                                                            : "User not present or wrong password"));
    else
        Message::AuthenticationResponseMessage(response_xml, authenticated,
                                               (authentication ? "Cannot login: user already connected"
                                                               : "User not present or wrong password"));

    write(Utils::XMLToString(response_xml));
    STDLOG->info((authenticated ? "User {} authenticated" : "User {} not present or wrong password"), user);

}

/** Create a new user based on user data */
void ServerEngine::tcp_connection::signingUp(rx::xml_node<> *data) {
    std::string user{data->first_node("user")->value()};
    std::string pass{data->first_node("pass")->value()};
    authenticated = Database::registerUser(user, pass);
    if (authenticated) {
        server->currentlyConnectedUsers.insert(user);
        currentUser = user;
        root = server->mainFolder / user;

        std::error_code er;
        fs::create_directories(root, er);
    }
    //Signal the already registered user
    rx::xml_document<> response_xml;
    Message::AuthenticationResponseMessage(response_xml, authenticated, (authenticated ? "User authenticated"
                                                                                       : "An user with this username is already registered"));

    write(Utils::XMLToString(response_xml));
    STDLOG->info((authenticated ? "User {} registered" : "User was already on database"), user);
}

/**  Parsing xml and exec the right operation based on the  service value
 * */
long long ServerEngine::tcp_connection::execCommand(rx::xml_document<> &doc, const std::string &document_string) {
    std::string service{doc.first_node()->first_node("service")->value()};
    auto data_node = doc.first_node()->first_node("data");
    if (service == "authentication") {
        if (!authenticated) {
            authenticate(data_node);
        }
    } else if (service == "signup") {
        if (!authenticated) {
            signingUp(data_node);
        }
    } else if (service == "sync") {
        if (authenticated) {
            return sync(data_node);
        }
    } else if (service == "restore") {
        if (authenticated) {
            // instantiate a new thread for async restore communication with client
            std::thread th{
                    [connection = shared_from_this()]() {
                        fs::path rootPath = connection->getRoot();
                        std::string connection_data = connection->userIP;
                        // for every file and directories in user dir send data to client
                        for (const auto &it : fs::recursive_directory_iterator(rootPath)) {
                            const fs::path &path = it.path();
                            rx::xml_document header;
                            Message::SyncMessage(header, path, rootPath);
                            STDLOG->info("Sending sync message to {}", connection_data);
                            VERBLOG->info(Utils::XMLDebugToString(header));
                            try {
                                connection->write(Utils::XMLToString(header));
                            } catch (Utils::ConnectionException &ex) {
                                ERRLOG->error("Connection closed by client {}", connection_data);
                                EXCLOG->error(ex.what());
                                return;
                            }
                            if (fs::is_regular_file(it.path())) {

                                const long long size = fs::file_size(path);
                                long long count = size / CHUNK_SIZE + 1;

                                std::ifstream readStream(path, std::ios::binary);

                                while (count--) {
                                    long long sizeToTransfer;
                                    sizeToTransfer = count > 0 ? CHUNK_SIZE : size % CHUNK_SIZE;
                                    std::vector<unsigned char> bufferFile(sizeToTransfer);
                                    readStream.read((char *) bufferFile.data(), sizeToTransfer);
                                    try {
                                        connection->writeRestore(bufferFile, sizeToTransfer);
                                    } catch (Utils::ConnectionException &ex) {
                                        ERRLOG->error("Connection closed by client {}", connection_data);
                                        EXCLOG->error(ex.what());
                                        return;
                                    }
                                }
                            }
                        }
                        rx::xml_document<> end;
                        Message::RestoreEndMessage(end);
                        try {
                            connection->write(Utils::XMLToString(end));
                        } catch (Utils::ConnectionException &ex) {
                            ERRLOG->error("Connection closed by client {}", connection_data);
                            EXCLOG->error(ex.what());
                            return;
                        }
                        connection->async_read_header();
                    }
            };
            th.detach();
            return -4;
        }
    } else if (service == "checksync") {
        if (authenticated) {
            // start a new thread to elaborate checksync response for client
            std::thread th{
                    [connetion = shared_from_this(), document_string]() {
                        fs::path rootPath = connetion->getRoot();
                        STDLOG->info("Exec CheckSync on dir {}", rootPath.string());
                        auto list = checkSync(rootPath, document_string);
                        rx::xml_document<> response;
                        Message::CheckSyncResponseMessage(response, list, rootPath);
                        try {
                            connetion->write(Utils::XMLToString(response));
                        } catch (Utils::ConnectionException &ex) {
                            EXCLOG->error(ex.what());
                            ERRLOG->error("Connection closed by client");
                        }
                        std::stringstream id;
                        id << std::this_thread::get_id();
                        STDLOG->info("Terminating thread for checksync on dir {}", rootPath.string());
                        connetion->async_read_header();
                    }
            };
            th.detach();
            return -4;
        }
    } else {
        ERRLOG->error("Command not found: {}", service);
        boost::system::error_code ignored_ec;
        socket_.close(ignored_ec);
    }

    return -5;
}

/** Before detroying the tcp_connection object signal to log the event, decrease the connection count and remove
 *  current user from the currently connected set
 * */
ServerEngine::tcp_connection::~tcp_connection() {
    try {
        if (socket_.is_open()) {
            STDLOG->info("Closing connection to {}:{}", socket_.remote_endpoint().address().to_string(),
                         socket_.remote_endpoint().port());
        }
    }
    catch (boost::wrapexcept<boost::system::system_error> &exc) {
        EXCLOG->error(exc.what());
    }
    server->connection_count.fetch_sub(1);
    if (currentUser.length()) server->currentlyConnectedUsers.remove(currentUser);
}

ServerEngine::tcp_connection::pointer
ServerEngine::tcp_connection::create(boost::asio::io_service &io_service, ServerEngine *server) {
    return pointer{new ServerEngine::tcp_connection{io_service, server}};
}

/** Write message to socket with a unique_lock to avoid multiple write instance
 * @param message message to write on the socket
 * */
void ServerEngine::tcp_connection::write(std::string message) {
    std::unique_lock lk{write_mutex};
    boost::system::error_code error;
    nw::write(socket_, nw::buffer(message), error);
    if (error) {
        ERRLOG->error("Error while writing response to client");
        throw Utils::ConnectionException(error.message());
    }
}

/** Write function for restore data (raw byte)
 * @param message raw bytes vector
 * @param size size of the vector
 * */
void ServerEngine::tcp_connection::writeRestore(std::vector<unsigned char> &message, long long size) {
    std::unique_lock lk{write_mutex};
    boost::system::error_code error;
    nw::write(socket_, nw::buffer(message, size), error);
    if (error) {
        ERRLOG->error("Error while writing response to client");
        throw Utils::ConnectionException(error.message());
    }
}

fs::path ServerEngine::tcp_connection::getRoot() {
    std::unique_lock lk{root_mutex};
    return root;
}

void ServerEngine::tcp_connection::deadline_handler(boost::system::error_code &ec) {
    if (deadlineTimer.expiry() <= nw::steady_timer::clock_type::now() || needToKill) {
        ERRLOG->error("Timer expired, connection to {} closed for inactivity", this->userIP);
        boost::system::error_code ignored_ec;
        if (socket_.is_open())
            socket_.close(ignored_ec);
    } else {
        deadlineTimer.async_wait(
                [capture0 = this](boost::system::error_code exc) { capture0->deadline_handler(exc); });
    }
}


////SERVER ENGINE
/// General class for server function
ServerEngine::ServerEngine(nw::io_context &io_context, int connection_limit, fs::path mainFolder, int port)
        : io_context_{io_context},
          acceptor_{io_context, nwip::tcp::endpoint{nwip::tcp::v4(), (short unsigned int) port}},
          connection_count{0},
          connection_limit{connection_limit},
          mainFolder{std::move(mainFolder)} {
    start_accept();
}

/** Async wait for a new connection request
 * */
void ServerEngine::start_accept() {
    ServerEngine::tcp_connection::pointer new_connection =
            ServerEngine::tcp_connection::create(io_context_, this);
    acceptor_.async_accept(new_connection->socket(),
                           [this, new_connection](const boost::system::error_code &ec) {
                               handle_accept(new_connection, ec);
                           });
}

/** Handle the connection request from a client
 * */
void ServerEngine::handle_accept(const ServerEngine::tcp_connection::pointer &new_connection,
                                 const boost::system::error_code &error) {
    // Limit on possible new connections, can create a connection only if limit is not reached
    if (!error && connection_count < connection_limit) {
        connection_count.fetch_add(1);
        new_connection->start();
        STDLOG->info("New conncetion open, {} remaining", connection_limit - connection_count);
    } else {
        ERRLOG->error("Error on opening a new connection");
    }
    start_accept();
}
