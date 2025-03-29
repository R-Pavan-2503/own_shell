#include <iostream>
#include <string>
#include <unordered_set>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#endif

using namespace std;

// Check if a file is executable (Only for Linux/Mac)
#ifndef _WIN32
bool is_executable(const string &path)
{
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0 && (buffer.st_mode & S_IXUSR));
}
#endif

// Find an executable in the PATH environment variable
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

// Execute external command (Cross-platform implementation)
void execute_external_command(const vector<string> &args)
{
  if (args.empty())
    return;

  string cmd_path = find_in_path(args[0]);
  if (cmd_path.empty())
  {
    cout << args[0] << ": command not found" << endl;
    return;
  }

#ifdef _WIN32
  // Windows: Use CreateProcess
  string command = cmd_path;
  for (size_t i = 1; i < args.size(); ++i)
  {
    command += " " + args[i];
  }

  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi;

  if (CreateProcessA(NULL, const_cast<char *>(command.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  else
  {
    cerr << "Failed to execute: " << command << endl;
  }
#else
  // Linux/Mac: Use fork + execvp
  vector<char *> c_args;
  for (const string &arg : args)
  {
    c_args.push_back(const_cast<char *>(arg.c_str()));
  }
  c_args.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0)
  {
    execvp(cmd_path.c_str(), c_args.data());
    perror("execvp failed");
    exit(1);
  }
  else if (pid > 0)
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

vector<string> parse_input(const string &input)
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
      // Inside double quotes, only escape certain characters
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

    // stringstream ss(input);
    // vector<string> args;
    // string word;
    // while (ss >> word)
    // {
    //   args.push_back(word);
    // }

    vector<string> args = parse_input(input);

    if (args.empty())
      continue;

    if (args[0] == "echo")
    {
      for (size_t i = 1; i < args.size(); ++i)
      {
        cout << args[i] << (i + 1 < args.size() ? " " : "");
      }
      cout << endl;
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

    // Run external command
    execute_external_command(args);
  }
  return 0;
}
