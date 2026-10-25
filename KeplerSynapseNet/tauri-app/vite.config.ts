import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";

// Svelte's package entry is the SSR runtime unless the "browser" export
// condition is selected. That runtime's onMount is a no-op, so a production
// bundle drops the boot callback and the splash never leaves. Point the
// package entry at the DOM runtime.
const browserSvelte = fileURLToPath(
  new URL("./node_modules/svelte/src/runtime/index.js", import.meta.url),
);

export default defineConfig({
  plugins: [svelte()],
  resolve: {
    alias: [{ find: /^svelte$/, replacement: browserSvelte }],
  },
  clearScreen: false,
  server: {
    port: 5173,
    strictPort: true,
  },
  envPrefix: ["VITE_", "TAURI_"],
  build: {
    target: "esnext",
    minify: !process.env.TAURI_DEBUG ? "esbuild" : false,
    sourcemap: !!process.env.TAURI_DEBUG,
  },
});
