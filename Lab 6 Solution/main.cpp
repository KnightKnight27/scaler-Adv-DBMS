#include "TransactionManager.hpp"
#include <iostream>



// Environment Simulation Setup

void PopulateInitialSeedData(TransactionManager& manager) {
    int seed_tx = manager.Begin();
    
    // Insert fundamental baseline values into rows 100 and 200
    manager.Write(seed_tx, 100, 500); 
    manager.Write(seed_tx, 200, 800); 
    
    manager.Commit(seed_tx);
    std::cout << "--- System Seed Initialization Complete ---\n\n";
}



// Simulation Mainline Execution

int main() {
    TransactionManager core_engine;
    PopulateInitialSeedData(core_engine);

    

    // Scenario 1: MVCC Snapshot Isolation (Readers vs. Writers decoupling)
    
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation (Readers do not block Writers) ===\n";
    int reader_tx = core_engine.Begin(); // Tx 1
    int writer_tx = core_engine.Begin(); // Tx 2

    int captured_value;
    
    // 1. Reader observes the original baseline value
    core_engine.Read(reader_tx, 100, captured_value);     
    
    // 2. Writer creates a speculative new version without blocking the reader
    core_engine.Write(writer_tx, 100, 999);    
    
    // 3. Reader evaluates their isolation view; the uncommitted 999 must stay hidden
    core_engine.Read(reader_tx, 100, captured_value);     
    
    // 4. Writer finalizes changes
    core_engine.Commit(writer_tx);
    
    // 5. Reader still reads the original 500 because their snapshot view was created prior to Tx 2's commit
    core_engine.Read(reader_tx, 100, captured_value);     
    core_engine.Commit(reader_tx);

    

    // Scenario 2: Write-Write Conflict (Two-Phase Locking Intercept)
    
    std::cout << "\n=== Scenario 2: Write-Write Conflict (Lock Delay Handling) ===\n";
    int active_holder_tx = core_engine.Begin(); // Tx 3
    int blocked_seeker_tx = core_engine.Begin(); // Tx 4

    // 1. Active transaction claims exclusive ownership of Row 200
    core_engine.Write(active_holder_tx, 200, 1000);    
    
    // 2. Seeking transaction attempts an overlapping write; fails immediately due to lock acquisition block
    core_engine.Write(blocked_seeker_tx, 200, 2000);    
    
    // 3. Original holder yields resource control on complete
    core_engine.Commit(active_holder_tx);             

    // 4. Terminate the secondary transaction safely
    core_engine.Abort(blocked_seeker_tx); 

    

    // Scenario 3: Deadlock Detection & Internal System Resolution
    
    std::cout << "\n=== Scenario 3: Deadlock Cycle Graph Resolution ===\n";
    int worker_alpha_tx = core_engine.Begin(); // Tx 5
    int worker_beta_tx = core_engine.Begin();  // Tx 6

    // 1. Worker Alpha locks Row 100
    core_engine.Write(worker_alpha_tx, 100, 1111);
    
    // 2. Worker Beta locks Row 200
    core_engine.Write(worker_beta_tx, 200, 2222);

    // 3. Worker Alpha requests Row 200 -> Becomes blocked by Worker Beta
    core_engine.Write(worker_alpha_tx, 200, 3333);

    // 4. Worker Beta requests Row 100 -> Intercepted! Forms a cycle: 5 -> 6 -> 5
    // The internal engine should flag this, abort the youngest transaction, and unwind its locks.
    core_engine.Write(worker_beta_tx, 100, 4444); 

    // 5. Clean up surviving worker transaction
    core_engine.Commit(worker_alpha_tx);

    return 0;
}