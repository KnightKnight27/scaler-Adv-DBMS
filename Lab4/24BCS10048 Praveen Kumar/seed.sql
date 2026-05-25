-- seed.sql
-- Creates and populates the books database used for Lab 4 hex dump analysis.
-- 20 rows with a padded synopsis column to force multi-page B-tree splits.

PRAGMA page_size = 4096;

CREATE TABLE books (
    id       INTEGER PRIMARY KEY,
    title    TEXT NOT NULL,
    author   TEXT NOT NULL,
    year     INTEGER,
    synopsis TEXT
);

INSERT INTO books VALUES (1,  'The C Programming Language',      'Kernighan & Ritchie', 1978,
    'A concise introduction to C covering the core language and standard library. The book walks through types, operators, control flow, functions, pointers, arrays, structures, and the I/O library. Each chapter closes with exercises drawn from real problems encountered building early Unix utilities. The prose is terse by design -- written by the same people who designed the language and the OS it runs on.');

INSERT INTO books VALUES (2,  'Structure and Interpretation of Computer Programs', 'Abelson & Sussman', 1985,
    'Introduces computation as a discipline of managing complexity. Uses Scheme to build interpreters, compilers, and evaluators from scratch, covering recursion, higher-order functions, streams, concurrency, and register machines. Widely known as SICP. Every chapter builds on the last, leading to a complete metacircular evaluator and then a compiler targeting a simulated register machine.');

INSERT INTO books VALUES (3,  'The Linux Programming Interface', 'Michael Kerrisk', 2010,
    'Definitive reference for Linux and UNIX system programming. Covers file I/O, processes, signals, threads, sockets, IPC, and virtual memory in exhaustive detail. Over 1500 pages. Each chapter includes working code examples that compile and run on a standard Linux installation. Kerrisk is the primary maintainer of the Linux man-pages project, and the accuracy of the syscall descriptions reflects that.');

INSERT INTO books VALUES (4,  'Computer Systems: A Programmers Perspective', 'Bryant & OHallaron', 2003,
    'Teaches computer systems from the programmers viewpoint: machine-level code, processor architecture, the memory hierarchy, linking, exceptional control flow, virtual memory, I/O, and networking. Known as CS:APP. The labs are particularly valued -- the bomb lab, the attack lab, and the malloc lab give students hands-on experience with the topics covered in lecture.');

INSERT INTO books VALUES (5,  'Database Internals', 'Alex Petrov', 2019,
    'Explores how database management systems are built: storage engines, B-tree and LSM-tree layouts, WAL, transactions, MVCC, and distributed consensus. Heavy on data structure internals and file-format details. Complements the Ramakrishnan textbook by focusing on implementation rather than query semantics. The storage chapter alone is worth the price of the book for anyone building or debugging a storage engine.');

INSERT INTO books VALUES (6,  'Designing Data-Intensive Applications', 'Martin Kleppmann', 2017,
    'Surveys the landscape of modern data systems: relational databases, document stores, graph databases, stream processing, and batch processing. Focuses on reliability, scalability, and maintainability. Covers encoding, replication, partitioning, transactions, and consistency models. Written for the practitioner who needs to choose between systems rather than the researcher building one from scratch.');

INSERT INTO books VALUES (7,  'Operating Systems: Three Easy Pieces', 'Arpaci-Dusseau', 2015,
    'Free textbook covering the three central themes of OS design: virtualization, concurrency, and persistence. The tone is conversational and the explanations build intuition before formalism. Widely used in undergraduate OS courses. The concurrency chapters in particular do a good job of explaining why locks are hard and why you should be suspicious of your own lock-free code.');

INSERT INTO books VALUES (8,  'The Art of Computer Programming, Vol 1', 'Donald Knuth', 1968,
    'Volume 1 of Knuths multivolume magnum opus covers fundamental algorithms and mathematical preliminaries: basic concepts, MIX assembly language, information structures, and sorting and searching foundations. The level of mathematical rigour is unusual for a computer science text. Many algorithms that appear in modern textbooks were first described and analysed here.');

INSERT INTO books VALUES (9,  'Clean Code', 'Robert C. Martin', 2008,
    'Argues that code should be readable first and correct second, presenting a set of principles for writing maintainable software: meaningful names, small functions, no side effects, and comprehensive tests. Some advice has aged poorly and sparked ongoing debate in the community, but the core argument -- that clarity is a professional obligation -- remains broadly accepted.');

INSERT INTO books VALUES (10, 'Introduction to Algorithms', 'CLRS', 2009,
    'The standard undergraduate algorithms textbook. Covers sorting, data structures, graph algorithms, dynamic programming, greedy algorithms, and NP-completeness. Pseudocode is used throughout, making the algorithms language-independent. The chapter on red-black trees is particularly careful, covering all insert and delete rotation cases with full proofs of correctness.');

INSERT INTO books VALUES (11, 'The Pragmatic Programmer', 'Hunt & Thomas', 1999,
    'A collection of practical advice for working software developers covering topics like version control, debugging, automation, and communication. Written as a series of tips rather than a linear narrative. The broken windows theory of software quality and the DRY (Do Not Repeat Yourself) principle both became widely adopted from this book.');

INSERT INTO books VALUES (12, 'Modern Operating Systems', 'Andrew Tanenbaum', 1992,
    'Comprehensive operating systems textbook covering processes, threads, memory management, file systems, I/O, deadlocks, and distributed systems. Tanenbaum also wrote Minix, which was the codebase Linus Torvalds used as a reference when starting the Linux kernel. The historical debates between Tanenbaum and Torvalds about kernel architecture are archived and occasionally still cited.');

INSERT INTO books VALUES (13, 'TCP/IP Illustrated, Vol 1', 'W. Richard Stevens', 1994,
    'Walks through every major protocol in the TCP/IP stack with packet traces captured on real networks. Covers ARP, IP, ICMP, UDP, TCP, DNS, and application protocols. The explanations are driven by actual wire traffic rather than abstract diagrams. Considered essential reading for anyone doing serious network programming or debugging.');

INSERT INTO books VALUES (14, 'Unix Network Programming, Vol 1', 'W. Richard Stevens', 1990,
    'The definitive guide to socket programming on Unix systems. Covers the socket API, TCP and UDP clients and servers, I/O multiplexing with select and poll, non-blocking I/O, routing sockets, and advanced topics like broadcasting and multicasting. Code examples are production-quality and have been copied into real systems.');

INSERT INTO books VALUES (15, 'Advanced Programming in the Unix Environment', 'W. Richard Stevens', 1992,
    'System-level programming in C on Unix: file I/O, standard I/O library, processes, signals, threads, daemon processes, and IPC. The third edition updated the examples to POSIX threads. Every topic is explained with code you can compile and run. Often cited alongside The Linux Programming Interface as the two books every systems programmer should own.');

INSERT INTO books VALUES (16, 'Computer Networks', 'Andrew Tanenbaum', 1981,
    'Bottom-up tour of computer networks from the physical layer through the application layer. Covers error detection, MAC protocols, routing, congestion control, and the web. The layered model used throughout the book influenced how networking is taught and how standards bodies structure their specifications.');

INSERT INTO books VALUES (17, 'Compilers: Principles, Techniques, and Tools', 'Aho, Lam, Sethi, Ullman', 1986,
    'The Dragon Book. Covers lexical analysis, parsing, semantic analysis, intermediate code generation, optimization, and code generation. The chapter on parsing is particularly thorough, covering LL, LR, LALR, and GLR grammars with proofs of the parsing table construction algorithms. The cover image of a knight fighting a dragon became iconic in the field.');

INSERT INTO books VALUES (18, 'The Mythical Man-Month', 'Frederick Brooks', 1975,
    'Essays on software engineering drawing on Brooks experience managing the OS/360 project at IBM. The central insight -- that adding people to a late project makes it later -- became known as Brooks Law. Other essays cover conceptual integrity, the surgical team model of software development, and why there is no silver bullet for the inherent complexity of building large software systems.');

INSERT INTO books VALUES (19, 'Refactoring', 'Martin Fowler', 1999,
    'Catalogues over seventy refactoring patterns: local variable elimination, method extraction, class splitting, and hierarchy restructuring. The first two chapters make the case for refactoring as a disciplined practice rather than random cleanup. The catalog itself is a reference that practitioners can use to name and justify changes when doing code review or discussing a pull request.');

INSERT INTO books VALUES (20, 'Site Reliability Engineering', 'Beyer, Jones, Petoff, Murphy', 2016,
    'Documents how Google runs its production systems: error budgets, SLOs, blameless postmortems, toil reduction, capacity planning, and the on-call rotation model. Freely available online. Influenced the SRE discipline more broadly and led to the adoption of error budgets and postmortem culture at many companies outside Google.');
