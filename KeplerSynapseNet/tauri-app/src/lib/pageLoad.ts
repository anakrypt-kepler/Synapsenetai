import { writable } from "svelte/store";

// True while a tab is fetching. The bar listens to this and does not take clicks or scroll.
export const pageBusy = writable(false);

let generation = 0;

// Returns a closer. A newer tab's begin() makes an older closer a no-op.
export function beginPageLoad(): () => void {
  const token = ++generation;
  pageBusy.set(true);
  return () => {
    if (token === generation) pageBusy.set(false);
  };
}
