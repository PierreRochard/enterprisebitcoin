#ifndef ENTERPRISE_DB_H
#define ENTERPRISE_DB_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#include <logging.h>
#include <pqxx/pqxx>

#include <enterprise/dotenv.h>

namespace enterprise {

// Builds and caches the connection string from .env
inline std::string PgConnInfo()
{
    static const std::string conninfo = [] {
        auto& dotenv = env;
        dotenv.config();
        std::ostringstream s;
        s << "dbname=" << dotenv["PGDB"]
          << " user=" << dotenv["PGUSER"]
          << " password=" << dotenv["PGPASSWORD"]
          << " hostaddr=" << dotenv["PGHOST"]
          << " port=" << dotenv["PGPORT"];
        return s.str();
    }();
    return conninfo;
}

// Thread-local connection provider to avoid reconnecting per block.
inline pqxx::connection& PgConnection()
{
    thread_local std::unique_ptr<pqxx::connection> conn;
    if (!conn || !conn->is_open()) {
        conn = std::make_unique<pqxx::connection>(PgConnInfo());
    }
    return *conn;
}

// Simple single-worker queue to offload DB work off the validation path.
class DbWorkQueue {
public:
    static DbWorkQueue& Instance()
    {
        static DbWorkQueue queue;
        return queue;
    }

    void Enqueue(std::function<void(pqxx::connection&)> job)
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_jobs.push(std::move(job));
        }
        m_cv.notify_one();
    }

    ~DbWorkQueue()
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_shutdown = true;
        }
        m_cv.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }

private:
    DbWorkQueue()
    {
        m_worker = std::thread([this] { this->Run(); });
    }

    void Run()
    {
        for (;;) {
            std::function<void(pqxx::connection&)> job;
            {
                std::unique_lock<std::mutex> l(m_mutex);
                m_cv.wait(l, [&] { return m_shutdown || !m_jobs.empty(); });
                if (m_shutdown && m_jobs.empty()) break;
                job = std::move(m_jobs.front());
                m_jobs.pop();
            }
            try {
                auto& conn = PgConnection();
                job(conn);
            } catch (const std::exception& e) {
                LogWarning("Enterprise DB worker failed: %s", e.what());
            }
        }
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void(pqxx::connection&)>> m_jobs;
    bool m_shutdown{false};
    std::thread m_worker;
};

} // namespace enterprise

#endif // ENTERPRISE_DB_H
