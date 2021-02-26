//
// Created by simone on 09/07/20.
//
#include "./../../Headers/Server/ServerEngine.h"
#include <thread>

int main(int argc, char** argv) {

     if (argc != 3){ 
        ERRLOG->error("Wrong number of arguments"); 
        std::cerr << "Wrong number of arguments" << std::endl;
        return 1;
    }

    char *end;
    int port = strtol(argv[2], &end, 10);
    if (*end != '\0') {
        ERRLOG->error("Port parameter must be a valid integer number"); 
        std::cerr << "Port parameter must be an integer number" << std::endl;
        return 1;
    }

    if(port < 0 || port > 65535){
        ERRLOG->error("Port number is out of range"); 
        std::cerr << "Port number is out of range" << std::endl;
        return 1;
    }

    fs::path mainFolder(argv[1]);
    if (!fs::is_directory(mainFolder)) {
        if(!fs::create_directory(mainFolder)){
            ERRLOG->error("Directory parameter is not a directory"); 
            std::cerr << "Directory parameter is not a directory" << std::endl;
            return 1;
        }   
    }

    nw::io_service service;
    ServerEngine serverEngine{service, 50, mainFolder, port};
    STDLOG->info("Listening on port {}", port);
    //starting async server operation
    service.run();
}