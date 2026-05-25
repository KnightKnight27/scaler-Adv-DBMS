# Lab 4 & Lab 5 Assignment Submission

This pull request contains the implementation and analysis for Lab 4 and Lab 5.

## Lab 4: SQLite Hex & B-Tree Analysis

- Analyzed the internal SQLite file format using hex dumps to study page structures, headers, cell pointer arrays, and B-Tree indexing organization.
- Detailed page allocations, schema definitions, and table/index leaf cell structure.

## Lab 5: Simplified B-Tree and Red-Black Tree Implementation

- Implemented a complete B-Tree structure in C++ supporting dynamic minimum degrees (t) with dynamic insertion slot searching via a sliding range search.
- Provided a robust, highly commented Red-Black Tree (RBT) codebase detailing rotation mechanics and the double-red insertion fixup cases.
- Included an interactive CLI suite and CMake environment for easy indexing exploration and structure integrity auditing.