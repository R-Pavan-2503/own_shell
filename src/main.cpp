#include <iostream>
#include <unordered_set>
using namespace std;

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
        cout << cmd << " not found" << endl;
      }
      continue;
    }

    cout << input << ": command not found" << endl;
  }

  return 0;
}
