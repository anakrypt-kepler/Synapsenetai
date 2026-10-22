// Search engines a NAAN agent can pin. "auto" keeps the rotating harvest policy.
// Ids match naanEngineCatalog() in the node.

export type NaanEngineChoice = { id: string; name: string };

export const NAAN_ENGINES: NaanEngineChoice[] = [
  { id: "auto", name: "Auto" },
  { id: "duckduckgo", name: "DuckDuckGo" },
  { id: "brave", name: "Brave" },
  { id: "wikipedia", name: "Wikipedia" },
  { id: "arxiv", name: "arXiv" },
  { id: "ahmia", name: "Ahmia" },
  { id: "torch", name: "Torch" },
  { id: "duckduckgo-onion", name: "DuckDuckGo onion" },
  { id: "darksearch", name: "DarkSearch" },
  { id: "deepsearch", name: "DeepSearch" },
  { id: "haystak", name: "Haystak" },
];
