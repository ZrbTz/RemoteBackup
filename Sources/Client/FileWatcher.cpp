#include "./../../Headers/Client/FileWatcher.h"

#include <utility>

FileWatcher::FileWatcher(const std::string& mainDirPath, Utils::threadqueue<std::pair<fs::path, FileStatus>> &q)
        : mainDirPath(mainDirPath), files_queue(q) {

    fs::recursive_directory_iterator b(mainDirPath);
    //Create the status map of target directory (path, last write time)
    for (const fs::directory_entry &file : b) {
        try {
            files[file.path().string()] = std::filesystem::last_write_time(file);
        } catch (std::filesystem::filesystem_error &ex) {
            ERRLOG->error("File deleted {} while getting info", file.path().c_str());
            EXCLOG->error(ex.what());
        }
    }
}


void FileWatcher::resetDirectory(){

    fs::recursive_directory_iterator b(mainDirPath);
    //Create the status map of target directory (path, last write time)
    files.clear();
    for (const fs::directory_entry &file : b) {
        try {
            files[file.path().string()] = std::filesystem::last_write_time(file);
        } catch (std::filesystem::filesystem_error &ex) {
            ERRLOG->error("File deleted {} while getting info", file.path().c_str());
            EXCLOG->error(ex.what());
        }
    }
}

/** Start filewatching operations
 * */
void FileWatcher::start() {
    watching = true;

    while (FileWatcher::test()) {
        //sleep for $delay seconds
        std::this_thread::sleep_for(delay);

        //checks if file was erased
        for (auto it = files.begin(); it != files.end();)
            if (!fs::exists(it->first)) {
                notifyChange(fs::path(it->first), FileStatus::erased);
                it = files.erase(it);
            } else it++;
        try {
            //check for new or updates files/directories
            fs::recursive_directory_iterator b(mainDirPath);
            for (const fs::directory_entry &file : b) {
                try {
                    fs::file_time_type lastWriteTime = fs::last_write_time(file);
                    std::string filePath = file.path().string();

                    //checks if file was created
                    if (files.find(filePath) == files.end()) {
                        files[filePath] = lastWriteTime;
                        notifyChange(filePath, FileStatus::created);
                    }
                        //checks if file was modified
                    else if (files[filePath] != lastWriteTime) {
                        files[filePath] = lastWriteTime;
                        if (fs::is_regular_file(filePath))notifyChange(filePath, FileStatus::modified);
                    }
                } catch (std::filesystem::filesystem_error &ex) {
                    ERRLOG->error("File {} deleted while getting info", file.path().c_str());
                    EXCLOG->error(ex.what());
                }
            }
        }catch (std::filesystem::filesystem_error &ex) {
            ERRLOG->error("Directory deleted while getting info");
            EXCLOG->error(ex.what());
        }
    }
}
/** Push in the queue changes of the file system
 * */
void FileWatcher::notifyChange(const fs::path &p, FileStatus s) {
    files_queue.push({p, s});
}

void FileWatcher::stop() {
    std::unique_lock lk{stop_mutex};
    watching = false;
}

/** Check termination condition
 * */
bool FileWatcher::test() {
    std::unique_lock lk{stop_mutex};
    bool res = watching;
    lk.unlock();
    return res;
}
