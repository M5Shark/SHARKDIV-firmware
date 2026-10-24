// Exercise the actual installer script with a fake serial device: no hardware access.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html = readFileSync(new URL('../docs/index.html', import.meta.url), 'utf8');
const source = [...html.matchAll(/<script type="module">([\s\S]*?)<\/script>/g)][0][1]
  .replace('await import("https://cdn.jsdelivr.net/npm/esptool-js@0.7.0/bundle.js")',
    '({ ESPLoader: FakeLoader, Transport: FakeTransport })');

async function fixture({ chip = 'ESP32-S3', cancelled = false, transferError = false } = {}) {
  const elements = new Map();
  const calls = { writes: [], resets: 0, disconnects: 0, registerWrites: 0 };
  const element = id => {
    if (!elements.has(id)) {
      const classes = new Set();
      elements.set(id, {
        hidden: true, checked: false, disabled: false, textContent: '', innerHTML: '',
        dataset: {}, style: {}, handlers: {}, attrs: {},
        classList: { toggle(k, v) { v ??= !classes.has(k); v ? classes.add(k) : classes.delete(k); return v; },
          add(k) { classes.add(k); }, remove(k) { classes.delete(k); }, contains(k) { return classes.has(k); } },
        setAttribute(k, v) { this.attrs[k] = v; }, removeAttribute(k) { delete this.attrs[k]; },
        addEventListener(k, fn) { this.handlers[k] = fn; },
      });
    }
    return elements.get(id);
  };
  class FakeTransport { async disconnect() { calls.disconnects++; } }
  class FakeLoader {
    constructor() { this.chip = { CHIP_NAME: chip }; }
    async main() { return chip; }
    async readReg() { return 0; }
    async writeReg() { calls.registerWrites++; }
    async after() { calls.resets++; }
    async writeFlash(options) {
      calls.writes.push(options);
      options.reportProgress(0, 4, 8);
      if (transferError) throw new Error('test cable failure');
      options.reportProgress(0, 8, 8);
    }
  }
  const sandbox = {
    FakeLoader, FakeTransport, Uint8Array, console, setTimeout,
    document: { getElementById: element },
    navigator: { serial: { async requestPort() {
      if (cancelled) throw new Error('No port selected');
      return {};
    } } },
    fetch: async (url, options) => ({
      ok: !options || options.method !== 'HEAD', status: options?.method === 'HEAD' ? 404 : 200,
      arrayBuffer: async () => new Uint8Array([0xe9, 0, 1, 2]).buffer,
    }),
  };
  vm.createContext(sandbox);
  await vm.runInContext(`(async () => { ${source}\n })()`, sandbox);
  await element('go').handlers.click();
  return { element, calls };
}

const success = await fixture();
assert.equal(success.calls.writes.length, 1);
assert.equal(success.calls.writes[0].fileArray[0].address, 0);
assert.equal(success.calls.writes[0].eraseAll, false);
assert.equal(success.calls.resets, 1);
assert.equal(success.calls.disconnects, 1);
assert.equal(success.element('atrack').attrs['aria-valuenow'], '100');
assert.equal(success.element('go').disabled, false);

const wrongChip = await fixture({ chip: 'ESP32' });
assert.equal(wrongChip.calls.writes.length, 0);
assert.equal(wrongChip.calls.registerWrites, 0);
assert.match(wrongChip.element('notice').innerHTML, /requires an ESP32-S3/);
assert.equal(wrongChip.calls.disconnects, 1);

const cancel = await fixture({ cancelled: true });
assert.equal(cancel.calls.writes.length, 0);
assert.equal(cancel.element('go').disabled, false);
assert.equal(cancel.element('progress').classList.contains('show'), false);

const fail = await fixture({ transferError: true });
assert.equal(fail.calls.resets, 0);
assert.equal(fail.calls.disconnects, 1);
assert.match(fail.element('status').textContent, /50%.*interrupted/);
assert.match(fail.element('log').textContent, /test cable failure/);
assert.equal(fail.element('go').disabled, false);

const manifest = JSON.parse(readFileSync(new URL('../docs/manifest.json', import.meta.url)));
assert.equal(manifest.builds[0].chipFamily, 'ESP32-S3');
assert.equal(manifest.builds[0].parts[0].offset, 0);
const binary = readFileSync(new URL('../docs/' + manifest.builds[0].parts[0].path, import.meta.url));
assert.equal(binary[0], 0xe9, 'Manifest must point to a merged ESP image');
assert.equal(binary[0x10000], 0xe9, 'Merged image must contain app at 0x10000');
console.log('Flasher: success, wrong chip, cancel, transfer failure and manifest checks passed');
