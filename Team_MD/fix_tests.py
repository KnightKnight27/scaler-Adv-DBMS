import os
import re

test_dir = r"c:\Second Year\Term 8\MiniDB_Project\Team_MD\src\test\java\com\minidb"

def fix_file(path):
    with open(path, 'r') as f:
        content = f.read()

    # BufferPool(..., ...) -> BufferPool(..., ..., null)
    # Be careful not to replace already fixed ones
    content = re.sub(r'new BufferPool\(([^,]+),\s*([^,)]+)\)', r'new BufferPool(\1, \2, null)', content)

    # HeapFile(..., ..., ...) -> HeapFile(..., ..., ..., null)
    content = re.sub(r'new HeapFile\(([^,]+),\s*([^,]+),\s*([^,)]+)\)', r'new HeapFile(\1, \2, \3, null)', content)

    # TransactionManager.getInstance() -> new com.minidb.txn.TransactionManager(null)
    content = content.replace('TransactionManager.getInstance()', 'new com.minidb.txn.TransactionManager(null)')
    content = content.replace('com.minidb.txn.TransactionManager.getInstance()', 'new com.minidb.txn.TransactionManager(null)')

    # SeqScanOperator(heapFile) -> SeqScanOperator(heapFile, tm)
    # But wait, some tests don't have tm defined. Let's just define a local tm.
    # We can pass `null` for OperatorTest where we don't care about txn, or `tm` where we do.
    # MVCCTest and FinalIntegrationTest use txns. OperatorTest does too? OperatorTest uses txns for visibility rules!
    # Wait, OperatorTest tests VisibilityRules? Yes, OperatorTest sets up txns.
    
    with open(path, 'w') as f:
        f.write(content)

for root, dirs, files in os.walk(test_dir):
    for f in files:
        if f.endswith('.java'):
            fix_file(os.path.join(root, f))

print("Fixed BufferPool, HeapFile, TransactionManager")
