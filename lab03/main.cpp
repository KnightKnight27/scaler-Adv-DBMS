//basic implementation of clocksweep, bss boilerplate to roll on....!

void accessPage(int target_page_id) {
    // ==========================================
    // PHASE 1: THE CACHE HIT (Lock-Free Read)
    // ==========================================
    if (page_table.find(target_page_id) != page_table.end()) {
        // The Hash Map knows this page! Get its physical index.
        int physical_index = page_table[target_page_id];

        // Flip the bit to 1 (Give it a second chance)
        metadata_array[physical_index].ref_bit = true;

        // We are done. The clock hand does NOT move.
        return;
    }


    // ==========================================
    // CACHE MISS (The Clock Sweep)
    // ==========================================
    // We enter an infinite loop. We do not leave this loop until hame place na mile jaaye new target_id ke liye

    while (true) {

        // yahan pe frame analysis hogi
        // We use the '&' reference so we modify the actual array, not a copy.
        FrameDescriptor& current_frame = metadata_array[clock_hand];

        // -----------------------------------------------------
        // SCENARIO A: The frame is empty, just spined DB.....!!!!!1
        // -----------------------------------------------------
        if (current_frame.page_id == -1) {
            current_frame.page_id = target_page_id;

            current_frame.ref_bit = true;

            page_table[target_page_id] = clock_hand;

            clock_hand = (clock_hand + 1) % capacity;


            break;
        }

        // -----------------------------------------------------
        // SCENARIO B: Frame is full, but uske paas Second Chance h(ref_bit == 1)
        // -----------------------------------------------------
        else if (current_frame.ref_bit == true) {
            current_frame.ref_bit = false;

            clock_hand = (clock_hand+1) % capacity;

        }

        // -----------------------------------------------------
        // C: Frame full h, so NO Second Chance (ref_bit == 0) -> EVICTION!
        // -----------------------------------------------------
        else if (current_frame.ref_bit == false) {
            page_table.erase(current_frame.page_id);

            current_frame.page_id = target_page_id;

            current_frame.ref_bit = true;

            page_table[target_page_id] = clock_hand;


            clock_hand = (clock_hand+1) % capacity;
            break;
        }
    }
}