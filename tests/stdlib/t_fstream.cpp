/* C++ <fstream> + a little <filesystem>, on the writable cache: utility drive. */
#include "rxdk_test.h"
#include <fstream>
#include <string>
#include <filesystem>
#include <sys/stat.h>

int main() {
    const char *path = "cache:/t_fstream.txt";
    {
        std::ofstream o(path);
        CHECK(o.is_open(), "ofstream opens for write");
        o << "line1 " << 42 << "\n" << 3.5 << " end\n";
    }
    {
        std::ifstream in(path);
        CHECK(in.is_open(), "ifstream opens for read");
        std::string w; int n = 0; double d = 0;
        in >> w >> n;
        CHECK_STR(w.c_str(), "line1", "ifstream >> word");
        CHECK_EQI(n, 42, "ifstream >> int");
        std::string restofline; std::getline(in, restofline);
        in >> d;
        CHECK(d == 3.5, "ifstream >> double on 2nd line");
    }
    {
        std::ifstream in(path);
        std::string all((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        CHECK(all.size() > 10, "slurp whole file via istreambuf_iterator");
    }

    // sanity: the plain C stat our fs sits on
    {
        struct stat sb; sb.st_size = 0;
        int rc = stat(path, &sb);
        CHECK_EQI(rc, 0, "stat() succeeds");
        CHECK(sb.st_size > 10, "stat() st_size > 10");
    }

    namespace fs = std::filesystem;
    CHECK(fs::exists(path), "filesystem::exists(written file)");
    CHECK(fs::file_size(path) > 10, "filesystem::file_size");
    CHECK(!fs::exists("cache:/does_not_exist_xyz.txt"), "exists(missing) is false");

    fs::path p("cache:/dir/file.txt");
    std::string fn = p.filename().string();   // hold the string (c_str of a temp dangles)
    std::string ex = p.extension().string();
    CHECK_STR(fn.c_str(), "file.txt", "path::filename");
    CHECK_STR(ex.c_str(), ".txt", "path::extension");

    CHECK(fs::remove(path), "filesystem::remove");
    CHECK(!fs::exists(path), "removed file gone");

    CHECK_DONE("fstream");
    return 0;
}
