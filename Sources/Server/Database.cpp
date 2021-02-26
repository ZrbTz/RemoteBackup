//
// Created by simone on 17/08/20.
//

#include "../../Headers/Server/Database.h"
#include <sqlite3.h>

#include "../../Headers/General/Utils.h"

static sqlite3 *DB;
std::once_flag init_database_flag;

/** Init database connection and create a new sqlite file if it's the first startup
 * */
void databaseInit() {
    fs::path database{"./../Database/users.sqlite"};
    bool exist = true;
    if (!fs::exists(database)) {
        fs::create_directory(database.parent_path());
        exist = false;
    }
    int status = sqlite3_open(database.c_str(), &DB);
    if (status) {
        DBLOG->error("Error while opening DB");
        throw Database::DatabaseException(sqlite3_errmsg(DB));
    }
    if (!exist) {
        char sql[99] = "CREATE TABLE \"users\" (\n \"username\"  TEXT,\n \"password\"  TEXT NOT NULL,\n PRIMARY KEY(\"username\")\n);\0";
        int rc = sqlite3_exec(DB, sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            DBLOG->error("Error while creating table in DB");
            throw Database::DatabaseException(sqlite3_errmsg(DB));
        }
    }
    DBLOG->info("DB connection opened");
}

/** Check for user presence in database
 * */
bool Database::isUserRegistered(const std::string &user, const std::string &pass) {

    std::call_once(init_database_flag, databaseInit);
    std::string query = "SELECT password = ? FROM users WHERE username = ?";

    //Calcolo dell'hash della password per il confronto
    std::stringstream hashStream;
    hashStream << pass;
    std::optional<std::string> hashedPass = Utils::SimpleSHA512(&hashStream);

    if (!hashedPass) {
        DBLOG->error("Hashing function has failed");
        throw Utils::UtilsException{"Error while hashing password for database query"};
    }

    //Preparazione dello statement per interrogare il database con l'iserimento di user e hash della password
    sqlite3_stmt *stmt;
    int prepare_status = sqlite3_prepare(DB, query.c_str(), query.length(), &stmt, nullptr);
    if (prepare_status != SQLITE_OK) {
        DBLOG->error("Prepared statement failed");
        throw Database::DatabaseException{sqlite3_errmsg(DB)};
    }
    if (sqlite3_bind_text(stmt, 1, hashedPass->c_str(), hashedPass->size(), SQLITE_STATIC)) {
        DBLOG->error("Prepared statement failed");
        throw Database::DatabaseException{sqlite3_errmsg(DB)};
    }
    if (sqlite3_bind_text(stmt, 2, user.c_str(), user.length(), SQLITE_STATIC)) {
        DBLOG->error("Prepared statement failed");
        throw Database::DatabaseException{sqlite3_errmsg(DB)};
    }
    DBLOG->info("Query execution");
    DBLOG->debug("Query: SELECT password = '{}' FROM users WHERE username = '{}'", *hashedPass, user);
    int value;
    switch (sqlite3_step(stmt)) {
        case SQLITE_ROW:
            value = sqlite3_column_int(stmt, 0);
            break;
        default:
            value = 0;
            break;
    }
    sqlite3_finalize(stmt);
    DBLOG->info(("Query completed"));
    if (value)
        DBLOG->info(("Corresponding password"));
    else
        DBLOG->warn("Not corresponding password");
    return value;
}

bool Database::registerUser(const std::string &user, const std::string &pass) {

    std::call_once(init_database_flag, databaseInit);
    std::string query = "SELECT count(*) FROM users WHERE username = ?";


    //Preparazione dello statement per interrogare il database con l'iserimento di user e hash della password
    sqlite3_stmt *stmt;
    if (sqlite3_prepare(DB, query.c_str(), query.length(), &stmt, nullptr) != SQLITE_OK) {
        DBLOG->error("Prepared statement failed");
        throw Database::DatabaseException{sqlite3_errmsg(DB)};
    }

    if (sqlite3_bind_text(stmt, 1, user.c_str(), user.length(), SQLITE_STATIC) != SQLITE_OK) {
        DBLOG->error(sqlite3_errmsg(DB));
        throw Database::DatabaseException{sqlite3_errmsg(DB)};
    }
    DBLOG->info("Query execution");
    DBLOG->debug("Query: SELECT count(*) FROM users WHERE username = '{}'", user);
    int userExist;
    switch (sqlite3_step(stmt)) {
        case SQLITE_ROW:
            userExist = sqlite3_column_int(stmt, 0);
            break;
        default:
            userExist = 0;
            break;
    }
    if (userExist) {
        DBLOG->info("User {} already exists on the database", user);
        return false;
    }
    sqlite3_finalize(stmt);
    DBLOG->info(("Query completed"));


    query = "INSERT INTO users VALUES (?, ?)";
    if (sqlite3_prepare(DB, query.c_str(), query.length(), &stmt, nullptr) != SQLITE_OK) {
        DBLOG->error(sqlite3_errmsg(DB));
        throw Database::DatabaseException{"Prepare statement failed"};
    }

    //Calcolo dell'hash della password per il confronto
    std::stringstream hashStream;
    hashStream << pass;
    std::optional<std::string> hashedPass = Utils::SimpleSHA512(&hashStream);

    if (!hashedPass) {
        DBLOG->error("Hashing function has failed");
        throw Utils::UtilsException{"Error while hashing password for database query"};
    }
    if (sqlite3_bind_text(stmt, 1, user.c_str(), user.length(), SQLITE_STATIC) != SQLITE_OK) {
        DBLOG->error(sqlite3_errmsg(DB));
        throw Database::DatabaseException{"Text bind failed"};
    }

    if (sqlite3_bind_text(stmt, 2, hashedPass->c_str(), hashedPass->size(), SQLITE_STATIC) != SQLITE_OK) {
        DBLOG->error(sqlite3_errmsg(DB));
        throw Database::DatabaseException{"Text bind failed"};
    }

    DBLOG->info(("Insert execution"));
    DBLOG->debug("INSERT INTO users VALUES ({}, {})", user, *hashedPass);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DBLOG->error(sqlite3_errmsg(DB));
        throw Database::DatabaseException{"Database insert failed"};
    }
    DBLOG->info(("Query completed"));

    return true;
}

/// Exception for database error
Database::DatabaseException::DatabaseException(const char *message) : message{message} {}

Database::DatabaseException::~DatabaseException() noexcept = default;

const char *Database::DatabaseException::what() const noexcept {
    return message.c_str();
}
