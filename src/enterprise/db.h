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
#include <unordered_set>
#include <vector>

#include <logging.h>
#include <pqxx/pqxx>

#include <enterprise/dotenv.h>

namespace enterprise {

class ConnectionRegistry {
public:
    static ConnectionRegistry& Instance()
    {
        static ConnectionRegistry registry;
        return registry;
    }

    void Register(pqxx::connection* conn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.insert(conn);
    }

    void CloseAll()
    {
        std::vector<pqxx::connection*> connections;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            connections.assign(m_connections.begin(), m_connections.end());
        }
        for (auto* conn : connections) {
            if (conn == nullptr) continue;
            try {
                if (conn->is_open()) conn->close();
            } catch (const std::exception& e) {
                LogWarning("Enterprise DB close failed: %s", e.what());
            }
        }
    }

private:
    std::mutex m_mutex;
    std::unordered_set<pqxx::connection*> m_connections;
};

// Builds and caches the connection string from .env
inline std::string PgConnInfo()
{
    static const std::string conninfo = [] {
        auto& dotenv = env;
        dotenv.config();
        std::ostringstream s;
        s << "dbname=" << dotenv["PGDB"]
          << " user=" << dotenv["PGUSER"];
        if (!dotenv["PGPASSWORD"].empty()) {
            s << " password=" << dotenv["PGPASSWORD"];
        }
        if (!dotenv["PGHOST"].empty()) {
            s << " host=" << dotenv["PGHOST"];
        }
        if (!dotenv["PGPORT"].empty()) {
            s << " port=" << dotenv["PGPORT"];
        }
        return s.str();
    }();
    return conninfo;
}

// Thread-local connection provider to avoid reconnecting per block.
inline pqxx::connection& PgConnection()
{
    // Keep the connection alive for the process lifetime and close it explicitly
    // during init::Shutdown(), before libpqxx's own finalizers run.
    thread_local pqxx::connection* conn{nullptr};
    if (conn == nullptr || !conn->is_open()) {
        conn = new pqxx::connection(PgConnInfo());
        ConnectionRegistry::Instance().Register(conn);
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
            if (m_shutdown) {
                LogWarning("Enterprise DB worker is shutting down, dropping queued job");
                return;
            }
            m_jobs.push(std::move(job));
        }
        m_cv.notify_one();
    }

    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_shutdown = true;
        }
        m_cv.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }

    ~DbWorkQueue()
    {
        Shutdown();
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

inline void ShutdownDb()
{
    DbWorkQueue::Instance().Shutdown();
    ConnectionRegistry::Instance().CloseAll();
}

} // namespace enterprise

#endif // ENTERPRISE_DB_H
