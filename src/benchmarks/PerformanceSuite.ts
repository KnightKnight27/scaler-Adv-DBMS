import { DatabaseSystem } from '../DatabaseSystem';

export class PerformanceSuite {
  static runBenchmark(): {
    writesPerSecond: number;
    readsPerSecond: number;
    latencyWithIndexMs: number;
    latencyWithoutIndexMs: number;
  } {
    const db = new DatabaseSystem();
    const writeCount = 1000;
    const readCount = 2000;

    const startWrite = performance.now();
    for (let i = 200; i < 200 + writeCount; i++) {
      db.executeSQL(`INSERT INTO users VALUES (${i}, 'User_${i}', ${20 + (i % 30)})`, null);
    }
    const endWrite = performance.now();
    const writeTimeMs = endWrite - startWrite;

    const startIndexRead = performance.now();
    for (let i = 200; i < 200 + readCount; i++) {
      db.executeSQL(`SELECT id, name FROM users WHERE id = ${i}`, null);
    }
    const endIndexRead = performance.now();
    const indexReadTimeMs = endIndexRead - startIndexRead;

    const startTableRead = performance.now();
    for (let i = 0; i < 200; i++) {
      db.executeSQL(`SELECT id, name FROM users WHERE name = 'User_250'`, null);
    }
    const endTableRead = performance.now();
    const tableReadTimeMs = endTableRead - startTableRead;

    return {
      writesPerSecond: Math.round((writeCount / writeTimeMs) * 1000),
      readsPerSecond: Math.round((readCount / indexReadTimeMs) * 1000),
      latencyWithIndexMs: parseFloat((indexReadTimeMs / readCount).toFixed(4)),
      latencyWithoutIndexMs: parseFloat((tableReadTimeMs / 200).toFixed(4))
    };
  }
}
