//
// Created by simone on 11/08/20.
//

#include "../../Headers/General/Utils.h"
#include <mutex>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <rapidxml/rapidxml_print.hpp>
#include <utility>


using rapidxml::xml_document;
using rapidxml::xml_attribute;
using rapidxml::xml_node;
using std::make_shared;

// Macro for debug discerning
#if DEBUG
#if SERVER
#define LOGNAME(name) name"_server.log"
#else
#define LOGNAME(name) name"_client.log"
#endif
#else
#define LOGNAME(name) name".log"
#endif

std::once_flag init_log_flag;

/** Encode unsigned char vector to base64 string
 * */
static std::string base64_encode(const unsigned char *in) {
    std::string out;
    std::string b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0, valb = -6;
    for (int i = 0; i < SHA512_DIGEST_LENGTH; ++i) {
        unsigned char c = in[i];
        val = (val << +8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

/*
 * Funzione che calcola il codice hash a partire da uno stream di byte
 * */
std::optional<std::string> Utils::SimpleSHA512(std::istream *input) {
    //vettore di byte di lunghezza pari al risultato dell'algoritmo sha512
    unsigned char md[SHA512_DIGEST_LENGTH];

    SHA512_CTX context;
    //se fallisce nella crezione del context ritorna un optional vuoto
    if (!SHA512_Init(&context))
        return std::optional<std::string>{};


    //legge lo stream ${READ_SIZE} caratteri alla volta e aggiorna il valore dell'hash di conseguenza,
    // se incappa in un qualsiasi errore ritorna un optional vuoto senza hash parziale del file
    char buffer[READ_SIZE];
    while (input->read(buffer, READ_SIZE)) {
        if (!SHA512_Update(&context, (unsigned char *) buffer, READ_SIZE))
            return std::optional<std::string>{};
    }
    if (!SHA512_Update(&context, (unsigned char *) buffer, input->gcount()))
        return std::optional<std::string>{};

    //effettua il calcolo finale dell'hash
    if (!SHA512_Final(md, &context))
        return std::optional<std::string>{};

    return base64_encode(md);
}

/**Init of logs, different patterns for server and client based on SERVER macro
 * */
void initLog() {
    ///[yy-mm-dd hh:mm-ss.ms time_zone] [level] message
    std::string file_pattern = "[%Y-%m-%d %H:%M:%S.%e %z] [thread %t] [%l] %v",
    ///[yy-mm-dd hh:mm-ss.ms time_zone] [log_file] [level_colored] message
    console_pattern = "[%Y-%m-%d %H:%M:%S.%e %z] [thread %t] [%n] [%^%l%$] %v";
    std::vector<spdlog::sink_ptr> error_sinks, exception_sinks, stdout_sinks, database_sinks;
    spdlog::init_thread_pool(8192, 1);

    //For each log instantiating a couple of sinks (standard or error output and file output)
#if SERVER
    auto stderr_color_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    stderr_color_sink->set_color(spdlog::level::warn, stderr_color_sink->yellow);
    stderr_color_sink->set_color(spdlog::level::err, stderr_color_sink->red);
    stderr_color_sink->set_pattern(console_pattern);

    auto stdout_color_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_color_sink->set_color(spdlog::level::warn, stdout_color_sink->yellow);
    stdout_color_sink->set_color(spdlog::level::info, stdout_color_sink->green);
    stdout_color_sink->set_pattern(console_pattern);
#endif
    //Per ogni tipo di log creo un log_rotante su file e aggiungono un output appropriato su linea di comando
    //Log for error handling
    auto rotating_error = make_shared<spdlog::sinks::rotating_file_sink_mt>(LOGNAME("./../LogFiles/error_log"),
                                                                            LOG_SIZE, LOG_NUMBER, false);
    rotating_error->set_pattern(file_pattern);
    error_sinks.push_back(rotating_error);
#if SERVER
    error_sinks.push_back(stderr_color_sink);
#endif
    auto error_logger = make_shared<spdlog::async_logger>("error_log", error_sinks.begin(),
                                                          error_sinks.end(), spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::block);
    spdlog::register_logger(error_logger);
    error_logger->info("----> Error log initialized <----");
    error_logger->flush_on(spdlog::level::info);

    //Log for  exception handling
    auto rotating_exception = make_shared<spdlog::sinks::rotating_file_sink_mt>(LOGNAME("./../LogFiles/exception_log"),
                                                                                LOG_SIZE, LOG_NUMBER, false);
    rotating_exception->set_pattern(file_pattern);
    exception_sinks.push_back(rotating_exception);
#if SERVER
    exception_sinks.push_back(stderr_color_sink);
#endif
    auto exception_logger = make_shared<spdlog::async_logger>("exception_log", exception_sinks.begin(),
                                                              exception_sinks.end(), spdlog::thread_pool(),
                                                              spdlog::async_overflow_policy::block);
    spdlog::register_logger(exception_logger);
    exception_logger->info("----> Exception log initialized <----");
    exception_logger->flush_on(spdlog::level::info);

    //Log for database messages
    auto rotating_database = make_shared<spdlog::sinks::rotating_file_sink_mt>(LOGNAME("./../LogFiles/database_log"),
                                                                               LOG_SIZE, LOG_NUMBER, false);
    rotating_database->set_pattern(file_pattern);
    database_sinks.push_back(rotating_database);
#if SERVER
    database_sinks.push_back(stderr_color_sink);
#endif
    auto database_logger = make_shared<spdlog::async_logger>("database_log", database_sinks.begin(),
                                                             database_sinks.end(), spdlog::thread_pool(),
                                                             spdlog::async_overflow_policy::block);

    spdlog::register_logger(database_logger);
    database_logger->info("----> Database log initialized <----");
    database_logger->flush_on(spdlog::level::info);

    //Log for standard output
    auto rotating_stdout = make_shared<spdlog::sinks::rotating_file_sink_mt>(LOGNAME("./../LogFiles/stdout_log"),
                                                                             LOG_SIZE, LOG_NUMBER, false);
    rotating_stdout->set_pattern(file_pattern);
    stdout_sinks.push_back(rotating_stdout);
#if SERVER
    stdout_sinks.push_back(stdout_color_sink);
#endif
    auto stdout_logger = make_shared<spdlog::async_logger>("stdout_log", stdout_sinks.begin(),
                                                           stdout_sinks.end(), spdlog::thread_pool(),
                                                           spdlog::async_overflow_policy::block);
    spdlog::register_logger(stdout_logger);
    stdout_logger->info("----> Stdout log initialized <----");
    stdout_logger->flush_on(spdlog::level::info);

    //Log for extremely long text {e.g. xml files}
    auto verbose_logger = spdlog::rotating_logger_mt<spdlog::async_factory>("verbose_log",
                                                                            LOGNAME("./../LogFiles/verbose_log"),
                                                                            LOG_SIZE,
                                                                            LOG_NUMBER, false);
    verbose_logger->set_pattern(file_pattern);
    verbose_logger->info("----> Verbose log initialized <----");
    verbose_logger->flush_on(spdlog::level::info);

#if DEBUG
    spdlog::set_level(spdlog::level::debug);
    stdout_logger->info("Log set to Debug mode");
#endif
}

/** Function to get the correct log based on Utils::Log enum*/
std::shared_ptr<spdlog::logger> Utils::getLog(Utils::Log log) {
    //Esegue le operazione iniziali sui log solo la prima volta che la getLog viene chiamata
    std::call_once(init_log_flag, initLog);
    switch (log) {
        case Utils::Error :
            return spdlog::get("error_log");
        case Utils::Exception :
            return spdlog::get("exception_log");
        case Utils::Stdout :
            return spdlog::get("stdout_log");
        case Utils::Verbose :
            return spdlog::get("verbose_log");
        case Utils::Database :
            return spdlog::get("database_log");
        default:
            return spdlog::get("stdout_log");
    }
}
/** Convert xml document to string for internal use
 * */
std::string Utils::XMLToString(rx::xml_document<> &doc) {
    std::stringstream ss;
    std::ostream_iterator<char> i(ss);
    rapidxml::print(i, doc, rapidxml::print_no_indenting);
    return ss.str();
}

/** Convert xml document to string for debug use (preserve indentation)
 * */
std::string Utils::XMLDebugToString(rx::xml_document<> &doc) {
    std::stringstream ss;
    std::ostream_iterator<char> i(ss);
    rapidxml::print(i, doc);
    return ss.str();
}


/// Exception for connection error
Utils::ConnectionException::ConnectionException(const char *message) : message{message} {}

Utils::ConnectionException::ConnectionException(std::string message) : message{std::move(message)} {}

Utils::ConnectionException::~ConnectionException() noexcept = default;

const char *Utils::ConnectionException::what() const noexcept {
    return message.c_str();
}

/// Exception for generic error on utils method
Utils::UtilsException::UtilsException(const char *message) : message{message} {}

Utils::UtilsException::~UtilsException() noexcept = default;

const char *Utils::UtilsException::what() const noexcept {
    return message.c_str();
}
