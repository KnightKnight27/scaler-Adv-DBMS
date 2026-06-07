// Clock Sweep Algorithm - Buffer Pool Page Replacement
// Patel Jash | 24BCS10632 | Batch B

void accessPage(int target_page_id) {
    // ===========================================
    // STEP 1: Check if page already exists in pool
    // ===========================================
    if (page_table.find(target_page_id) != page_table.end()) {
        // Page found in hash map — retrieve the frame index
        int physical_index = page_table[target_page_id];

        // Mark reference bit as 1 so it gets another chance
        metadata_array[physical_index].ref_bit = true;

        // Cache hit — no need to move the clock pointer
        return;
    }


    // ===========================================
    // STEP 2: Page not in pool — run clock sweep
    // ===========================================
    // Keep looping until we find a suitable frame for the incoming page

    while (true) {

        // Grab a reference to the current frame the hand is pointing at
        FrameDescriptor& current_frame = metadata_array[clock_hand];

        // ------------------------------------------------
        // Case A: Frame is unused (no page loaded yet)
        // ------------------------------------------------
        if (current_frame.page_id == -1) {
            current_frame.page_id = target_page_id;

            current_frame.ref_bit = true;

            page_table[target_page_id] = clock_hand;

            clock_hand = (clock_hand + 1) % capacity;


            break;
        }

        // ------------------------------------------------
        // Case B: Frame occupied but has second chance (ref_bit is 1)
        // ------------------------------------------------
        else if (current_frame.ref_bit == true) {
            // Clear the ref bit and skip to next frame
            current_frame.ref_bit = false;

            clock_hand = (clock_hand + 1) % capacity;

        }

        // ------------------------------------------------
        // Case C: Frame occupied, no second chance (ref_bit is 0) — evict it
        // ------------------------------------------------
        else if (current_frame.ref_bit == false) {
            // Remove old page mapping from the table
            page_table.erase(current_frame.page_id);

            // Load the new page into this frame
            current_frame.page_id = target_page_id;

            current_frame.ref_bit = true;

            page_table[target_page_id] = clock_hand;


            clock_hand = (clock_hand + 1) % capacity;
            break;
        }
    }
}