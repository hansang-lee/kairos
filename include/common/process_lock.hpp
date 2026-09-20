#pragma once

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <filesystem>
#include <string>

#include "common/util.hpp"

namespace util {

/**
 * @brief Refuses to run when another instance already is.
 *
 * The trading apps share mutable state on disk — the risk guard's order count,
 * the position store's peaks, the journal. Two processes each load that state,
 * act on it, and write it back, so the second to save erases the first's work:
 * orders vanish from the day's count and the cap stops holding. Worse, both can
 * decide to buy the same ticker from the same balance and commit twice the
 * intended cash.
 *
 * A systemd timer firing while the same command is run by hand is enough to
 * cause it, which is not an unlikely sequence.
 *
 * Uses flock, so the lock is released by the kernel even if the process is
 * killed — no stale lock file can wedge the next run.
 */
class ProcessLock {
   public:
    /**
     * @param name Lock identity; instances sharing a name exclude each other.
     * @param dir  Lock directory. Empty (default) resolves to <project-root>/data.
     */
    explicit ProcessLock(const std::string& name, const std::string& dir = "") {
        const std::string lockDir = dir.empty() ? resolveFromExe("data") : dir;

        std::error_code ec;
        std::filesystem::create_directories(lockDir, ec);
        path_ = lockDir + "/" + name + ".lock";

        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            // Without a lock file the safe choice is to allow the run: refusing to
            // trade because a lock could not be created would be its own failure.
            held_ = true;
            return;
        }
        held_ = (::flock(fd_, LOCK_EX | LOCK_NB) == 0);
    }

    ~ProcessLock() {
        if (fd_ >= 0) {
            ::close(fd_);  // releases the flock
        }
    }

    ProcessLock(const ProcessLock&)            = delete;
    ProcessLock& operator=(const ProcessLock&) = delete;

    /** @brief False when another instance holds the lock; the caller should exit. */
    [[nodiscard]] bool held() const { return held_; }

    [[nodiscard]] const std::string& path() const { return path_; }

   private:
    std::string path_;
    int         fd_   = -1;
    bool        held_ = false;
};

}  // namespace util
