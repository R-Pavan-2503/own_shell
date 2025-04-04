#include <iostream>
#include <string>
#include <unordered_set>

#include <cstdlib>
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include <fstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>  // For _getch() on Windows
#include <direct.h> // For _chdir() on Windows
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h> // For chdir() and getcwd()
#include <fcntl.h>
#include <cstring>
#include <termios.h> // Only included for Unix systems
#include <dirent.h>  // For directory operations on Unix
#endif

using namespace std;

// Structure to hold redirection information
struct RedirectInfo
{
  string filename;
  bool append;
};

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

void execute_external_command(const vector<string> &args, const RedirectInfo &stdout_info, const RedirectInfo &stderr_info)
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
  if (!stdout_info.filename.empty())
  {
    DWORD dwCreationDisposition = stdout_info.append ? OPEN_ALWAYS : CREATE_ALWAYS;
    hFile = CreateFileA(stdout_info.filename.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                        dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
      cerr << "Failed to open file: " << stdout_info.filename << endl;
      return;
    }

    // If appending, seek to end of file
    if (stdout_info.append)
    {
      SetFilePointer(hFile, 0, NULL, FILE_END);
    }
  }

  HANDLE hErrorFile = NULL;
  if (!stderr_info.filename.empty())
  {
    DWORD dwCreationDisposition = stderr_info.append ? OPEN_ALWAYS : CREATE_ALWAYS;
    hErrorFile = CreateFileA(stderr_info.filename.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                             dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hErrorFile == INVALID_HANDLE_VALUE)
    {
      cerr << "Failed to open file: " << stderr_info.filename << endl;
      if (hFile)
        CloseHandle(hFile);
      return;
    }

    // If appending, seek to end of file
    if (stderr_info.append)
    {
      SetFilePointer(hErrorFile, 0, NULL, FILE_END);
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
    if (!stdout_info.filename.empty())
    {
      int flags = O_WRONLY | O_CREAT;
      flags |= stdout_info.append ? O_APPEND : O_TRUNC;
      int fd = open(stdout_info.filename.c_str(), flags, 0644);
      if (fd == -1)
      {
        perror("open output file failed");
        exit(1);
      }
      dup2(fd, STDOUT_FILENO); // Redirect stdout
      close(fd);
    }

    if (!stderr_info.filename.empty())
    {
      int flags = O_WRONLY | O_CREAT;
      flags |= stderr_info.append ? O_APPEND : O_TRUNC;
      int fd = open(stderr_info.filename.c_str(), flags, 0644);
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

vector<string> parse_input(const string &input, RedirectInfo &stdout_info, RedirectInfo &stderr_info)
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
    else if ((!in_single_quotes && !in_double_quotes) &&
             ((c == '>' && i + 1 < input.size() && input[i + 1] == '>') ||                                                // >>
              (c == '1' && i + 1 < input.size() && input[i + 1] == '>' && i + 2 < input.size() && input[i + 2] == '>') || // 1>>
              (c == '2' && i + 1 < input.size() && input[i + 1] == '>' && i + 2 < input.size() && input[i + 2] == '>')))  // 2>>
    {
      // Handle >> operators (append mode)
      if (!word.empty())
      {
        args.push_back(word);
        word.clear();
      }

      bool is_stderr = (c == '2');

      // Skip the operator (>> or 1>> or 2>>)
      if (c == '>')
        i++;
      else
        i += 2;

      // Skip spaces before the filename
      while (i + 1 < input.size() && input[i + 1] == ' ')
        i++;

      size_t j = i + 1;
      while (j < input.size() && input[j] != ' ' &&
             !(input[j] == '>' ||
               (input[j] == '1' && j + 1 < input.size() && input[j + 1] == '>') ||
               (input[j] == '2' && j + 1 < input.size() && input[j + 1] == '>')))
        j++;

      if (i + 1 < input.size())
      {
        string filename = input.substr(i + 1, j - i - 1);
        if (is_stderr)
        {
          stderr_info.filename = filename;
          stderr_info.append = true;
        }
        else
        {
          stdout_info.filename = filename;
          stdout_info.append = true;
        }
      }

      i = j - 1;
    }
    else if ((!in_single_quotes && !in_double_quotes) &&
             ((c == '>') ||
              (c == '1' && i + 1 < input.size() && input[i + 1] == '>') ||
              (c == '2' && i + 1 < input.size() && input[i + 1] == '>')))
    {
      // Handle > operators (truncate mode)
      if (!word.empty())
      {
        args.push_back(word);
        word.clear();
      }

      bool is_stderr = (c == '2' && i + 1 < input.size() && input[i + 1] == '>');

      // Skip the operator (> or 1> or 2>)
      if (c != '>')
        i++;

      // Skip spaces before the filename
      while (i + 1 < input.size() && input[i + 1] == ' ')
        i++;

      size_t j = i + 1;
      while (j < input.size() && input[j] != ' ' &&
             !(input[j] == '>' ||
               (input[j] == '1' && j + 1 < input.size() && input[j + 1] == '>') ||
               (input[j] == '2' && j + 1 < input.size() && input[j + 1] == '>')))
        j++;

      if (i + 1 < input.size())
      {
        string filename = input.substr(i + 1, j - i - 1);
        if (is_stderr)
        {
          stderr_info.filename = filename;
          stderr_info.append = false;
        }
        else
        {
          stdout_info.filename = filename;
          stdout_info.append = false;
        }
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

// Function to find the longest common prefix of a vector of strings
string find_longest_common_prefix(const vector<string> &matches)
{
  if (matches.empty())
  {
    return "";
  }
  if (matches.size() == 1)
  {
    return matches[0];
  }

  string prefix = matches[0];
  for (size_t i = 1; i < matches.size(); ++i)
  {
    // Find common prefix between current prefix and next string
    size_t j = 0;
    while (j < prefix.length() && j < matches[i].length() &&
           prefix[j] == matches[i][j])
    {
      j++;
    }
    // Update prefix to the common part
    prefix = prefix.substr(0, j);

    // If no common prefix found, exit early
    if (prefix.empty())
    {
      break;
    }
  }

  return prefix;
}

// Enhanced function to handle tab completion for builtin commands and executables in PATH
// Modified to return all matches instead of just one, sorted alphabetically
vector<string> complete_command(const string &partial_cmd)
{
  // List of built-in commands (same as in main)
  static const unordered_set<string> builtins = {"echo", "type", "exit"};
  vector<string> matches;

  // Check if the partial command matches any builtin
  for (const auto &cmd : builtins)
  {
    if (cmd.substr(0, partial_cmd.length()) == partial_cmd)
    {
      matches.push_back(cmd);
    }
  }

  // If we found builtin matches, sort and return them
  if (!matches.empty())
  {
    sort(matches.begin(), matches.end());
    return matches;
  }

  // Search for executables in PATH
  char *path_env = getenv("PATH");
  if (!path_env)
    return matches; // Empty vector

  stringstream ss(path_env);
  string dir;

  while (getline(ss, dir, ':'))
  {
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    string search_path = dir + "\\*";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE)
    {
      do
      {
        string filename = findData.cFileName;
        if (filename.substr(0, partial_cmd.length()) == partial_cmd)
        {
          // Check if the file is executable
          string full_path = dir + "\\" + filename;
          if (GetFileAttributesA(full_path.c_str()) != INVALID_FILE_ATTRIBUTES)
          {
            matches.push_back(filename);
          }
        }
      } while (FindNextFileA(hFind, &findData));

      FindClose(hFind);
    }
#else
    DIR *dp = opendir(dir.c_str());
    if (dp != NULL)
    {
      struct dirent *entry;
      while ((entry = readdir(dp)) != NULL)
      {
        string filename = entry->d_name;
        if (filename.substr(0, partial_cmd.length()) == partial_cmd)
        {
          // Check if the file is executable
          string full_path = dir + "/" + filename;
          struct stat buffer;
          if (stat(full_path.c_str(), &buffer) == 0 &&
              (buffer.st_mode & S_IXUSR))
          {
            matches.push_back(filename);
          }
        }
      }
      closedir(dp);
    }
#endif
  }

  // Sort matches alphabetically
  sort(matches.begin(), matches.end());
  return matches;
}

string get_input_with_completion()
{
  string input;
  size_t cursor_pos = 0;
  bool tab_pressed_once = false;
  vector<string> previous_matches;

#ifdef _WIN32
  cout << "$ " << flush;

  while (true)
  {
    int c = _getch();

    if (c == 13)
    { // Enter key
      cout << endl;
      break;
    }
    else if (c == 8 || c == 127)
    { // Backspace
      if (cursor_pos > 0)
      {
        input.erase(cursor_pos - 1, 1);
        cursor_pos--;
        cout << "\b \b" << flush;
        tab_pressed_once = false;
      }
    }
    else if (c == 9)
    { // Tab key
      if (cursor_pos > 0 && input.find(' ') == string::npos)
      {
        string partial_cmd = input.substr(0, cursor_pos);
        vector<string> matches = complete_command(partial_cmd);

        if (matches.empty())
        {
          cout << '\a' << flush;
          tab_pressed_once = false;
        }
        else
        {
          string common_prefix = find_longest_common_prefix(matches);

          if (common_prefix.length() > partial_cmd.length())
          {
            for (size_t i = 0; i < cursor_pos; i++)
            {
              cout << "\b \b" << flush;
            }

            input = common_prefix + input.substr(cursor_pos);
            cursor_pos = common_prefix.length();
            cout << input.substr(0, cursor_pos) << flush;

            // FIX: Only add space if this is the **only match**
            if (matches.size() == 1)
            {
              input += " ";
              cursor_pos++;
              cout << " " << flush;
            }

            tab_pressed_once = false;
          }
          else if (!tab_pressed_once)
          {
            cout << '\a' << flush;
            tab_pressed_once = true;
            previous_matches = matches;
          }
          else
          {
            cout << endl;
            for (const auto &match : matches)
            {
              cout << match << "  ";
            }
            cout << endl
                 << "$ " << input.substr(0, cursor_pos) << flush;
            tab_pressed_once = false;
          }
        }
      }
      else
      {
        tab_pressed_once = false;
      }
    }
    else if (c >= 32 && c < 127)
    {
      input.insert(cursor_pos, 1, static_cast<char>(c));
      cursor_pos++;
      cout << static_cast<char>(c) << flush;
      tab_pressed_once = false;
    }
  }
#else
  struct termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);
  new_term = old_term;
  new_term.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

  cout << "$ " << flush;

  while (true)
  {
    char c = getchar();

    if (c == '\n')
    {
      cout << endl;
      break;
    }
    else if (c == 127 || c == 8)
    {
      if (cursor_pos > 0)
      {
        input.erase(cursor_pos - 1, 1);
        cursor_pos--;
        cout << "\b \b" << flush;
        tab_pressed_once = false;
      }
    }
    else if (c == 9)
    {
      if (cursor_pos > 0 && input.find(' ') == string::npos)
      {
        string partial_cmd = input.substr(0, cursor_pos);
        vector<string> matches = complete_command(partial_cmd);

        if (matches.empty())
        {
          cout << '\a' << flush;
          tab_pressed_once = false;
        }
        else
        {
          string common_prefix = find_longest_common_prefix(matches);

          if (common_prefix.length() > partial_cmd.length())
          {
            for (size_t i = 0; i < cursor_pos; i++)
            {
              cout << "\b \b" << flush;
            }

            input = common_prefix + input.substr(cursor_pos);
            cursor_pos = common_prefix.length();
            cout << input.substr(0, cursor_pos) << flush;

            // FIX: Only add space if this is the **only match**
            if (matches.size() == 1)
            {
              input += " ";
              cursor_pos++;
              cout << " " << flush;
            }

            tab_pressed_once = false;
          }
          else if (!tab_pressed_once)
          {
            cout << '\a' << flush;
            tab_pressed_once = true;
            previous_matches = matches;
          }
          else
          {
            cout << endl;
            for (const auto &match : matches)
            {
              cout << match << "  ";
            }
            cout << endl
                 << "$ " << input.substr(0, cursor_pos) << flush;
            tab_pressed_once = false;
          }
        }
      }
      else
      {
        tab_pressed_once = false;
      }
    }
    else if (c >= 32 && c < 127)
    {
      input.insert(cursor_pos, 1, static_cast<char>(c));
      cursor_pos++;
      cout << c << flush;
      tab_pressed_once = false;
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
#endif

  return input;
}

// Function to execute cd command

// Function to execute cd command
#include <iostream>
#include <unistd.h> // For chdir() and getcwd()
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unordered_set>
#include <vector>
#include <fstream>
#include <sstream>
#include <limits.h> // For PATH_MAX in POSIX systems
#include <cstdlib>  // For getenv()

using namespace std;

// Function to execute cd command
void execute_cd(const std::string &path)
{
  std::string final_path = path;

  // If the path starts with '~', replace it with the HOME environment variable
  if (path[0] == '~')
  {
    const char *home = getenv("HOME");
    if (home)
    {
      final_path = home + std::string(path.substr(1)); // Replace ~ with HOME
    }
    else
    {
      std::cerr << "cd: HOME not set" << std::endl;
      return;
    }
  }

#ifdef _WIN32
  if (_chdir(final_path.c_str()) == -1)
  { // For Windows
    std::cerr << "cd: " << final_path << ": No such file or directory" << std::endl;
  }
#else
  if (chdir(final_path.c_str()) == -1)
  { // For Unix-like systems
    std::cerr << "cd: " << final_path << ": No such file or directory" << std::endl;
  }
#endif
}

// Existing function for pwd
void execute_builtin_pwd()
{
  char cwd[PATH_MAX]; // Use PATH_MAX for POSIX systems
  if (getcwd(cwd, sizeof(cwd)) != NULL)
  { // POSIX alternative to _getcwd()
    cout << cwd << endl;
  }
  else
  {
    perror("pwd");
  }
}

int main()
{
  cout << unitbuf;
  cerr << unitbuf;

  unordered_set<string> builtins = {"echo", "type", "exit", "pwd", "cd"};

  while (true)
  {
    string input = get_input_with_completion();

    if (input == "exit 0")
    {
      return 0;
    }

    RedirectInfo stdout_info = {"", false};
    RedirectInfo stderr_info = {"", false};
    vector<string> args = parse_input(input, stdout_info, stderr_info);

    if (args.empty())
      continue;

    if (args[0] == "pwd")
    {
      execute_builtin_pwd();
      continue;
    }

    if (args[0] == "echo")
    {
      ofstream out;
      ofstream err;
      streambuf *coutbuf = cout.rdbuf();
      streambuf *cerrbuf = cerr.rdbuf();

      if (!stdout_info.filename.empty())
      {
        ios_base::openmode mode = ios::out;
        if (stdout_info.append)
          mode |= ios::app;

        out.open(stdout_info.filename, mode);
        cout.rdbuf(out.rdbuf());
      }

      if (!stderr_info.filename.empty())
      {
        ios_base::openmode mode = ios::out;
        if (stderr_info.append)
          mode |= ios::app;

        err.open(stderr_info.filename, mode);
        cerr.rdbuf(err.rdbuf());
      }

      for (size_t i = 1; i < args.size(); ++i)
      {
        cout << args[i] << (i + 1 < args.size() ? " " : "");
      }
      cout << endl;

      if (!stdout_info.filename.empty())
      {
        cout.rdbuf(coutbuf);
        out.close();
      }

      if (!stderr_info.filename.empty())
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

    // Handling 'cd' command
    if (args[0] == "cd")
    {
      if (args.size() < 2)
      {
        cerr << "cd: missing operand" << endl;
      }
      else
      {
        string path = args[1];
        execute_cd(path); // Call the execute_cd function
      }
      continue;
    }

    execute_external_command(args, stdout_info, stderr_info);
  }

  return 0;
}
