#pragma once

#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <functional>
#include <iostream>
#include "./../../Headers/General/Utils.h"


namespace fs = std::filesystem;

enum class FileStatus {
    created, modified, erased
};

#define DELAY_TIME 4

class FileWatcher {
    std::mutex stop_mutex;
    std::string mainDirPath;
    std::chrono::duration<int, std::milli> delay = std::chrono::seconds(DELAY_TIME);
    Utils::threadqueue<std::pair<fs::path, FileStatus>> &files_queue;

    std::unordered_map<std::string, std::filesystem::file_time_type> files;
    bool watching = false;

    void notifyChange(const fs::path &p, FileStatus s);

public:
    FileWatcher(const std::string& mainDirPath, Utils::threadqueue<std::pair<fs::path, FileStatus>> &q);

    void start();

    void stop();

    bool test();

    void resetDirectory();
};

