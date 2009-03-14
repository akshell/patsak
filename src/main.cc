
// (c) 2009 by Anton Korenyushkin

/// \file main.cc
/// Program entry point

#include "js.h"
#include "db.h"
#include "server.h"

#include <asio.hpp>

#include <getopt.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>

using namespace std;
using namespace ku;
using asio::local::stream_protocol;


////////////////////////////////////////////////////////////////////////////////
// server stuff
////////////////////////////////////////////////////////////////////////////////

namespace
{
    bool ReadRequest(stream_protocol::socket& socket, string& request)
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

    
    bool HandleRequest(Program& program, stream_protocol::socket& socket)
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
    

    void RunServer(const string& db_options,
                   const string& js_file_name,
                   const string& socket_dir,
                   const string& name)
    {
        DB db(db_options, "main");
        Program program(js_file_name, db, cerr);

        string socket_name(socket_dir + '/' + name);
        remove(socket_name.c_str());
        asio::io_service io_service;
        stream_protocol::acceptor acceptor(io_service);
        stream_protocol::endpoint endpoint(socket_name);
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
        
        for (bool go_on = true; go_on; ) {
            stream_protocol::socket socket(io_service);
            acceptor.accept(socket, error);
            if (error)
                Fail("accept() error: " + error.message());
            go_on = HandleRequest(program, socket);
            socket.close();
        }

        acceptor.close();
        remove(socket_name.c_str());
    }
}

////////////////////////////////////////////////////////////////////////////////
// RunTest
////////////////////////////////////////////////////////////////////////////////

namespace
{
    void RunTest(const string& db_options,
                 const string& js_file_name,
                 istream& is,
                 bool interactive)
    {
        DB db(db_options, "main");
        Program program(js_file_name, db, cerr);
        while (!is.eof()) {
            if (interactive)
                cout << "ku> ";
            string expr;
            getline(is, expr);
            if (expr.empty())
                continue;
            auto_ptr<EvalResult> result_ptr(program.Eval(expr));
            cout << result_ptr->GetStatus() << '\n';
            cout.write(result_ptr->GetData(), result_ptr->GetSize());
            cout << '\n';
        }
        if (interactive)
            cout << '\n';
    }
}

////////////////////////////////////////////////////////////////////////////////
// main() stuff
////////////////////////////////////////////////////////////////////////////////

namespace
{
    void PrintUsage(const string& prog_name)
    {
        cerr << "Usage: " << prog_name << " options... file\n\n"
            " -h, --help               display this help\n"
            " -t, --test               run in test mode\n"
            " -s, --server             run in server mode\n"
            " -o, --db-options=OPTIONS database options\n"
            " -f, --test-file=FILE     read test expressions from a file\n"
            " -e, --eval=EXPR          evaluate the specified expression\n"
            " -l, --log-file=FILE      use FILE for logging instead of cerr\n"
            " -d, --socket-dir=DIR     apps socket directory\n"
            " -n, --name=NAME          name of this app\n";
    }
}


int main(int argc, char** argv)
{
    static struct option long_options[] = {
        {"help", 0, 0, 'h'},
        {"test", 0, 0, 't'},
        {"server", 0, 0, 's'},
        {"db-options", 1, 0, 'o'},
        {"test-file", 1, 0, 'f'},
        {"eval", 1, 0, 'e'},
        {"log-file", 1, 0, 'l'},
        {"socket-dir", 1, 0, 'd'},
        {"name", 1, 0, 'n'},
        {0, 0, 0, 0}
    };

    enum {UNSPECIFIED_MODE, TEST_MODE, SERVER_MODE} mode = UNSPECIFIED_MODE;
    string db_options, test_file_name, eval_expr, socket_dir, name;
    
    for (;;) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "htso:f:e:l:d:n:",
                            long_options, &option_index);
        if (c == -1)
            break;
        switch (c) {
        case 'h':
            PrintUsage(argv[0]);
            return 0;
        case 't':
            mode = TEST_MODE;
            break;
        case 's':
            mode = SERVER_MODE;
            break;
        case 'o':
            db_options = optarg;
            break;
        case 'f':
            test_file_name = optarg;
            break;
        case 'e':
            eval_expr = optarg;
            break;
        case 'l':
            OpenLogFile(optarg);
            break;
        case 'd':
            socket_dir = optarg;
            break;
        case 'n':
            name = optarg;
            break;
        default:
            cerr << "Unknown argument " << static_cast<char>(c) << '\n';
            return 1;
        }
    }
    
    if (mode == UNSPECIFIED_MODE || optind + 1 != argc) {
        cerr << "Mode and one js file must be specified\n";
        return 1;
    }

    string js_file_name(argv[optind]);
    if (mode == TEST_MODE) {
        if (!name.empty()) {
            cerr << "App name is specific to server mode\n";
            return 1;
        }
        if (!test_file_name.empty() && !eval_expr.empty()) {
            cerr << "Test file and eval expr could not be specified together\n";
            return 1;
        }
        if (!test_file_name.empty()) {
            ifstream ifs(test_file_name.c_str(), ios::in);
            if (!ifs.is_open()) {
                cerr << "Can't open test file\n";
                return 1;
            }
            RunTest(db_options, js_file_name, ifs, false);
        } else if (!eval_expr.empty()) {
            istringstream iss(eval_expr);
            RunTest(db_options, js_file_name, iss, false);
        } else {
            RunTest(db_options, js_file_name, cin, true);
        }
    } else {
        KU_ASSERT(mode == SERVER_MODE);
        if (!test_file_name.empty() || !eval_expr.empty()) {
            cerr << "Test file and eval expr are specific to test mode\n";
            return 1;
        }
        if (socket_dir.empty() || name.empty()) {
            cerr <<
                "Socket dir and app name must be specified in server mode\n";
            return 1;
        }
        RunServer(db_options, js_file_name, socket_dir, name);
    }
    return 0;
}
