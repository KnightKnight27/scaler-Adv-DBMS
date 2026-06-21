import threading
import time

class TransactionAbortException(Exception):
    """Raised when a transaction must abort, e.g., due to deadlock detection."""
    pass

class LockManager:
    def __init__(self, verbose: bool = False):
        self.lock = threading.Lock()
        self.cond = threading.Condition(self.lock)
        
        # Maps resource_id -> list of (txn_id, lock_type)
        self.lock_table = {}
        
        # Maps txn_id -> set of resource_ids held
        self.txn_locks = {}
        
        # Maps txn_id -> resource_id currently waiting on
        self.waiting_for = {}
        
        # Maps txn_id -> requested lock_type when waiting
        self.waiting_lock_type = {}
        
        # Set of txn_ids that have been aborted by deadlock detection
        self.aborted_txns = set()
        
        self.verbose = verbose

    def log(self, message: str):
        if self.verbose:
            print(f"[LockManager] {message}")

    def acquire_lock(self, txn_id: int, resource_id: str, lock_type: str) -> bool:
        """
        Acquires a lock for txn_id on resource_id.
        Blocks if incompatible lock is held by another transaction.
        Raises TransactionAbortException if deadlock is detected and this transaction is aborted.
        """
        lock_type = lock_type.upper()
        if lock_type not in ("SHARED", "EXCLUSIVE"):
            raise ValueError(f"Invalid lock type: {lock_type}")

        with self.lock:
            self.log(f"Txn {txn_id} requesting {lock_type} lock on {resource_id}")
            
            # Check if already holds a compatible lock
            if self._has_compatible_lock_already(txn_id, resource_id, lock_type):
                self.log(f"Txn {txn_id} already holds compatible lock on {resource_id}")
                return True

            # Loop until lock is acquired or transaction is aborted
            while True:
                if txn_id in self.aborted_txns:
                    self.aborted_txns.remove(txn_id)
                    self.log(f"Txn {txn_id} aborted due to deadlock detection")
                    raise TransactionAbortException(f"Deadlock detected. Transaction {txn_id} aborted.")

                if self._is_compatible(txn_id, resource_id, lock_type):
                    self._grant_lock(txn_id, resource_id, lock_type)
                    # Clear waiting state if any
                    if txn_id in self.waiting_for:
                        del self.waiting_for[txn_id]
                        del self.waiting_lock_type[txn_id]
                    return True

                # Not compatible: we must wait. Let's record waiting state and check for deadlocks.
                self.waiting_for[txn_id] = resource_id
                self.waiting_lock_type[txn_id] = lock_type
                
                # Check deadlocks
                self._detect_and_resolve_deadlocks()
                
                # If we were aborted during deadlock detection, loop again to raise exception
                if txn_id in self.aborted_txns:
                    continue
                
                self.log(f"Txn {txn_id} blocking on {resource_id} for {lock_type}")
                # Wait on condition
                self.cond.wait()

    def _has_compatible_lock_already(self, txn_id: int, resource_id: str, lock_type: str) -> bool:
        holders = self.lock_table.get(resource_id, [])
        for holder_txn, holder_type in holders:
            if holder_txn == txn_id:
                if holder_type == "EXCLUSIVE" or lock_type == "SHARED":
                    return True
        return False

    def _is_compatible(self, txn_id: int, resource_id: str, lock_type: str) -> bool:
        holders = self.lock_table.get(resource_id, [])
        if not holders:
            return True
            
        other_holders = [h for h in holders if h[0] != txn_id]
        if not other_holders:
            return True  # Only the requesting transaction holds a lock on it

        if lock_type == "EXCLUSIVE":
            # Exclusive needs no other transactions to hold any lock
            return len(other_holders) == 0
        
        # Shared lock needs no other transactions to hold EXCLUSIVE
        for other_txn, other_type in other_holders:
            if other_type == "EXCLUSIVE":
                return False
        return True

    def _grant_lock(self, txn_id: int, resource_id: str, lock_type: str):
        # Handle lock upgrades or adding lock
        holders = self.lock_table.setdefault(resource_id, [])
        
        # If txn already holds a lock, upgrade it or replace it
        for i, (holder_txn, holder_type) in enumerate(holders):
            if holder_txn == txn_id:
                if holder_type == "SHARED" and lock_type == "EXCLUSIVE":
                    holders[i] = (txn_id, "EXCLUSIVE")
                    self.log(f"[Lock acquired: EXCLUSIVE on {resource_id} (Upgraded from SHARED) for Txn {txn_id}]")
                return

        # Otherwise add new lock
        holders.append((txn_id, lock_type))
        self.txn_locks.setdefault(txn_id, set()).add(resource_id)
        self.log(f"[Lock acquired: {lock_type} on {resource_id} for Txn {txn_id}]")

    def release_locks(self, txn_id: int):
        with self.lock:
            resources = list(self.txn_locks.get(txn_id, []))
            for res in resources:
                holders = self.lock_table.get(res, [])
                # Remove this txn's locks
                self.lock_table[res] = [h for h in holders if h[0] != txn_id]
                if not self.lock_table[res]:
                    del self.lock_table[res]
                self.log(f"[Lock released: on {res} for Txn {txn_id}]")
            
            if txn_id in self.txn_locks:
                del self.txn_locks[txn_id]
            if txn_id in self.waiting_for:
                del self.waiting_for[txn_id]
                del self.waiting_lock_type[txn_id]
            
            self.cond.notify_all()

    def _detect_and_resolve_deadlocks(self):
        """
        Builds a wait-for graph and checks for cycles.
        If a cycle is detected, aborts the youngest transaction (highest txn_id) in the cycle.
        Repeats until no cycles remain.
        """
        while True:
            # Build wait-for graph: txn -> set of txns it is waiting for
            graph = {}
            for waiter_txn, resource_id in self.waiting_for.items():
                if waiter_txn in self.aborted_txns:
                    continue
                req_type = self.waiting_lock_type[waiter_txn]
                holders = self.lock_table.get(resource_id, [])
                
                # Waiter waits for all holders with incompatible locks
                for holder_txn, holder_type in holders:
                    if holder_txn != waiter_txn and holder_txn not in self.aborted_txns:
                        # Check compatibility of waiter's request with holder's lock
                        incompatible = False
                        if req_type == "EXCLUSIVE":
                            incompatible = True
                        elif holder_type == "EXCLUSIVE":
                            incompatible = True
                        
                        if incompatible:
                            graph.setdefault(waiter_txn, set()).add(holder_txn)

            # Detect a cycle using DFS
            cycle = self._find_cycle(graph)
            if not cycle:
                break

            # Found a cycle! Abort the youngest transaction (highest txn_id)
            youngest = max(cycle)
            self.log(f"Deadlock cycle detected: {cycle}. Aborting youngest transaction: {youngest}")
            self.aborted_txns.add(youngest)
            
            # Remove from waiting state
            if youngest in self.waiting_for:
                del self.waiting_for[youngest]
                del self.waiting_lock_type[youngest]
                
            self.cond.notify_all()

    def _find_cycle(self, graph: dict) -> list:
        visited = {}  # 0=unvisited, 1=visiting, 2=visited
        path = []

        def dfs(node):
            visited[node] = 1
            path.append(node)
            for neighbor in graph.get(node, []):
                if visited.get(neighbor, 0) == 1:
                    # Cycle found! Extract cycle from path
                    idx = path.index(neighbor)
                    return path[idx:]
                elif visited.get(neighbor, 0) == 0:
                    cycle = dfs(neighbor)
                    if cycle:
                        return cycle
            path.pop()
            visited[node] = 2
            return None

        for node in graph:
            if visited.get(node, 0) == 0:
                cycle = dfs(node)
                if cycle:
                    return cycle
        return None
