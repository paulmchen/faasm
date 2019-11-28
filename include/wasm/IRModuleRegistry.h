#pragma once

#include <WAVM/Runtime/Intrinsics.h>
#include <WAVM/IR/Module.h>

#include <shared_mutex>
#include <util/config.h>

// Note that page size in wasm is 64kiB
#define ONE_MB_PAGES 16
#define ONE_GB_PAGES 1024 * ONE_MB_PAGES
#define MAX_TABLE_SIZE 500000

// WARNING - when changing this, must also change in linker args for functions
#define MAX_MEMORY_PAGES 4 * ONE_GB_PAGES

using namespace WAVM;

namespace wasm {
    class IRModuleRegistry {
    public:
        IRModuleRegistry();

        IR::Module &getModule(const std::string &user, const std::string &func, const std::string &path);

        Runtime::ModuleRef getCompiledModule(const std::string &user, const std::string &func,
                const std::string &path);

        U64 getSharedModuleTableSize(const std::string &user, const std::string &func,
                                     const std::string &path);

    private:
        std::shared_mutex registryMutex;
        std::unordered_map<std::string, IR::Module> moduleMap;
        std::unordered_map<std::string, Runtime::ModuleRef> compiledModuleMap;
        std::unordered_map<std::string, int> originalTableSizes;

        util::SystemConfig &conf;

        int getModuleCount(const std::string &key);

        int getCompiledModuleCount(const std::string &key);

        IR::Module &getMainModule(const std::string &user, const std::string &func);

        IR::Module &getSharedModule(const std::string &user, const std::string &func, const std::string &path);

        Runtime::ModuleRef getCompiledMainModule(const std::string &user, const std::string &func);

        Runtime::ModuleRef getCompiledSharedModule(const std::string &user, const std::string &func,
                const std::string &path);
    };

    IRModuleRegistry &getIRModuleRegistry();
}
