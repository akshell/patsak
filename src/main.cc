// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"

#include <boost/program_options.hpp>

#include <fstream>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>


using namespace std;
using namespace ak;
namespace po = boost::program_options;


namespace
{
    const size_t MAX_EXPR_SIZE = 4096;
    const char* DEFAULT_HOST = "127.0.0.1";
    const char* DEFAULT_PORT = "8000";


    void MakePathAbsolute(const string& curr_path, string& path)
    {
        if (path.empty() || path[0] != '/')
            path = curr_path + '/' + path;
    }


    void RequireOption(const string& name, const string& value)
    {
        if (value.empty()) {
            cerr << "Option " << name << " is required\n";
            exit(1);
        }
    }


    void HandleStop(int /*signal*/)
    {
        exit(0);
    }


    pid_t LaunchWorker(const vector<int>& worker_fds, int listen_fd, int& fd)
    {
        int fd_pair[2];
        int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair);
        AK_ASSERT_EQUAL(ret, 0);
        pid_t pid = fork();
        AK_ASSERT(pid != -1);
        if (pid) {
            fd = fd_pair[0];
            close(fd_pair[1]);
        } else {
            fd = fd_pair[1];
            close(fd_pair[0]);
            close(listen_fd);
            BOOST_FOREACH(int worker_fd, worker_fds)
                close(worker_fd);
        }
        return pid;
    }


    int Serve(int listen_fd, size_t worker_count)
    {
        vector<int> worker_fds;
        worker_fds.reserve(worker_count);
        for (size_t i = 0; i < worker_count; ++i) {
            int fd;
            if (!LaunchWorker(worker_fds, listen_fd, fd))
                return fd;
            worker_fds.push_back(fd);
        }
        for (size_t i = 0;; i = (i + 1) % worker_count) {
            int conn_fd = accept(listen_fd, 0, 0);
            AK_ASSERT(conn_fd != -1);
            struct msghdr msg;
            msg.msg_name = 0;
            msg.msg_namelen = 0;
            char op = 'H';
            struct iovec iov;
            iov.iov_base = &op;
            iov.iov_len = 1;
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            char control[CMSG_SPACE(sizeof(int))];
            msg.msg_control = control;
            msg.msg_controllen = sizeof(control);
            struct cmsghdr* cmsg_ptr = CMSG_FIRSTHDR(&msg);
            cmsg_ptr->cmsg_level = SOL_SOCKET;
            cmsg_ptr->cmsg_type = SCM_RIGHTS;
            cmsg_ptr->cmsg_len = CMSG_LEN(sizeof(int));
            *reinterpret_cast<int*>(CMSG_DATA(cmsg_ptr)) = conn_fd;
            ssize_t sent = sendmsg(worker_fds[i], &msg, 0);
            if (sent != 1) {
                int fd;
                if (!LaunchWorker(worker_fds, listen_fd, fd)) {
                    close(conn_fd);
                    return fd;
                }
                close(worker_fds[i]);
                worker_fds[i] = fd;
                sent = sendmsg(fd, &msg, 0);
                AK_ASSERT_EQUAL(sent, 1);
            }
            close(conn_fd);
        }
    }
}


int main(int argc, char** argv)
{
    po::options_description generic_options("Generic options");
    generic_options.add_options()
        ("help,h", "print help message")
        ("version,v", "print version")
        ("config,c", po::value<string>(), "config file")
        ;

    string code_path, lib_path;
    Strings git_options;
    string repo_name;
    string db_options, schema_name, tablespace_name;
    string log_path;
    size_t worker_count;
    po::options_description config_options("Config options");
    config_options.add_options()
        ("app,a", po::value<string>(&code_path), "app code directory")
        ("lib,l", po::value<string>(&lib_path), "lib directory")
        ("git,g", po::value<Strings>(&git_options), "git path pattern")
        ("repo,r", po::value<string>(&repo_name), "repository to run")
        ("db,d", po::value<string>(&db_options), "database options")
        ("schema,s",
         po::value<string>(&schema_name)->default_value("public"),
         "database schema")
        ("tablespace,t",
         po::value<string>(&tablespace_name)->default_value("pg_default"),
         "database tablespace")
        ("workers,w",
         po::value<size_t>(&worker_count)->default_value(5),
         "serve worker count")
        ("log,o", po::value<string>(&log_path), "log file")
        ("background,b", "serve in background")
        ;

    string command;
    string place_or_expr;
    string log_id;
    po::options_description hidden_options;
    hidden_options.add_options()
        ("command", po::value<string>(&command))
        ("place-or-expr", po::value<string>(&place_or_expr))
        ("log-id", po::value<string>(&log_id))
        ;

    po::positional_options_description positional_options;
    positional_options
        .add("command", 1)
        .add("place-or-expr", 1)
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
        ifstream config_file(vm.count("config")
                             ? vm["config"].as<string>().c_str()
                             : "/etc/patsak.conf");
        if (config_file.is_open()) {
            po::store(po::parse_config_file(config_file, config_options), vm);
            config_file.close();
        } else if (vm.count("config")) {
            cerr << "Failed to open config file\n";
            return 1;
        }
        po::notify(vm);
    } catch (po::error& err) {
        cerr << err.what() << '\n';
        return 1;
    }

    if (vm.count("version")) {
        // REVISION must be provided as a compiler option
        cout << "patsak 0.3 (" << REVISION << ")\n";
        return 0;
    }

    if (vm.count("help") || command.empty()) {
        po::options_description visible_options(
            string("Usage: ") +
            argv[0] + " [options] serve [PORT or ADDR:PORT or PATH[:MODE]]\n"
            "       " +
            argv[0] + " [options] eval  EXPRESSION");
        visible_options.add(generic_options).add(config_options);
        cout << visible_options;
        return !vm.count("help");
    }

    InitDebug(log_id);

    RequireOption("app", code_path);
    RequireOption("lib", lib_path);
    RequireOption("db", db_options);

    if (git_options.empty() && !repo_name.empty()) {
        cerr << "--repo must be specified with --git\n";
        return 1;
    }

    GitPathPatterns git_path_patterns;
    BOOST_FOREACH(const string& git_option, git_options) {
        size_t first_idx = git_option.find("%s");
        size_t second_idx = (first_idx == string::npos
                             ? string::npos
                             : git_option.find("%s", first_idx + 2));
        if (second_idx == string::npos) {
            cerr << "Git path pattern must contain two %s placeholders\n";
            return 1;
        }
        git_path_patterns.push_back(
            GitPathPattern(
                git_option.substr(0, first_idx),
                git_option.substr(first_idx + 2, second_idx - first_idx - 2),
                git_option.substr(second_idx+ 2)));
    }

    pid_t parent_pid = 0;
    int server_fd;

    if (command == "serve") {
        string& place(place_or_expr);
        bool local = place.find_first_of('/') != string::npos;
        if (vm.count("background")) {
            RequireOption("log", log_path);
            if (!freopen(log_path.c_str(), "a", stderr)) {
                cout << "Failed to open log file: " << strerror(errno) << '\n';
                return 1;
            }
            char* curr_path = get_current_dir_name();
            AK_ASSERT(curr_path);
            MakePathAbsolute(curr_path, code_path);
            MakePathAbsolute(curr_path, lib_path);
            BOOST_FOREACH(GitPathPattern& git_path_pattern, git_path_patterns)
                MakePathAbsolute(curr_path, git_path_pattern.prefix);
            if (local)
                MakePathAbsolute(curr_path, place);
            free(curr_path);
            pid_t pid = fork();
            AK_ASSERT(pid != -1);
            if (pid)
                return 0;
            umask(0);
            pid_t sid = setsid();
            AK_ASSERT(sid != -1);
            int ret = chdir("/");
            AK_ASSERT_EQUAL(ret, 0);
        }
        int listen_fd;
        size_t colon_idx = place.find_first_of(':');
        if (local) {
            listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            AK_ASSERT(listen_fd != -1);
            string socket_path = place.substr(0, colon_idx);
            unlink(socket_path.c_str());
            struct sockaddr_un address;
            address.sun_family = AF_UNIX;
            strncpy(address.sun_path,
                    socket_path.c_str(),
                    sizeof(address.sun_path) - 1);
            if (bind(listen_fd,
                     reinterpret_cast<struct sockaddr*>(&address),
                     SUN_LEN(&address)))
                Fail(strerror(errno));
            if (colon_idx != string::npos) {
                int ret = chmod(socket_path.c_str(),
                                strtol(place.c_str() + colon_idx + 1, 0, 8));
                AK_ASSERT_EQUAL(ret, 0);
            }
            cout << "Running at unix:" << socket_path << '\n';
        } else {
            string host, port;
            if (place.empty()) {
                host = DEFAULT_HOST;
                port = DEFAULT_PORT;
            } else if (colon_idx == string::npos) {
                host = DEFAULT_HOST;
                port = place;
            } else {
                host = place.substr(0, colon_idx);
                port = place.substr(colon_idx + 1);
            }
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo* first_info_ptr;
            if (int ret = getaddrinfo(host.c_str(), port.c_str(),
                                      &hints, &first_info_ptr))
                Fail(gai_strerror(ret));
            struct addrinfo* info_ptr = first_info_ptr;
            do {
                listen_fd = socket(info_ptr->ai_family,
                                   info_ptr->ai_socktype,
                                   info_ptr->ai_protocol);
                AK_ASSERT(listen_fd != -1);
                int yes = 1;
                int ret = setsockopt(
                    listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
                AK_ASSERT_EQUAL(ret, 0);
                if (!bind(listen_fd, info_ptr->ai_addr, info_ptr->ai_addrlen))
                    break;
                close(listen_fd);
                info_ptr = info_ptr->ai_next;
            } while (info_ptr);
            if (!info_ptr)
                Fail(strerror(errno));
            freeaddrinfo(first_info_ptr);
            cout << "Running at " << host << ':' << port << '\n';
        }
        int ret = listen(listen_fd, SOMAXCONN);
        AK_ASSERT_EQUAL(ret, 0);
        if (vm.count("background")) {
            FILE* file_ptr = freopen("/dev/null", "w", stdout);
            AK_ASSERT(file_ptr);
            file_ptr = freopen("/dev/null", "r", stdin);
            AK_ASSERT(file_ptr);
        } else {
            cout << "Quit with Control-C.\n";
            cout.flush();
        }
        struct sigaction action;
        action.sa_handler = HandleStop;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        ret = sigaction(SIGTERM, &action, 0);
        AK_ASSERT_EQUAL(ret, 0);
        ret = sigaction(SIGINT, &action, 0);
        AK_ASSERT_EQUAL(ret, 0);
        action.sa_handler = SIG_IGN;
        action.sa_flags = SA_NOCLDWAIT;
        ret = sigaction(SIGCHLD, &action, 0);
        AK_ASSERT_EQUAL(ret, 0);
        server_fd = Serve(listen_fd, worker_count);
    } else if (command == "work") {
        parent_pid = getppid();
        server_fd = STDIN_FILENO;
    } else if (command == "eval") {
        server_fd = -1;
    } else {
        cerr << "Unknown command: " << command << '\n';
        return 1;
    }

    InitJS(code_path,
           lib_path,
           git_path_patterns,
           repo_name,
           db_options,
           schema_name,
           tablespace_name,
           parent_pid);

    if (server_fd == -1) {
        const string& expr(place_or_expr);
        string result;
        EvalExpr(expr.data(), expr.size(), result);
        cout << result << '\n';
        return 0;
    }

    do {
        struct msghdr msg;
        msg.msg_name = 0;
        msg.msg_namelen = 0;
        struct iovec iov;
        char op;
        iov.iov_base = &op;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        char control[CMSG_SPACE(sizeof(int))];
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        ssize_t count = recvmsg(server_fd, &msg, 0);
        if (count != 1)
            break;
        struct cmsghdr* cmsg_ptr = CMSG_FIRSTHDR(&msg);
        int conn_fd = *reinterpret_cast<int*>(CMSG_DATA(cmsg_ptr));
        if (op == 'H') {
            HandleRequest(conn_fd); // closes the socket
        } else {
            AK_ASSERT_EQUAL(op, 'E');
            char expr[MAX_EXPR_SIZE];
            size_t received = 0;
            do {
                count = read(
                    conn_fd, expr + received, MAX_EXPR_SIZE - received);
                received += count;
            } while (count > 0);
            if (count != -1) {
                string result;
                char status = EvalExpr(expr, received, result) ? 'S' : 'F';
                struct iovec iovecs[2];
                iovecs[0].iov_base = &status;
                iovecs[0].iov_len = 1;
                iovecs[1].iov_base = const_cast<char*>(result.data());
                iovecs[1].iov_len = result.size();
                count = writev(conn_fd, iovecs, 2);
                size_t sent = count - 1;
                while (count > 0 && sent < result.size()) {
                    count = write(conn_fd,
                                  result.data() + sent,
                                  result.size() - sent);
                    sent += count;
                }
            }
            close(conn_fd);
        }
        if (parent_pid)
            kill(parent_pid, SIGRTMIN);
    } while (!ProgramIsDead());

    return 0;
}
