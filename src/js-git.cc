// (c) 2010-2011 by Anton Korenyushkin

#include "js-git.h"
#include "js-common.h"
#include "js-binary.h"

#include <git/odb.h>

#include <fstream>
#include <dirent.h>
#include <sys/stat.h>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// RepoBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    GitPathPatterns path_patterns;


    class RepoBg {
    public:
        DECLARE_JS_CONSTRUCTOR(RepoBg);

        RepoBg(const string& owner_name, const string& lib_name);
        ~RepoBg();

    private:
        string path_;
        git_odb* odb_ptr_;

        static void CheckName(const string& name);
        void ReadLooseRef(const string& name, Handle<Object> object) const;
        void ReadLooseRefs(const string& name, Handle<Object> object) const;
        Handle<Object> ReadRefs() const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ReadObjectCb,
                             const Arguments&) const;

    };
}


DEFINE_JS_CONSTRUCTOR(RepoBg, "Repo", /*object_template*/, proto_template)
{
    SetFunction(proto_template, "readObject", ReadObjectCb);
}


DEFINE_JS_CONSTRUCTOR_CALLBACK(RepoBg, args)
{
    CheckArgsLength(args, 2);
    auto_ptr<RepoBg> repo_ptr(
        new RepoBg(Stringify(args[0]), Stringify(args[1])));
    Set(args.This(), "refs", repo_ptr->ReadRefs());
    return repo_ptr.release();
}


RepoBg::RepoBg(const string& owner_name, const string& lib_name)
{
    CheckName(owner_name);
    CheckName(lib_name);
    BOOST_FOREACH(const GitPathPattern& path_pattern, path_patterns) {
        path_ = (path_pattern.prefix + owner_name +
                 path_pattern.suffix + lib_name +
                 path_pattern.ending);
        struct stat st;
        if (!stat(path_.c_str(), &st)) {
            int ret = git_odb_open(&odb_ptr_, (path_ + "/objects").c_str());
            AK_ASSERT_EQUAL(ret, 0);
            return;
        }
    }
    throw Error(Error::VALUE, "No such lib");
}


RepoBg::~RepoBg()
{
    git_odb_close(odb_ptr_);
}


void RepoBg::CheckName(const string& name)
{
    if (name.empty())
        throw Error(Error::VALUE, "Empty name");
    BOOST_FOREACH(char c, name)
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-'))
            throw Error(Error::VALUE, "Incorrect name");
}


void RepoBg::ReadLooseRef(const string& name, Handle<Object> object) const
{
    ifstream file((path_ + '/' + name).c_str());
    if (file.is_open()) {
        string line;
        getline(file, line);
        Set(object, name, String::New(line.c_str()));
        file.close();
    }
}


void RepoBg::ReadLooseRefs(const string& name, Handle<Object> object) const
{
    DIR* dir_ptr = opendir((path_ + '/' + name).c_str());
    if (dir_ptr) {
        while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
            string filename(dirent_ptr->d_name);
            if (filename != "." && filename != "..")
                ReadLooseRefs(name + '/' + filename, object);
        }
        closedir(dir_ptr);
    } else {
        ReadLooseRef(name, object);
    }
}


Handle<Object> RepoBg::ReadRefs() const
{
    Handle<Object> result(Object::New());
    ReadLooseRef("HEAD", result);
    ReadLooseRefs("refs", result);
    ifstream file((path_ + "/packed-refs").c_str());
    if (file.is_open()) {
        while (file.good()) {
            string line;
            getline(file, line);
            if (line.size() >= 42 && line[0] != '#' && line[40] == ' ')
                Set(result, line.substr(41), String::New(line.data(), 40));
        }
        file.close();
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RepoBg, ReadObjectCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    git_oid oid;
    Binarizator binarizator(args[0]);
    switch (binarizator.GetSize()) {
    case 20:
        git_oid_mkraw(
            &oid,
            reinterpret_cast<const unsigned char*>(binarizator.GetData()));
        break;
    case 40:
        if (!git_oid_mkstr(&oid, binarizator.GetData()))
            break;
    default:
        throw Error(Error::VALUE, "Invalid object id");
    }
    git_obj obj;
    if (git_odb_read(&obj, odb_ptr_, &oid))
        throw Error(Error::VALUE, "Object not found");
    Handle<Object> result(Object::New());
    Set(result, "type", String::NewSymbol(git_obj_type_to_string(obj.type)));
    const char* data = static_cast<char*>(obj.data);
    Set(result, "data",
        NewBinary(auto_ptr<Chars>(new Chars(data, data + obj.len))));
    git_obj_close(&obj);
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// InitGit
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitGit(const GitPathPatterns& path_patterns)
{
    ::path_patterns = path_patterns;
    Handle<Object> result(Object::New());
    PutClass<RepoBg>(result);
    return result;
}
