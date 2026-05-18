# Lab 2 Report: Comparison between SQLite3 and PostgreSQL

## Objective
Compare and contrast SQLite3 and PostgreSQL database management systems based on various parameters.

## 1. Architecture

### SQLite3
- Serverless: Embedded directly into the application
- Zero-configuration: No server setup required
- Single-file database: Entire database stored in one file
- Library-based: Linked directly into the application

### PostgreSQL
- Client-Server architecture
- Requires a running server process
- Multi-process architecture (postmaster)
- Supports remote connections over TCP/IP

## 2. Data Types

### SQLite3
- Typeless at storage level
- Uses type affinity system
- Supports 5 types: TEXT, NUMERIC, INTEGER, REAL, BLOB
- Limited built-in types

### PostgreSQL
- Rich type system with many built-in types
- Supports: numeric, character, binary, boolean, date/time, geometric, network, JSON, XML, arrays
- User-defined types support
- Custom types and domains

## 3. Concurrency

### SQLite3
- Uses database-level locking
- Writer lock blocks all readers
- Limited concurrent write support
- WAL mode improves read concurrency

### PostgreSQL
- MVCC (Multi-Version Concurrency Control)
- Row-level locking
- Supports true parallel writes
- Better for high-concurrency scenarios

## 4. Performance

### SQLite3
- Excellent for read-heavy workloads
- Low overhead, fast for small datasets
- No network latency
- Limited by single-threaded writes

### PostgreSQL
- Optimized for complex queries
- Better for large datasets
- Supports indexing strategies
- Query planning and optimization

## 5. Scalability

### SQLite3
- Best for small to medium applications
- Not suitable for high-traffic websites
- Single-user single-process scenarios
- Limited to single machine

### PostgreSQL
- Scales horizontally and vertically
- Can handle millions of records
- Supports partitioning
- Replication and clustering support

## 6. Use Cases

### SQLite3 Best For:
- Mobile applications
- Embedded systems
- Small websites
- Testing and development
- Desktop applications

### PostgreSQL Best For:
- Enterprise applications
- Data warehousing
- Complex analytical queries
- Web applications with high traffic
- GIS applications (PostGIS)

## 7. ACID Compliance

### SQLite3
- Fully ACID compliant for single database
- Atomic transactions at file level
- Journal-based transaction logging

### PostgreSQL
- Fully ACID compliant
- MVCC provides consistency
- Advanced transaction isolation levels
- Point-in-time recovery support

## 8. Security

### SQLite3
- File-level permissions
- No built-in user authentication
- No network security features

### PostgreSQL
- Role-based access control
- Row-level security
- Column-level security
- SSL encryption support
- Authentication methods (MD5, SCRAM, certificate)

## Conclusion

SQLite3 is ideal for applications requiring simplicity, portability, and lightweight operations. PostgreSQL is the choice for enterprise applications needing advanced features, scalability, and robust concurrency control.