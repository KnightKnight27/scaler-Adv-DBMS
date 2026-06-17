import { Page, Tuple, Slot } from '../types';

export class PageManager {
  static PAGE_SIZE = 4096;

  static createEmptyPage(pageId: number): Page {
    return {
      header: {
        pageId,
        freeSpaceOffset: 0,
        slotCount: 0
      },
      slots: [],
      data: []
    };
  }

  static insertTuple(page: Page, tuple: Tuple): number {
    const serialized = JSON.stringify(tuple);
    const size = serialized.length;
    
    const slotId = page.slots.length;
    page.slots.push({
      offset: page.header.freeSpaceOffset,
      size,
      active: true
    });
    
    page.data.push(serialized);
    page.header.freeSpaceOffset += size;
    page.header.slotCount++;
    return slotId;
  }

  static getTuple(page: Page, slotId: number): Tuple | null {
    const slot = page.slots[slotId];
    if (!slot || !slot.active) return null;
    return JSON.parse(page.data[slotId]);
  }

  static deleteTuple(page: Page, slotId: number) {
    if (page.slots[slotId]) {
      page.slots[slotId].active = false;
    }
  }

  static updateTuple(page: Page, slotId: number, updated: Tuple) {
    if (page.slots[slotId]) {
      page.data[slotId] = JSON.stringify(updated);
    }
  }
}
