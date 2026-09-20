#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>

#include "common/util.hpp"

namespace util {

/**
 * @brief Duplicates everything written to stdout and stderr into a log file.
 *
 * Under systemd the output would reach journald, which is fine until it rotates;
 * run by hand it reaches a terminal and nothing else. Either way there was no
 * durable record of what a trading run actually printed.
 *
 * Implemented as a streambuf filter so no existing `std::cout <<` has to change:
 * writes go to the original destination and to the file. RAII — the original
 * buffers are restored on destruction, so this is safe to construct in main().
 */
class RunLog {
   public:
    /**
     * @param appName Used in the filename: logs/<appName>-YYYY-MM-DD.log (KST).
     * @param dir     Log directory. Empty (default) resolves to <project-root>/logs.
     */
    explicit RunLog(const std::string& appName, const std::string& dir = "") {
        const std::string logDir = dir.empty() ? resolveFromExe("logs") : dir;

        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        if (ec) {
            return;  // no log file; the run still proceeds and still prints
        }

        path_ = logDir + "/" + appName + "-" + kstDate() + ".log";
        file_.open(path_, std::ios::app);
        if (!file_.is_open()) {
            path_.clear();
            return;
        }

        // A run boundary, so a day's file reads as a sequence of runs.
        file_ << "\n===== " << appName << "  " << kstTimestamp() << " KST =====\n" << std::flush;

        outTee_ = std::make_unique<TeeBuf>(std::cout.rdbuf(), file_.rdbuf());
        errTee_ = std::make_unique<TeeBuf>(std::cerr.rdbuf(), file_.rdbuf());
        oldOut_ = std::cout.rdbuf(outTee_.get());
        oldErr_ = std::cerr.rdbuf(errTee_.get());

        installCrashHandlers();
    }

    ~RunLog() {
        if (oldOut_) {
            std::cout.rdbuf(oldOut_);
        }
        if (oldErr_) {
            std::cerr.rdbuf(oldErr_);
        }
    }

    RunLog(const RunLog&)            = delete;
    RunLog& operator=(const RunLog&) = delete;

    /** @brief Log file path, empty when logging could not be set up. */
    [[nodiscard]] const std::string& path() const { return path_; }

   private:
    /**
     * @brief Record the reason for an abnormal exit, which the tee cannot see.
     *
     * An uncaught exception and a fatal signal are both reported by the runtime
     * straight to C stderr, bypassing std::cerr and therefore the tee. The log
     * would end mid-sentence with no explanation — precisely when the explanation
     * matters most. These write the cause to the file before letting the process
     * die as it otherwise would.
     */
    void installCrashHandlers() {
        logPath() = path_;

        std::set_terminate([] {
            std::string reason = "terminate called";
            if (auto ex = std::current_exception()) {
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception& e) {
                    reason += std::string(": ") + e.what();
                } catch (...) {
                    reason += ": unknown exception";
                }
            }
            appendCrashLine(reason);
            std::abort();
        });

        for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
            std::signal(sig, [](int s) {
                appendCrashLine("fatal signal " + std::to_string(s));
                // Restore the default and re-raise, so the exit status and any core
                // dump are what they would have been without this handler.
                std::signal(s, SIG_DFL);
                std::raise(s);
            });
        }
    }

    /** Path of the active log, for handlers that cannot capture state. */
    static std::string& logPath() {
        static std::string path;
        return path;
    }

    /** Append one line by fd, the only safe way to write from a signal handler. */
    static void appendCrashLine(const std::string& reason) {
        if (logPath().empty()) {
            return;
        }
        const int fd = ::open(logPath().c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (fd < 0) {
            return;
        }
        const std::string line = "[!] ABNORMAL EXIT — " + reason + "\n";
        const ssize_t     n    = ::write(fd, line.c_str(), line.size());
        (void)n;
        ::close(fd);
    }

    /** Writes each character to two buffers. */
    class TeeBuf: public std::streambuf {
       public:
        TeeBuf(std::streambuf* a, std::streambuf* b)
            : a_(a)
            , b_(b) {}

       protected:
        int overflow(int c) override {
            if (c == EOF) {
                return !EOF;
            }
            const int r1 = a_->sputc(static_cast<char>(c));
            const int r2 = b_->sputc(static_cast<char>(c));
            // Flush the file at every line. A long-running loop is normally ended by
            // a signal, which skips destructors — without this the log would lag
            // behind and lose its tail exactly when it is most wanted.
            if (c == '\n') {
                b_->pubsync();
            }
            return (r1 == EOF || r2 == EOF) ? EOF : c;
        }

        int sync() override {
            const int r1 = a_->pubsync();
            const int r2 = b_->pubsync();
            return (r1 == 0 && r2 == 0) ? 0 : -1;
        }

       private:
        std::streambuf* a_;
        std::streambuf* b_;
    };

    static std::string kstDate() {
        const std::time_t  kst = std::time(nullptr) + 9 * 3600;
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d");
        return oss.str();
    }

    static std::string kstTimestamp() {
        const std::time_t  kst = std::time(nullptr) + 9 * 3600;
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&kst), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::string             path_;
    std::ofstream           file_;
    std::unique_ptr<TeeBuf> outTee_;
    std::unique_ptr<TeeBuf> errTee_;
    std::streambuf*         oldOut_ = nullptr;
    std::streambuf*         oldErr_ = nullptr;
};

}  // namespace util
