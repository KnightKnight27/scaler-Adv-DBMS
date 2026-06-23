def is_visible(xmin, xmax, tx_id, active_tx_ids, committed_txs, is_mvcc=True):
    if not is_mvcc:
        return True

    # 1. Check xmin (creation)
    xmin_visible = False
    if xmin == tx_id:
        xmin_visible = True
    elif xmin in committed_txs and xmin not in active_tx_ids:
        xmin_visible = True

    if not xmin_visible:
        return False

    # 2. Check xmax (deletion)
    if xmax == 0:
        return True
    
    if xmax == tx_id:
        return False  # Deleted by current transaction
        
    if xmax in committed_txs and xmax not in active_tx_ids:
        return False  # Deleted by committed transaction

    return True  # xmax is active or uncommitted, so deletion is not visible yet
