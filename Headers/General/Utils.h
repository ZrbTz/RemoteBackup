//
// Created by simone on 11/08/20.
//

#ifndef FILE_TRANSFER_TEST_UTILS_H
#define FILE_TRANSFER_TEST_UTILS_H

#include <openssl/sha.h>
#include <iostream>
#include <rapidxml/rapidxml.hpp>
#include <filesystem>
#include <string>
#include <optional>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <spdlog/spdlog.h>
#include <set>

#define DBLOG Utils::getLog(Utils::Database)
#define STDLOG Utils::getLog(Utils::Stdout)
#define ERRLOG Utils::getLog(Utils::Error)
#define EXCLOG Utils::getLog(Utils::Exception)
#define VERBLOG Utils::getLog(Utils::Verbose)
#define READ_SIZE 1024
#define LOG_SIZE 1024 * 1024 * 3
#define LOG_NUMBER 2

namespace fs = std::filesystem;
namespace rx = rapidxml;

namespace Utils {

    template<class T>
    class threadqueue {
        std::queue<T> q;
        std::condition_variable cv;
        std::mutex mt;
        bool last = false;
    public:
        void push(T element) {
            std::unique_lock<std::mutex> lk(mt);
            q.push(element);
            cv.notify_one();
        }

        std::optional<T> pop() {
            std::unique_lock<std::mutex> lk(mt);
            while (q.empty() && !last) cv.wait(lk);
            T res;
            if (!q.empty()) {
                res = q.front();
                q.pop();
                return res;
            }
            return {};
        }

        void notify_last() {
            std::unique_lock<std::mutex> lk(mt);
            last = true;
            cv.notify_all();
        }
    };

    class threadSetString{
        std::set<std::string> set;
        std::mutex mt;

    public:
        //returns false if element was already in the set
        bool insert(std::string s){
            std::unique_lock<std::mutex> lk(mt);
            auto b = set.insert(s);
            return b.second;
        }

        void remove(std::string s){
            std::unique_lock<std::mutex> lk(mt);
            set.erase(set.find(s));
        }
    };

    enum Log {
        Error,
        Exception,
        Stdout,
        Verbose,
        Database
    };

    std::optional<std::string> SimpleSHA512(std::istream *input);

    std::string XMLToString(rx::xml_document<> &);

    std::string XMLDebugToString(rx::xml_document<> &doc);

    std::shared_ptr<spdlog::logger> getLog(Log);

    class ConnectionException : std::exception {
    public:
        explicit ConnectionException(const char *message);

        explicit ConnectionException(std::string message);

        virtual ~ConnectionException() throw();

        virtual const char *what() const throw();

    private:
        std::string message;
    };

    class UtilsException : std::exception {
    public:
        explicit UtilsException(const char *message);

        virtual ~UtilsException() throw();

        virtual const char *what() const throw();

    private:
        std::string message;
    };


}
#endif //FILE_TRANSFER_TEST_UTILS_H
