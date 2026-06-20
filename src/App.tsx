import React, { useState, useEffect } from 'react';
import { DatabaseSystem } from './engine/DatabaseSystem';
import { Transaction, LogRecord } from './engine/types';
import { Terminal, Database, ShieldAlert, Cpu, Award, Play, RotateCcw, AlertTriangle } from 'lucide-react';

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
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '2rem' }} className="glass-panel" p="1rem">
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
            <Award size={14} /> Team ConcurrencyMasters
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
                <button onClick={handleCrash} className="glow-btn-danger" style={{ display: 'flex', alignItems: 'center', gap: '5px' }}>
                  <AlertTriangle size={16} /> Crash System
                </button>
                <button onClick={handleRecovery} className="glow-btn-primary" style={{ display: 'flex', alignItems: 'center', gap: '5px' }}>
                  <RotateCcw size={16} /> WAL Recovery
                </button>
              </div>
            </div>
            <div style={{ flex: 1, backgroundColor: '#020617', borderRadius: '8px', padding: '1rem', overflowY: 'auto', fontFamily: 'monospace', fontSize: '0.8rem', border: '1px solid #1e293b', color: '#fca5a5' }}>
              {recoveryLog.length === 0 ? (
                <div style={{ color: '#64748b' }}>System healthy. Trigger a crash or insert transactions to write WAL log records, then click recover to view ARIES replay details.</div>
              ) : (
                recoveryLog.map((log, idx) => (
                  <div key={idx} style={{ marginBottom: '4px' }}>{log}</div>
                ))
              )}
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
    </div>
  );
}
