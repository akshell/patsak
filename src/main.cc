
// (c) 2009 by Anton Korenyushkin

/// \file main.cc
/// Program entry point

#include "js.h"
#include "db.h"

#include <asio.hpp>
#include <boost/program_options.hpp>
#include <boost/assign/std/vector.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include <sys/file.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>


using namespace std;
using namespace ku;
using asio::local::stream_protocol;
namespace po = boost::program_options;
using namespace boost::assign;
using boost::bind;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    boost::posix_time::milliseconds CONNECT_TIMEOUT(1000);
    boost::posix_time::milliseconds READ_TIMEOUT(1000);
}

////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

namespace
{
    void Daemonize()
    {
        if (getppid() == 1)
            return;
        pid_t pid = fork();
        if (pid < 0) {
            perror(0);
            exit(1);
        }
        if (pid > 0) {
            cerr << "PID " << pid << '\n';
            exit(0);
        }

        umask(0);
        if (setsid() < 0) {
            perror(0);
            exit(1);
        }
        if (chdir("/") < 0) {
            perror(0);
            exit(1);
        }
    }


    void MakePathAbsolute(const string& base_path, string& path)
    {
        if (path.empty() || path[0] != '/')
            path = base_path + '/' + path;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Lock
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Lock {
    public:
        Lock(const string& path);
        ~Lock();

    private:
        int fd_;
    };
}


Lock::Lock(const string& path)
{
    fd_ = open(path.c_str(), O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
    KU_ASSERT(fd_ != -1);
    int ret = flock(fd_, LOCK_EX);
    KU_ASSERT(!ret);
}


Lock::~Lock()
{
    close(fd_);
}

////////////////////////////////////////////////////////////////////////////////
// Connector
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Connector {
    public:
        enum Result {
            OK,
            FAIL,
            TIMED_OUT
        };
        
        Connector(stream_protocol::socket& socket);
        Result operator()(const stream_protocol::endpoint& endpoint);

    private:
        static const Result INITIAL = static_cast<Result>(-1);
        
        stream_protocol::socket& socket_;
        asio::deadline_timer timer_;
        Result result_;

        void HandleConnect(const asio::error_code& error);
        void HandleTimer(const asio::error_code& error);
    };
}


Connector::Connector(stream_protocol::socket& socket)
    : socket_(socket)
    , timer_(socket.get_io_service())
{
}


Connector::Result
Connector::operator()(const stream_protocol::endpoint& endpoint)
{
    socket_.get_io_service().reset();
    result_ = INITIAL;
    socket_.async_connect(endpoint, bind(&Connector::HandleConnect, this, _1));
    timer_.expires_from_now(CONNECT_TIMEOUT);
    timer_.async_wait(bind(&Connector::HandleTimer, this, _1));
    socket_.get_io_service().run();
    KU_ASSERT(result_ != INITIAL);
    return result_;
}


void Connector::HandleConnect(const asio::error_code& error)
{
    if (result_ == INITIAL) {
        timer_.cancel();
        result_ = error ? FAIL : OK;
    }
}


void Connector::HandleTimer(const asio::error_code& /*error*/)
{
    if (result_ == INITIAL) {
        socket_.cancel();
        result_ = TIMED_OUT;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Reader
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Reader {
    public:
        Reader(stream_protocol::socket& socket, Chars& data);
        bool operator()();

    private:
        enum State {
            INITIAL,
            OK,
            TIMED_OUT
        };
        
        stream_protocol::socket& socket_;
        asio::streambuf buf_;
        Chars& data_;
        asio::deadline_timer timer_;
        State state_;

        void HandleRead(const asio::error_code& error, size_t size);
        void HandleTimer(const asio::error_code& error);
    };
}


Reader::Reader(stream_protocol::socket& socket, Chars& data)
    : socket_(socket)
    , data_(data)
    , timer_(socket.get_io_service())
{
}


bool Reader::operator()()
{
    state_ = INITIAL;
    async_read(socket_, buf_, bind(&Reader::HandleRead, this, _1, _2));
    timer_.expires_from_now(READ_TIMEOUT);
    timer_.async_wait(bind(&Reader::HandleTimer, this, _1));
    socket_.get_io_service().reset();
    socket_.get_io_service().run();
    KU_ASSERT(state_ != INITIAL);
    return state_ == OK;
}


void Reader::HandleRead(const asio::error_code& /*error*/, size_t size)
{
    if (state_ == INITIAL) {
        timer_.cancel();
        data_.resize(size);
        buf_.commit(size);
        istream(&buf_).read(&data_[0], size);
        state_ = OK;
    }
}


void Reader::HandleTimer(const asio::error_code& /*error*/)
{
    if (state_ == INITIAL) {
        socket_.cancel();
        state_ = TIMED_OUT;
    }
}

////////////////////////////////////////////////////////////////////////////////
// AppAccessorImpl
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class AppAccessorImpl : public AppAccessor {
    public:
        AppAccessorImpl(const string& self_name,
                        const string& code_dir,
                        const string& socket_dir,
                        const string& guard_dir,
                        const Strings& args,
                        const string& user); // user is held by reference
        
        virtual Status Process(const string& self_name,
                               const Chars* data_ptr,
                               const Strings& file_pathes,
                               const string& request,
                               Chars& result);

        virtual bool Exists(const string& app_name);
        
    private:
        asio::io_service io_service_;
        string self_name_;
        string code_dir_;
        string socket_dir_;
        string guard_dir_;
        Strings args_;
        const string& user_;

        void LaunchApp(const string& app_name);
    };
}


AppAccessorImpl::AppAccessorImpl(const string& self_name,
                                 const string& code_dir,
                                 const string& socket_dir,
                                 const string& guard_dir,
                                 const Strings& args,
                                 const string& user)
    : self_name_(self_name)
    , code_dir_(code_dir)
    , socket_dir_(socket_dir)
    , guard_dir_(guard_dir)
    , args_(args)
    , user_(user)
{
}


AppAccessor::Status AppAccessorImpl::Process(const string& app_name,
                                             const Chars* data_ptr,
                                             const Strings& file_pathes,
                                             const string& request,
                                             Chars& result)
{
    if (app_name.empty() || app_name.find('/') != string::npos ||
        app_name == "."  || app_name == "..")
        return INVALID_APP_NAME;
    if (app_name == self_name_)
        return SELF_CALL;
    
    stream_protocol::endpoint endpoint(socket_dir_ + "/release/" + app_name);
    stream_protocol::socket socket(io_service_);

    Connector connector(socket);
    Connector::Result connect_result = connector(endpoint);
    if (connect_result == Connector::TIMED_OUT)
        return TIMED_OUT;
    if (connect_result == Connector::FAIL) {
        Lock(guard_dir_ + "/release/" + app_name);
        if (!Exists(app_name))
            return NO_SUCH_APP;
        connect_result = connector(endpoint);
        if (connect_result == Connector::TIMED_OUT)
            return TIMED_OUT;
        if (connect_result == Connector::FAIL) {
            LaunchApp(app_name);
            connect_result = connector(endpoint);
        }
    }
    KU_ASSERT(connect_result == Connector::OK);

    vector<asio::const_buffer> buffers;
    buffers.reserve(file_pathes.size() + 5);

    string process_header("PROCESS");
    buffers.push_back(asio::buffer(process_header));

    string data_header;
    if (data_ptr) {
        data_header = "\nDATA " + lexical_cast<string>(data_ptr->size()) + '\n';
        buffers.push_back(asio::buffer(data_header));
        buffers.push_back(asio::buffer(*data_ptr));
    }

    vector<string> file_descrs;
    file_descrs.reserve(file_pathes.size());
    BOOST_FOREACH(const string& file_path, file_pathes) {
        file_descrs.push_back("\nFILE " + file_path);
        buffers.push_back(asio::buffer(file_descrs.back()));
    }

    string request_header("\nUSER " + user_ +
                          "\nREQUEST " +
                          lexical_cast<string>(request.size()) +
                          '\n');
    buffers.push_back(asio::buffer(request_header));
    buffers.push_back(asio::buffer(request));

    asio::error_code error_code;
    asio::write(socket, buffers, asio::transfer_all(), error_code);
    KU_ASSERT(!error_code);

    Reader reader(socket, result);
    if (!reader())
        return TIMED_OUT;
    return OK;
}


bool AppAccessorImpl::Exists(const string& app_name)
{
    string code_path(code_dir_ + "/release/" + app_name);
    struct stat st;
    int ret = stat(code_path.c_str(), &st);
    if (ret == -1) {
        KU_ASSERT(errno == ENOENT);
        return false;
    }
    KU_ASSERT(S_ISDIR(st.st_mode));
    return true;
}


void AppAccessorImpl::LaunchApp(const string& app_name)
{
    int pipe_fds[2];
    int ret = pipe(pipe_fds);
    KU_ASSERT(!ret);
    pid_t pid = fork();
    KU_ASSERT(pid != -1);
    if (pid) {
        close(pipe_fds[1]);
        pid_t child_pid = waitpid(pid, 0, 0);
        KU_ASSERT(child_pid == pid);
        char buf[6];
        ssize_t count = read(pipe_fds[0], buf, 6);
        close(pipe_fds[0]);
        KU_ASSERT(count == 6);
        KU_ASSERT(string(buf, buf + 6) == "READY\n");
    } else {
        close(pipe_fds[0]);
        freopen("/dev/null", "w", stderr);
        int fd = dup2(pipe_fds[1], 1);
        KU_ASSERT(fd == 1);
        if (pipe_fds[1] != 1)
            close(pipe_fds[1]);
        vector<char*> argv(args_.size() + 2);
        for (size_t i = 0; i < args_.size(); ++i)
            argv[i] = const_cast<char*>(args_[i].c_str());
        argv[args_.size()] = const_cast<char*>(app_name.c_str());
        argv.back() = 0;
        execv(argv[0], &argv[0]);
        Fail(strerror(errno));
    }
}

////////////////////////////////////////////////////////////////////////////////
// RequestHandler
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class RequestHandler {
    public:
        RequestHandler(Program& program,
                       string& user,
                       stream_protocol::socket& socket);
        bool Handle();

    private:
        struct ProcessingError : public runtime_error {
            ProcessingError(const string& message) : runtime_error(message) {}
        };
        
        Program& program_;
        string& user_;
        stream_protocol::socket& socket_;
        asio::streambuf buf_;
        istream is_;

        void HandleProcess();
        void Write(const string& str) const;
        void NextLine();
        void ReadLine();
        void Read(size_t size);
    };
}

RequestHandler::RequestHandler(Program& program,
                               string& user,
                               stream_protocol::socket& socket)
    : program_(program)
    , user_(user)
    , socket_(socket)
    , is_(&buf_)
{
    is_.exceptions(ios::eofbit | ios::failbit | ios::badbit);
}
        

bool RequestHandler::Handle()
{
    try {
        ReadLine();
        string request;
        is_ >> request;
        if (request == "PROCESS") {
            HandleProcess();
            return program_.IsOperable();
        }
        if (request == "STATUS") {
            Write("OK\n");
            return true;
        }
        if (request == "STOP") {
            Write("OK\n");
            return false;
        }
        throw ProcessingError("Unknown request: " + request);
    } catch (const asio::system_error& err) {
        Log(string("Error on socket: ") + err.what());
        return true;
    } catch (const exception& err) {
        Log(string("Error during request processing: ") + err.what());
        asio::error_code error_code;
        asio::write(socket_,
                    asio::buffer(string("FAIL\n") + err.what()),
                    asio::transfer_all(),
                    error_code);
        return true;
    }
}


void RequestHandler::HandleProcess()
{
    auto_ptr<Response> response_ptr;
    if (is_.peek() == ' ') {
        string expr_str;
        getline(is_, expr_str);
        Chars expr(expr_str.begin(), expr_str.end());
        user_ = "";
        response_ptr = program_.Eval(user_, expr);
    } else {
        NextLine();
        ReadLine();
        string command;
        is_ >> command;
        auto_ptr<Chars> data_ptr;
        if (command == "DATA") {
            size_t size;
            istream(&buf_) >> size;
            NextLine();
            Read(size + 1);
            data_ptr.reset(new Chars(size));
            is_.read(&(data_ptr->front()), size);
            NextLine();
            ReadLine();
            is_ >> command;
        }
        Strings file_pathes;
        while (command == "FILE") {
            if (is_.get() != ' ')
                throw ProcessingError("Bad FILE command");
            file_pathes.push_back(string());
            getline(is_, file_pathes.back());
            ReadLine();
            is_ >> command;
        }
        if (command == "USER") {
            if (is_.get() != ' ')
                throw ProcessingError("Bad USER command");
            getline(is_, user_);
            ReadLine();
            is_ >> command;
        } else {
            user_ = "";
        }
        if (command != "REQUEST" && command != "EXPR")
            throw ProcessingError("Unexpected command: " + command);
        size_t size;
        is_ >> size;
        NextLine();
        Read(size);
        Chars input(size);
        is_.read(&input[0], size);
        if (command == "EXPR") {
            if (data_ptr.get())
                throw ProcessingError("DATA is not supported by EXPR");
            if (!file_pathes.empty())
                throw ProcessingError("FILE is not supported by EXPR");
            response_ptr = program_.Eval(user_, input);
        } else {
            response_ptr = program_.Process(user_,
                                            input,
                                            file_pathes,
                                            data_ptr);
        }
    }
    KU_ASSERT(response_ptr.get());
    vector<asio::const_buffer> buffers;
    string beginning = response_ptr->GetStatus() + '\n';
    buffers.push_back(asio::buffer(beginning));
    buffers.push_back(asio::buffer(response_ptr->GetData(),
                                   response_ptr->GetSize()));
    asio::write(socket_, buffers);    
}


void RequestHandler::Write(const string& str) const
{
    asio::write(socket_, asio::buffer(str));
}


void RequestHandler::NextLine()
{
    if (is_.get() != '\n')
        throw ProcessingError("Ill formed request");
}


void RequestHandler::ReadLine()
{
    asio::read_until(socket_, buf_, '\n');
}


namespace
{
    class TransferController {
    public:
        TransferController(size_t size) : size_(size) {}
        
        size_t operator()(const asio::error_code&, size_t bytes_read) const {
            return bytes_read > size_ ? 0 : size_ - bytes_read;
        }

    private:
        size_t size_;
    };
}


void RequestHandler::Read(size_t size)
{
    if (size > buf_.size())
        asio::read(socket_, buf_, TransferController(size - buf_.size()));
}

////////////////////////////////////////////////////////////////////////////////
// MainRunner
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class MainRunner {
    public:
        MainRunner(int argc, char** argv);
        void Run();
        
    private:
        string expr_, user_;
        string log_dir_, code_dir_, media_dir_;
        string socket_dir_, guard_dir_;
        string db_user_, db_password_, db_name_;
        string app_name_, owner_name_, spot_name_;
        bool test_mode_;

        void Parse(int argc, char** argv);
        
        static void RequireOption(const string& name,
                                  const string& value);

        void Check() const;
        void MakePathesAbsolute();
        bool IsRelease() const;
        string GetPathSuffix() const;
        auto_ptr<DB> InitDB() const;
        auto_ptr<AppAccessor> InitAppAccessor() const;

        auto_ptr<Program> InitProgram(DB& db,
                                      AppAccessor& app_accessor) const;
        
        void RunTest(Program& program, istream& is) const;
        
        static bool HandleRequest(Program& program,
                                  stream_protocol::socket& socket);

        void RunServer(Program& program);
    };
}    


MainRunner::MainRunner(int argc, char** argv)
{
    Parse(argc, argv);
    Check();
}


void MainRunner::Run()
{
    MakePathesAbsolute();
    if (!test_mode_) {
        OpenLogFile(log_dir_ + GetPathSuffix());
        Daemonize();
    }
    auto_ptr<DB> db_ptr(InitDB());
    auto_ptr<AppAccessor> app_accessor_ptr(InitAppAccessor());
    auto_ptr<Program> program_ptr(InitProgram(*db_ptr, *app_accessor_ptr));
    if (test_mode_) {
        if (expr_.empty()) {
            RunTest(*program_ptr, cin);
        } else {
            istringstream iss(expr_);
            RunTest(*program_ptr, iss);
        }
    } else {
        RunServer(*program_ptr);
    }
}


void MainRunner::Parse(int argc, char** argv)
{
    po::options_description generic_options("Generic options");
    generic_options.add_options()
        ("help,h", "print help message")
        ("config-file,f",
         po::value<string>()->default_value("/etc/patsak"),
         "config file path")
        ("test,t", po::bool_switch(&test_mode_), "test mode")
        ("expr,e",
         po::value<string>(&expr_),
         "expr for evaluation (test mode only)")
        ("user,u",
         po::value<string>(&user_),
         "user name (test mode only)")
        ;
    
    po::options_description config_options("Config options");
    config_options.add_options()
        ("log-dir,l", po::value<string>(&log_dir_), "log directory")
        ("socket-dir,s", po::value<string>(&socket_dir_), "socket directory")
        ("guard-dir,g", po::value<string>(&guard_dir_), "guard directory")
        ("code-dir,c", po::value<string>(&code_dir_), "code directory")
        ("media-dir,m", po::value<string>(&media_dir_), "media directory")
        ("db-user",
         po::value<string>(&db_user_)->default_value("patsak"),
         "database user")
        ("db-password", po::value<string>(&db_password_), "database password")
        ("db-name",
         po::value<string>(&db_name_)->default_value("ak"),
         "database name")
        ;

    po::options_description hidden_options;
    hidden_options.add_options()
        ("app-name", po::value<string>(&app_name_))
        ("owner-name", po::value<string>(&owner_name_))
        ("spot-name", po::value<string>(&spot_name_))
        ;

    po::positional_options_description positional_options;
    positional_options
        .add("app-name", 1)
        .add("owner-name", 1)
        .add("spot-name", 1)
        ;

    po::options_description cmdline_options;
    cmdline_options
        .add(generic_options)
        .add(config_options)
        .add(hidden_options);

    po::variables_map vm;
    try {
        po::store((po::command_line_parser(argc, argv)
                   .options(cmdline_options)
                   .positional(positional_options).run()),
                  vm);
        ifstream config_file(vm["config-file"].as<string>().c_str());
        if (config_file.is_open()) {
            po::store(po::parse_config_file(config_file, config_options), vm);
            config_file.close();
        }
        po::notify(vm);
    } catch (po::error& err) {
        cerr << err.what() << '\n';
        exit(1);
    }

    if (vm.count("help")) {
        po::options_description visible_options(
            string("Usage: ") + argv[0] +
            " [options] app_name [user_name spot_name]");
        visible_options.add(generic_options).add(config_options);
        cout << visible_options << "\n";
        exit(0);
    }
}


void MainRunner::RequireOption(const string& name,
                               const string& value)
{
    if (value.empty()) {
        cerr << "option " << name << " is required\n";
        exit(1);
    }
}


void MainRunner::Check() const
{
    RequireOption("log-dir", log_dir_);
    RequireOption("socket-dir", socket_dir_);
    RequireOption("guard-dir", guard_dir_);
    RequireOption("code-dir", code_dir_);
    RequireOption("media-dir", media_dir_);
    RequireOption("db-user", db_user_);
    RequireOption("db-password", db_password_);
    if (!test_mode_) {
        if (!expr_.empty() || !user_.empty()) {
            cerr << "expr and user option are specific to test mode\n";
            exit(1);
        }
    }
    if (app_name_.empty()) {
        cerr << "app name must be specified\n";
        exit(1);
    }
    if (!owner_name_.empty() && spot_name_.empty()) {
        cerr << "owner name and spot name must be specified together\n";
        exit(1);
    }
}


void MainRunner::MakePathesAbsolute()
{
    char* curr_dir = get_current_dir_name();
    if (!curr_dir) {
        perror("get_current_dir_name");
        exit(1);
    }
    MakePathAbsolute(curr_dir, log_dir_);
    MakePathAbsolute(curr_dir, socket_dir_);
    MakePathAbsolute(curr_dir, guard_dir_);
    MakePathAbsolute(curr_dir, code_dir_);
    MakePathAbsolute(curr_dir, media_dir_);
    free(curr_dir);
}


bool MainRunner::IsRelease() const
{
    return owner_name_.empty();
}


string MainRunner::GetPathSuffix() const
{
    return (IsRelease()
            ? "/release/" + app_name_
            : "/spots/" + app_name_ + '/' + owner_name_ + '/' + spot_name_);
}

auto_ptr<DB> MainRunner::InitDB() const
{
    string options("user=" + db_user_ +
                   " password=" + db_password_ +
                   " dbname=" + db_name_);
    string schema_name(':' + app_name_);
    if (!IsRelease())
        schema_name += ':' + owner_name_ + ':' + spot_name_;
    return auto_ptr<DB>(new DB(options, schema_name));
}


auto_ptr<AppAccessor> MainRunner::InitAppAccessor() const
{
    Strings args;
    args +=
        "/proc/self/exe",
        "--log-dir", log_dir_,
        "--socket-dir", socket_dir_,
        "--guard-dir", guard_dir_,
        "--code-dir", code_dir_,
        "--media-dir", media_dir_,
        "--db-user", db_user_,
        "--db-password", db_password_,
        "--db-name", db_name_;
    return auto_ptr<AppAccessor>(new AppAccessorImpl(app_name_,
                                                     code_dir_,
                                                     socket_dir_,
                                                     guard_dir_,
                                                     args,
                                                     user_));
}


auto_ptr<Program> MainRunner::InitProgram(DB& db,
                                          AppAccessor& app_accessor) const
{
    string code_path(code_dir_ + GetPathSuffix());
    string media_path(media_dir_ + GetPathSuffix());
    return auto_ptr<Program>(new Program(code_path,
                                         code_dir_ + "/release/",
                                         media_path,
                                         db,
                                         app_accessor));
}


void MainRunner::RunTest(Program& program, istream& is) const
{
    while (!is.eof()) {
        string expr_str;
        getline(is, expr_str);
        if (expr_str.empty())
            continue;
        Chars expr(expr_str.begin(), expr_str.end());
        auto_ptr<Response> response_ptr(program.Eval(user_, expr));
        cout << response_ptr->GetStatus() << '\n';
        cout.write(response_ptr->GetData(), response_ptr->GetSize());
        cout << '\n';
    }
}


void MainRunner::RunServer(Program& program)
{
    string socket_path(socket_dir_ + GetPathSuffix());
    remove(socket_path.c_str());
    asio::io_service io_service;
    stream_protocol::acceptor acceptor(io_service);
    stream_protocol::endpoint endpoint(socket_path);
    try {
        acceptor.open(endpoint.protocol());
        acceptor.bind(endpoint);
        acceptor.listen();

        cout << "READY\n";
        cout.flush();
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/tmp/patsak-log", "a", stderr);
    
        for (bool go_on = true; go_on; ) {
            stream_protocol::socket socket(io_service);
            acceptor.accept(socket);
            RequestHandler request_handler(program, user_, socket);
            go_on = request_handler.Handle();
            socket.close();
        }
        
        acceptor.close();
    } catch (const asio::system_error& err) {
        Log(string("Network error: ") + err.what());
    }
    remove(socket_path.c_str());
}

////////////////////////////////////////////////////////////////////////////////
// main
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    MainRunner runner(argc, argv);
    runner.Run();
    return 0;
}
