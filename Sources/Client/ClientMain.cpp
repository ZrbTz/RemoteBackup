#include "./../../Headers/Client/ClientEngine.h"

int main(int argc, char **argv) {

    if (argc != 4) {
        ERRLOG->error("Wrong number of arguments");
        std::cerr << "Wrong number of arguments" << std::endl;
        return 1;
    }

    if (!fs::is_directory(argv[1])) {
        ERRLOG->error("Directory parameter is not a directory");
        std::cerr << "Directory parameter is not a directory" << std::endl;
        return 1;
    }
    fs::path mainDirPath(argv[1]);

    std::string ipString(argv[2]);
    boost::system::error_code ec;
    nwip::address ip(nwip::address::from_string(ipString, ec));
    if (ec) {
        ERRLOG->error("Ip parameter must be a valid ipv4 address");
        std::cerr << "Ip parameter must be a valid ipv4 address" << std::endl;
        return 1;
    }

    char *end;
    int port = strtol(argv[3], &end, 10);
    if (*end != '\0') {
        ERRLOG->error("Port parameter must be a valid integer number");
        std::cerr << "Port parameter must be an integer number" << std::endl;
        return 1;
    }

    if (port < 0 || port > 65535) {
        ERRLOG->error("Port number is out of range");
        std::cerr << "Port number is out of range" << std::endl;
        return 1;
    }

    //Initialize tpc service
    nw::io_service io_service;
    ClientEngine client(io_service, mainDirPath, ip, port);
    bool connected = client.connect();
    try {
        if (connected) {
            //check if new user , in case skip restore
            if (!client.isNewUSer()) {
                int com = -1;
                while (com < 0 || com > 2) {
                    std::cout
                            << "---------------------\n- 0 to start monitoring\n- 1 to restore data from remote server\n- 2 to restore and start monitoring\nSelect an option: ";
                    std::cin >> com;
                    if (std::cin.fail()) {
                        std::cin.clear();
                        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                        com = -1;
                    }
                    if (com == 1 || com == 2) {
                        char conf;
                        std::cout << "The current content of the folder will be deleted, are you sure? (Y/n)\n";
                        std::cin >> conf;
                        if (conf != 'Y') com = -1;
                    }
                }
                if (com == 1 || com == 2) {
                    //  Case restore
                    // retry restore until the server state equal to local state
                    while (!client.getRestoreEnded()) {
                        client.resetFolder();
                        client.restore();
                        //start boost service for async operation and wait util the end of the operation
                        io_service.run();
                        io_service.reset();
                    }
                    client.resetWatcherDirectory();
                } else {
                    // Case checksync
                    while (!client.getChecksyncEnded()) {
                        client.checkSync();
                        io_service.run();
                        io_service.reset();
                    }
                }
                if (com == 1)
                    return 0;
            }
        } else {
            ERRLOG->error("Couldn't connect to the server");
            return 1;
        }
    } catch (Utils::ConnectionException &ex) {
        ERRLOG->error("Server closed connection");
        EXCLOG->error(ex.what());
        return -1;
    }
    //starting threads for async communication and file check
    std::thread first(&ClientEngine::startWatch, &client);
    std::thread second(&ClientEngine::startSync, &client, true);

    std::cout << "---------------------" << std::endl;
    while (true) {
        std::string com;
        std::cout << "Write exit to stop: ";
        std::cin >> com;
        if (com == "exit")
            break;
    }
    //terminating and waiting on threads
    client.stop();
    std::cout << "Closing file watcher" << std::endl;
    first.join();
    std::cout << "Closing connection" << std::endl;
    client.close();
    second.join();
    std::cout << "Execution terminated" << std::endl;

    return 0;
}

