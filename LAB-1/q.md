Alright — here’s your **complete A→Z assignment document** based on the transcript + notes, structured exactly like a proper submission you can use directly (and even copy as README).

---

# 📄 **Advanced DBMS – Lab Assignment 1**

## **Topic: File I/O, System Calls & Storage Journey**

---

# 🧭 1. Objective

The goal of this assignment is to:

* Understand how **data is stored and retrieved**
* Explore the **full journey of file operations**
* Learn how **applications interact with OS and disk**
* Build intuition for **database storage internals**

As discussed in class, a database fundamentally:

> **stores and retrieves data** 

---

# 🧪 2. Problem Statement

You are required to:

### ✅ Write a program (Java / C++ / C)

That performs:

* File **open**
* File **read**
* File **write**

---

# ⚙️ 3. Constraints (VERY IMPORTANT)

* ❌ No high-level libraries
* ✅ Prefer **raw system calls / low-level APIs**
* ❌ Do NOT rely on abstractions without understanding
* ✅ Must demonstrate **internal working knowledge**

From instructions:

> “Do it with raw system calls… no libraries… understand everything.” 

---

# 💻 4. Implementation Requirements

## If using Java (your case)

Use:

* `FileInputStream`
* `FileOutputStream`

Avoid:

* Scanner
* BufferedReader (unless explained internally)

---

## Sample Flow (what your program should do)

1. Open file
2. Write some data
3. Read data
4. Print output

---

# 🔍 5. Mandatory Analysis (CORE OF ASSIGNMENT)

You must **analyze and document the full journey**:

## Flow to explain:

```
User Program → JVM → OS (syscall) → Kernel → Disk → RAM → Back
```

---

# 🧠 6. Concepts You MUST Cover

## 🔹 OS + File System Concepts

* File Descriptor
* System Calls (`open`, `read`, `write`)
* `strace`
* inode

---

## 🔹 Storage Concepts

* RAM vs Disk
* Volatile vs Persistent storage 
* Buffered writes
* Blocks & Pages

---

## 🔹 Hardware Concepts

* Disk drivers
* DMA (Direct Memory Access)
* Why CPU is bypassed

---

## 🔹 DB Context (from notes)

* Storage engine basics
* Why disk is needed (persistence)
* Why RAM is faster (~ns vs ms)

---

# 🧰 7. strace Requirement (MANDATORY)

Run your program using:

```bash
strace -f java YourProgram
```

### You must:

* Capture syscalls:

  * `open`
  * `read`
  * `write`
* Explain what each does

---

# 📝 8. Documentation (README)

You must submit a **README file** containing:

## Sections:

### 1. Introduction

* What your program does

### 2. Code Explanation

* How file operations are implemented

### 3. System Call Analysis

* Output from `strace`
* Explanation of syscalls

### 4. Internal Flow

Explain:

* Java → JVM → OS → Disk

### 5. Concepts Explained

* File descriptor
* inode
* DMA
* buffering
* pages / blocks

### 6. Learnings

* What you understood

---

# 📦 9. Submission Format

* Code file
* README.md
* (Optional) strace output file

👉 Submission via GitHub repo (to be shared later) 

---

# ⏳ 10. Deadline

* **Till Sunday**

> “Do it till Sunday… make sure you understand everything.” 

---

# 🤖 11. LLM / AI Usage Policy

* ✅ Allowed for:

  * Code generation
  * Concept understanding

* ❗ BUT:

  * You must **understand everything**
  * You must **document in your own words**

From instructor:

> “Use LLM… but understand everything.” 

---

# 🚫 12. Common Mistakes (Avoid These)

* Only writing code (NO explanation)
* Using high-level libraries blindly
* Not using `strace`
* Copy-pasting explanations
* Ignoring OS concepts

---

# 🎯 13. Evaluation Criteria

Based on notes: 

| Criteria      | Weight |
| ------------- | ------ |
| Understanding | High   |
| Correctness   | ~50%   |
| Performance   | ~20%   |
| Code Quality  | ~20%   |

---

# 🧠 14. What This Assignment Builds

This is foundation for:

* Storage Engine
* Buffer Pool
* B+ Trees
* WAL (Write Ahead Logging)
* Database internals

---

# 🏁 Final Checklist

Before submitting:

* [ ] Code works
* [ ] Used low-level file operations
* [ ] Ran `strace`
* [ ] Explained syscalls
* [ ] Covered all required concepts
* [ ] README complete
* [ ] Understood everything

---
