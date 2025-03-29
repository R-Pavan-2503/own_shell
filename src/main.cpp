#include <iostream>
using namespace std;

int main()
{
  // Flush after every cout / std:cerr
  cout << unitbuf;
  cerr << unitbuf;

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
    cout << input << ": command not found" << endl;
  }

  return 0;
}
