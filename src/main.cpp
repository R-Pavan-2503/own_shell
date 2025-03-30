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

void execute_external_command(const vector<string> &args, const string &output_file)
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

  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hFile ? hFile : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE); // Keep stderr in terminal

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
      freopen(output_file.c_str(), "w", stdout); // Redirect stdout
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

vector<string> parse_input(const string &input, string &output_file)
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
    else if ((c == '>' || (c == '1' && i + 1 < input.size() && input[i + 1] == '>')) &&
             !in_single_quotes && !in_double_quotes)
    {
      if (!word.empty())
      {
        args.push_back(word);
        word.clear();
      }

      // Skip `1>` if present
      if (c == '1' && input[i + 1] == '>')
        i++;

      // Skip spaces before the filename
      while (i + 1 < input.size() && input[i + 1] == ' ')
        i++;

      size_t j = i + 1;
      while (j < input.size() && input[j] != ' ')
        j++;

      output_file = input.substr(i + 1, j - i - 1);
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
    vector<string> args = parse_input(input, output_file);

    if (args.empty())
      continue;

    if (args[0] == "echo")
    {
      ofstream out;
      streambuf *coutbuf = cout.rdbuf();

      if (!output_file.empty())
      {
        out.open(output_file);
        cout.rdbuf(out.rdbuf());
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

    execute_external_command(args, output_file);
  }
  return 0;
}
