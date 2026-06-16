#include <iostream>
#include <fcntl.h>	// For open(), O_CREAT, O_WRONLY
#include <unistd.h> // For write(), close()

using namespace std;

int main()
{
	int fd = open("new.txt", O_RDWR | O_APPEND | O_CREAT, 0644);
	cout << "The file descriptor is : " << fd << "\n";

	if (fd == -1)
	{
		cerr << "The file does not exist!\n";
	}

	string s = "nee new one";

	// cout << s << "\n";

	for (int i = 0; i < 3; i++)
	{
		string toWrite = s + "_" + to_string(i) + "\n";
		auto out = write(fd, toWrite.data(), toWrite.size());

		cout << out << '\n';
	}

	// cout << out;

	close(fd);

	return 0;
}

int naaaaaaaaah()
{
	int fd = open("./file.txt", O_RDWR);
	cout << "The file descriptor is : " << fd << "\n";

	if (fd == -1)
	{
		cerr << "The file does not exist!\n";
		return 1;
	}

	string s = "New text from sytem calls";

	// cout << s << "\n";

	auto out = write(fd, s.data(), s.size());

	cout << out;

	return 0;
}
