// Clock Sweep Page Replacement Algorithm
// Marks recently accessed pages and evicts the first page
// that does not receive a second chance.

void requestPage(int pageId) {

    // =====================================
    // PAGE HIT
    // =====================================
    auto entry = page_table.find(pageId);

    if (entry != page_table.end()) {

        int frameIndex = entry->second;

        // Mark page as recently used
        metadata_array[frameIndex].ref_bit = true;

        return;
    }

    // =====================================
    // PAGE MISS
    // Search for a frame using Clock Sweep
    // =====================================

    while (true) {

        FrameDescriptor &frame = metadata_array[clock_hand];

        // ---------------------------------
        // CASE 1: Free frame available
        // ---------------------------------
        if (frame.page_id == -1) {

            frame.page_id = pageId;
            frame.ref_bit = true;

            page_table[pageId] = clock_hand;

            clock_hand = (clock_hand + 1) % capacity;

            return;
        }

        // ---------------------------------
        // CASE 2: Page gets a second chance
        // ---------------------------------
        if (frame.ref_bit) {

            frame.ref_bit = false;

            clock_hand = (clock_hand + 1) % capacity;

            continue;
        }

        // ---------------------------------
        // CASE 3: Replace victim page
        // ---------------------------------
        page_table.erase(frame.page_id);

        frame.page_id = pageId;
        frame.ref_bit = true;

        page_table[pageId] = clock_hand;

        clock_hand = (clock_hand + 1) % capacity;

        return;
    }
}