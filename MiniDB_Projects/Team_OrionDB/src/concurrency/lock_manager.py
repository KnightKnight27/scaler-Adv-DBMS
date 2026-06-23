import threading
from collections import defaultdict

class LockRequest:
    def __init__(self, txn_id, lock_mode):
        self.txn_id = txn_id
        self.lock_mode = lock_mode  # 'S' or 'X'
        self.event = threading.Event()
        self.aborted = False

class LockManager:
    def __init__(self):
        self.lock = threading.Lock()
        # resource_id -> list of LockRequest (both granted and waiting)
        self.lock_table = defaultdict(list)
        # txn_id -> set of resource_ids locked
        self.txn_locks = defaultdict(set)
        
        # Wait-for graph: txn_id -> set of txn_ids that it is waiting for
        self.wait_for_graph = defaultdict(set)

    def acquire_shared(self, txn_id, resource_id):
        return self._acquire(txn_id, resource_id, 'S')

    def acquire_exclusive(self, txn_id, resource_id):
        return self._acquire(txn_id, resource_id, 'X')

    def _acquire(self, txn_id, resource_id, mode):
        req = LockRequest(txn_id, mode)
        
        with self.lock:
            # Check if this transaction already holds an X lock on the resource
            existing_locks = self.lock_table[resource_id]
            for r in existing_locks:
                if r.txn_id == txn_id and r.event.is_set():
                    if r.lock_mode == 'X' or mode == 'S':
                        # Already holds a sufficient lock
                        return True
                    elif r.lock_mode == 'S' and mode == 'X':
                        # Lock upgrade (simplified: if only this txn holds it, upgrade, otherwise conflict)
                        holders = [hl for hl in existing_locks if hl.event.is_set()]
                        if len(holders) == 1 and holders[0].txn_id == txn_id:
                            holders[0].lock_mode = 'X'
                            return True
            
            # Add request to lock table queue
            existing_locks.append(req)
            
            # Check if request can be granted immediately
            if self._is_compatible(resource_id, req):
                req.event.set()
                self.txn_locks[txn_id].add(resource_id)
                return True
                
            # If not compatible, we must wait. Let's update Wait-For Graph and check for deadlocks.
            holders = [hl for hl in existing_locks if hl.event.is_set() and hl.txn_id != txn_id]
            for h in holders:
                self.wait_for_graph[txn_id].add(h.txn_id)
                
            # Detect deadlock
            if self._has_deadlock():
                # Deadlock detected! We abort the youngest transaction in the cycle.
                # In our design, the transaction with the largest Txn ID is the youngest.
                # Let's clean up our request and return False.
                existing_locks.remove(req)
                self._remove_wait_edges(txn_id)
                return False

        # Wait outside the LockManager lock to avoid blocking other transactions
        # E.g. wait up to 5 seconds to avoid indefinite blocks (lock timeout)
        granted = req.event.wait(timeout=5.0)
        
        with self.lock:
            if not granted:
                # Lock timeout
                if req in self.lock_table[resource_id]:
                    self.lock_table[resource_id].remove(req)
                self._remove_wait_edges(txn_id)
                return False
                
            if req.aborted:
                return False
                
            self.txn_locks[txn_id].add(resource_id)
            return True

    def _is_compatible(self, resource_id, request):
        queue = self.lock_table[resource_id]
        # Any request ahead of us in the queue that has been granted?
        for req in queue:
            if req == request:
                break
            # If there is a granted request that conflicts, not compatible
            if req.event.is_set():
                if request.lock_mode == 'X' or req.lock_mode == 'X':
                    return False
        return True

    def _has_deadlock(self):
        # Run DFS cycle detection on wait_for_graph
        visited = {}  # txn_id -> state: 0 = unvisited, 1 = visiting, 2 = visited
        
        def dfs(node):
            visited[node] = 1  # visiting
            for neighbor in self.wait_for_graph[node]:
                state = visited.get(neighbor, 0)
                if state == 1:
                    return True  # Found cycle!
                elif state == 0:
                    if dfs(neighbor):
                        return True
            visited[node] = 2  # visited
            return False

        for node in list(self.wait_for_graph.keys()):
            if visited.get(node, 0) == 0:
                if dfs(node):
                    return True
        return False

    def _remove_wait_edges(self, txn_id):
        if txn_id in self.wait_for_graph:
            del self.wait_for_graph[txn_id]
        for key in list(self.wait_for_graph.keys()):
            if txn_id in self.wait_for_graph[key]:
                self.wait_for_graph[key].remove(txn_id)

    def release_locks(self, txn_id):
        with self.lock:
            locked_resources = list(self.txn_locks[txn_id])
            for res_id in locked_resources:
                queue = self.lock_table[res_id]
                # Find and remove our granted requests
                for req in list(queue):
                    if req.txn_id == txn_id and req.event.is_set():
                        queue.remove(req)
                
                # Check waiting requests on this resource and grant them
                for req in queue:
                    if not req.event.is_set() and self._is_compatible(res_id, req):
                        req.event.set()
                        
                if not queue:
                    del self.lock_table[res_id]
                    
            del self.txn_locks[txn_id]
            self._remove_wait_edges(txn_id)
