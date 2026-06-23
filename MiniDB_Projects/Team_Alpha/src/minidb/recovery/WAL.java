package minidb.recovery;

import minidb.common.Types.*;
import java.io.*;
import java.util.*;

/**
 * Write-Ahead Log (WAL) — the heart of crash recovery.
 *
 * The WAL protocol guarantees durability + atomicity:
 *   - Every change is appended to the log file (and fsync'd) BEFORE the
 *     corresponding data page is modified or flushed.
 *   - A transaction is only "committed" once its COMMIT record is durably on
 *     the log. After a crash we can REDO committed work and UNDO uncommitted
 *     work to reach a consistent state.
 *
 * Log record format (one per line, human-readable for easy viva inspection):
 *
 *   <lsn>|<txnId>|<type>|<table>|<pageId>|<slot>|<base64-after>|<base64-before>
 *
 * Types: BEGIN, COMMIT, ABORT, INSERT, DELETE, UPDATE, CHECKPOINT
 *
 * Using a text log keeps the format inspectable during the demo; a production
 * system would use a compact binary format.
 */
public final class WAL implements Closeable {
    public enum Type { BEGIN, COMMIT, ABORT, INSERT, DELETE, UPDATE, CHECKPOINT }

    public static final class Record {
        public long lsn;
        public long txnId;
        public Type type;
        public String table;
        public RID rid;
        public Object[] after;   // image after the change
        public Object[] before;  // image before the change (for UNDO)
    }

    private final String path;
    private final BufferedWriter out;
    private long nextLsn = 1;

    public WAL(String path) throws IOException {
        this.path = path;
        this.out = new BufferedWriter(new FileWriter(path, true)); // append
    }

    private synchronized long append(long txnId, Type type, String table,
                                     RID rid, Object[] after, Object[] before) {
        long lsn = nextLsn++;
        try {
            StringBuilder sb = new StringBuilder();
            sb.append(lsn).append('|')
              .append(txnId).append('|')
              .append(type).append('|')
              .append(table == null ? "" : table).append('|')
              .append(rid == null ? -1 : rid.pageId).append('|')
              .append(rid == null ? -1 : rid.slot).append('|')
              .append(encode(after)).append('|')
              .append(encode(before));
            out.write(sb.toString());
            out.newLine();
            out.flush();            // force the log to the OS (durability point)
        } catch (IOException e) {
            throw new RuntimeException("WAL append failed", e);
        }
        return lsn;
    }

    public long logBegin(long txnId)  { return append(txnId, Type.BEGIN, null, null, null, null); }
    public long logCommit(long txnId) { return append(txnId, Type.COMMIT, null, null, null, null); }
    public long logAbort(long txnId)  { return append(txnId, Type.ABORT, null, null, null, null); }

    public long logInsert(long txnId, String table, RID rid, Tuple t) {
        return append(txnId, Type.INSERT, table, rid, t.values, null);
    }
    public long logDelete(long txnId, String table, RID rid, Tuple before) {
        return append(txnId, Type.DELETE, table, rid, null, before.values);
    }
    public long logUpdate(long txnId, String table, RID rid, Tuple before, Tuple after) {
        return append(txnId, Type.UPDATE, table, rid, after.values, before.values);
    }
    public long logCheckpoint() { return append(0, Type.CHECKPOINT, null, null, null, null); }

    /** Read and parse every record in the log (used by the recovery manager). */
    public List<Record> readAll() {
        List<Record> recs = new ArrayList<>();
        File f = new File(path);
        if (!f.exists()) return recs;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isBlank()) continue;
                String[] parts = line.split("\\|", -1);
                Record rec = new Record();
                rec.lsn = Long.parseLong(parts[0]);
                rec.txnId = Long.parseLong(parts[1]);
                rec.type = Type.valueOf(parts[2]);
                rec.table = parts[3].isEmpty() ? null : parts[3];
                int pid = Integer.parseInt(parts[4]);
                int slot = Integer.parseInt(parts[5]);
                rec.rid = (pid < 0) ? null : new RID(pid, slot);
                rec.after = decode(parts[6]);
                rec.before = decode(parts[7]);
                recs.add(rec);
                if (rec.lsn >= nextLsn) nextLsn = rec.lsn + 1;
            }
        } catch (IOException e) {
            throw new RuntimeException("WAL read failed", e);
        }
        return recs;
    }

    // ---- value (de)serialization for the log ----
    // We encode an Object[] as a comma-joined, Base64-per-field string so that
    // strings containing separators are safe.
    private static String encode(Object[] vals) {
        if (vals == null) return "";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < vals.length; i++) {
            if (i > 0) sb.append(',');
            String s = (vals[i] == null) ? "" : vals[i].toString();
            String tag = (vals[i] instanceof Integer) ? "I" : "S";
            sb.append(tag).append(Base64.getEncoder().encodeToString(s.getBytes()));
        }
        return sb.toString();
    }
    private static Object[] decode(String s) {
        if (s == null || s.isEmpty()) return null;
        String[] fields = s.split(",", -1);
        Object[] out = new Object[fields.length];
        for (int i = 0; i < fields.length; i++) {
            char tag = fields[i].charAt(0);
            String raw = new String(Base64.getDecoder().decode(fields[i].substring(1)));
            out[i] = (tag == 'I') ? Integer.parseInt(raw) : raw;
        }
        return out;
    }

    @Override public void close() throws IOException { out.close(); }
}
