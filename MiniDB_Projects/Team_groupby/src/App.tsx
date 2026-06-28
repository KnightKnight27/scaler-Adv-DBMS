import { useState, useEffect } from 'react';
import { DatabaseSystem } from './engine/DatabaseSystem';
import { Transaction, LogRecord } from './engine/types';
import { Terminal, Database, ShieldAlert, Cpu, Award, RotateCcw, AlertTriangle, Activity, Zap } from 'lucide-react';
import { PerformanceSuite } from './benchmarks/PerformanceSuite';

export default function App() {
  const [db] = useState(() => new DatabaseSystem());
  const [sessions, setSessions] = useState<{
    id: number;
    terminalInput: string;
    terminalLog: { type: 'input' | 'output' | 'error' | 'plan'; text: string }[];
    txn: Transaction | null;
  }[]>([
    { id: 1, terminalInput: '', terminalLog: [{ type: 'output', text: 'MiniDB Session 1 ready. Try: SELECT * FROM users' }], txn: null },
    { id: 2, terminalInput: '', terminalLog: [{ type: 'output', text: 'MiniDB Session 2 ready. Try: SELECT * FROM users' }], txn: null }
  ]);
  const [activeSession, setActiveSession] = useState(1);
  const [sysLogs, setSysLogs] = useState<LogRecord[]>([]);
  const [bufferFrames, setBufferFrames] = useState<any[]>([]);
  const [diskPages, setDiskPages] = useState<any[]>([]);
  const [recoveryLog, setRecoveryLog] = useState<string[]>([]);
  const [bTreeNodes, setBTreeNodes] = useState<any>(null);
  const [benchmarkResults, setBenchmarkResults] = useState<{
    writesPerSecond: number;
    readsPerSecond: number;
    latencyWithIndexMs: number;
    latencyWithoutIndexMs: number;
  } | null>(null);
  const [runningBenchmark, setRunningBenchmark] = useState(false);

  const runBenchmark = () => {
    setRunningBenchmark(true);
    setTimeout(() => {
      try {
        const res = PerformanceSuite.runBenchmark();
        setBenchmarkResults(res);
      } catch (err) {
        console.error(err);
      } finally {
        setRunningBenchmark(false);
      }
    }, 100);
  };

  const refreshEngineState = () => {
    setSysLogs([...db.logManager.getLogs()]);
    setBufferFrames([...db.bufferPool.getFrames()]);
    const pages: any[] = [];
    db.bufferPool.getDiskPages().forEach((p, id) => {
      pages.push({ id, ...p });
    });
    setDiskPages(pages);
    if (db.indices.has('users_pk')) {
      const tree = db.indices.get('users_pk')!;
      setBTreeNodes(JSON.parse(JSON.stringify(tree.root)));
    }
  };

  useEffect(() => {
    refreshEngineState();
  }, [db]);

  const handleCommand = (sId: number) => {
    const session = sessions.find(s => s.id === sId)!;
    const cmd = session.terminalInput.trim();
    if (!cmd) return;

    const newLog = [...session.terminalLog, { type: 'input' as const, text: `minidb> ${cmd}` }];
    const execResult = db.executeSQL(cmd, session.txn);

    if (execResult.error) {
      newLog.push({ type: 'error' as const, text: `ERROR: ${execResult.error}` });
    } else {
      if (execResult.plan) {
        newLog.push({ type: 'plan' as const, text: `Query Plan: ${execResult.plan.details}` });
      }
      execResult.results.forEach(res => {
        newLog.push({ type: 'output' as const, text: typeof res === 'string' ? res : JSON.stringify(res) });
      });
    }

    setSessions(prev => prev.map(s => s.id === sId ? {
      ...s,
      terminalInput: '',
      terminalLog: newLog,
      txn: execResult.txn === undefined ? s.txn : execResult.txn
    } : s));

    refreshEngineState();
  };

  const handleCrash = () => {
    db.simulateCrash();
    setRecoveryLog(['[CRASH] System crashed hard! Buffer pool wiped clean. Database is in inconsistent disk state.']);
    refreshEngineState();
  };

  const handleRecovery = () => {
    const res = db.recover();
    setRecoveryLog(res.recoverySteps);
    refreshEngineState();
  };

  const renderTree = (node: any): JSX.Element | null => {
    if (!node) return null;
    return (
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', margin: '0 0.5rem' }}>
        <div className={`tree-node-visual ${!node.isLeaf ? 'tree-node-internal' : ''}`}>
          <div style={{ fontWeight: 'bold', borderBottom: '1px solid rgba(255,255,255,0.2)', marginBottom: '2px' }}>
            {node.isLeaf ? 'Leaf' : 'Internal'}
          </div>
          <div style={{ fontSize: '0.9rem' }}>{node.keys.join(', ') || 'empty'}</div>
        </div>
        {!node.isLeaf && node.children && (
          <div style={{ display: 'flex', marginTop: '0.8rem', borderTop: '1px dashed #475569', paddingTop: '0.5rem' }}>
            {node.children.map((child: any, idx: number) => (
              <div key={idx}>{renderTree(child)}</div>
            ))}
          </div>
        )}
      </div>
    );
  };

  return (
    <div style={{ padding: '2rem', maxWidth: '1440px', margin: '0 auto' }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '2rem', padding: '1rem' }} className="glass-panel">
        <div style={{ display: 'flex', alignItems: 'center', gap: '1rem', padding: '1rem' }}>
          <Database size={40} color="#6366f1" />
          <div>
            <h1 style={{ margin: 0, fontSize: '1.8rem', background: 'linear-gradient(to right, #818cf8, #6366f1)', WebkitBackgroundClip: 'text', WebkitTextFillColor: 'transparent' }}>
              MiniDB Playground
            </h1>
            <p style={{ margin: 0, fontSize: '0.9rem', color: '#94a3b8' }}>Capstone relational engine implementation &bull; Track B: MVCC</p>
          </div>
        </div>
        <div style={{ display: 'flex', gap: '1rem', paddingRight: '1rem' }}>
          <span style={{ fontSize: '0.85rem', color: '#6366f1', border: '1px solid rgba(99,102,241,0.4)', borderRadius: '20px', padding: '0.3rem 0.8rem', display: 'flex', alignItems: 'center', gap: '5px' }}>
            <Award size={14} /> Team groupby
          </span>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1.5rem' }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          <div className="glass-panel" style={{ padding: '1.5rem', display: 'flex', flexDirection: 'column', height: '420px' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                <Terminal size={20} color="#818cf8" />
                <h2 style={{ margin: 0, fontSize: '1.2rem' }}>Interactive Multi-Session Terminals</h2>
              </div>
              <div style={{ display: 'flex', gap: '0.5rem' }}>
                <button onClick={() => setActiveSession(1)} className="glow-btn-primary" style={{ padding: '0.3rem 0.8rem', opacity: activeSession === 1 ? 1 : 0.5 }}>Session 1 {sessions[0].txn ? '(Txn active)' : ''}</button>
                <button onClick={() => setActiveSession(2)} className="glow-btn-primary" style={{ padding: '0.3rem 0.8rem', opacity: activeSession === 2 ? 1 : 0.5 }}>Session 2 {sessions[1].txn ? '(Txn active)' : ''}</button>
              </div>
            </div>

            {sessions.map(session => (
              <div key={session.id} style={{ display: session.id === activeSession ? 'flex' : 'none', flexDirection: 'column', flex: 1, overflow: 'hidden' }}>
                <div style={{ flex: 1, backgroundColor: '#020617', borderRadius: '8px', padding: '1rem', overflowY: 'auto', fontFamily: 'monospace', marginBottom: '0.8rem', border: '1px solid #1e293b' }}>
                  {session.terminalLog.map((log, idx) => (
                    <div key={idx} className="console-line" style={{
                      color: log.type === 'input' ? '#38bdf8' : log.type === 'error' ? '#ef4444' : log.type === 'plan' ? '#fbbf24' : '#34d399'
                    }}>
                      {log.text}
                    </div>
                  ))}
                </div>
                <div style={{ display: 'flex', gap: '0.5rem' }}>
                  <input
                    type="text"
                    value={session.terminalInput}
                    onChange={e => {
                      const val = e.target.value;
                      setSessions(prev => prev.map(s => s.id === session.id ? { ...s, terminalInput: val } : s));
                    }}
                    onKeyDown={e => e.key === 'Enter' && handleCommand(session.id)}
                    style={{ flex: 1, backgroundColor: '#0f172a', border: '1px solid #334155', borderRadius: '6px', padding: '0.5rem', color: '#fff', outline: 'none' }}
                    placeholder="Enter SQL command e.g., INSERT INTO users VALUES (40, 'Rishi', 22)..."
                  />
                  <button onClick={() => handleCommand(session.id)} className="glow-btn-primary" style={{ padding: '0.5rem 1.2rem' }}>Run</button>
                </div>
              </div>
            ))}
          </div>

          <div className="glass-panel" style={{ padding: '1.5rem', height: '330px', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '1rem' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                <ShieldAlert size={20} color="#f87171" />
                <h2 style={{ margin: 0, fontSize: '1.2rem' }}>Write-Ahead Logging (WAL) & Crash Recovery</h2>
              </div>
              <div style={{ display: 'flex', gap: '0.5rem' }}>
                <button onClick={handleCrash} className="glow-btn-danger" style={{ display: 'flex', alignItems: 'center', gap: '5px', padding: '0.4rem 0.8rem', fontSize: '0.85rem' }}>
                  <AlertTriangle size={14} /> Crash System
                </button>
                <button onClick={handleRecovery} className="glow-btn-primary" style={{ display: 'flex', alignItems: 'center', gap: '5px', padding: '0.4rem 0.8rem', fontSize: '0.85rem' }}>
                  <RotateCcw size={14} /> WAL Recovery
                </button>
              </div>
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1rem', flex: 1, minHeight: 0 }}>
              <div style={{ display: 'flex', flexDirection: 'column', minHeight: 0 }}>
                <div style={{ fontSize: '0.8rem', fontWeight: 'bold', color: '#818cf8', marginBottom: '0.25rem' }}>Live WAL Log Buffer</div>
                <div style={{ flex: 1, backgroundColor: '#020617', borderRadius: '8px', padding: '0.75rem', overflowY: 'auto', fontFamily: 'monospace', fontSize: '0.75rem', border: '1px solid #1e293b', color: '#94a3b8' }}>
                  {sysLogs.length === 0 ? (
                    <div style={{ color: '#64748b' }}>No WAL records. Run queries to append logs.</div>
                  ) : (
                    sysLogs.map((log, idx) => (
                      <div key={idx} style={{ marginBottom: '4px', borderBottom: '1px solid rgba(255,255,255,0.03)', paddingBottom: '2px' }}>
                        <span style={{ color: '#6366f1' }}>LSN #{log.lsn}</span> [Tx {log.txnId}] <span style={{ color: '#34d399', fontWeight: 'bold' }}>{log.type}</span>{log.tableName ? ` on ${log.tableName}` : ''}
                      </div>
                    ))
                  )}
                </div>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', minHeight: 0 }}>
                <div style={{ fontSize: '0.8rem', fontWeight: 'bold', color: '#fca5a5', marginBottom: '0.25rem' }}>ARIES Recovery Phase Outputs</div>
                <div style={{ flex: 1, backgroundColor: '#020617', borderRadius: '8px', padding: '0.75rem', overflowY: 'auto', fontFamily: 'monospace', fontSize: '0.75rem', border: '1px solid #1e293b', color: '#fca5a5' }}>
                  {recoveryLog.length === 0 ? (
                    <div style={{ color: '#64748b' }}>System healthy. Trigger a crash or run txns, then click WAL Recovery.</div>
                  ) : (
                    recoveryLog.map((log, idx) => (
                      <div key={idx} style={{ marginBottom: '4px' }}>{log}</div>
                    ))
                  )}
                </div>
              </div>
            </div>
          </div>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          <div className="glass-panel" style={{ padding: '1.5rem', height: '420px', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '1rem' }}>
              <Cpu size={20} color="#34d399" />
              <h2 style={{ margin: 0, fontSize: '1.2rem' }}>Buffer Pool & Physical Disk Pages</h2>
            </div>
            
            <h3 style={{ fontSize: '0.9rem', color: '#a7f3d0', margin: '0 0 0.5rem 0' }}>Active Buffer Pool Frames (Cache)</h3>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: '0.5rem', marginBottom: '1rem' }}>
              {bufferFrames.map((frame, idx) => (
                <div key={idx} style={{ border: '1px solid #334155', borderRadius: '6px', padding: '0.5rem', fontSize: '0.75rem', background: frame.pageId ? 'rgba(52,211,153,0.1)' : 'rgba(30,41,59,0.3)' }}>
                  <div><strong>Frame {frame.frameId}</strong></div>
                  <div>Page ID: {frame.pageId ?? 'empty'}</div>
                  <div>Pin Count: {frame.pinCount}</div>
                  <div>Dirty: {frame.isDirty ? 'Yes' : 'No'}</div>
                </div>
              ))}
            </div>

            <h3 style={{ fontSize: '0.9rem', color: '#a7f3d0', margin: '0 0 0.5rem 0' }}>Heap Disk Page File Storage</h3>
            <div style={{ flex: 1, overflowY: 'auto', display: 'flex', flexDirection: 'column', gap: '0.5rem' }}>
              {diskPages.map(page => (
                <div key={page.id} style={{ border: '1px solid #1e293b', borderRadius: '6px', padding: '0.5rem', background: 'rgba(15,23,42,0.6)', fontSize: '0.75rem' }}>
                  <div style={{ fontWeight: 'bold', marginBottom: '4px' }}>Page {page.id} - {page.id === 1 ? 'users heap file' : 'orders heap file'}</div>
                  <div style={{ display: 'flex', gap: '0.5rem', flexWrap: 'wrap' }}>
                    {page.slots.map((slot: any, sIdx: number) => {
                      const data = page.data[sIdx] ? JSON.parse(page.data[sIdx]) : null;
                      return (
                        <div key={sIdx} style={{ padding: '0.3rem', borderRadius: '4px', background: slot.active ? 'rgba(52,211,153,0.15)' : 'rgba(239,68,68,0.15)', border: `1px solid ${slot.active ? '#10b981' : '#ef4444'}` }}>
                          <div>Slot {sIdx}</div>
                          {data && (
                            <div>
                              <div>ID: {data.id}</div>
                              <div>xmin: {data.xmin} | xmax: {data.xmax}</div>
                            </div>
                          )}
                        </div>
                      );
                    })}
                  </div>
                </div>
              ))}
            </div>
          </div>

          <div className="glass-panel" style={{ padding: '1.5rem', height: '330px', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '1rem' }}>
              <Database size={20} color="#fbbf24" />
              <h2 style={{ margin: 0, fontSize: '1.2rem' }}>Primary Key B+ Tree Index Visualization (users_pk)</h2>
            </div>
            <div style={{ flex: 1, overflow: 'auto', display: 'flex', justifyContent: 'center', alignItems: 'center', background: '#020617', borderRadius: '8px', border: '1px solid #1e293b', padding: '1rem' }}>
              {bTreeNodes ? renderTree(bTreeNodes) : <div style={{ color: '#64748b' }}>No index nodes found</div>}
            </div>
          </div>
        </div>
      </div>

      {/* Engine Performance Benchmarks Panel */}
      <div className="glass-panel" style={{ padding: '1.5rem', marginTop: '1.5rem', display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
            <Activity size={22} color="#6366f1" />
            <h2 style={{ margin: 0, fontSize: '1.2rem' }}>Engine Performance Benchmarks</h2>
          </div>
          <button 
            onClick={runBenchmark} 
            disabled={runningBenchmark}
            className="glow-btn-primary" 
            style={{ display: 'flex', alignItems: 'center', gap: '8px', padding: '0.5rem 1.5rem' }}
          >
            {runningBenchmark ? (
              <>
                <span className="spinner" style={{
                  display: 'inline-block',
                  width: '14px',
                  height: '14px',
                  border: '2px solid rgba(255,255,255,0.3)',
                  borderTopColor: '#fff',
                  borderRadius: '50%',
                  animation: 'spin 0.8s linear infinite'
                }} />
                Running Benchmarks...
              </>
            ) : (
              <>
                <Zap size={16} /> Run Performance Suite
              </>
            )}
          </button>
        </div>

        {benchmarkResults ? (
          <div>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: '1rem', marginBottom: '1.5rem' }}>
              {/* Card 1: Writes */}
              <div style={{ background: 'rgba(16,185,129,0.08)', border: '1px solid rgba(16,185,129,0.2)', borderRadius: '10px', padding: '1.2rem', textAlign: 'center' }}>
                <div style={{ fontSize: '0.85rem', color: '#a7f3d0', textTransform: 'uppercase', letterSpacing: '0.05em', marginBottom: '0.5rem', fontWeight: 600 }}>Writes Throughput</div>
                <div style={{ fontSize: '1.8rem', fontWeight: 'bold', color: '#10b981' }}>{benchmarkResults.writesPerSecond.toLocaleString()} <span style={{ fontSize: '0.9rem', fontWeight: 'normal', color: '#64748b' }}>op/s</span></div>
                <div style={{ fontSize: '0.75rem', color: '#64748b', marginTop: '0.3rem' }}>1,000 INSERT operations</div>
              </div>
              
              {/* Card 2: Reads */}
              <div style={{ background: 'rgba(59,130,246,0.08)', border: '1px solid rgba(59,130,246,0.2)', borderRadius: '10px', padding: '1.2rem', textAlign: 'center' }}>
                <div style={{ fontSize: '0.85rem', color: '#bfdbfe', textTransform: 'uppercase', letterSpacing: '0.05em', marginBottom: '0.5rem', fontWeight: 600 }}>Reads Throughput</div>
                <div style={{ fontSize: '1.8rem', fontWeight: 'bold', color: '#3b82f6' }}>{benchmarkResults.readsPerSecond.toLocaleString()} <span style={{ fontSize: '0.9rem', fontWeight: 'normal', color: '#64748b' }}>op/s</span></div>
                <div style={{ fontSize: '0.75rem', color: '#64748b', marginTop: '0.3rem' }}>2,000 Point SELECT lookups</div>
              </div>

              {/* Card 3: Latency with Index */}
              <div style={{ background: 'rgba(245,158,11,0.08)', border: '1px solid rgba(245,158,11,0.2)', borderRadius: '10px', padding: '1.2rem', textAlign: 'center' }}>
                <div style={{ fontSize: '0.85rem', color: '#fde68a', textTransform: 'uppercase', letterSpacing: '0.05em', marginBottom: '0.5rem', fontWeight: 600 }}>Index Scan Latency</div>
                <div style={{ fontSize: '1.8rem', fontWeight: 'bold', color: '#f59e0b' }}>{benchmarkResults.latencyWithIndexMs} <span style={{ fontSize: '0.9rem', fontWeight: 'normal', color: '#64748b' }}>ms</span></div>
                <div style={{ fontSize: '0.75rem', color: '#64748b', marginTop: '0.3rem' }}>B+ Tree Primary Key search</div>
              </div>

              {/* Card 4: Latency without Index */}
              <div style={{ background: 'rgba(239,68,68,0.08)', border: '1px solid rgba(239,68,68,0.2)', borderRadius: '10px', padding: '1.2rem', textAlign: 'center' }}>
                <div style={{ fontSize: '0.85rem', color: '#fca5a5', textTransform: 'uppercase', letterSpacing: '0.05em', marginBottom: '0.5rem', fontWeight: 600 }}>Table Scan Latency</div>
                <div style={{ fontSize: '1.8rem', fontWeight: 'bold', color: '#ef4444' }}>{benchmarkResults.latencyWithoutIndexMs} <span style={{ fontSize: '0.9rem', fontWeight: 'normal', color: '#64748b' }}>ms</span></div>
                <div style={{ fontSize: '0.75rem', color: '#64748b', marginTop: '0.3rem' }}>Full Table scan comparison</div>
              </div>
            </div>

            {/* Performance Comparison Indicator */}
            <div style={{ background: 'rgba(30,41,59,0.5)', border: '1px solid #334155', borderRadius: '8px', padding: '1rem', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
                <Zap size={20} color="#fbbf24" style={{ animation: 'pulse 1.5s infinite' }} />
                <span style={{ fontSize: '0.95rem' }}>
                  Index-based lookup resolves query requests approximately{' '}
                  <strong style={{ color: '#fbbf24', fontSize: '1.1rem' }}>
                    {Math.round(benchmarkResults.latencyWithoutIndexMs / benchmarkResults.latencyWithIndexMs) || 1}x
                  </strong>{' '}
                  faster than full sequential table scans.
                </span>
              </div>
              <div style={{ width: '200px', height: '10px', backgroundColor: '#1e293b', borderRadius: '5px', overflow: 'hidden', display: 'flex' }}>
                <div style={{ width: '95%', backgroundColor: '#f59e0b' }} />
                <div style={{ width: '5%', backgroundColor: '#ef4444' }} />
              </div>
            </div>
          </div>
        ) : (
          <div style={{ padding: '2rem', textAlign: 'center', color: '#64748b', background: '#020617', borderRadius: '8px', border: '1px solid #1e293b' }}>
            Click the "Run Performance Suite" button above to run throughput and latency benchmarks directly on the engine.
          </div>
        )}
      </div>
    </div>
  );
}
