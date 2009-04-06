
// (c) 2009 by Anton Korenyushkin

/// \file main.cc
/// Program entry point

#include "js.h"
#include "db.h"

#include <asio.hpp>
#include <boost/program_options.hpp>

#include <getopt.h>
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
        string db_user_, db_password_;
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
        
        static bool ReadRequest(stream_protocol::socket& socket,
                                string& request);

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
    ostringstream oss;
    oss << "user=" << db_user_
        << " password=" << db_password_
        << " dbname=" << app_name_;
    if (!IsRelease())
        oss << '@' << user_name_
            << '@' << tag_name_;
    return auto_ptr<DB>(new DB(oss.str(), "main"));
}


auto_ptr<Program> MainRunner::InitProgram(DB& db) const
{
    string js_file_path(code_dir_ + GetPathSuffix() + "/main.js");
    return auto_ptr<Program>(new Program(js_file_path, db, cerr)); // TODO
}


void MainRunner::RunTest(Program& program, istream& is) const
{
    while (!is.eof()) {
        string expr;
        getline(is, expr);
        if (expr.empty())
            continue;
        auto_ptr<EvalResult> result_ptr(program.Eval(expr));
        cout << result_ptr->GetStatus() << '\n';
        cout.write(result_ptr->GetData(), result_ptr->GetSize());
        cout << '\n';
    }
}


bool MainRunner::ReadRequest(stream_protocol::socket& socket, string& request)
{
    request.clear();
    do {
        char data[1024];
        asio::error_code error;
        size_t length = socket.read_some(asio::buffer(data), error);
        if (error) {
            Log("Error while reading request: " + error.message());
            return false;
        }
        request.append(data, length);
    } while (request[request.size() - 1] != '\n');
    request.resize(request.size() - 1);
    return true;
}

    
bool MainRunner::HandleRequest(Program& program,
                               stream_protocol::socket& socket)
{
    std::string request;
    if (!ReadRequest(socket, request))
        return true;
    asio::error_code error;
    if (request.substr(0, 5) == "EVAL ") {
        auto_ptr<EvalResult>
            eval_result_ptr(program.Eval(request.substr(5)));
        KU_ASSERT(eval_result_ptr.get());
        vector<asio::const_buffer> buffers;
        string beginning = eval_result_ptr->GetStatus() + ' ';
        buffers.push_back(asio::buffer(beginning));
        buffers.push_back(asio::buffer(eval_result_ptr->GetData(),
                                       eval_result_ptr->GetSize()));
        asio::write(socket, buffers, asio::transfer_all(), error);
    } else {
        string reply;
        if (request == "STATUS" || request == "STOP") {
            reply = "OK";
        } else {
            reply = "FAIL";
            Log("Bad request: " + request);
        }
        asio::write(socket,
                    asio::buffer(reply),
                    asio::transfer_all(),
                    error);
    }
    if (error)
        Log("Write error: " + error.message());
    return request != "STOP";
}


void MainRunner::RunServer(Program& program) const
{
    string socket_path(socket_dir_ + GetPathSuffix());
    remove(socket_path.c_str());
    asio::io_service io_service;
    stream_protocol::acceptor acceptor(io_service);
    stream_protocol::endpoint endpoint(socket_path);
    asio::error_code error;
    acceptor.open(endpoint.protocol(), error);
    if (error)
        Fail("open() error: " + error.message());
    acceptor.bind(endpoint, error);
    if (error)
        Fail("bind() error: " + error.message());
    acceptor.listen(asio::socket_base::max_connections, error);
    if (error)
        Fail("listen() error: " + error.message());

    cout << "READY\n";
    cout.flush();
    freopen( "/dev/null", "r", stdin);
    freopen( "/dev/null", "w", stdout);
    freopen( "/dev/null", "w", stderr);
    
    for (bool go_on = true; go_on; ) {
        stream_protocol::socket socket(io_service);
        acceptor.accept(socket, error);
        if (error)
            Fail("accept() error: " + error.message());
        go_on = HandleRequest(program, socket);
        socket.close();
    }

    acceptor.close();
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
