import { Page, BufferFrame } from '../types';
import { PageManager } from './PageManager';

export class BufferPoolManager {
  private frames: BufferFrame[] = [];
  private pool: Map<number, Page> = new Map();
  private diskPages: Map<number, Page> = new Map();
  constructor(size: number = 8) {
    for (let i = 0; i < size; i++) {
      this.frames.push({
        frameId: i,
        pageId: null,
        pinCount: 0,
        isDirty: false,
        lastAccessed: 0
      });
    }
  }

  getFrames(): BufferFrame[] {
    return this.frames;
  }

  getDiskPages(): Map<number, Page> {
    return this.diskPages;
  }

  fetchPage(pageId: number): Page {
    if (this.pool.has(pageId)) {
      const frame = this.frames.find(f => f.pageId === pageId)!;
      frame.pinCount++;
      frame.lastAccessed = Date.now();
      return this.pool.get(pageId)!;
    }

    let targetFrame = this.frames.find(f => f.pageId === null);
    if (!targetFrame) {
      targetFrame = this.findEvictionCandidate();
      if (!targetFrame) {
        throw new Error("Buffer pool full! All pages are pinned.");
      }
      this.evictFrame(targetFrame);
    }

    let page = this.diskPages.get(pageId);
    if (!page) {
      page = PageManager.createEmptyPage(pageId);
      this.diskPages.set(pageId, page);
    }

    this.pool.set(pageId, page);
    targetFrame.pageId = pageId;
    targetFrame.pinCount = 1;
    targetFrame.isDirty = false;
    targetFrame.lastAccessed = Date.now();

    return page;
  }

  unpinPage(pageId: number, isDirty: boolean) {
    const frame = this.frames.find(f => f.pageId === pageId);
    if (frame) {
      if (frame.pinCount > 0) frame.pinCount--;
      if (isDirty) frame.isDirty = true;
    }
  }

  flushAll() {
    for (const frame of this.frames) {
      if (frame.pageId !== null && frame.isDirty) {
        this.evictFrame(frame);
      }
    }
  }

  private findEvictionCandidate(): BufferFrame | undefined {
    let candidate: BufferFrame | undefined = undefined;
    let minTime = Infinity;
    for (const frame of this.frames) {
      if (frame.pinCount === 0 && frame.lastAccessed < minTime) {
        minTime = frame.lastAccessed;
        candidate = frame;
      }
    }
    return candidate;
  }

  private evictFrame(frame: BufferFrame) {
    if (frame.pageId === null) return;
    if (frame.isDirty) {
      const page = this.pool.get(frame.pageId)!;
      this.diskPages.set(frame.pageId, page);
    }
    this.pool.delete(frame.pageId);
    frame.pageId = null;
    frame.isDirty = false;
    frame.pinCount = 0;
  }
}
