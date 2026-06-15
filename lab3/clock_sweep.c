/*
 * Lab 3: Clock Sweep Cache Replacement Algorithm
 * Student: Talin Daga (24bcs10321)
 *
 * Demonstrates the Clock Sweep page replacement policy used in
 * database buffer managers (e.g., PostgreSQL shared_buffers).
 *
 * Build: gcc -Wall -Wextra -o clock_sweep clock_sweep.c
 * Run:   ./clock_sweep
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define CACHE_SIZE  5
#define DATA_LEN    32
#define LOG_LEN     128

/* ── Data Structures ─────────────────────────────────────────── */

typedef struct {
    int  page_id;
    int  ref_bit;   /* 0 = cold (not recently used), 1 = warm (recently used) */
    bool valid;
    char data[DATA_LEN];
} Frame;

typedef struct {
    Frame frames[CACHE_SIZE];
    int   hand;         /* clock hand index */
    int   hits;
    int   misses;
    int   evictions;
} Cache;

/* ── Utility ─────────────────────────────────────────────────── */

static void sep(const char *title)
{
    printf("\n============================================================\n");
    printf("  %s\n", title);
    printf("============================================================\n");
}

static void log_event(const char *tag, const char *msg)
{
    printf("  [%-10s] %s\n", tag, msg);
}

static void print_state(const Cache *c)
{
    printf("\n  %-6s  %-9s  %-8s  %-6s  %-28s\n",
           "Frame", "Page ID", "Ref Bit", "Valid", "Data");
    printf("  %-6s  %-9s  %-8s  %-6s  %-28s\n",
           "------", "-------", "-------", "-----", "----------------------------");

    for (int i = 0; i < CACHE_SIZE; i++) {
        const Frame *f = &c->frames[i];
        if (f->valid) {
            printf("  %-6d  %-9d  %-8d  %-6s  %-28s",
                   i, f->page_id, f->ref_bit, "YES", f->data);
        } else {
            printf("  %-6d  %-9s  %-8d  %-6s  %-28s",
                   i, "[empty]", 0, "NO", "-");
        }
        if (i == c->hand)
            printf("  <<< clock hand");
        printf("\n");
    }
    printf("\n  Stats:  hits=%-4d  misses=%-4d  evictions=%d\n",
           c->hits, c->misses, c->evictions);
}

/* ── Cache Operations ────────────────────────────────────────── */

void cache_init(Cache *c)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        c->frames[i].page_id = -1;
        c->frames[i].ref_bit = 0;
        c->frames[i].valid   = false;
        strncpy(c->frames[i].data, "-", DATA_LEN - 1);
        c->frames[i].data[DATA_LEN - 1] = '\0';
    }
    c->hand      = 0;
    c->hits      = 0;
    c->misses    = 0;
    c->evictions = 0;
}

/* Returns frame index of page_id if cached, -1 otherwise. */
static int cache_lookup(const Cache *c, int page_id)
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (c->frames[i].valid && c->frames[i].page_id == page_id)
            return i;
    }
    return -1;
}

/*
 * Finds a victim frame for replacement using the clock sweep algorithm.
 * Returns the index of the frame to use (empty or evicted).
 *
 * Rules:
 *   - Prefer any empty frame (no eviction needed).
 *   - Otherwise rotate the clock hand:
 *       ref_bit == 1  ->  clear to 0 (second chance), advance hand
 *       ref_bit == 0  ->  evict this frame, advance hand, return
 */
static int clock_sweep(Cache *c)
{
    char buf[LOG_LEN];

    /* Prefer an empty frame */
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!c->frames[i].valid) {
            snprintf(buf, LOG_LEN,
                     "Empty frame %d found — using it (no eviction needed)", i);
            log_event("SWEEP", buf);
            return i;
        }
    }

    /* Cache full: run clock sweep */
    log_event("SWEEP", "Cache full. Running clock sweep...");

    for (;;) {
        Frame *f = &c->frames[c->hand];

        snprintf(buf, LOG_LEN,
                 "Frame %d  page=%-3d  ref_bit=%d",
                 c->hand, f->page_id, f->ref_bit);
        log_event("HAND", buf);

        if (f->ref_bit == 1) {
            f->ref_bit = 0;
            snprintf(buf, LOG_LEN,
                     "Page %d  ref_bit cleared to 0 (second chance given) — advancing hand",
                     f->page_id);
            log_event("2ND CHANCE", buf);
            c->hand = (c->hand + 1) % CACHE_SIZE;
        } else {
            /* ref_bit == 0: evict */
            int victim = c->hand;
            snprintf(buf, LOG_LEN,
                     "Page %d evicted from frame %d  (ref_bit=0, cold page)",
                     f->page_id, victim);
            log_event("EVICT", buf);
            c->evictions++;
            c->hand = (c->hand + 1) % CACHE_SIZE;
            return victim;
        }
    }
}

/*
 * Access page_id.
 *   Cache hit  : set ref_bit = 1.
 *   Cache miss : run clock_sweep, load page into chosen frame.
 *
 * Newly loaded pages start with ref_bit = 0.
 * A page must be *accessed* (hit) to earn ref_bit = 1 and survive the next sweep.
 */
void cache_access(Cache *c, int page_id, const char *page_data)
{
    char buf[LOG_LEN];

    snprintf(buf, LOG_LEN, "Requesting page %d", page_id);
    log_event("ACCESS", buf);

    int idx = cache_lookup(c, page_id);

    if (idx != -1) {
        /* ── HIT ── */
        c->frames[idx].ref_bit = 1;
        c->hits++;
        snprintf(buf, LOG_LEN,
                 "HIT  — page %d found in frame %d  (ref_bit set to 1)",
                 page_id, idx);
        log_event("RESULT", buf);
        return;
    }

    /* ── MISS ── */
    c->misses++;
    snprintf(buf, LOG_LEN, "MISS — page %d not in cache", page_id);
    log_event("RESULT", buf);

    int target  = clock_sweep(c);
    Frame *f    = &c->frames[target];

    f->page_id  = page_id;
    f->ref_bit  = 0;    /* ref_bit starts cold; must be accessed to become warm */
    f->valid    = true;
    snprintf(f->data, DATA_LEN, "%.31s", page_data ? page_data : "data");

    snprintf(buf, LOG_LEN,
             "Page %d loaded into frame %d  (ref_bit=0)", page_id, target);
    log_event("LOAD", buf);
}

/* ── Main: Six Tasks ─────────────────────────────────────────── */

int main(void)
{
    Cache c;
    char  data[DATA_LEN];

    printf("============================================================\n");
    printf("  Lab 3: Clock Sweep Cache Replacement Algorithm\n");
    printf("  Student: Talin Daga (24bcs10321)\n");
    printf("  Cache capacity: %d frames\n", CACHE_SIZE);
    printf("============================================================\n");

    /* ── Task 1: Cache Initialization ─────────────────────────── */
    sep("TASK 1: Cache Initialization");
    cache_init(&c);
    log_event("INIT", "Cache created. All frames empty. Clock hand at frame 0.");
    log_event("INIT", "ref_bit=0 for all frames. hand starts at 0.");
    print_state(&c);


    /* ── Task 2: Cache Population ──────────────────────────────── */
    sep("TASK 2: Cache Population (loading pages 1 – 5)");
    printf("\n");

    for (int pg = 1; pg <= CACHE_SIZE; pg++) {
        snprintf(data, DATA_LEN, "Page_%d_data", pg);
        printf("  --- Load page %d ---\n", pg);
        cache_access(&c, pg, data);
        printf("\n");
    }

    printf("  All %d frames occupied. Cache state:\n", CACHE_SIZE);
    print_state(&c);
    printf("\n  Observation: all pages loaded with ref_bit=0.\n");
    printf("  A page must be *accessed* (hit) to earn ref_bit=1.\n");


    /* ── Task 3: Access Pattern Analysis ──────────────────────── */
    sep("TASK 3: Access Pattern Analysis");
    printf("\n  Access plan:\n");
    printf("    Page 1 -> 3 accesses  (high frequency)\n");
    printf("    Page 3 -> 2 accesses  (medium frequency)\n");
    printf("    Page 5 -> 1 access    (low frequency)\n");
    printf("    Page 2, Page 4 -> NOT accessed (cold)\n\n");

    const int pages[]   = {1, 1, 1, 3, 3, 5};
    const int n_access  = (int)(sizeof(pages) / sizeof(pages[0]));

    for (int i = 0; i < n_access; i++) {
        printf("  --- Access page %d ---\n", pages[i]);
        cache_access(&c, pages[i], NULL);
        printf("\n");
    }

    printf("  Cache state after access pattern:\n");
    print_state(&c);
    printf("\n  Observation:\n");
    printf("    Pages 1, 3, 5 : ref_bit=1  (recently used — will survive first sweep)\n");
    printf("    Pages 2, 4    : ref_bit=0  (cold — candidates for eviction)\n");


    /* ── Task 4: Clock Sweep Observation ──────────────────────── */
    sep("TASK 4: Clock Sweep Observation (insert page 6)");
    printf("\n  Requesting page 6 — cache is full, clock sweep will run.\n");
    printf("  Hand starts at frame 0 (page 1, ref_bit=1).\n\n");

    cache_access(&c, 6, "Page_6_data");

    printf("\n  Cache state after inserting page 6:\n");
    print_state(&c);
    printf("\n  Observation:\n");
    printf("    Frame 0 (page 1): ref_bit=1 -> cleared, page 1 gets a second chance.\n");
    printf("    Frame 1 (page 2): ref_bit=0 -> page 2 EVICTED (cold, no second chance).\n");
    printf("    Page 6 loaded into frame 1 with ref_bit=0.\n");


    /* ── Task 5: Cache Replacement Analysis ───────────────────── */
    sep("TASK 5: Cache Replacement Analysis (pages 7 and 8)");
    printf("\n  Two more pages will cause further evictions.\n\n");

    printf("  --- Insert page 7 ---\n");
    cache_access(&c, 7, "Page_7_data");
    printf("\n  Cache state after page 7:\n");
    print_state(&c);

    printf("\n  --- Insert page 8 ---\n");
    cache_access(&c, 8, "Page_8_data");
    printf("\n  Cache state after page 8:\n");
    print_state(&c);

    printf("\n  Comparison with other replacement policies:\n\n");
    printf("  %-14s  Scans circularly; clears ref_bit before evicting.\n",
           "Clock Sweep:");
    printf("  %-14s  Each page gets at least one second chance.\n",
           "");
    printf("  %-14s  O(n) worst case, O(1) amortised — low overhead.\n\n",
           "");
    printf("  %-14s  Always evicts the page inserted earliest.\n",
           "FIFO:");
    printf("  %-14s  No second-chance; can evict frequently used pages.\n\n",
           "");
    printf("  %-14s  Evicts the page that was least recently *accessed*.\n",
           "LRU:");
    printf("  %-14s  Most accurate but requires updating a timestamp\n",
           "");
    printf("  %-14s  (or LRU stack) on every single cache hit — high overhead.\n\n",
           "");


    /* ── Task 6: Execution Summary and Log ─────────────────────── */
    sep("TASK 6: Execution Summary and Log");
    int total = c.hits + c.misses;
    printf("\n  %-22s  %d\n",  "Total cache accesses:", total);
    printf("  %-22s  %d  (%.1f%%)\n",
           "Cache hits:",    c.hits,
           total ? 100.0 * c.hits / total : 0.0);
    printf("  %-22s  %d  (%.1f%%)\n",
           "Cache misses:",  c.misses,
           total ? 100.0 * c.misses / total : 0.0);
    printf("  %-22s  %d\n",  "Evictions:", c.evictions);

    printf("\n  Final cache state:\n");
    print_state(&c);

    printf("\n  Key takeaways:\n");
    printf("  1. Pages load with ref_bit=0 (cold).\n");
    printf("  2. A cache HIT sets ref_bit=1, protecting the page for one more sweep.\n");
    printf("  3. When the hand encounters ref_bit=1 it clears it (second chance).\n");
    printf("  4. When the hand encounters ref_bit=0 it evicts that page.\n");
    printf("  5. Frequently accessed pages accumulate ref_bit=1 and survive longer.\n");
    printf("  6. Cold pages (no hits since last sweep) are the first to be evicted.\n");

    return 0;
}
