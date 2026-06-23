#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlite3.h"

#define PAGE_SIZE 4096

/* ── helpers ──────────────────────────────────────────────────────────────── */

static uint16_t read_u16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

/* decode a SQLite varint; returns bytes consumed */
static int read_varint(const uint8_t *buf, int pos, uint64_t *out) {
    uint64_t val = 0; int shift = 0, start = pos;
    do { val |= (uint64_t)(buf[pos] & 0x7F) << shift; shift += 7; } while (buf[pos++] & 0x80);
    *out = val; return pos - start;
}

static const char *page_type_name(uint8_t t) {
    switch (t) {
        case 0x02: return "Interior Index B-Tree";
        case 0x05: return "Interior Table B-Tree";
        case 0x0A: return "Leaf Index B-Tree";
        case 0x0D: return "Leaf Table B-Tree";
        default:   return "Unknown";
    }
}

/* ── hex dump ─────────────────────────────────────────────────────────────── */

static void hex_dump(const uint8_t *data, size_t len, FILE *out) {
    for (size_t i = 0; i < len; i += 16) {
        if (i % PAGE_SIZE == 0)
            fprintf(out, "\n=== PAGE %zu (offset 0x%zx) ===\n", i/PAGE_SIZE+1, i);
        fprintf(out, "%08zx: ", i);
        for (int j = 0; j < 16; j++) {
            if (i+j < len) fprintf(out, "%02x ", data[i+j]);
            else           fprintf(out, "   ");
        }
        fprintf(out, " ");
        for (int j = 0; j < 16 && i+j < len; j++) {
            uint8_t c = data[i+j];
            fprintf(out, "%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        fprintf(out, "\n");
    }
}

/* ── B-tree page analysis ─────────────────────────────────────────────────── */

static void analyze_page(const uint8_t *data, int page_no, const char *label) {
    const uint8_t *pg = data + (page_no - 1) * PAGE_SIZE;
    int base = (page_no == 1) ? 100 : 0;   /* page 1 has 100-byte file header */

    uint8_t  ptype   = pg[base];
    uint16_t n_cells = read_u16(pg + base + 3);
    uint16_t content = read_u16(pg + base + 5);

    printf("\n=== PAGE %d — %s ===\n", page_no, label);
    printf("  Type         : 0x%02X (%s)\n", ptype, page_type_name(ptype));
    printf("  Cell count   : %u\n", n_cells);
    printf("  Content start: %u\n", content);

    int ptr_base = base + 8;
    printf("  Cell pointers:\n");
    for (int i = 0; i < n_cells && i < 5; i++) {
        uint16_t ptr = read_u16(pg + ptr_base + i * 2);
        printf("    [%2d] → 0x%04X\n", i+1, ptr);
    }
    if (n_cells > 5) printf("    ... (%d more)\n", n_cells - 5);

    /* decode first row for table leaf pages */
    if (ptype == 0x0D && n_cells > 0) {
        uint16_t ptr = read_u16(pg + ptr_base);
        int pos = ptr;
        uint64_t payload_len, rowid, hdr_len;
        pos += read_varint(pg, pos, &payload_len);
        pos += read_varint(pg, pos, &rowid);
        int hdr_start = pos;
        pos += read_varint(pg, pos, &hdr_len);
        int hdr_end = hdr_start + (int)hdr_len;

        /* collect serial types */
        uint64_t stypes[16]; int ns = 0;
        while (pos < hdr_end && ns < 16)
            pos += read_varint(pg, pos, &stypes[ns++]);

        /* first text column (serial >= 13, odd) */
        int data_pos = hdr_end;
        for (int i = 0; i < ns; i++) {
            if (stypes[i] >= 13 && stypes[i] % 2 == 1) {
                int tlen = (int)(stypes[i] - 13) / 2;
                printf("  First row    : rowid=%llu, name=\"%.*s\"\n",
                       (unsigned long long)rowid, tlen, pg + data_pos);
                break;
            }
            /* skip over this column's data */
            uint64_t st = stypes[i];
            int skip = (st == 0) ? 0 : (st <= 4) ? (int)st :
                       (st == 5) ? 6 : (st == 6 || st == 7) ? 8 :
                       (st >= 12) ? (int)(st - 12) / 2 : 0;
            data_pos += skip;
        }
    }
}

/* ── file header ──────────────────────────────────────────────────────────── */

static void print_file_header(const uint8_t *data) {
    printf("=== FILE HEADER ===\n");
    printf("  Magic        : %.15s\n", data);
    printf("  Page size    : %u bytes\n", read_u16(data + 0x10));
    printf("  Total pages  : %u\n",      read_u32(data + 0x1C));
    uint32_t enc = read_u32(data + 0x38);
    const char *enc_names[] = {"", "UTF-8", "UTF-16LE", "UTF-16BE"};
    printf("  Encoding     : %s\n", enc <= 3 ? enc_names[enc] : "?");
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    /* 1. create database */
    sqlite3 *db;
    sqlite3_open("lab4_btree.db", &db);
    sqlite3_exec(db,
        "CREATE TABLE students ("
        "  id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER, gpa REAL);"
        "CREATE TABLE enrollments ("
        "  id INTEGER PRIMARY KEY, student_id INTEGER NOT NULL,"
        "  course TEXT NOT NULL, grade TEXT,"
        "  FOREIGN KEY(student_id) REFERENCES students(id));"
        "CREATE INDEX idx_enrollments_student ON enrollments(student_id);",
        NULL, NULL, NULL);

    const char *names[] = {"Alice","Bob","Charlie","Diana","Eve","Frank",
                           "Grace","Heidi","Ivan","Judy","Karl","Liam",
                           "Mona","Nick","Olivia"};
    int ages[] = {20,21,22,20,19,21,22,20,24,21,22,23,20,21,22};
    double gpas[] = {3.9,3.0,3.8,3.7,3.6,3.2,3.8,3.7,2.2,3.9,3.0,3.0,3.7,3.1,3.7};

    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "INSERT INTO students VALUES(?,?,?,?)", -1, &st, NULL);
    for (int i = 0; i < 15; i++) {
        sqlite3_bind_int(st, 1, i+1);
        sqlite3_bind_text(st, 2, names[i], -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 3, ages[i]);
        sqlite3_bind_double(st, 4, gpas[i]);
        sqlite3_step(st); sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    const char *courses[] = {"CS101","CS201","CS301","MATH101","MATH201","PHY101"};
    const char *grades[]  = {"A","A-","B+","B","B-","C+"};
    sqlite3_prepare_v2(db,
        "INSERT INTO enrollments(student_id,course,grade) VALUES(?,?,?)",
        -1, &st, NULL);
    for (int s = 1; s <= 15; s++)
        for (int c = 0; c < 6; c++) {
            sqlite3_bind_int(st, 1, s);
            sqlite3_bind_text(st, 2, courses[c], -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, grades[c],  -1, SQLITE_STATIC);
            sqlite3_step(st); sqlite3_reset(st);
        }
    sqlite3_finalize(st);
    sqlite3_close(db);
    printf("Database created: lab4_btree.db\n");

    /* 2. read raw bytes */
    FILE *f = fopen("lab4_btree.db", "rb");
    fseek(f, 0, SEEK_END); long fsize = ftell(f); rewind(f);
    uint8_t *data = (uint8_t *)malloc(fsize);
    fread(data, 1, fsize, f); fclose(f);
    printf("File size: %ld bytes (%ld pages)\n\n", fsize, fsize/PAGE_SIZE);

    /* 3. hex dump to file */
    FILE *hf = fopen("hexdump.txt", "w");
    hex_dump(data, fsize, hf); fclose(hf);
    printf("Hex dump written to hexdump.txt\n");

    /* 4. analysis */
    print_file_header(data);
    analyze_page(data, 1, "sqlite_master (schema)");
    analyze_page(data, 2, "students table");
    analyze_page(data, 3, "enrollments table");
    analyze_page(data, 4, "idx_enrollments_student (index)");

    free(data);
    return 0;
}
