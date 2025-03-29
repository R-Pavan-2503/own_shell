#include <iostream>
#include <string>
#include <unordered_set>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <sys/stat.h>
using namespace std;

bool is_executable(const string &path)
{
  struct stat buffer;
  return (stat(path.c_str(), &buffer) == 0 && (buffer.st_mode & S_IXUSR));
}

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
    if (is_executable(full_path))
    {
      return full_path; 
    }
  }
  return "";
}

int main()
{
  // Flush after every cout / std:cerr
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

    if (input.rfind("echo", 0) == 0)
    {
      cout << input.substr(5) << endl;
      continue;
    }

    if (input.rfind("type", 0) == 0)
    {
      string cmd = input.substr(5);

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

    cout << input << ": command not found" << endl;
  }

  return 0;
}
