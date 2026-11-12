import {skinNames} from './skins.ts';

/** Resolve the requested preference, not the skin applied by the module.
 * Cost: each preference read; at most one comparison per generated name. */
export function currentSkin(stored: unknown): string {
  return typeof stored === 'string' && skinNames.includes(stored) ? stored : skinNames[0];
}
