# 📁 Lab 1

# **Lab 1: File Handling Using System Calls in Linux**

## **Aim**

To perform file input and output operations using Linux system calls in C.

---

## **Description**

This experiment demonstrates the use of **Linux system calls** for file handling. Unlike standard library functions such as fopen(), fread(), and fwrite(), system calls interact directly with the operating system kernel, providing low-level control over file operations.

The program performs the following tasks:

### **1\. Open a File for Reading**

* Opens file.txt in read-only mode.  
* Obtains a file descriptor from the operating system.  
* Uses the file descriptor for subsequent file operations.

### **2\. Read Data from the File**

* Reads up to 512 bytes from the file.  
* Stores the data in a buffer.  
* Displays the contents on the terminal.  
* Prints the total number of bytes read.

### **3\. Close the File**

* Releases the file descriptor after the read operation.  
* Frees system resources associated with the file.

### **4\. Open the File in Append Mode**

* Reopens the same file in write mode with append enabled.  
* Ensures that new data is added at the end of the file without modifying existing content.

### **5\. Write Data to the File**

* Appends a string to the file.  
* Displays the total number of bytes written.

### **6\. Close the File Again**

* Closes the file descriptor after the write operation.  
* Ensures proper resource management.

### **7\. Error Handling**

* Checks the return value of every system call.  
* Displays meaningful error messages when an operation fails.  
* Uses system error information to identify the cause of failures.

---

## **System Calls Used**

| System Call | Purpose |
| ----- | ----- |
| open() | Opens a file and returns a file descriptor |
| read() | Reads data from a file descriptor |
| write() | Writes data to a file descriptor |
| close() | Closes a file descriptor |

---

## **Observations**

* Every opened file is assigned a unique file descriptor by the operating system.  
* Data can be read directly from a file using the file descriptor.  
* Append mode prevents existing data from being overwritten.  
* Proper error handling improves program reliability.  
* Files should always be closed after use to release system resources.

---

## **Learning Outcomes**

After completing this experiment, students will be able to:

* Understand the concept of Linux system calls.  
* Work with file descriptors for file operations.  
* Perform file reading and writing using low-level system calls.  
* Implement basic error handling for file operations.  
* Understand the difference between system calls and standard library file functions.  
* Gain practical experience with kernel-level file management.

---

# 📁 Lab 2

# **Lab 2: SQLite3 and PostgreSQL Performance Exploration**

## **Objective**

The objective of this lab is to explore the internal storage and performance characteristics of **SQLite3** and **PostgreSQL**. Students will analyze database page structures, examine storage organization, measure query execution times, experiment with memory-mapped I/O (mmap), and compare the behavior of both database systems.

---

## **Description**

Database systems organize and store data using pages and internal storage structures. Understanding how databases manage storage and execute queries is important for analyzing performance and selecting the appropriate database for different applications.

In this experiment, students will perform a detailed study of two widely used database systems:

* **SQLite3** – an embedded database that stores all data in a single file.  
* **PostgreSQL** – a client-server relational database management system designed for multi-user environments.

Students will investigate database file structures, page organization, memory usage, and query performance. They will also analyze the impact of memory-mapped I/O on database operations and compare the advantages and limitations of both systems.

---

## **Tasks**

### **1\. SQLite3 Exploration**

Install SQLite3 and create or use an existing sample database.

#### **File Size Analysis**

* Examine the database file size.  
* Observe how database size changes as records are added.  
* Record storage requirements.

#### **Page Information**

Retrieve page-related information from SQLite.

Observe:

* Page size  
* Total number of pages  
* Relationship between page count and database size

#### **Memory-Mapped I/O (mmap)**

Investigate SQLite's memory-mapped I/O mechanism.

Observe:

* Current mmap configuration  
* Effects of modifying mmap size  
* Changes in query execution behavior

#### **Query Performance Measurement**

Measure query execution time under different configurations.

Compare:

* Query execution with mmap enabled  
* Query execution with mmap disabled

Analyze differences in performance.

#### **Process Monitoring**

Monitor SQLite-related processes and system resource usage.

Observe:

* CPU utilization  
* Memory consumption  
* Process behavior during query execution

---

### **2\. PostgreSQL Exploration**

Install PostgreSQL and create a sample database.

#### **Database Storage Information**

Retrieve storage-related information from PostgreSQL.

Observe:

* Block size  
* Relation size  
* Estimated page count

Analyze how PostgreSQL organizes data internally.

#### **Query Performance Measurement**

Measure query execution performance.

Observe:

* Execution time  
* Query plan information  
* Resource utilization

Analyze factors affecting query performance.

#### **Process Monitoring**

Monitor PostgreSQL processes.

Observe:

* Server processes  
* Memory usage  
* Background services  
* Active query execution

---

### **3\. Comparison Study**

Compare SQLite3 and PostgreSQL using experimental observations.

Analyze:

#### **Architecture**

* Embedded database model  
* Client-server architecture

#### **Storage Organization**

* Page size  
* Page count  
* File structure

#### **Query Performance**

* Execution speed  
* Response time  
* Resource usage

#### **Memory Mapping**

* mmap behavior  
* Impact on performance

#### **Ease of Setup**

* Installation complexity  
* Configuration requirements

#### **Suitable Applications**

* Small-scale applications  
* Enterprise systems  
* Embedded devices  
* Multi-user environments

---

## **Observations**

During the experiment, students should record:

* Database file sizes  
* Page sizes  
* Page counts  
* Query execution times  
* Memory-mapping configurations  
* CPU and memory usage  
* Differences between SQLite3 and PostgreSQL architectures

---

## **Analysis Questions**

1. What is the purpose of database pages?  
2. How does SQLite store data differently from PostgreSQL?  
3. What is memory-mapped I/O and why is it used?  
4. How does mmap affect query performance?  
5. Why does PostgreSQL use a client-server architecture?  
6. What factors influence query execution time?  
7. Which database is more suitable for embedded applications?  
8. Which database is more suitable for large multi-user systems?  
9. How do storage structures affect performance?

# 📁 Lab 3

# **Lab 3: Clock Sweep Cache Replacement Algorithm**

## **Objective**

The objective of this lab is to study and understand the **Clock Sweep Cache Replacement Algorithm**, a widely used page replacement strategy in database management systems and operating systems. Students will analyze how the algorithm manages cached data, tracks page usage, and selects pages for eviction when the cache becomes full.

---

## **Overview**

Efficient memory management is essential for improving the performance of database systems. Since memory resources are limited, a cache replacement policy is required to determine which data should remain in memory and which data should be removed when space is needed.

The Clock Sweep algorithm is a practical approximation of the Least Recently Used (LRU) strategy. Instead of maintaining detailed access histories for all pages, it uses a reference indicator and a rotating pointer (clock hand) to identify pages that have not been used recently. This approach provides good performance while keeping implementation overhead low.

---

## **Tasks**

### **1\. Cache Initialization**

Create and initialize a cache that uses the Clock Sweep replacement policy. Observe the initial state of the cache and understand how entries are managed internally.

### **2\. Cache Population**

Insert multiple entries into the cache and observe how the cache stores and tracks them. Record the state of the cache after all entries have been added.

### **3\. Access Pattern Analysis**

Access cache entries using different frequencies. Some entries should be accessed more frequently than others. Observe how the algorithm records page usage and how frequently accessed pages receive higher priority for retention.

### **4\. Clock Sweep Observation**

Study the movement of the clock hand and analyze how the algorithm scans cache entries when searching for a replacement candidate. Observe how pages are given a second chance before being evicted.

### **5\. Cache Replacement Analysis**

When the cache reaches capacity, observe which entries are selected for eviction. Analyze how recent access patterns influence replacement decisions and compare the results with what would happen under FIFO or LRU policies.

### **6\. Logging and Monitoring**

Examine the execution logs generated during cache operations. Record important events such as cache insertions, cache hits, cache misses, reference-bit updates, and page replacements.

# 📁 Lab 4

# **Lab 4: SQLite3 Database Internal Structure Analysis Using XXD**

## **Objective**

The objective of this lab is to explore and analyze the internal storage structure of a SQLite3 database file using hexadecimal inspection tools. Students will investigate how SQLite stores database metadata, schema definitions, B-tree pages, records, and page headers directly within the database file.

The lab provides practical exposure to low-level database storage concepts and demonstrates how database systems organize information on disk.

---

## **Description**

In this experiment, a SQLite database containing a sample table is created and populated with records. The database file is then examined using hexadecimal dump utilities to understand how SQLite organizes data internally.

Students will study various components of the database file, including:

* SQLite database header  
* Database page structure  
* B-tree organization  
* Table records  
* Cell pointer arrays  
* Record payloads  
* Schema metadata  
* Root page references

The experiment demonstrates that SQLite stores both user data and metadata within the same database file using a B-tree-based architecture. By examining the raw file contents, students gain insight into how modern database systems physically store and retrieve information.

---

## **Tasks**

### **1\. Database Creation**

Create a SQLite database and define a table structure containing multiple records.

Observe:

* Table schema  
* Number of records inserted  
* Database file creation

---

### **2\. Database Metadata Analysis**

Retrieve database information using SQLite metadata commands.

Record:

* Page size  
* Page count  
* Root page information  
* Table metadata

Analyze how SQLite organizes these details internally.

---

### **3\. SQLite File Header Inspection**

Examine the beginning of the database file using a hexadecimal dump utility.

Observe:

* SQLite file signature  
* Database format identifier  
* Page size information  
* Header fields

Verify that the file is recognized as a valid SQLite database.

---

### **4\. B-Tree Page Analysis**

Study the structure of database pages.

Analyze:

* Page type  
* Number of records stored  
* Cell content area  
* Free space management

Observe how SQLite uses B-tree pages to organize records efficiently.

---

### **5\. Cell Pointer Array Examination**

Inspect the cell pointer array within a page.

Observe:

* Record offsets  
* Pointer organization  
* Record location mechanism

Understand how SQLite locates individual records without scanning the entire page.

---

### **6\. Record Storage Analysis**

Examine the record payload section of the database.

Identify:

* Stored text values  
* Row information  
* Record organization  
* Variable-length storage

Observe how actual user data is represented inside the database file.

---

### **7\. Schema Storage Analysis**

Investigate how SQLite stores schema definitions.

Observe:

* Table definitions  
* Metadata records  
* Internal catalog information

Verify that SQL table creation statements are physically stored inside the database file.

---

### **8\. Physical File Layout Study**

Analyze the overall layout of the database file.

Observe:

* Header location  
* Metadata pages  
* Table pages  
* Record storage areas

Understand how multiple components coexist within a single database file.

# 📁 Lab 5

# **Lab 5: Red-Black Tree Implementation**

## **Objective**

The objective of this lab is to implement and analyze a **Red-Black Tree**, a self-balancing binary search tree that maintains efficient search and insertion operations. Students will understand how balancing mechanisms such as rotations and recoloring ensure that the tree remains balanced after updates.

---

## **Description**

In this experiment, a Red-Black Tree is implemented to store and manage integer values efficiently. The tree supports insertion, searching, and traversal operations while automatically maintaining balance through the Red-Black Tree properties.

Unlike a standard Binary Search Tree (BST), which can become skewed and degrade performance, a Red-Black Tree ensures that the height of the tree remains approximately balanced. This guarantees efficient operation performance even as the number of stored elements increases.

The implementation demonstrates how newly inserted nodes are placed according to Binary Search Tree rules and then adjusted using recoloring and rotation operations to maintain Red-Black Tree properties.

---

## **Tasks**

### **1\. Tree Initialization**

Create an empty Red-Black Tree and initialize the required data structures. Observe how the tree starts without any nodes and prepares for future insertions.

### **2\. Node Insertion**

Insert multiple values into the tree. During insertion, each value is first placed according to Binary Search Tree rules. After insertion, balancing operations are performed automatically to preserve the Red-Black Tree properties.

Observe:

* Position of inserted nodes  
* Changes in node colors  
* Structural modifications to maintain balance

### **3\. Balancing Operations**

Analyze how the tree restores balance after insertion.

Observe:

* Recoloring of nodes  
* Left rotations  
* Right rotations  
* Cases where multiple balancing operations are required

Record how these operations prevent the tree from becoming unbalanced.

### **4\. Search Operations**

Perform searches for both existing and non-existing values.

Observe:

* Search path followed within the tree  
* Number of comparisons required  
* Search efficiency due to balanced structure

### **5\. Tree Traversal**

Perform an inorder traversal of the tree.

Verify:

* Elements are displayed in sorted order  
* Node colors are correctly maintained  
* Tree structure satisfies Binary Search Tree ordering

### **6\. Property Verification**

Verify that the Red-Black Tree maintains all required properties after every insertion.

Observe how balancing operations ensure the correctness of the tree structure.

# 📁 Lab 6

# **Lab 6: B-Tree Index Implementation**

## **Objective**

The objective of this lab is to understand and implement a **B-Tree Index**, one of the most widely used indexing structures in database management systems. Students will learn how B-Trees organize data efficiently, support fast searching, and maintain balance during insertion operations.

---

## **Description**

In this experiment, a B-Tree index is implemented to store key-value pairs and support efficient data retrieval. The tree automatically maintains a balanced structure by splitting nodes when they become full, ensuring that search and insertion operations remain efficient even as the dataset grows.

B-Trees are extensively used in database systems, file systems, and storage engines because they minimize disk accesses and provide logarithmic-time operations. Unlike binary search trees, B-Trees can store multiple keys within a single node, reducing tree height and improving performance for large datasets.

The implementation demonstrates how records are inserted into the tree, how nodes are split when capacity limits are exceeded, and how searches are performed by traversing the appropriate branches of the tree.

---

## **Tasks**

### **1\. B-Tree Initialization**

Create a B-Tree with a specified degree and initialize the root node.

Observe:

* Tree structure at initialization  
* Node capacity limits  
* Minimum and maximum number of records per node

---

### **2\. Record Insertion**

Insert multiple key-value pairs into the B-Tree.

Observe:

* Placement of records within nodes  
* Ordering of keys  
* Growth of the tree structure

Analyze how records are organized to maintain sorted order.

---

### **3\. Node Splitting**

When a node becomes full, observe how the B-Tree performs a split operation.

Record:

* The median key selected for promotion  
* Creation of new child nodes  
* Redistribution of records  
* Changes in tree height

Study how splitting maintains balance throughout the tree.

---

### **4\. Search Operations**

Perform searches for existing and non-existing keys.

Observe:

* Traversal path followed  
* Number of node accesses  
* Search efficiency

Verify that the correct record is returned when the key exists.

---

### **5\. Tree Structure Analysis**

Print and analyze the structure of the B-Tree after multiple insertions.

Observe:

* Distribution of keys among nodes  
* Parent-child relationships  
* Tree depth  
* Balanced nature of the tree

---

### **6\. Indexing Behavior**

Analyze how the B-Tree acts as an index structure.

Observe:

* Ordered storage of keys  
* Efficient navigation between nodes  
* Reduced search space at each level

Relate these observations to database indexing concepts.

---

## **B-Tree Properties**

The implementation must maintain the following properties:

1. All keys within a node are stored in sorted order.  
2. Every non-leaf node contains child pointers.  
3. All leaf nodes exist at the same depth.  
4. Nodes contain a limited range of keys based on the tree degree.  
5. The tree remains balanced after insertions.  
6. Search, insertion, and traversal operations are performed efficiently.

