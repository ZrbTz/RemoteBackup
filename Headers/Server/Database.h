//
// Created by simone on 17/08/20.
//

#ifndef FILE_TRANSFER_TEST_DATABASE_H
#define FILE_TRANSFER_TEST_DATABASE_H

#include <string>

namespace Database {
    bool isUserRegistered(const std::string &user, const std::string &pass);

    bool registerUser(const std::string &user, const std::string &pass);

    class DatabaseException : std::exception {
    public:
        explicit DatabaseException(const char *message);

        virtual ~DatabaseException() throw();

        virtual const char *what() const throw();

    private:
        std::string message;
    };
}

#endif //FILE_TRANSFER_TEST_DATABASE_H
