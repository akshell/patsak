
// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"
#include "db.h"

#include <boost/program_options.hpp>
#include <boost/foreach.hpp>

#include <fstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>


using namespace std;
using namespace ak;
namespace po = boost::program_options;


namespace
{
    void MakePathAbsolute(const string& base_path, string& path)
    {
        if (!path.empty() && path[0] != '/')
            path = base_path + '/' + path;
    }


    void RequireOption(const string& name, const string& value)
    {
        if (value.empty()) {
            cerr << "Option " << name << " is required\n";
            exit(1);
        }
    }


    ssize_t Receive(int fd, char* buf, size_t size)
    {
        size_t total = 0;
        while (total < size) {
            ssize_t received = recv(fd, buf + total, size - total, 0);
            if (received == -1)
                return -1;
            if (!received)
                break;
            total += received;
        }
        return total;
    }
}


int main(int argc, char** argv)
{
    po::options_description generic_options("Generic options");
    generic_options.add_options()
        ("help,h", "print help message")
        ("rev,r", "print revision")
        ("config-file,f",
         po::value<string>()->default_value("/ak/patsak.conf"),
         "config file path")
        ;

    string log_path, code_path, media_path;
    string git_path_pattern;
    string db_name, user_name, password, schema_name, tablespace_name;
    po::options_description config_options("Config options");
    config_options.add_options()
        ("log-file,l", po::value<string>(&log_path), "log file")
        ("code-dir,c", po::value<string>(&code_path), "code directory")
        ("media-dir,m", po::value<string>(&media_path), "media directory")
        ("git-pattern,g",
         po::value<string>(&git_path_pattern),
         "git path pattern")
        ("db-name,n", po::value<string>(&db_name), "database name")
        ("db-user,u", po::value<string>(&user_name), "database user")
        ("db-password,p", po::value<string>(&password), "database password")
        ("db-schema,s",
         po::value<string>(&schema_name)->default_value("public"),
         "database schema")
        ("db-tablespace,t",
         po::value<string>(&tablespace_name)->default_value("pg_default"),
         "database tablespace")
        ;

    string command;
    string socket_path_or_expr;
    string app_name, owner_name, spot_name;
    po::options_description hidden_options;
    hidden_options.add_options()
        ("command", po::value<string>(&command))
        ("socket-path-or-expr", po::value<string>(&socket_path_or_expr))
        ("app", po::value<string>(&app_name))
        ("owner", po::value<string>(&owner_name))
        ("spot", po::value<string>(&spot_name))
        ;

    po::positional_options_description positional_options;
    positional_options
        .add("command", 1)
        .add("socket-path-or-expr", 1)
        .add("app", 1)
        .add("owner", 1)
        .add("spot", 1)
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
        return 1;
    }

    if (vm.count("rev")) {
        // REVISION must be provided as a compiler option
        cout << REVISION << '\n';
        return 0;
    }

    if (vm.count("help") || command.empty()) {
        po::options_description visible_options(
            string("Usage: ") + argv[0] +
            " [options] serve PATH APP [OWNER SPOT]\n"
            "       " + argv[0] +
            " [options] eval  EXPR APP [OWNER SPOT]");
        visible_options.add(generic_options).add(config_options);
        cout << visible_options << '\n';
        return !vm.count("help");
    }

    bool eval = command == "eval";
    Chars expr;
    string socket_path;
    if (eval) {
        expr.assign(socket_path_or_expr.begin(), socket_path_or_expr.end());
    } else if (command == "serve") {
        socket_path = socket_path_or_expr;
        if (socket_path.empty()) {
            cerr << "Socket path must be specified\n";
            return 1;
        }
    } else {
        cerr << "Unknown command: " << command << '\n';
        return 1;
    }

    if (app_name.empty()) {
        cerr << "App name must be specified\n";
        return 1;
    }
    if (!owner_name.empty() && spot_name.empty()) {
        cerr << "Owner name and spot name must be specified together\n";
        return 1;
    }

    RequireOption("log-file", log_path);
    RequireOption("code-dir", code_path);
    RequireOption("media-dir", media_path);
    RequireOption("db-name", db_name);
    RequireOption("db-user", user_name);
    RequireOption("db-password", password);

    if (!eval) {
        char* curr_dir = get_current_dir_name();
        AK_ASSERT(curr_dir);
        MakePathAbsolute(curr_dir, log_path);
        MakePathAbsolute(curr_dir, socket_path);
        MakePathAbsolute(curr_dir, code_path);
        MakePathAbsolute(curr_dir, media_path);
        MakePathAbsolute(curr_dir, git_path_pattern);
        free(curr_dir);
        pid_t pid = fork();
        AK_ASSERT(pid != -1);
        if (pid > 0)
            return 0;
        umask(0);
        pid_t sid = setsid();
        AK_ASSERT(sid != -1);
        int ret = chdir("/");
        AK_ASSERT(ret == 0);
    }

    DB db("user=" + user_name + " password=" + password + " dbname=" + db_name,
          schema_name,
          tablespace_name);

    string path_suffix =
        spot_name.empty()
        ? "/release/" + app_name
        : "/spots/" + app_name + '/' + owner_name + '/' + spot_name;
    string spaced_owner_name(owner_name);
    BOOST_FOREACH(char& c, spaced_owner_name)
        if (c == '-')
            c = ' ';
    string git_path_prefix, git_path_suffix;
    if (!git_path_pattern.empty()) {
        size_t pos = git_path_pattern.find("%s");
        if (pos == string::npos) {
            cerr << "Git path pattern must contain a %s placeholder\n";
            return 1;
        }
        git_path_prefix = git_path_pattern.substr(0, pos);
        git_path_suffix = git_path_pattern.substr(pos + 2);
    }

    Program program(Place(app_name, spaced_owner_name, spot_name),
                    code_path + path_suffix,
                    media_path + path_suffix,
                    git_path_prefix,
                    git_path_suffix,
                    db);

    if (eval) {
        program.Eval(expr, STDOUT_FILENO);
        cout << '\n';
        return 0;
    }

    freopen("/dev/null", "r", stdin);
    freopen(log_path.c_str(), "a", stderr);

    int listen_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    AK_ASSERT(listen_fd != -1);
    unlink(socket_path.c_str());
    struct sockaddr_un address;
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path,
            socket_path.c_str(),
            sizeof(address.sun_path) - 1);
    struct sockaddr* address_ptr = reinterpret_cast<struct sockaddr*>(&address);
    socklen_t address_size = SUN_LEN(&address);
    if (bind(listen_fd, address_ptr, address_size) != 0)
        Fail(strerror(errno));
    int ret = listen(listen_fd, SOMAXCONN);
    AK_ASSERT_EQUAL(ret, 0);

    cout << "READY" << endl;
    freopen("/dev/null", "w", stdout);

    do {
        int conn_fd = accept(listen_fd, address_ptr, &address_size);
        AK_ASSERT(conn_fd != -1);
        static const ssize_t OP_SIZE = 5;
        char op_buf[OP_SIZE];
        if (Receive(conn_fd, op_buf, OP_SIZE) != OP_SIZE) {
            close(conn_fd);
            continue;
        }
        string op(op_buf, op_buf + OP_SIZE);
        if (op == "STOP\n") {
            close(conn_fd);
            break;
        }
        if (op == "HNDL\n") {
            program.Process(conn_fd); // closes the socket
            continue;
        }
        if (op == "EVAL\n") {
            static const ssize_t CHUNK_SIZE = 4096;
            Chars data;
            size_t size = 0;
            ssize_t received;
            do {
                data.resize(size + CHUNK_SIZE);
                received = Receive(conn_fd, &data[size], CHUNK_SIZE);
                size += received;
            } while (received == CHUNK_SIZE);
            if (received != -1) {
                data.resize(size);
                program.Eval(data, conn_fd);
            }
        }
        close(conn_fd);
    } while (!program.IsDead());

    close(listen_fd);

    return 0;
}
