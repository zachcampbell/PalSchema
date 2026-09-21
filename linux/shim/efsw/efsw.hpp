#pragma once
// palhook Linux stub for efsw (Entropia File System Watcher). PalSchema only uses it for optional
// auto-reload of mod files; on the dedicated server the watcher is a no-op that never fires.
#include <string>
#include <cstdint>
namespace efsw {
    using WatchID = long;
    namespace Actions { enum Action { Add = 1, Delete = 2, Modified = 3, Moved = 4 }; }
    using Action = Actions::Action;
    class FileWatchListener {
    public:
        virtual ~FileWatchListener() = default;
        virtual void handleFileAction(WatchID watchid, const std::string& dir, const std::string& filename, Action action, std::string oldFilename = "") = 0;
    };
    class FileWatcher {
    public:
        FileWatcher() = default;
        explicit FileWatcher(bool) {}
        WatchID addWatch(const std::string&, FileWatchListener*, bool = false) { return 1; }
        void removeWatch(const std::string&) {}
        void removeWatch(WatchID) {}
        void watch() {}
        bool followSymlinks() const { return false; }
        void followSymlinks(bool) {}
        bool allowOutOfScopeLinks() const { return false; }
        void allowOutOfScopeLinks(bool) {}
    };
}
