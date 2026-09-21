// palhook: PS::LinuxMirror sink for the full PalSchema build. Appends every PalSchema log line to
// /tmp/paltest/palschema.log (UE4SS's own console output also carries it via Output::send).
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
namespace PS {
    void LinuxMirror(const std::u16string& line)
    {
        static std::mutex m; std::lock_guard<std::mutex> lock(m);
        static FILE* f = std::fopen("/tmp/paltest/palschema.log", "a");
        if (!f) return;
        std::string n; n.reserve(line.size());
        for (char16_t c : line) n.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        std::time_t t = std::time(nullptr); char ts[16]; std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&t));
        std::fprintf(f, "[%s] %s%s", ts, n.c_str(), (n.empty() || n.back() != '\n') ? "\n" : ""); std::fflush(f);
    }
}
