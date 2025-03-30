#include <iostream>
#include <string>
#include <unordered_set>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#endif

using namespace std;

#ifndef _WIN32
bool is_executable(const string &path)
{
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0 && (buffer.st_mode & S_IXUSR));
}
#endif

string find_in_path(const string &cmd)
{
  char *path_env = getenv("PATH");
  if (!path_env)
    return "";

  stringstream ss(path_env);
  string dir;

  while (getline(ss, dir, ':'))
  {
    string full_path = dir + "/" + cmd;

#ifndef _WIN32
    if (is_executable(full_path))
    {
#else
    if (GetFileAttributesA(full_path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
#endif
      return full_path;
    }
  }
  return "";
}

void execute_external_command(const vector<string> &args, const string &output_file, const string &error_file)
{
  if (args.empty())
    return;

  string cmd_path = find_in_path(args[0]);
  if (cmd_path.empty())
  {
    cerr << args[0] << ": command not found" << endl;
    return;
  }

#ifdef _WIN32
  // Windows: Use CreateProcess with output redirection
  string command = cmd_path;
  for (size_t i = 1; i < args.size(); ++i)
  {
    command += " " + args[i];
  }

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE; // Allow child to inherit handle

  HANDLE hFile = NULL;
  if (!output_file.empty())
  {
    hFile = CreateFileA(output_file.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
      cerr << "Failed to open file: " << output_file << endl;
      return;
    }
  }

  HANDLE hErrorFile = NULL;
  if (!error_file.empty())
  {
    hErrorFile = CreateFileA(error_file.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hErrorFile == INVALID_HANDLE_VALUE)
    {
      cerr << "Failed to open file: " << error_file << endl;
      if (hFile)
        CloseHandle(hFile);
      return;
    }
  }

  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hFile ? hFile : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = hErrorFile ? hErrorFile : GetStdHandle(STD_ERROR_HANDLE);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  if (CreateProcessA(NULL, const_cast<char *>(command.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
  {
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  else
  {
    cerr << "Failed to execute: " << command << endl;
  }

  if (hFile)
    CloseHandle(hFile);
  if (hErrorFile)
    CloseHandle(hErrorFile);
#else
  // Linux/Mac: Use fork + execvp with output redirection
  vector<char *> c_args;
  for (const string &arg : args)
  {
    c_args.push_back(const_cast<char *>(arg.c_str()));
  }
  c_args.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0) // Child process
  {
    if (!output_file.empty())
    {
      int fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd == -1)
      {
        perror("open output file failed");
        exit(1);
      }
      dup2(fd, STDOUT_FILENO); // Redirect stdout
      close(fd);
    }

    if (!error_file.empty())
    {
      int fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd == -1)
      {
        perror("open error file failed");
        exit(1);
      }
      dup2(fd, STDERR_FILENO); // Redirect stderr
      close(fd);
    }

    execvp(cmd_path.c_str(), c_args.data());
    perror("execvp failed");
    exit(1);
  }
  else if (pid > 0) // Parent process
  {
    int status;
    waitpid(pid, &status, 0);
  }
  else
  {
    perror("fork failed");
  }
#endif
}

vector<string> parse_input(const string &input, string &output_file, string &error_file)
{
  vector<string> args;
  string word;
  bool in_single_quotes = false;
  bool in_double_quotes = false;
  bool escaped = false;

  for (size_t i = 0; i < input.size(); ++i)
  {
    char c = input[i];

    if (escaped)
    {
      if (in_double_quotes && (c != '\\' && c != '$' && c != '"' && c != '\n'))
        word += '\\';
      word += c;
      escaped = false;
    }
    else if (c == '\\' && !in_single_quotes)
    {
      escaped = true;
    }
    else if (c == '"' && !in_single_quotes)
    {
      in_double_quotes = !in_double_quotes;
    }
    else if (c == '\'' && !in_double_quotes)
    {
      in_single_quotes = !in_single_quotes;
    }
    else if ((c == '>' ||
              (c == '1' && i + 1 < input.size() && input[i + 1] == '>') ||
              (c == '2' && i + 1 < input.size() && input[i + 1] == '>')) &&
             !in_single_quotes && !in_double_quotes)
    {
      if (!word.empty())
      {
        args.push_back(word);
        word.clear();
      }

      // Determine which redirection we're handling
      bool is_stderr = (c == '2' && input[i + 1] == '>');

      // Skip the operator (> or 1> or 2>)
      if (c != '>')
        i++;

      // Skip spaces before the filename
      while (i + 1 < input.size() && input[i + 1] == ' ')
        i++;

      size_t j = i + 1;
      while (j < input.size() && input[j] != ' ' && input[j] != '>' &&
             !(input[j] == '2' && j + 1 < input.size() && input[j + 1] == '>') &&
             !(input[j] == '1' && j + 1 < input.size() && input[j + 1] == '>'))
        j++;

      if (i + 1 < input.size())
      {
        string filename = input.substr(i + 1, j - i - 1);
        if (is_stderr)
          error_file = filename;
        else
          output_file = filename;
      }

      i = j - 1;
    }
    else if (c == ' ' && !in_single_quotes && !in_double_quotes)
    {
      if (!word.empty())
      {
        args.push_back(word);
        word.clear();
      }
    }
    else
    {
      word += c;
    }
  }

  if (!word.empty())
  {
    args.push_back(word);
  }

  return args;
}

int main()
{
  cout << unitbuf;
  cerr << unitbuf;

  unordered_set<string> builtins = {"echo", "type", "exit"};

  while (true)
  {
    cout << "$ ";

    string input;
    if (!getline(cin, input))
    {
      break;
    }

    if (input == "exit 0")
    {
      return 0;
    }

    string output_file;
    string error_file;
    vector<string> args = parse_input(input, output_file, error_file);

    if (args.empty())
      continue;

    if (args[0] == "echo")
    {
      ofstream out;
      ofstream err;
      streambuf *coutbuf = cout.rdbuf();
      streambuf *cerrbuf = cerr.rdbuf();

      if (!output_file.empty())
      {
        out.open(output_file);
        cout.rdbuf(out.rdbuf());
      }

      if (!error_file.empty())
      {
        err.open(error_file);
        cerr.rdbuf(err.rdbuf());
      }

      for (size_t i = 1; i < args.size(); ++i)
      {
        cout << args[i] << (i + 1 < args.size() ? " " : "");
      }
      cout << endl;

      if (!output_file.empty())
      {
        cout.rdbuf(coutbuf);
        out.close();
      }

      if (!error_file.empty())
      {
        cerr.rdbuf(cerrbuf);
        err.close();
      }
      continue;
    }

    if (args[0] == "type")
    {
      if (args.size() < 2)
      {
        cout << "type: missing operand" << endl;
        continue;
      }
      string cmd = args[1];
      if (builtins.count(cmd))
      {
        cout << cmd << " is a shell builtin" << endl;
      }
      else
      {
        string path = find_in_path(cmd);
        if (!path.empty())
        {
          cout << cmd << " is " << path << endl;
        }
        else
        {
          cout << cmd << ": not found" << endl;
        }
      }
      continue;
    }

    execute_external_command(args, output_file, error_file);
  }
  return 0;
}