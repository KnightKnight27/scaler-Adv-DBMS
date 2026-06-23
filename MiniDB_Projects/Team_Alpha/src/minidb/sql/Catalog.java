package minidb.sql;

import minidb.common.Types.*;
import minidb.index.BPlusTree;
import minidb.storage.*;
import minidb.recovery.WAL;
import java.io.*;
import java.util.*;

/**
 * The Catalog is the database's metadata store: it knows every table, its
 * schema, its pages, and its indexes. Each table is backed by its own heap file
 * (data/<table>.heap) so page schemas are unambiguous.
 */
public final class Catalog {
    private final WAL wal;
    private final String dataDir;
    private final int poolSize;

    private final Map<String, Table> tables = new LinkedHashMap<>();
    private final Map<String, Schema> schemas = new LinkedHashMap<>();
    private final Map<String, Map<String, BPlusTree>> indexes = new HashMap<>();
    private final Map<String, String> primaryKeys = new HashMap<>();

    // ---- table statistics (maintained incrementally for the optimizer) ----
    // Real databases keep cached statistics rather than scanning at plan time.
    private final Map<String, Integer> rowCounts = new HashMap<>();
    private final Map<String, Map<String, Set<Object>>> distinctVals = new HashMap<>();

    public Catalog(WAL wal, String dataDir, int poolSize) {
        this.wal = wal;
        this.dataDir = dataDir;
        this.poolSize = poolSize;
    }

    private String catalogPath() { return dataDir + "/catalog.txt"; }
    private String heapPath(String table) { return dataDir + "/" + table.toLowerCase() + ".heap"; }

    public boolean hasTable(String name) { return tables.containsKey(name.toLowerCase()); }
    public Table table(String name) { return tables.get(name.toLowerCase()); }
    public Schema schema(String name) { return schemas.get(name.toLowerCase()); }
    public Collection<Table> allTables() { return tables.values(); }
    public String primaryKey(String table) { return primaryKeys.get(table.toLowerCase()); }

    public Map<String, BPlusTree> indexesFor(String table) {
        return indexes.getOrDefault(table.toLowerCase(), Collections.emptyMap());
    }
    public BPlusTree index(String table, String column) {
        return indexesFor(table).get(column.toLowerCase());
    }

    public Table createTable(String name, Schema schema, String pk) {
        String key = name.toLowerCase();
        if (tables.containsKey(key)) throw new RuntimeException("Table exists: " + name);
        try {
            DiskManager disk = new DiskManager(heapPath(name));
            Table t = new Table(name, schema, disk, wal, poolSize);
            tables.put(key, t);
            schemas.put(key, schema);
            indexes.put(key, new HashMap<>());
            if (pk != null) {
                primaryKeys.put(key, pk.toLowerCase());
                indexes.get(key).put(pk.toLowerCase(), new BPlusTree());
            }
            persist();
            return t;
        } catch (IOException e) { throw new RuntimeException(e); }
    }

    public void createIndex(String table, String column) {
        String key = table.toLowerCase();
        BPlusTree bt = new BPlusTree();
        indexes.get(key).put(column.toLowerCase(), bt);
        Schema sc = schemas.get(key);
        int ci = sc.indexOf(column);
        for (Tuple t : tables.get(key).scan())
            if (t.get(ci) instanceof Integer) bt.insert((Integer) t.get(ci), t.rid);
    }

    public void indexInsert(String table, Tuple t) {
        String key = table.toLowerCase();
        Schema sc = schemas.get(key);
        for (Map.Entry<String, BPlusTree> e : indexes.get(key).entrySet()) {
            int ci = sc.indexOf(e.getKey());
            if (t.get(ci) instanceof Integer) e.getValue().insert((Integer) t.get(ci), t.rid);
        }
        // maintain statistics
        rowCounts.merge(key, 1, Integer::sum);
        Map<String, Set<Object>> dv = distinctVals.computeIfAbsent(key, k -> new HashMap<>());
        for (Column c : sc.columns) {
            int ci = sc.indexOf(c.name);
            dv.computeIfAbsent(c.name.toLowerCase(), k -> new HashSet<>()).add(t.get(ci));
        }
    }

    public void indexDelete(String table, Tuple t) {
        String key = table.toLowerCase();
        Schema sc = schemas.get(key);
        for (Map.Entry<String, BPlusTree> e : indexes.get(key).entrySet()) {
            int ci = sc.indexOf(e.getKey());
            if (t.get(ci) instanceof Integer) e.getValue().delete((Integer) t.get(ci));
        }
        rowCounts.merge(key, -1, Integer::sum);
    }

    /** Cached row count for the optimizer (falls back to a scan once if cold). */
    public int rowCount(String table) {
        String key = table.toLowerCase();
        Integer n = rowCounts.get(key);
        if (n == null) { n = table(table).scan().size(); rowCounts.put(key, n); }
        return Math.max(0, n);
    }

    /** Cached distinct-value estimate for a column. */
    public int distinctCount(String table, String column) {
        String key = table.toLowerCase();
        Map<String, Set<Object>> dv = distinctVals.get(key);
        if (dv != null && dv.containsKey(column.toLowerCase()))
            return Math.max(1, dv.get(column.toLowerCase()).size());
        return Math.max(1, Math.min(rowCount(table), 10));
    }

    // Format: TABLE|name|pk|col:type,col:type,...
    public void persist() {
        try (BufferedWriter w = new BufferedWriter(new FileWriter(catalogPath()))) {
            for (Map.Entry<String, Table> e : tables.entrySet()) {
                Table t = e.getValue();
                StringBuilder sb = new StringBuilder("TABLE|").append(t.name).append('|');
                sb.append(primaryKeys.getOrDefault(e.getKey(), "")).append('|');
                StringJoiner cols = new StringJoiner(",");
                for (Column c : t.schema.columns) cols.add(c.name + ":" + c.type);
                sb.append(cols);
                w.write(sb.toString()); w.newLine();
            }
        } catch (IOException ex) { throw new RuntimeException(ex); }
    }

    public void load() {
        File f = new File(catalogPath());
        if (!f.exists()) return;
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isBlank()) continue;
                String[] p = line.split("\\|", -1);
                String name = p[1];
                String pk = p[2].isEmpty() ? null : p[2];
                List<Column> cols = new ArrayList<>();
                for (String cd : p[3].split(",")) {
                    String[] kv = cd.split(":");
                    cols.add(new Column(kv[0], ColType.valueOf(kv[1])));
                }
                Schema sc = new Schema(cols);
                Table t = createTable(name, sc, pk);
                String key = name.toLowerCase();
                int ci2 = (pk != null) ? sc.indexOf(pk) : -1;
                BPlusTree bt = (pk != null) ? indexes.get(key).get(pk.toLowerCase()) : null;
                Map<String, Set<Object>> dv = distinctVals.computeIfAbsent(key, k -> new HashMap<>());
                int rc = 0;
                for (Tuple tup : t.scan()) {
                    if (tup.deleted) continue;
                    rc++;
                    if (bt != null && tup.get(ci2) instanceof Integer)
                        bt.insert((Integer) tup.get(ci2), tup.rid);
                    for (Column c : sc.columns)
                        dv.computeIfAbsent(c.name.toLowerCase(), k -> new HashSet<>()).add(tup.get(sc.indexOf(c.name)));
                }
                rowCounts.put(key, rc);
            }
        } catch (IOException ex) { throw new RuntimeException(ex); }
    }

    public void flushAll() { for (Table t : tables.values()) t.flush(); }
}
