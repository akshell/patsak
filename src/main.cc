
// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"
#include "db.h"

#include <asio.hpp>
#include <boost/program_options.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <fstream>


using namespace std;
using namespace ku;
using asio::local::stream_protocol;
namespace po = boost::program_options;
using boost::bind;


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
// RequestHandler
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class RequestHandler {
    public:
        RequestHandler(Program& program, stream_protocol::socket& socket);
        bool Handle();

    private:
        struct ProcessingError : public runtime_error {
            ProcessingError(const string& message) : runtime_error(message) {}
        };

        Program& program_;
        stream_protocol::socket& socket_;
        asio::streambuf buf_;
        istream is_;

        void HandleProcess();
        void Write(const string& str) const;
        void NextLine();
        void ReadLine();
        void Read(size_t size);
        string ReadCommandTail();
    };
}

RequestHandler::RequestHandler(Program& program,
                               stream_protocol::socket& socket)
    : program_(program)
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
            return !program_.IsDead();
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
        if (err.code().value() != EPIPE)
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
        response_ptr = program_.Eval("", expr);
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
            if (size) {
                data_ptr.reset(new Chars(size));
                is_.read(&(data_ptr->front()), size);
            }
            NextLine();
            ReadLine();
            is_ >> command;
        }
        Strings file_pathes;
        while (command == "FILE") {
            file_pathes.push_back(ReadCommandTail());
            is_ >> command;
        }
        string user;
        if (command == "USER") {
            user = ReadCommandTail();
            is_ >> command;
        }
        string issuer;
        if (command == "ISSUER") {
            issuer = ReadCommandTail();
            is_ >> command;
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
            if (!issuer.empty())
                throw ProcessingError("ISSUER is not supported by EXPR");
            response_ptr = program_.Eval(user, input);
        } else {
            response_ptr = program_.Process(user,
                                            input,
                                            file_pathes,
                                            data_ptr,
                                            issuer);
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


string RequestHandler::ReadCommandTail()
{
    if (is_.get() != ' ')
        throw ProcessingError("Bad command tail");
    string result;
    getline(is_, result);
    ReadLine();
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// Acceptor
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Acceptor {
    public:
        Acceptor(stream_protocol::acceptor& acceptor, unsigned wait);
        auto_ptr<stream_protocol::socket> operator()();

    private:
        stream_protocol::acceptor& acceptor_;
        asio::deadline_timer timer_;
        unsigned wait_;
        bool handled_;
        bool accepted_;

        void HandleAccept(const asio::error_code& error);
        void HandleTimer(const asio::error_code& error);
    };
}


Acceptor::Acceptor(stream_protocol::acceptor& acceptor, unsigned wait)
    : acceptor_(acceptor)
    , timer_(acceptor.get_io_service())
    , wait_(wait)
    , handled_(false)
    , accepted_(false)
{
}


auto_ptr<stream_protocol::socket> Acceptor::operator()()
{
    acceptor_.get_io_service().reset();
    auto_ptr<stream_protocol::socket> socket_ptr(
        new stream_protocol::socket(acceptor_.get_io_service()));
    acceptor_.async_accept(*socket_ptr,
                           bind(&Acceptor::HandleAccept, this, _1));
    timer_.expires_from_now(boost::posix_time::seconds(wait_));
    timer_.async_wait(bind(&Acceptor::HandleTimer, this, _1));
    acceptor_.get_io_service().run();
    KU_ASSERT(handled_);
    return accepted_ ? socket_ptr : auto_ptr<stream_protocol::socket>();
}


void Acceptor::HandleAccept(const asio::error_code& error)
{
    if (!handled_) {
        timer_.cancel();
        KU_ASSERT(!error);
        handled_ = accepted_ = true;
    }
}


void Acceptor::HandleTimer(const asio::error_code& /*error*/)
{
    if (!handled_) {
        acceptor_.cancel();
        handled_ = true;
    }
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
        string patsak_pattern_;
        string log_file_, code_dir_, media_dir_;
        string socket_dir_, guard_dir_;
        string db_user_, db_password_, db_name_;
        string app_name_, owner_name_, spot_name_;
        string path_suffix_;
        bool test_mode_;
        unsigned wait_;
        string config_file_;
        bool pass_opts_;

        void Parse(int argc, char** argv);

        static void RequireOption(const string& name,
                                  const string& value);

        void Check() const;
        void MakePathesAbsolute();
        bool IsRelease() const;
        auto_ptr<DB> InitDB() const;

        auto_ptr<Program> InitProgram(DB& db) const;

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
    if (!test_mode_)
        Daemonize();
    auto_ptr<DB> db_ptr(InitDB());
    auto_ptr<Program> program_ptr(InitProgram(*db_ptr));
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
        ("rev,r", "print revision")
        ("config-file,f",
         po::value<string>(&config_file_)->default_value("/ak/patsak.conf"),
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
        ("patsak-pattern,p",
         po::value<string>(&patsak_pattern_),
         "patsak path pattern")
        ("log-file,l", po::value<string>(&log_file_), "log file")
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
        ("wait,w",
         po::value<unsigned>(&wait_)->default_value(100),
         "seconds to wait for connection")
        ("pass-opts",
         po::bool_switch(&pass_opts_),
         "pass options to children, not config")
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

    if (vm.count("rev")) {
        // REVISION must be provided as a compiler option
        cout << REVISION << '\n';
        exit(0);
    }

    path_suffix_ =
        IsRelease()
        ? "/release/" + app_name_
        : "/spots/" + app_name_ + '/' + owner_name_ + '/' + spot_name_;
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
    RequireOption("log-file", log_file_);
    RequireOption("socket-dir", socket_dir_);
    RequireOption("guard-dir", guard_dir_);
    RequireOption("code-dir", code_dir_);
    RequireOption("media-dir", media_dir_);
    RequireOption("db-user", db_user_);
    RequireOption("db-password", db_password_);
    if (!test_mode_ && !(expr_.empty() && user_.empty())) {
        cerr << "expr and user option are specific to test mode\n";
        exit(1);
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
    MakePathAbsolute(curr_dir, log_file_);
    MakePathAbsolute(curr_dir, socket_dir_);
    MakePathAbsolute(curr_dir, guard_dir_);
    MakePathAbsolute(curr_dir, code_dir_);
    MakePathAbsolute(curr_dir, media_dir_);
    MakePathAbsolute(curr_dir, config_file_);
    free(curr_dir);
}


bool MainRunner::IsRelease() const
{
    return owner_name_.empty();
}


auto_ptr<DB> MainRunner::InitDB() const
{
    string options("user=" + db_user_ +
                   " password=" + db_password_ +
                   " dbname=" + db_name_);
    string schema_name(':' + app_name_);
    if (!IsRelease())
        schema_name += ':' + owner_name_ + ':' + spot_name_;
    return auto_ptr<DB>(new DB(options, schema_name, app_name_));
}


auto_ptr<Program> MainRunner::InitProgram(DB& db) const
{
    string spaced_owner_name(owner_name_);
    BOOST_FOREACH(char& c, spaced_owner_name)
        if (c == '-')
            c = ' ';
    return auto_ptr<Program>(new Program(Place(app_name_,
                                               spaced_owner_name,
                                               spot_name_),
                                         code_dir_ + path_suffix_,
                                         code_dir_ + "/release/",
                                         media_dir_ + path_suffix_,
                                         media_dir_ + "/release/",
                                         db));
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
    freopen("/dev/null", "r", stdin);
    freopen(log_file_.c_str(), "a", stderr);
    log_prefix = ((IsRelease()
                   ? app_name_
                   : app_name_ + ':' + owner_name_ + '@' + spot_name_) +
                  ": ");

    string socket_path(socket_dir_ + path_suffix_);
    remove(socket_path.c_str());
    asio::io_service io_service;
    stream_protocol::acceptor acceptor(io_service);
    stream_protocol::endpoint endpoint(socket_path);
    try {
        acceptor.open(endpoint.protocol());
        fcntl(acceptor.native(), F_SETFD, FD_CLOEXEC);
        acceptor.bind(endpoint);
        acceptor.listen();

        cout << "READY\n";
        cout.flush();
        freopen("/dev/null", "w", stdout);

        for (;;) {
            auto_ptr<stream_protocol::socket> socket_ptr(
                Acceptor(acceptor, wait_)());
            if (!socket_ptr.get())
                break;
            fcntl(socket_ptr->native(), F_SETFD, FD_CLOEXEC);
            RequestHandler request_handler(program, *socket_ptr);
            bool proceed = request_handler.Handle();
            socket_ptr->close();
            if (!proceed)
                break;
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
