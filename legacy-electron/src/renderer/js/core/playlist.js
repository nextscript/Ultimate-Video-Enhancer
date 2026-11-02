// Wiedergabeliste (Gegenstück zu VLCs playlist/vlc_playlist_t).
export class Playlist extends EventTarget {
  constructor() {
    super();
    this.items = [];
    this.index = -1;
    this.repeat = 'none';  // none | all | one
    this.shuffle = false;
    this.history = [];
  }

  emit(type, detail) { this.dispatchEvent(new CustomEvent(type, { detail })); }
  get current() { return this.items[this.index] || null; }

  add(items, { replace = false, insertNext = false } = {}) {
    const list = (items || []).filter((i) => i && i.kind !== 'subtitle').map((i) => Object.assign({ uid: crypto.randomUUID() }, i));
    if (replace) { this.items = []; this.index = -1; this.history = []; }
    const at = insertNext && this.index >= 0 ? this.index + 1 : this.items.length;
    this.items.splice(at, 0, ...list);
    if (!replace && insertNext && this.index >= at) this.index += list.length;
    this.emit('change');
    return at;
  }

  remove(uids) {
    const set = new Set(uids);
    const cur = this.current;
    this.items = this.items.filter((i) => !set.has(i.uid));
    this.index = cur ? this.items.indexOf(cur) : -1;
    this.emit('change', { removedCurrent: cur && set.has(cur.uid) });
  }

  clear() { this.items = []; this.index = -1; this.history = []; this.emit('change', { removedCurrent: true }); }

  move(uid, toIndex) {
    const from = this.items.findIndex((i) => i.uid === uid);
    if (from < 0) return;
    const cur = this.current;
    const [it] = this.items.splice(from, 1);
    this.items.splice(Math.max(0, Math.min(toIndex > from ? toIndex - 1 : toIndex, this.items.length)), 0, it);
    this.index = cur ? this.items.indexOf(cur) : -1;
    this.emit('change');
  }

  sort() {
    const cur = this.current;
    this.items.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' }));
    this.index = cur ? this.items.indexOf(cur) : -1;
    this.emit('change');
  }

  select(i) {
    if (i < 0 || i >= this.items.length) return null;
    if (this.index >= 0 && this.index !== i) this.history.push(this.index);
    this.index = i;
    this.emit('select', this.current);
    return this.current;
  }

  // auto=true: Aufruf am Titelende (berücksichtigt "eins wiederholen")
  nextIndex(auto) {
    const n = this.items.length;
    if (!n) return -1;
    if (auto && this.repeat === 'one') return this.index;
    if (this.shuffle && n > 1) {
      let i; do { i = Math.floor(Math.random() * n); } while (i === this.index);
      return i;
    }
    if (this.index + 1 < n) return this.index + 1;
    return this.repeat === 'all' || !auto ? (this.repeat === 'all' ? 0 : -1) : -1;
  }

  next(auto = false) { const i = this.nextIndex(auto); return i >= 0 ? this.select(i) : null; }

  prev() {
    if (this.shuffle && this.history.length) {
      const i = this.history.pop();
      this.index = i; this.emit('select', this.current); return this.current;
    }
    if (this.index > 0) return this.select(this.index - 1);
    if (this.repeat === 'all' && this.items.length) return this.select(this.items.length - 1);
    return null;
  }

  update(uid, patch) {
    const it = this.items.find((i) => i.uid === uid);
    if (it) { Object.assign(it, patch); this.emit('item', it); }
  }
}
