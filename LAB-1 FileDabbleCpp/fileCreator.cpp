#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

using namespace std;

void writeLine(string input, int lineNumber, int fd)
{

    if (input.length() == 0 || input[input.length() - 1] != '\n')
    {
        input.append("\n");
    }
    write(fd, input.data(), input.size());
};

string sanitize(string fileName)
{
    // TODO : IMPLEMENT THIS
    return fileName;
};

int main()
{

    cout << "Name the file : <file_name>.<extention>" << '\n';
    string fileName;
    cin >> fileName;

    fileName = sanitize(fileName);

    string filePath = "./" + fileName;

    int fd = open(filePath.c_str(), O_RDWR | O_CREAT, 0644);

    if (fd == -1)
    {
        cerr << "FATAL Error, cannot open the file. Please restart the program." << '\n';
        return 1;
    }

    cout << "How many lines in the program ?" << '\n';
    int numberOfLines;
    cin >> numberOfLines;

    vector<string> inputs;

    for (int i = 0; i < numberOfLines; i++)
    {
        cout << "Enter Line " << i + 1 << " | .exit to stop" << '\n';
        string input;
        cin >> input;

        if (input == ".exit")
            break;

        inputs.push_back(input);
    }

    for (int i = 0; i < inputs.size(); i++)
    {
        writeLine(inputs[i], i + 1, fd);
    }

    if (close(fd) == -1)
    {
        cerr << "ERROR closing the file!" << '\n';
    }
    else
    {
        cout << "Created the file successfully" << '\n';
    }

    return 0;
}