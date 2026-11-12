import test from 'node:test';
import assert from 'node:assert/strict';
import {currentSkin} from '../src/skin.ts';
import {skinNames} from '../src/skins.ts';

test('currentSkin preserves every generated name and defaults invalid preferences', () => {
  for (const name of skinNames) assert.equal(currentSkin(name), name);
  for (const value of [undefined, null, '', 7, {}, 'unknown', 'constructor', '__proto__']) {
    assert.equal(currentSkin(value), skinNames[0]);
  }
});
