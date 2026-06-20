// Unit tests: HeapBlockWalk bucket classification + top-N tracking.
// pio test -e native_heap_blocks
//
// Tests the pure-data logic behind /api/debug/heap-blocks. The endpoint
// itself (in WebUIPlugin::handleDebugHeapBlocks) wraps heap_caps_walk()
// — the IDF heap-walker function — with a callback that unpacks each
// block's size/used state and calls heap_block_walker_record(). The
// recording function is what's tested here.
//
// What we verify:
//   1. Bucket classification — blocks land in the correct size bucket
//      (the bucket whose upper-bound exceeds the block size)
//   2. Free/used separation — free and used counters are independent
//   3. Top-N tracking — largest N allocated blocks recorded in
//      descending order
//   4. Bucket-bound semantics — the bucket-upper field is EXCLUSIVE
//      (block of size N goes to the FIRST bucket where upper > N)
//   5. SIZE_MAX sentinel — the last bucket catches everything else
//
// These tests are pure data processing — no Heater/Pump/PSM dependencies,
// no thermal model, no mock_arduino state. Just feed synthetic block
// streams into the recorder and assert on the output struct.

#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "display/util/heap_block_walker.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Test 1 — Bucket classification with one block per bucket
// ---------------------------------------------------------------------------
//
// Feed one block at each bucket boundary; verify each lands in the
// correct bucket. Bucket upper bounds are {128, 512, 2048, 8192, 32768,
// SIZE_MAX}. A block of size N belongs to the FIRST bucket whose upper
// strictly exceeds N (i.e., the smallest matching bucket).
static void test_each_bucket_gets_one_block() {
    HeapBlockWalk w;
    // <128
    heap_block_walker_record(w, 64, true);
    // 128-512
    heap_block_walker_record(w, 300, true);
    // 512-2048
    heap_block_walker_record(w, 1024, true);
    // 2048-8192
    heap_block_walker_record(w, 4096, true);
    // 8192-32768
    heap_block_walker_record(w, 16000, true);
    // 32K+
    heap_block_walker_record(w, 64000, true);

    for (size_t i = 0; i < HeapBlockWalk::BUCKETS; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(1, w.usedCount[i], "each bucket must contain exactly one used block");
    }
    // Free counters untouched.
    for (size_t i = 0; i < HeapBlockWalk::BUCKETS; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(0, w.freeCount[i], "no free blocks recorded for used-only stream");
    }
}

// ---------------------------------------------------------------------------
// Test 2 — Exact-boundary semantics
// ---------------------------------------------------------------------------
//
// A block at exactly the upper bound goes to the NEXT bucket (the bound
// is exclusive). Block size 128 lands in bucket 1 (128-512), not bucket 0.
static void test_bucket_upper_is_exclusive() {
    HeapBlockWalk w;
    heap_block_walker_record(w, 127, true);     // bucket 0 (<128)
    heap_block_walker_record(w, 128, true);     // bucket 1 (128-512)
    heap_block_walker_record(w, 511, true);     // bucket 1
    heap_block_walker_record(w, 512, true);     // bucket 2 (512-2048)
    heap_block_walker_record(w, 32767, true);   // bucket 4 (8192-32768)
    heap_block_walker_record(w, 32768, true);   // bucket 5 (32K+)

    TEST_ASSERT_EQUAL_MESSAGE(1, w.usedCount[0], "127 belongs in bucket 0 (<128)");
    TEST_ASSERT_EQUAL_MESSAGE(2, w.usedCount[1], "128 and 511 belong in bucket 1 (128-512)");
    TEST_ASSERT_EQUAL_MESSAGE(1, w.usedCount[2], "512 belongs in bucket 2 (512-2048)");
    TEST_ASSERT_EQUAL_MESSAGE(1, w.usedCount[4], "32767 belongs in bucket 4 (8192-32768)");
    TEST_ASSERT_EQUAL_MESSAGE(1, w.usedCount[5], "32768 belongs in bucket 5 (32K+, exclusive upper)");
}

// ---------------------------------------------------------------------------
// Test 3 — Byte totals accumulate per bucket
// ---------------------------------------------------------------------------
static void test_bytes_accumulate_per_bucket() {
    HeapBlockWalk w;
    // Three blocks in bucket 2 (512-2048): 600 + 700 + 1500 = 2800 bytes
    heap_block_walker_record(w, 600, true);
    heap_block_walker_record(w, 700, true);
    heap_block_walker_record(w, 1500, true);

    TEST_ASSERT_EQUAL_MESSAGE(3, w.usedCount[2], "three used blocks in bucket 2");
    TEST_ASSERT_EQUAL_MESSAGE(2800, w.usedBytes[2], "used bytes must sum across blocks in bucket");
}

// ---------------------------------------------------------------------------
// Test 4 — Free vs used are tracked independently
// ---------------------------------------------------------------------------
//
// Feed mixed free+used at the same bucket; verify the counters don't bleed
// into each other.
static void test_free_and_used_independent() {
    HeapBlockWalk w;
    heap_block_walker_record(w, 1000, false);   // free, bucket 2
    heap_block_walker_record(w, 1000, true);    // used, bucket 2
    heap_block_walker_record(w, 1500, false);   // free, bucket 2

    TEST_ASSERT_EQUAL_MESSAGE(2, w.freeCount[2], "two free blocks in bucket 2");
    TEST_ASSERT_EQUAL_MESSAGE(1, w.usedCount[2], "one used block in bucket 2");
    TEST_ASSERT_EQUAL_MESSAGE(2500, w.freeBytes[2], "free bytes only sum free blocks");
    TEST_ASSERT_EQUAL_MESSAGE(1000, w.usedBytes[2], "used bytes only sum used blocks");
}

// ---------------------------------------------------------------------------
// Test 5 — Top-N: largest N allocated blocks recorded in descending order
// ---------------------------------------------------------------------------
//
// The top-N captures the N largest USED blocks (free blocks not eligible).
// Feed a known size distribution including duplicates and verify the
// resulting topAllocSizes array.
static void test_top_n_largest_in_descending_order() {
    HeapBlockWalk w;
    // Submit blocks in random order
    const size_t sizes[] = {100, 50000, 200, 35000, 10, 40000, 999, 30000, 25000, 20000, 15000, 5000};
    for (size_t s : sizes) {
        heap_block_walker_record(w, s, true);
    }

    // Top 8 used sizes from above: 50000, 40000, 35000, 30000, 25000,
    // 20000, 15000, 5000  (sorted desc).
    const size_t expected[HeapBlockWalk::TOPN] = {50000, 40000, 35000, 30000, 25000, 20000, 15000, 5000};
    for (size_t i = 0; i < HeapBlockWalk::TOPN; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(expected[i], w.topAllocSizes[i],
                                  "top-N entries must be in descending order");
    }
}

// ---------------------------------------------------------------------------
// Test 6 — Free blocks excluded from top-N
// ---------------------------------------------------------------------------
//
// Top-N tracks allocations, not free regions. A 100KB free block must NOT
// show up in topAllocSizes.
static void test_top_n_excludes_free_blocks() {
    HeapBlockWalk w;
    heap_block_walker_record(w, 100000, false); // huge free region
    heap_block_walker_record(w, 500, true);
    heap_block_walker_record(w, 200, true);

    // Only the two used blocks should make it into topAllocSizes.
    TEST_ASSERT_EQUAL_MESSAGE(500, w.topAllocSizes[0], "largest used block (500) at index 0");
    TEST_ASSERT_EQUAL_MESSAGE(200, w.topAllocSizes[1], "second-largest used block (200) at index 1");
    TEST_ASSERT_EQUAL_MESSAGE(0, w.topAllocSizes[2], "no third entry — sentinel 0 marks end");
}

// ---------------------------------------------------------------------------
// Test 7 — Top-N saturation: only the largest N survive when given >N inputs
// ---------------------------------------------------------------------------
//
// When 20 blocks are fed, only the 8 largest survive in topAllocSizes.
// The smaller 12 must be dropped.
static void test_top_n_drops_small_when_full() {
    HeapBlockWalk w;
    // Feed sizes 1..20 — top 8 should be 20..13
    for (size_t i = 1; i <= 20; ++i) {
        heap_block_walker_record(w, i * 100, true); // 100..2000
    }
    const size_t expected[HeapBlockWalk::TOPN] = {2000, 1900, 1800, 1700, 1600, 1500, 1400, 1300};
    for (size_t i = 0; i < HeapBlockWalk::TOPN; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(expected[i], w.topAllocSizes[i],
                                  "only the 8 largest survive in topAllocSizes");
    }
}

// ---------------------------------------------------------------------------
// Test 8 — Empty walk yields zero-initialized state
// ---------------------------------------------------------------------------
//
// Defensive: a walk that visits no blocks must leave the struct in its
// default zero-initialized state. (Default-initializes already test this
// at construction, but verifying after a no-op walk catches accidental
// non-trivial defaults.)
static void test_empty_walk_zero_state() {
    HeapBlockWalk w;

    for (size_t i = 0; i < HeapBlockWalk::BUCKETS; ++i) {
        TEST_ASSERT_EQUAL(0, w.freeCount[i]);
        TEST_ASSERT_EQUAL(0, w.usedCount[i]);
        TEST_ASSERT_EQUAL(0, w.freeBytes[i]);
        TEST_ASSERT_EQUAL(0, w.usedBytes[i]);
    }
    for (size_t i = 0; i < HeapBlockWalk::TOPN; ++i) {
        TEST_ASSERT_EQUAL(0, w.topAllocSizes[i]);
    }
}

// ---------------------------------------------------------------------------
// Test 9 — Realistic gaggimate distribution (smoke test)
// ---------------------------------------------------------------------------
//
// Approximate the shape of the heap we observe live: lots of small ArduinoJson
// nodes (<128), some mid-sized buffers, a couple of large allocations.
// Verifies the aggregated histogram has a sane shape.
static void test_realistic_distribution_smoke() {
    HeapBlockWalk w;
    // 100 tiny ArduinoJson nodes (~32-64 B each)
    for (int i = 0; i < 100; ++i) {
        heap_block_walker_record(w, 48, true);
    }
    // 20 small string buffers (~200 B)
    for (int i = 0; i < 20; ++i) {
        heap_block_walker_record(w, 200, true);
    }
    // 5 ws/http buffers (~4KB)
    for (int i = 0; i < 5; ++i) {
        heap_block_walker_record(w, 4096, true);
    }
    // 1 profiles:list response (~35KB)
    heap_block_walker_record(w, 35000, true);
    // 1 large free hole between allocations (~50KB)
    heap_block_walker_record(w, 50000, false);

    TEST_ASSERT_EQUAL(100, w.usedCount[0]);             // <128
    TEST_ASSERT_EQUAL(20, w.usedCount[1]);              // 128-512
    TEST_ASSERT_EQUAL(5, w.usedCount[3]);               // 2-8K
    TEST_ASSERT_EQUAL(1, w.usedCount[5]);               // 32K+ (profiles)
    TEST_ASSERT_EQUAL(1, w.freeCount[5]);               // 32K+ (free hole)
    TEST_ASSERT_EQUAL(35000, w.usedBytes[5]);
    TEST_ASSERT_EQUAL(50000, w.freeBytes[5]);
    TEST_ASSERT_EQUAL(35000, w.topAllocSizes[0]);       // 35K is largest used
    TEST_ASSERT_EQUAL(4096, w.topAllocSizes[1]);        // next-largest is the 4K buffer
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_each_bucket_gets_one_block);
    RUN_TEST(test_bucket_upper_is_exclusive);
    RUN_TEST(test_bytes_accumulate_per_bucket);
    RUN_TEST(test_free_and_used_independent);
    RUN_TEST(test_top_n_largest_in_descending_order);
    RUN_TEST(test_top_n_excludes_free_blocks);
    RUN_TEST(test_top_n_drops_small_when_full);
    RUN_TEST(test_empty_walk_zero_state);
    RUN_TEST(test_realistic_distribution_smoke);
    return UNITY_END();
}
