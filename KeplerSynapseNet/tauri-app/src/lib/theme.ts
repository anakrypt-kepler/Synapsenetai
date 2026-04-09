import { writable } from "svelte/store";

export type Theme = "dark" | "light";

const STORAGE_KEY = "synapsenet-theme";

function getInitialTheme(): Theme {
  const stored = localStorage.getItem(STORAGE_KEY);
  if (stored === "light" || stored === "dark") {
    return stored;
  }
  return "dark";
}

function createThemeStore() {
  const { subscribe, set, update } = writable<Theme>(getInitialTheme());

  function apply(theme: Theme) {
    document.documentElement.setAttribute("data-theme", theme);
    localStorage.setItem(STORAGE_KEY, theme);
  }

  apply(getInitialTheme());

  return {
    subscribe,
    toggle() {
      update((current) => {
        const next: Theme = current === "dark" ? "light" : "dark";
        apply(next);
        set(next);
        return next;
      });
    },
    set(theme: Theme) {
      apply(theme);
      set(theme);
    },
  };
}

export const theme = createThemeStore();
