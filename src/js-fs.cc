// (c) 2009-2011 by Anton Korenyushkin

#include "js-fs.h"
#include "js-common.h"
#include "js-binary.h"
#include "db.h"

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <sys/file.h>
#include <limits>
#include <fstream>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// FileStorageBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class FileBg {
    public:
        DECLARE_JS_CLASS(FileBg);

        FileBg(const string& path);
        ~FileBg();

    private:
        ifstream file;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CloseCb,
                             const Arguments&);

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ReadLineCb,
                             const Arguments&);

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GoodCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(FileBg, "File", /*object_template*/, proto_template)
{
    SetFunction(proto_template, "close", CloseCb);
    SetFunction(proto_template, "readLine", ReadLineCb);
    SetFunction(proto_template, "good", GoodCb);
}


FileBg::FileBg(const string& path)
    : file(path.c_str())
{
    if (!file.is_open())
        throw Error(Error::NO_SUCH_ENTRY, "No such entry: " + path);
}


FileBg::~FileBg()
{
    if (file.is_open())
        file.close();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, CloseCb,
                    const Arguments&, /*args*/)
{
    if (file.is_open())
        file.close();
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, ReadLineCb,
                    const Arguments&, /*args*/)
{
    string str;
    getline(file, str);
    return String::New(str.c_str());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, GoodCb,
                    const Arguments&, /*args*/) const
{
    return Boolean::New(file.good());
}

////////////////////////////////////////////////////////////////////////////////
// FileStorageBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class FileStorageBg {
    public:
        DECLARE_JS_CLASS(FileStorageBg);

        FileStorageBg(const string& root_path);

    private:
        string root_path_;

        string ReadAbsPath(const string& path) const;
        string ReadAbsPath(const Arguments& args) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ExistsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IsFolderCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IsFileCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ReadCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, OpenCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ListCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(FileStorageBg, "FileStorage",
                /*object_template*/, proto_template)
{
    SetFunction(proto_template, "exists", ExistsCb);
    SetFunction(proto_template, "isFolder", IsFolderCb);
    SetFunction(proto_template, "isFile", IsFileCb);
    SetFunction(proto_template, "read", ReadCb);
    SetFunction(proto_template, "open", OpenCb);
    SetFunction(proto_template, "list", ListCb);
}


FileStorageBg::FileStorageBg(const string& root_path)
    : root_path_(root_path)
{
}


string FileStorageBg::ReadAbsPath(const string& path) const
{
    size_t depth = 0;
    for (size_t from = 0; from < path.size(); ++from) {
        size_t to = path.find('/', from);
        if (to == from)
            continue;
        string component = path.substr(from, to - from);
        if (component == "..") {
            if (!depth)
                throw Error(
                    Error::VALUE,
                    "Path leads beyond the root directory: " + path);
            --depth;
        } else if (component != ".") {
            ++depth;
        }
        if (to == string::npos)
            break;
        from = to;
    }
    return root_path_ + '/' + path;
}


string FileStorageBg::ReadAbsPath(const Arguments& args) const
{
    CheckArgsLength(args, 1);
    return ReadAbsPath(Stringify(args[0]));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, ExistsCb,
                    const Arguments&, args) const
{
    struct stat st;
    return Boolean::New(!stat(ReadAbsPath(args).c_str(), &st));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, IsFolderCb,
                    const Arguments&, args) const
{
    struct stat st;
    return Boolean::New(
        !stat(ReadAbsPath(args).c_str(), &st) && S_ISDIR(st.st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, IsFileCb,
                    const Arguments&, args) const
{
    struct stat st;
    return Boolean::New(
        !stat(ReadAbsPath(args).c_str(), &st) && S_ISREG(st.st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, ReadCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(Stringify(args[0]));
    int fd = open(ReadAbsPath(path).c_str(), O_RDONLY);
    if (fd == -1)
        throw Error(Error::NO_SUCH_ENTRY, "No such entry: " + path);
    struct stat st;
    int ret = fstat(fd, &st);
    AK_ASSERT_EQUAL(ret, 0);
    if (S_ISDIR(st.st_mode)) {
        close(fd);
        throw Error(Error::ENTRY_IS_FOLDER, "Entry is folder: " + path);
    }
    auto_ptr<Chars> data_ptr(new Chars(st.st_size));
    ssize_t count = read(fd, &data_ptr->front(), st.st_size);
    AK_ASSERT_EQUAL(count, st.st_size);
    close(fd);
    return NewBinary(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, OpenCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    return JSNew<FileBg>(Stringify(args[0]));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, ListCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(Stringify(args[0]));
    DIR* dir_ptr = opendir(ReadAbsPath(path).c_str());
    if (!dir_ptr)
        throw (errno == ENOTDIR
               ? Error(Error::ENTRY_IS_FILE, "Entry is file: " + path)
               : Error(Error::NO_SUCH_ENTRY, "No such entry: " + path));
    Handle<Array> result(Array::New());
    size_t i = 0;
    while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
        string name(dirent_ptr->d_name);
        if (name != "." && name != "..")
            result->Set(Integer::New(i++), String::New(name.c_str()));
    }
    closedir(dir_ptr);
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// InitFS
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitFS(const string& code_path,
                          const string& lib_path)
{
    Handle<Object> result(Object::New());
    PutClass<FileStorageBg>(result);
    Set(result, "code", JSNew<FileStorageBg>(code_path));
    Set(result, "lib", JSNew<FileStorageBg>(lib_path));
    Set(result, "comstor", JSNew<FileStorageBg>("/akshell/comstor"), DontEnum);
    return result;
}
