/*
 * ============================================================================
 * Name: Patel Jash
 * Batch: B
 * Roll: 24BCS10632
 * Lab: 01
 * Title: Storage_Engine
 * ============================================================================
 */

// #include <bits/stdc++.h> // iss se bas standard libraries aati, but apan ko
// POSIX standard libs chaiye.

#include <cstring>
#include <fcntl.h> // open()
#include <iostream>
#include <sys/stat.h>
#include <unistd.h> // read(), write(), close()

using namespace std;
#define endl                                                                   \
  "\n" // taki baar merko \n naa likhna pade, endl muscle memory me hai :)

int main() {
  // ==============
  // 1. OPEN FILE
  // ==============
  int fileFd = open( // limited int num for fd w/ diff meaning
      "jash-det.txt",
      O_RDWR | O_CREAT | O_APPEND, // bit flags to control how file is opened, | to combine
                        // the bits
      0644 // file permision,only jab file create kar rahe ho toh, 0 for octal
  );

  if (fileFd < 0) // fail condition me OS -1 return karta hai
  {
    cerr << "Failed to open file" << endl; // cerr : error throw karne ke liye
    return -1;
  }

  cout << "Jash's File Descriptor : " << fileFd << endl;

  // compile : g++ main.cpp -std=c++17 -o app
  // -std flag to set the version, -o to set the custom name of the output file

  //===================
  // 2. WRITE TO FILE
  //===================
  const char *data = "Jash doing Raw Linux Sys Calls!\n";
  // why not string? cuz string is a proper "container" which kernel can't
  // understand, const char* ek pointer hai jo direct uss string ko point kar
  // rha hai in the memory, and kernel exactly wants this address. write() ek
  // syscall hai toh uske ander waise bhi string nahi jaa payegi if you still
  // wanna send a string then you can send as msg.c_str(), it will do string ->
  // const char*

  //
  ssize_t wroteBytes =
      write( // ye ander ki data ko overwrite kardega as current offset
             // (starting point) 0 (start) hoga
          fileFd, data,
          strlen(data) // kinda kitne bytes hai wo return karta hai but uss
                      // datatype? me jo write() ko chaiye, to tell kernel kitne
                      // bytes likhne hai
      );
  // abhi offset change ho gaya hoga, if you will do again write then wo aage se
  // hoga.

  if (wroteBytes < 0) // this was the exact reason why we wrote "ssize_t",
                      // iska matlab hai signed size type and hence it can
                      // return both positive val and neg when failure
  {
    cerr << "Nahi likh paya" << endl;
    close(fileFd);
    return -1;
  }

  cout << "Bytes Written : " << wroteBytes << endl;

  string data2 = "Jash ka ye msg kidhar hoga ?";

  ssize_t wroteMore = write(fileFd, data2.c_str(), strlen(data2.c_str()));

  if (wroteMore < 0) {
    cerr << "Khatam" << endl;
    close(fileFd);
    return -1;
  }

  cout << "Wapas se itna likha : " << wroteMore << endl;
  // dekha, line 2 pe tha apna new data, line 2 due to "\n" in the prev write
  // ops.

  // ==========================
  // 3. Moving the file offset
  // =========================

  lseek(fileFd,   // konse file ka change karna hai
        0,       // kitna
        SEEK_SET // kaha se
  );
  // it will move 0 from starting(SEEK_SET) for our "fd" => starting offset, les
  // go

  // ab write ya read karke dekhlo starting se hoga.

  // =============
  // 4. READ FILE
  // =============

  char buf[1024];
  // char array as read() works with bytes and not strings
  // so apan ne OS ko ek memory area dedi upcoming data store karne ke liye

  ssize_t readBytes =
      read(fileFd,          // apna fd
           buf,             // kidhar store karna hai
           sizeof(buf) - 1  // max bytes to store, -1 as apan ne ek byte null
                            // terminator ke liye reserve kii hai
      );

  if (readBytes < 0) {
    cerr << "khatam" << endl;
    close(fileFd);
    return -1;
  }

  buf[readBytes] = '\0'; // null terminator, well ye cout ko inform karne ke
                         // liye hai kii yanhi tak print karna otherwise you
                         // will se some garbage values being printed.

  cout << "\nJash's Content Read from File: " << endl;
  cout << buf << endl; // the benefit of char array, you can directly print
                       // it's whole contents like this

  // =============
  // 5. CLOSE FILE
  // =============

  close(fileFd); // closing the fd is necessary to stop memory from being leaked

  return 411;
}