
// (c) 2009 by Anton Korenyushkin

/// \file main.cc
/// Program entry point

#include "js.h"
#include "db.h"

#include <asio.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>

using namespace std;
using namespace ku;
using asio::local::stream_protocol;
namespace po = boost::program_options;


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

        void HandleEval();
        void Write(const string& str) const;
        void NextLine();
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
        asio::read_until(socket_, buf_, '\n');
        string request;
        is_ >> request;
        if (request == "EVAL") {
            HandleEval();
            return true;
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


void RequestHandler::HandleEval()
{
    Chars expr;
    auto_ptr<Chars> data_ptr;
    if (is_.peek() == ' ') {
        string expr_str;
        getline(is_, expr_str);
        expr.assign(expr_str.begin(), expr_str.end());
    } else {
        NextLine();
        asio::read_until(socket_, buf_, '\n');
        string command;
        is_ >> command;
        if (command == "DATA") {
            size_t size;
            is_ >> size;
            NextLine();
            data_ptr.reset(new Chars(size));
            is_.read(&(data_ptr->front()), size);
            NextLine();
            is_ >> command;
        }
        if (command != "EXPR")
            throw ProcessingError("Unexpected command: " + command);
        size_t size;
        is_ >> size;
        NextLine();
        expr.resize(size);
        is_.read(&expr.front(), size);
    }
    auto_ptr<EvalResult> eval_result_ptr(program_.Eval(expr, data_ptr));
    KU_ASSERT(eval_result_ptr.get());
    vector<asio::const_buffer> buffers;
    string beginning = eval_result_ptr->GetStatus() + '\n';
    buffers.push_back(asio::buffer(beginning));
    buffers.push_back(asio::buffer(eval_result_ptr->GetData(),
                                   eval_result_ptr->GetSize()));
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
        string eval_exprs_;
        string log_dir_, socket_dir_, code_dir_, include_dir_;
        string db_user_, db_password_, db_prefix_;
        string app_name_, user_name_, tag_name_;
        bool test_mode_;

        void Parse(int argc, char** argv);
        
        static void RequireOption(const string& name,
                                  const string& value,
                                  const string& addition=string());

        void Check() const;
        void MakePathesAbsolute();
        bool IsRelease() const;
        string GetPathSuffix() const;
        auto_ptr<DB> InitDB() const;
        auto_ptr<Program> InitProgram(DB& db) const;
        void RunTest(Program& program, istream& is) const;
        
        static bool HandleRequest(Program& program,
                                  stream_protocol::socket& socket);

        void RunServer(Program& program) const;
    };
}    


MainRunner::MainRunner(int argc, char** argv)
{
    Parse(argc, argv);
    Check();
}


void MainRunner::Run()
{
    if (!test_mode_) {
        OpenLogFile(log_dir_ + GetPathSuffix());
        MakePathesAbsolute();
        Daemonize();
    }
    auto_ptr<DB> db_ptr(InitDB());
    auto_ptr<Program> program_ptr(InitProgram(*db_ptr));
    if (test_mode_) {
        if (eval_exprs_.empty()) {
            RunTest(*program_ptr, cin);
        } else {
            istringstream iss(eval_exprs_);
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
        ("eval,e",
         po::value<string>(&eval_exprs_),
         "expression for evaluation (test mode only, \\n delimited)")
        ;
    
    po::options_description config_options("Config options");
    config_options.add_options()
        ("log-dir,l", po::value<string>(&log_dir_), "log directory")
        ("socket-dir,s", po::value<string>(&socket_dir_), "socket directory")
        ("code-dir,c", po::value<string>(&code_dir_), "code directory")
        ("include-dir,i", po::value<string>(&include_dir_), "include directory")
        ("db-user,u", po::value<string>(&db_user_), "database user")
        ("db-password,p", po::value<string>(&db_password_), "database password")
        ("db-prefix",
         po::value<string>(&db_prefix_)->default_value("ak_"),
         "prefix for database names")
        ;

    po::options_description hidden_options;
    hidden_options.add_options()
        ("app-name", po::value<string>(&app_name_))
        ("user-name", po::value<string>(&user_name_))
        ("tag-name", po::value<string>(&tag_name_))
        ;

    po::positional_options_description positional_options;
    positional_options
        .add("app-name", 1)
        .add("user-name", 1)
        .add("tag-name", 1)
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
            " [options] app_name [user_name tag_name]");
        visible_options.add(generic_options).add(config_options);
        cout << visible_options << "\n";
        exit(0);
    }
}


void MainRunner::RequireOption(const string& name,
                                const string& value,
                                const string& addition)
{
    if (value.empty()) {
        cerr << "option " << name << " is required" << addition << '\n';
        exit(1);
    }
}


void MainRunner::Check() const
{
    RequireOption("code-dir", code_dir_);
    RequireOption("include-dir", include_dir_);
    RequireOption("db-user", db_user_);
    RequireOption("db-password", db_password_);
    if (!test_mode_) {
        RequireOption("log-dir", log_dir_, " for server mode");
        RequireOption("socket-dir", socket_dir_, " for server mode");
        if (!eval_exprs_.empty()) {
            cerr << "eval expressions option is specific to test mode\n";
            exit(1);
        }
    }
    if (app_name_.empty()) {
        cerr << "app_name must be specified\n";
        exit(1);
    }
    if (!user_name_.empty() && tag_name_.empty()) {
        cerr << "user_name and tag_name must be specified together\n";
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
    MakePathAbsolute(curr_dir, code_dir_);
    MakePathAbsolute(curr_dir, include_dir_);
    free(curr_dir);
}


bool MainRunner::IsRelease() const
{
    return user_name_.empty();
}


string MainRunner::GetPathSuffix() const
{
    return (IsRelease()
            ? "/release/" + app_name_
            : "/tags/" + app_name_ + '/' + user_name_ + '/' + tag_name_);
}

auto_ptr<DB> MainRunner::InitDB() const
{
    string options("user=" + db_user_ +
                   " password=" + db_password_ +
                   " dbname=" + db_prefix_ + app_name_);
    string schema_name(IsRelease() ? "public" : user_name_ + ':' + tag_name_);
    return auto_ptr<DB>(new DB(options, schema_name));
}


auto_ptr<Program> MainRunner::InitProgram(DB& db) const
{
    string js_file_path(code_dir_ + GetPathSuffix() + "/main.js");
    return auto_ptr<Program>(new Program(js_file_path, db));
}


void MainRunner::RunTest(Program& program, istream& is) const
{
    while (!is.eof()) {
        string expr_str;
        getline(is, expr_str);
        Chars expr(expr_str.begin(), expr_str.end());
        if (expr.empty())
            continue;
        auto_ptr<EvalResult> result_ptr(program.Eval(expr));
        cout << result_ptr->GetStatus() << '\n';
        cout.write(result_ptr->GetData(), result_ptr->GetSize());
        cout << '\n';
    }
}


void MainRunner::RunServer(Program& program) const
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
        freopen("/dev/null", "w", stderr);
    
        for (bool go_on = true; go_on; ) {
            stream_protocol::socket socket(io_service);
            acceptor.accept(socket);
            RequestHandler request_handler(program, socket);
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
