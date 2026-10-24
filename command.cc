/*
 * 
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"

/* ------------------------------------------------------------------ */
/*  Helpers for parsing                                                */
/* ------------------------------------------------------------------ */

static void showPrompt()
{
    printf("myshell>");
    fflush(stdout);
}

static bool isWord(const Token &t)
{
    return t.type == TOKEN_COMMAND || t.type == TOKEN_ARGUMENT;
}

/*
 * The tokenizer splits on whitespace only, so  "Hello, World!"  arrives as two
 * tokens.  Here we re-join tokens until the closing quote is found, and strip
 * the quotes.  quoted tells the caller the word was quoted (no wildcards).
 * Returns false if the quote is never closed.
 */
static bool readWord(std::vector<Token> &tokens, size_t &i, std::string &out, bool &quoted)
{
    std::string word = tokens[i].value;
    quoted = false;

    if (!word.empty() && (word[0] == '"' || word[0] == '\''))
    {
        char q = word[0];
        quoted = true;
        while (!(word.size() >= 2 && word.back() == q))
        {
            if (i + 1 >= tokens.size() || tokens[i + 1].type == TOKEN_EOF)
                return false; // unterminated quote
            i++;
            word += " " + tokens[i].value;
        }
        word = word.substr(1, word.size() - 2);
    }
    out = word;
    return true;
}

static void addArgument(SimpleCommand *sc, const std::string &word, bool quoted)
{
    // Wildcard expansion: * and ? (bonus)
    if (!quoted && word.find_first_of("*?") != std::string::npos)
    {
        glob_t g;
        if (glob(word.c_str(), GLOB_NOCHECK, NULL, &g) == 0)
        {
            for (size_t k = 0; k < g.gl_pathc; k++)
                sc->insertArgument(strdup(g.gl_pathv[k]));
            globfree(&g);
            return;
        }
    }
    sc->insertArgument(strdup(word.c_str()));
}

static void syntaxError(const char *msg)
{
    fprintf(stderr, "myshell: syntax error: %s\n", msg);
    Command::_currentCommand.clear();
    showPrompt();
}

/* ------------------------------------------------------------------ */
/*  parse()                                                            */
/* ------------------------------------------------------------------ */

void parse(std::vector<Token> &tokens)
{
    Command &cmd = Command::_currentCommand;
    cmd.clear();
    cmd._out_error = 0; // clear() does not reset these two
    cmd._append = 0;

    SimpleCommand *sc = new SimpleCommand();
    cmd.insertSimpleCommand(sc);
    bool sawOperator = false;

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token &t = tokens[i];

        if (t.type == TOKEN_EOF)
            break;

        if (isWord(t))
        {
            std::string w;
            bool quoted;
            if (!readWord(tokens, i, w, quoted))
                return syntaxError("unterminated quote");
            addArgument(sc, w, quoted);
        }
        else if (t.type == TOKEN_PIPE)
        {
            if (sc->_numberOfArguments == 0)
                return syntaxError("missing command before '|'");
            sc = new SimpleCommand();
            cmd.insertSimpleCommand(sc);
            sawOperator = true;
        }
        else if (t.type == TOKEN_BACKGROUND)
        {
            if (tokens[i + 1].type != TOKEN_EOF)
                return syntaxError("'&' must be the last token");
            cmd._background = 1;
            sawOperator = true;
        }
        else
        {
            // Redirection operators: > >> < 2> >>&
            if (i + 1 >= tokens.size() || !isWord(tokens[i + 1]))
                return syntaxError("missing file name after redirection operator");

            TokenType op = t.type;
            i++;
            std::string file;
            bool quoted;
            if (!readWord(tokens, i, file, quoted))
                return syntaxError("unterminated quote");

            if (op == TOKEN_INPUT)
            {
                if (cmd._inputFile)
                    return syntaxError("ambiguous input redirection");
                cmd._inputFile = strdup(file.c_str());
            }
            else if (op == TOKEN_REDIRECT || op == TOKEN_APPEND)
            {
                if (cmd._outFile)
                    return syntaxError("ambiguous output redirection");
                cmd._outFile = strdup(file.c_str());
                cmd._append = (op == TOKEN_APPEND);
            }
            else if (op == TOKEN_ERROR)
            {
                if (cmd._errFile)
                    return syntaxError("ambiguous error redirection");
                cmd._errFile = strdup(file.c_str());
            }
            else if (op == TOKEN_REDIRECT_AND_ERROR)
            {
                if (cmd._outFile || cmd._errFile)
                    return syntaxError("ambiguous output redirection");
                cmd._outFile = strdup(file.c_str());
                cmd._errFile = strdup(file.c_str());
                cmd._out_error = 1;
                cmd._append = 1;
            }
            sawOperator = true;
        }
    }

    // The last simple command must not be empty
    if (sc->_numberOfArguments == 0)
    {
        if (cmd._numberOfSimpleCommands > 1)
            return syntaxError("missing command after '|'");
        if (sawOperator)
            return syntaxError("missing command");
        cmd.clear(); // empty line
        showPrompt();
        return;
    }

    cmd.execute();
}

/* ------------------------------------------------------------------ */
/*  SimpleCommand / Command                                            */
/* ------------------------------------------------------------------ */

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        free(_simpleCommands[i]);
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}

/* ------------------------------------------------------------------ */
/*  execute()                                                          */
/* ------------------------------------------------------------------ */

static void closeIfOpen(int fd)
{
    if (fd >= 0)
        close(fd);
}

void Command::execute()
{
    print();

    if (_numberOfSimpleCommands == 0 || _simpleCommands[0]->_numberOfArguments == 0)
    {
        clear();
        showPrompt();
        return;
    }

    /* ---- built-ins (run inside the shell, no fork) ---- */
    if (_numberOfSimpleCommands == 1)
    {
        char **a = _simpleCommands[0]->_arguments;

        if (strcmp(a[0], "exit") == 0)
        {
            printf("Good bye!!\n");
            clear();
            exit(0);
        }
        if (strcmp(a[0], "cd") == 0)
        {
            const char *dir = a[1] ? a[1] : getenv("HOME");
            if (!dir)
                fprintf(stderr, "cd: HOME not set\n");
            else if (chdir(dir) != 0)
                fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
            clear();
            showPrompt();
            return;
        }
    }

    /* ---- open redirection files in the parent so errors are reported once ---- */
    int infd = -1, outfd = -1, errfd = -1;

    if (_inputFile)
    {
        infd = open(_inputFile, O_RDONLY);
        if (infd < 0)
        {
            fprintf(stderr, "%s: %s\n", _inputFile, strerror(errno));
            clear();
            showPrompt();
            return;
        }
    }
    if (_outFile)
    {
        int flags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
        outfd = open(_outFile, flags, 0644);
        if (outfd < 0)
        {
            fprintf(stderr, "%s: %s\n", _outFile, strerror(errno));
            closeIfOpen(infd);
            clear();
            showPrompt();
            return;
        }
    }
    if (_errFile && !_out_error)
    {
        errfd = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errfd < 0)
        {
            fprintf(stderr, "%s: %s\n", _errFile, strerror(errno));
            closeIfOpen(infd);
            closeIfOpen(outfd);
            clear();
            showPrompt();
            return;
        }
    }

    /* ---- create the pipeline ---- */
    fflush(stdout);
    int n = _numberOfSimpleCommands;
    int prevRead = -1;
    pid_t lastPid = -1;

    for (int i = 0; i < n; i++)
    {
        int fds[2] = {-1, -1};
        if (i < n - 1 && pipe(fds) < 0)
        {
            perror("pipe");
            break;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            closeIfOpen(fds[0]);
            closeIfOpen(fds[1]);
            break;
        }

        if (pid == 0)
        {
            // child
            signal(SIGINT, SIG_DFL); // shell ignores Ctrl-C, children must not

            if (i == 0)
            {
                if (infd >= 0)
                    dup2(infd, 0);
            }
            else
                dup2(prevRead, 0);

            if (i < n - 1)
                dup2(fds[1], 1);
            else if (outfd >= 0)
            {
                dup2(outfd, 1);
                if (_out_error)
                    dup2(outfd, 2);
            }
            if (errfd >= 0)
                dup2(errfd, 2);

            closeIfOpen(prevRead);
            closeIfOpen(fds[0]);
            closeIfOpen(fds[1]);
            closeIfOpen(infd);
            closeIfOpen(outfd);
            closeIfOpen(errfd);

            char **args = _simpleCommands[i]->_arguments;
            execvp(args[0], args);

            if (errno == ENOENT)
                fprintf(stderr, "%s: command not found\n", args[0]);
            else
                fprintf(stderr, "%s: %s\n", args[0], strerror(errno));
            _exit(127);
        }

        // parent
        closeIfOpen(prevRead);
        closeIfOpen(fds[1]);
        prevRead = fds[0];
        lastPid = pid;
    }

    closeIfOpen(prevRead);
    closeIfOpen(infd);
    closeIfOpen(outfd);
    closeIfOpen(errfd);

    if (!_background && lastPid > 0)
    {
        int status;
        // The SIGCHLD handler may reap the child first -> ECHILD is fine.
        while (waitpid(lastPid, &status, 0) < 0 && errno == EINTR)
            ;
    }

    clear();
    prompt();
}

void Command::prompt()
{
    printf("myshell>");
    fflush(stdout);
    std::string input;
    while (true)
    {
        std::getline(std::cin, input); 
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

/* ------------------------------------------------------------------ */
/*  Signals + log file                                                 */
/* ------------------------------------------------------------------ */

static int logFd = -1;

// SIGCHLD: reap every finished child and write a line to shell.log
static void sigchldHandler(int)
{
    int savedErrno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        char ts[32], buf[160];
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

        int len;
        if (WIFEXITED(status))
            len = snprintf(buf, sizeof(buf), "[%s] Child %d terminated (exit status %d)\n",
                           ts, (int)pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            len = snprintf(buf, sizeof(buf), "[%s] Child %d killed by signal %d\n",
                           ts, (int)pid, WTERMSIG(status));
        else
            len = snprintf(buf, sizeof(buf), "[%s] Child %d terminated\n", ts, (int)pid);

        if (logFd >= 0 && len > 0)
            write(logFd, buf, len);
    }
    errno = savedErrno;
}

// Runs before main(): ignore Ctrl-C and install the SIGCHLD log handler,
// so main() stays exactly as provided.
static struct ShellInit
{
    ShellInit()
    {
        signal(SIGINT, SIG_IGN);

        logFd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sigchldHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        sigaction(SIGCHLD, &sa, NULL);
    }
} shellInit;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}
