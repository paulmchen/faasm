#pragma once

#include <infra/infra.h>
#include <util/util.h>

#include <string>
#include <exception>
#include <tuple>
#include <thread>

#include <boost/filesystem.hpp>

#include <proto/faasm.pb.h>


namespace worker {
    /** Thread pool */
    class ThreadPool {
    public:
        explicit ThreadPool(int nThreads);

        int getSetExecuting();
        void setFinished(int threadIdx);

    private:
        // Variables for keeping track of threads
        int nThreads;
        std::set<int> allThreads;
        std::set<int> executingThreads;

        int getAvailable();
    };

    class NoThreadsException: public std::exception { };

    /** CGroup management */
    enum CgroupMode {cg_off, cg_on};

    class CGroup {
    public:
        explicit CGroup(const std::string &name);

        void limitCpu();
        void addThread(int threadId);

    private:
        std::string name;
        CgroupMode mode;
        std::vector<std::string> controllers;

        boost::filesystem::path getPathToController(const std::string &controller);
        boost::filesystem::path getPathToFile(const std::string &controller, const std::string &file);
        void mkdirForController(const std::string &controller);
    };

    /** Network isolation */
    enum NetworkIsolationMode {ns_off, ns_on};

    class NetworkNamespace {
    public:
        explicit NetworkNamespace(const std::string &name);
        void addThread(int threadId);
    };

    /** Worker wrapper */
    class Worker {
    public:
        Worker();

        void start();
    private:
        std::shared_ptr<CGroup> cgroup;
    };
}