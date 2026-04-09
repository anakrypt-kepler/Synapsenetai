const vscode = require("vscode");
const http = require("http");
const https = require("https");
const fs = require("fs");
const os = require("os");
const path = require("path");
const cp = require("child_process");

function getConfig() {
  const cfg = vscode.workspace.getConfiguration("synapsenet");
  return {
    host: cfg.get("rpcHost", "127.0.0.1"),
    port: cfg.get("rpcPort", 8332),
    rpcAuthToken: String(cfg.get("rpcAuthToken", process.env.SYNAPSENET_RPC_AUTH_TOKEN || "") || "").trim(),
    rpcCookieFile: String(cfg.get("rpcCookieFile", "") || "").trim(),
    maxTokens: cfg.get("aiMaxTokens", 256),
    temperature: cfg.get("aiTemperature", 0.2),
    inlineEnabled: cfg.get("inlineEnabled", true),
    inlineMaxTokens: cfg.get("inlineMaxTokens", 96),
    inlineDebounceMs: cfg.get("inlineDebounceMs", 250),
    inlineTemperature: cfg.get("inlineTemperature", cfg.get("aiTemperature", 0.2)),
    patchMaxTokens: cfg.get("patchMaxTokens", 1024),
    patchTemperature: cfg.get("patchTemperature", 0.2),
    remoteSessionId: String(cfg.get("remoteSessionId", "") || ""),
    remoteUseByDefault: cfg.get("remoteUseByDefault", false),
    questRepo: String(cfg.get("questRepo", "") || "").trim(),
    questToken: String(cfg.get("questToken", "") || "").trim(),
    questMinSubmitPowBits: Math.max(1, Math.min(256, Number(cfg.get("questMinSubmitPowBits", 16)) || 16))
  };
}

function resolveRpcCookiePath(cookieFile) {
  const raw = String(cookieFile || "").trim();
  if (!raw) {
    return path.join(os.homedir(), ".synapsenet", "rpc.cookie");
  }
  if (path.isAbsolute(raw)) return raw;
  return path.join(os.homedir(), ".synapsenet", raw);
}

function loadRpcAuthHeader(cfg) {
  const explicit = String(cfg.rpcAuthToken || "").trim();
  if (explicit) {
    if (/^(Bearer|Basic)\s+/i.test(explicit)) return explicit;
    return "Bearer " + explicit;
  }

  const cookiePath = resolveRpcCookiePath(cfg.rpcCookieFile);
  try {
    const token = String(fs.readFileSync(cookiePath, "utf8") || "").trim();
    if (token) return "Bearer " + token;
  } catch (_) {}

  return "";
}

function rpcCall(method, params) {
  const cfg = getConfig();
  const payload = JSON.stringify({ jsonrpc: "2.0", id: 1, method, params });
  const options = {
    hostname: cfg.host,
    port: cfg.port,
    path: "/",
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Content-Length": Buffer.byteLength(payload)
    }
  };
  const authHeader = loadRpcAuthHeader(cfg);
  if (authHeader) options.headers.Authorization = authHeader;

  return new Promise((resolve, reject) => {
    const req = http.request(options, (res) => {
      let data = "";
      res.setEncoding("utf8");
      res.on("data", (chunk) => (data += chunk));
      res.on("end", () => {
        try {
          const parsed = JSON.parse(data);
          if (parsed && parsed.error) {
            const msg = parsed.error && parsed.error.message ? parsed.error.message : "RPC error";
            reject(new Error(msg));
            return;
          }
          resolve(parsed.result);
        } catch (e) {
          reject(e);
        }
      });
    });
    const timeoutMs = method === "ai.complete" ? 300000 : 15000;
    req.setTimeout(timeoutMs, () => req.destroy(new Error("RPC timeout")));
    req.on("error", reject);
    req.write(payload);
    req.end();
  });
}

function runProcess(bin, args, opts) {
  const cwd = opts && opts.cwd ? opts.cwd : process.cwd();
  const timeoutMs = opts && opts.timeoutMs ? opts.timeoutMs : 15000;

  return new Promise((resolve, reject) => {
    const child = cp.spawn(bin, args, {
      cwd,
      env: process.env,
      shell: false
    });

    let stdout = "";
    let stderr = "";
    let timedOut = false;
    let settled = false;
    const timer = setTimeout(() => {
      timedOut = true;
      try {
        child.kill("SIGKILL");
      } catch (_) {}
    }, timeoutMs);

    const finish = (err, result) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (err) {
        reject(err);
        return;
      }
      resolve(result);
    };

    child.stdout.on("data", (buf) => {
      stdout += String(buf);
    });
    child.stderr.on("data", (buf) => {
      stderr += String(buf);
    });
    child.on("error", (err) => finish(err));
    child.on("close", (code) => {
      if (timedOut) {
        const e = new Error(bin + " timeout");
        e.stdout = stdout;
        e.stderr = stderr;
        e.code = -1;
        finish(e);
        return;
      }
      if (code !== 0) {
        const msg = String(stderr || stdout || (bin + " exited with code " + String(code))).trim();
        const e = new Error(msg);
        e.stdout = stdout;
        e.stderr = stderr;
        e.code = code;
        finish(e);
        return;
      }
      finish(null, { stdout, stderr, code });
    });
  });
}

function runGit(args, cwd, timeoutMs) {
  return runProcess("git", args, { cwd, timeoutMs: timeoutMs || 15000 });
}

function isValidRepoName(repo) {
  return /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(String(repo || "").trim());
}

function normalizeRepoName(repo) {
  return String(repo || "").trim();
}

function equalFold(a, b) {
  return String(a || "").toLowerCase() === String(b || "").toLowerCase();
}

function githubRepoFromRemote(remoteUrl) {
  let raw = String(remoteUrl || "").trim();
  if (!raw) return "";
  if (raw.endsWith(".git")) raw = raw.slice(0, -4);

  let m = /^git@github\.com:([^\/\s]+\/[^\/\s]+)$/i.exec(raw);
  if (m) return m[1];
  m = /^ssh:\/\/git@github\.com\/([^\/\s]+\/[^\/\s]+)$/i.exec(raw);
  if (m) return m[1];
  m = /^https?:\/\/github\.com\/([^\/\s]+\/[^\/\s]+)$/i.exec(raw);
  if (m) return m[1];
  return "";
}

function buildGitHubRepoApiPath(repo, suffix) {
  const parts = normalizeRepoName(repo).split("/");
  if (parts.length !== 2) throw new Error("repo must be owner/name");
  return "/repos/" + encodeURIComponent(parts[0]) + "/" + encodeURIComponent(parts[1]) + (suffix || "");
}

function githubRequest(method, apiPath, token, body, timeoutMs) {
  const bodyText = body ? JSON.stringify(body) : "";
  const options = {
    hostname: "api.github.com",
    path: apiPath,
    method,
    headers: {
      Accept: "application/vnd.github+json",
      "User-Agent": "synapsenet-vscode",
      "X-GitHub-Api-Version": "2022-11-28"
    }
  };
  if (token) options.headers.Authorization = "Bearer " + String(token).trim();
  if (bodyText) {
    options.headers["Content-Type"] = "application/json";
    options.headers["Content-Length"] = Buffer.byteLength(bodyText);
  }

  return new Promise((resolve, reject) => {
    const req = https.request(options, (res) => {
      let data = "";
      res.setEncoding("utf8");
      res.on("data", (chunk) => (data += chunk));
      res.on("end", () => {
        let parsed = null;
        if (data) {
          try {
            parsed = JSON.parse(data);
          } catch (_) {
            parsed = null;
          }
        }
        if (res.statusCode < 200 || res.statusCode >= 300) {
          const msg = parsed && parsed.message ? String(parsed.message) : String(data || "").trim();
          reject(new Error("github http " + String(res.statusCode) + ": " + msg));
          return;
        }
        resolve(parsed !== null ? parsed : data);
      });
    });
    req.setTimeout(timeoutMs || 15000, () => req.destroy(new Error("GitHub timeout")));
    req.on("error", reject);
    if (bodyText) req.write(bodyText);
    req.end();
  });
}

function leadingZeroBitsHex(hashHex) {
  const h = String(hashHex || "").trim().toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(h)) throw new Error("submitId must be 64 hex chars");
  const b = Buffer.from(h, "hex");
  let total = 0;
  for (let i = 0; i < b.length; i++) {
    const v = b[i];
    if (v === 0) {
      total += 8;
      continue;
    }
    for (let bit = 7; bit >= 0; bit--) {
      if (((v >> bit) & 1) === 0) {
        total++;
      } else {
        return total;
      }
    }
  }
  return total;
}

function normalizeQuestActive(value) {
  if (!value || typeof value !== "object") return null;
  const issue = Number(value.issue || value.activeIssue || value.number || 0);
  if (!Number.isFinite(issue) || issue <= 0) return null;
  return {
    issue: Math.trunc(issue),
    title: String(value.title || value.activeTitle || ""),
    url: String(value.url || value.activeUrl || "")
  };
}

function updateQuestStatusBar(item, active) {
  if (!item) return;
  const q = normalizeQuestActive(active);
  if (!q) {
    item.hide();
    return;
  }
  item.text = "$(target) Q#" + String(q.issue);
  item.tooltip = q.title ? ("Active quest: #" + String(q.issue) + " " + q.title) : ("Active quest: #" + String(q.issue));
  item.show();
}

async function persistQuestActive(context, questState, statusBar, active) {
  const normalized = normalizeQuestActive(active);
  questState.active = normalized;
  await context.globalState.update("synapsenet.quest.active", normalized);
  updateQuestStatusBar(statusBar, normalized);
}

function questActionKey(action) {
  if (action === "fork") return "synapsenet.quest.lastForkAt";
  if (action === "pr") return "synapsenet.quest.lastPrAt";
  throw new Error("unknown quest action");
}

function questActionCooldownSec(action) {
  if (action === "fork") return 10 * 60;
  if (action === "pr") return 60;
  throw new Error("unknown quest action");
}

function questRateLimitRemaining(context, action, nowSec) {
  const now = Number.isFinite(nowSec) ? nowSec : Math.floor(Date.now() / 1000);
  const last = Number(context.globalState.get(questActionKey(action), 0) || 0);
  if (!Number.isFinite(last) || last <= 0) return 0;
  const wait = questActionCooldownSec(action) - (now - last);
  return wait > 0 ? wait : 0;
}

async function questRecordAction(context, action, nowSec) {
  const now = Number.isFinite(nowSec) ? nowSec : Math.floor(Date.now() / 1000);
  await context.globalState.update(questActionKey(action), now);
}

function normalizeIssueTitle(s) {
  let t = String(s || "").toLowerCase().trim();
  t = t
    .replace(/[:\-_.,;()\[\]{}!?#\/\\\"']/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  if (t.length < 10) return "";
  return t;
}

function detectDuplicateIssueMap(items) {
  const buckets = new Map();
  for (const it of items) {
    const key = normalizeIssueTitle(it.title);
    if (!key || !it.number) continue;
    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(it.number);
  }
  const dup = new Set();
  for (const nums of buckets.values()) {
    if (!Array.isArray(nums) || nums.length < 2) continue;
    for (const n of nums) dup.add(Number(n));
  }
  return dup;
}

async function githubFetchOpenIssues(repo, token) {
  const pathPart = buildGitHubRepoApiPath(repo, "/issues?state=open&per_page=50");
  const out = await githubRequest("GET", pathPart, token, null, 15000);
  if (!Array.isArray(out)) return [];
  const issues = [];
  for (const it of out) {
    if (!it || typeof it !== "object") continue;
    if (it.pull_request) continue;
    const number = Number(it.number || 0);
    const title = String(it.title || "").trim();
    const htmlUrl = String(it.html_url || "").trim();
    if (!Number.isFinite(number) || number <= 0 || !title) continue;
    issues.push({
      number: Math.trunc(number),
      title,
      html_url: htmlUrl
    });
  }
  issues.sort((a, b) => a.number - b.number);
  return issues;
}

async function githubFetchDefaultBranch(repo, token) {
  const pathPart = buildGitHubRepoApiPath(repo, "");
  const out = await githubRequest("GET", pathPart, token, null, 15000);
  const branch = out && out.default_branch ? String(out.default_branch).trim() : "";
  if (!branch) throw new Error("github: empty default_branch");
  return branch;
}

async function githubCreatePullRequest(repo, token, params) {
  const pathPart = buildGitHubRepoApiPath(repo, "/pulls");
  const body = {
    title: params.title,
    head: params.head,
    base: params.base,
    body: params.body,
    draft: !!params.draft
  };
  const out = await githubRequest("POST", pathPart, token, body, 20000);
  if (!out || !out.html_url) throw new Error("github: empty pr url");
  return {
    number: Number(out.number || 0),
    html_url: String(out.html_url || "")
  };
}

async function githubCreateFork(repo, token) {
  const pathPart = buildGitHubRepoApiPath(repo, "/forks");
  const out = await githubRequest("POST", pathPart, token, null, 20000);
  if (!out || !out.full_name) throw new Error("github: empty fork full_name");
  return {
    full_name: String(out.full_name || ""),
    html_url: String(out.html_url || ""),
    clone_url: String(out.clone_url || ""),
    ssh_url: String(out.ssh_url || "")
  };
}

async function validateQuestSubmitId(submitId, minPowBits) {
  const sid = String(submitId || "").trim().toLowerCase();
  if (!sid) throw new Error("submitId required");
  const bits = leadingZeroBitsHex(sid);
  if (bits < minPowBits) {
    throw new Error("submitId PoW too low: " + String(bits) + " < " + String(minPowBits) + " leading-zero bits");
  }
  const fetched = await rpcCall("poe.fetch_code", { id: sid });
  if (!fetched || !fetched.submitId) throw new Error("poe.fetch_code returned empty submitId");
  return fetched;
}

function parseMaybeJson(value) {
  if (typeof value !== "string") return value;
  try {
    return JSON.parse(value);
  } catch (_) {
    return value;
  }
}

function toRemoteOffers(value) {
  const parsed = parseMaybeJson(value);
  if (Array.isArray(parsed)) return parsed;
  if (parsed && Array.isArray(parsed.offers)) return parsed.offers;
  return [];
}

function shortId(value, len) {
  const s = String(value || "");
  const n = Number.isFinite(len) ? len : 10;
  if (s.length <= n) return s;
  return s.slice(0, n) + "…";
}

function formatAtoms(v) {
  const n = Number(v || 0);
  if (!Number.isFinite(n) || n < 0) return "0";
  return String(Math.trunc(n));
}

function formatSlots(maxSlots, usedSlots) {
  const max = Number(maxSlots || 0);
  const used = Number(usedSlots || 0);
  if (!Number.isFinite(max) || max <= 0) return "0/0";
  const safeUsed = Number.isFinite(used) && used > 0 ? Math.min(Math.trunc(used), Math.trunc(max)) : 0;
  return String(Math.trunc(max - safeUsed)) + "/" + String(Math.trunc(max));
}

async function persistRemoteSession(context, chatState, session) {
  const normalized = session && session.sessionId ? session : null;
  chatState.remoteSession = normalized;
  await context.globalState.update("synapsenet.remoteSession", normalized);
  const cfg = vscode.workspace.getConfiguration("synapsenet");
  await cfg.update("remoteSessionId", normalized ? String(normalized.sessionId) : "", vscode.ConfigurationTarget.Global);
}

function sleepMs(ms, token) {
  return new Promise((resolve) => {
    if (!ms || ms <= 0) return resolve();
    const t = setTimeout(resolve, ms);
    if (token) {
      token.onCancellationRequested(() => {
        clearTimeout(t);
        resolve();
      });
    }
  });
}

function normalizeCitations(input) {
  if (!input) return [];
  const raw = input.replace(/;/g, ",");
  const parts = raw.split(",").map((s) => s.trim()).filter(Boolean);
  return parts;
}

function cleanModelText(text) {
  if (!text) return "";
  let t = String(text);
  t = t.replace(/^\s*```[a-zA-Z0-9_-]*\s*/m, "");
  t = t.replace(/```+\s*$/m, "");
  return t;
}

function stripLeadingRole(text) {
  if (!text) return "";
  let t = String(text);
  t = t.replace(/^\s*(assistant|ai|synapsenet)\s*:\s*/i, "");
  return t;
}

function buildCompletionPrompt(document, position) {
  const cfg = getConfig();
  const full = document.getText();
  const offset = document.offsetAt(position);
  const prefixMax = 12000;
  const suffixMax = 2000;
  const prefixStart = Math.max(0, offset - prefixMax);
  const suffixEnd = Math.min(full.length, offset + suffixMax);
  const prefix = full.slice(prefixStart, offset);
  const suffix = full.slice(offset, suffixEnd);
  const lang = document.languageId || "text";

  return {
    prompt:
      "You are a local code completion engine. Return only the code continuation (no markdown).\n" +
      "Language: " + lang + "\n" +
      "-----\n" +
      prefix +
      "\n<cursor>\n" +
      suffix +
      "\n-----\n" +
      "Continue after <cursor>.",
    maxTokens: cfg.maxTokens,
    temperature: cfg.temperature
  };
}

function buildInlineCompletionParams(document, position) {
  const cfg = getConfig();
  const full = document.getText();
  const offset = document.offsetAt(position);
  const prefixMax = 8000;
  const suffixMax = 800;
  const prefixStart = Math.max(0, offset - prefixMax);
  const suffixEnd = Math.min(full.length, offset + suffixMax);
  const prefix = full.slice(prefixStart, offset);
  const suffix = full.slice(offset, suffixEnd);
  const lang = document.languageId || "text";

  return {
    prompt:
      "You are a local code completion engine. Return ONLY the code to insert at <cursor> (no markdown, no explanations).\n" +
      "Language: " + lang + "\n" +
      "-----\n" +
      prefix +
      "\n<cursor>\n" +
      suffix +
      "\n-----\n" +
      "Continue after <cursor>.",
    maxTokens: cfg.inlineMaxTokens,
    temperature: cfg.inlineTemperature,
    topP: 0.9,
    topK: 40,
    stopSequences: ["\n```", "```", "\n\n\n"]
  };
}

function getWorkspaceRoot() {
  const folders = vscode.workspace.workspaceFolders;
  if (!folders || folders.length === 0) return null;
  return folders[0].uri.fsPath;
}

function getWorkspaceRelPath(uri) {
  const root = getWorkspaceRoot();
  if (!root) return null;
  const abs = uri.fsPath;
  const rel = path.relative(root, abs);
  if (!rel || rel.startsWith("..") || path.isAbsolute(rel)) return null;
  return rel.replace(/\\/g, "/");
}

function splitLinesPreserve(text) {
  if (!text) return [];
  const t = String(text).replace(/\r\n/g, "\n");
  return t.split("\n");
}

function parseUnifiedDiff(diffText) {
  const lines = splitLinesPreserve(diffText);
  const fileDiffs = [];

  const reDiff = /^diff --git a\/(.+?) b\/(.+)$/;
  const reOld = /^--- (?:a\/(.+)|\/dev\/null)$/;
  const reNew = /^\+\+\+ (?:b\/(.+)|\/dev\/null)$/;
  const reHunk = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/;

  let i = 0;
  while (i < lines.length) {
    const m = reDiff.exec(lines[i]);
    if (!m) {
      i++;
      continue;
    }

    const file = {
      aPath: m[1],
      bPath: m[2],
      oldPath: null,
      newPath: null,
      hunks: []
    };
    i++;

    while (i < lines.length) {
      if (reDiff.test(lines[i])) break;

      const oldM = reOld.exec(lines[i]);
      if (oldM) {
        file.oldPath = oldM[1] || null;
        i++;
        continue;
      }
      const newM = reNew.exec(lines[i]);
      if (newM) {
        file.newPath = newM[1] || null;
        i++;
        continue;
      }

      const h = reHunk.exec(lines[i]);
      if (h) {
        const hunk = {
          oldStart: parseInt(h[1], 10),
          oldCount: h[2] ? parseInt(h[2], 10) : 1,
          newStart: parseInt(h[3], 10),
          newCount: h[4] ? parseInt(h[4], 10) : 1,
          lines: []
        };
        i++;
        while (i < lines.length) {
          if (reDiff.test(lines[i]) || reHunk.test(lines[i])) break;
          const ln = lines[i];
          if (!ln) {
            hunk.lines.push({ type: " ", content: "" });
            i++;
            continue;
          }
          const ch = ln[0];
          if (ch === " " || ch === "+" || ch === "-") {
            hunk.lines.push({ type: ch, content: ln.slice(1) });
            i++;
            continue;
          }
          if (ch === "\\") {
            i++;
            continue;
          }
          i++;
        }
        file.hunks.push(hunk);
        continue;
      }

      i++;
    }

    if (!file.oldPath && !file.newPath) {
      throw new Error("Bad diff: missing ---/+++ headers");
    }
    fileDiffs.push(file);
  }

  if (fileDiffs.length === 0) throw new Error("No diff found");
  return fileDiffs;
}

function applyHunksToText(originalText, hunks) {
  const originalLines = splitLinesPreserve(originalText);
  let idx = 0;
  const out = [];

  for (const hunk of hunks) {
    const target = Math.max(0, (hunk.oldStart || 1) - 1);
    while (idx < target && idx < originalLines.length) out.push(originalLines[idx++]);

    for (const ln of hunk.lines) {
      if (ln.type === " ") {
        const cur = idx < originalLines.length ? originalLines[idx] : undefined;
        if (cur !== ln.content) throw new Error("Hunk context mismatch");
        out.push(cur);
        idx++;
        continue;
      }
      if (ln.type === "-") {
        const cur = idx < originalLines.length ? originalLines[idx] : undefined;
        if (cur !== ln.content) throw new Error("Hunk delete mismatch");
        idx++;
        continue;
      }
      if (ln.type === "+") {
        out.push(ln.content);
        continue;
      }
    }
  }

  while (idx < originalLines.length) out.push(originalLines[idx++]);
  return out.join("\n");
}

async function computePatchEdits(diffText) {
  const root = getWorkspaceRoot();
  if (!root) throw new Error("No workspace folder");

  const fileDiffs = parseUnifiedDiff(diffText);
  const edits = [];

  for (const fd of fileDiffs) {
    const rel = fd.newPath || fd.oldPath;
    if (!rel) throw new Error("Diff missing file path");
    if (rel.startsWith("/") || rel.includes("..")) throw new Error("Unsafe path in diff: " + rel);

    if (fd.newPath === null) {
      throw new Error("Delete file diffs are not supported yet");
    }

    const absPath = path.join(root, rel);
    const uri = vscode.Uri.file(absPath);

    let originalText = "";
    let exists = true;
    try {
      const doc = await vscode.workspace.openTextDocument(uri);
      originalText = doc.getText();
    } catch (_) {
      exists = false;
      originalText = "";
    }

    const newText = applyHunksToText(originalText, fd.hunks);
    edits.push({ relPath: rel, uri, exists, originalText, newText });
  }

  return edits;
}

async function cmdModelStatus(output) {
  const res = await rpcCall("model.status", {});
  output.appendLine(JSON.stringify(res, null, 2));
  vscode.window.showInformationMessage("SynapseNet model: " + (res && res.state ? res.state : "unknown"));
}

async function cmdModelList(output) {
  const res = await rpcCall("model.list", {});
  output.appendLine(JSON.stringify(res, null, 2));

  const items = Array.isArray(res) ? res : [];
  if (items.length === 0) {
    vscode.window.showInformationMessage("No models found (try adding .gguf under ~/.synapsenet/models)");
    return;
  }

  const pick = await vscode.window.showQuickPick(
    items.map((m) => ({
      label: m.name || path.basename(m.path || ""),
      description: m.path || "",
      detail: m.sizeBytes ? String(m.sizeBytes) + " bytes" : "",
      _path: m.path
    })),
    { placeHolder: "Select a model to load" }
  );
  if (!pick || !pick._path) return;

  const loaded = await rpcCall("model.load", { path: pick._path });
  output.appendLine(JSON.stringify(loaded, null, 2));
  if (loaded && loaded.ok) {
    vscode.window.showInformationMessage("Model loaded: " + (loaded.name || pick.label));
  } else {
    vscode.window.showErrorMessage("Model load failed: " + (loaded && loaded.error ? loaded.error : "unknown"));
  }
}

async function cmdModelLoad(output) {
  const home = process.env.HOME || process.env.USERPROFILE || "";
  const defaultDir = home ? path.join(home, ".synapsenet", "models") : "";
  const defaultUri = defaultDir ? vscode.Uri.file(defaultDir) : undefined;

  const picked = await vscode.window.showOpenDialog({
    canSelectMany: false,
    canSelectFiles: true,
    canSelectFolders: false,
    defaultUri,
    filters: { Models: ["gguf"] },
    openLabel: "Load GGUF Model"
  });
  if (!picked || picked.length === 0) return;
  const modelPath = picked[0].fsPath;

  const res = await rpcCall("model.load", { path: modelPath });
  output.appendLine(JSON.stringify(res, null, 2));
  if (res && res.ok) {
    vscode.window.showInformationMessage("Model loaded: " + (res.name || path.basename(modelPath)));
  } else {
    vscode.window.showErrorMessage("Model load failed: " + (res && res.error ? res.error : "unknown"));
  }
}

async function cmdModelUnload(output) {
  const res = await rpcCall("model.unload", {});
  output.appendLine(JSON.stringify(res, null, 2));
  vscode.window.showInformationMessage("Model unloaded");
}

async function pickRemoteOffer(output) {
  const res = await rpcCall("model.remote.list", {});
  output.appendLine(JSON.stringify(res, null, 2));
  const offers = toRemoteOffers(res).filter((o) => o && o.offerId);
  if (offers.length === 0) {
    vscode.window.showInformationMessage("No remote offers available");
    return null;
  }

  const picks = offers.map((o) => {
    const modelId = String(o.modelId || "remote");
    const price = formatAtoms(o.pricePerRequestAtoms);
    const slots = formatSlots(o.maxSlots, o.usedSlots);
    return {
      label: modelId,
      description: "offer " + shortId(o.offerId, 12) + " • " + price + " atoms/req",
      detail: "slots " + slots + " • peer " + shortId(o.peerId, 14),
      _offer: o
    };
  });

  const pick = await vscode.window.showQuickPick(picks, { placeHolder: "Select remote model offer" });
  if (!pick || !pick._offer) return null;
  return pick._offer;
}

async function cmdRemoteList(output) {
  const res = await rpcCall("model.remote.list", {});
  output.appendLine(JSON.stringify(res, null, 2));
  const offers = toRemoteOffers(res);
  const count = Array.isArray(offers) ? offers.length : 0;
  vscode.window.showInformationMessage("Remote offers: " + String(count));
}

async function cmdRemoteRent(context, output, chatState) {
  const offer = await pickRemoteOffer(output);
  if (!offer) return;
  const res = await rpcCall("model.remote.rent", { offerId: String(offer.offerId) });
  output.appendLine(JSON.stringify(res, null, 2));
  const sessionId = String((res && res.sessionId) || "");
  if (!sessionId) {
    throw new Error("Rent failed: no sessionId returned");
  }
  const session = {
    sessionId,
    offerId: String((res && res.offerId) || offer.offerId || ""),
    peerId: String((res && res.peerId) || offer.peerId || ""),
    providerAddress: String((res && res.providerAddress) || offer.providerAddress || ""),
    pricePerRequestAtoms: Number((res && res.pricePerRequestAtoms) || offer.pricePerRequestAtoms || 0),
    expiresAt: Number((res && res.expiresAt) || offer.expiresAt || 0)
  };
  await persistRemoteSession(context, chatState, session);
  vscode.window.showInformationMessage("Remote session ready: " + shortId(sessionId, 14));
}

async function cmdRemoteEnd(context, output, chatState) {
  const cfg = getConfig();
  let sessionId = chatState.remoteSession && chatState.remoteSession.sessionId ? String(chatState.remoteSession.sessionId) : cfg.remoteSessionId;
  if (!sessionId) {
    const entered = await vscode.window.showInputBox({ prompt: "Remote session ID" });
    if (!entered) return;
    sessionId = String(entered).trim();
  }
  if (!sessionId) return;
  const res = await rpcCall("model.remote.end", { sessionId });
  output.appendLine(JSON.stringify(res, null, 2));
  await persistRemoteSession(context, chatState, null);
  vscode.window.showInformationMessage("Remote session ended");
}

async function cmdAiStop(output) {
  const res = await rpcCall("ai.stop", {});
  output.appendLine(JSON.stringify(res, null, 2));
  vscode.window.showInformationMessage("AI stop requested");
}

async function cmdAiComplete(output) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showErrorMessage("No active editor");
    return;
  }

  const doc = editor.document;
  const pos = editor.selection.active;
  const selection = editor.selection && !editor.selection.isEmpty ? doc.getText(editor.selection) : "";

  let params;
  const cfg = getConfig();
  if (selection) {
    params = {
      prompt:
        "You are a local coding assistant. Return only code (no markdown).\n" +
        "Language: " + (doc.languageId || "text") + "\n" +
        "-----\n" +
        selection +
        "\n-----\n" +
        "Improve/continue this code.",
      maxTokens: cfg.maxTokens,
      temperature: cfg.temperature
    };
  } else {
    params = buildCompletionPrompt(doc, pos);
  }

  if (cfg.remoteUseByDefault && cfg.remoteSessionId) {
    params.remote = true;
    params.remoteSessionId = cfg.remoteSessionId;
  }

  const res = await rpcCall("ai.complete", params);
  output.appendLine(JSON.stringify({ request: params, response: res }, null, 2));

  const text = cleanModelText(res && res.text ? String(res.text) : "");
  if (!text) {
    vscode.window.showErrorMessage("Empty completion");
    return;
  }

  await editor.edit((edit) => {
    edit.insert(pos, text);
  });
}

async function cmdPoeSubmitCode(output) {
  const title = await vscode.window.showInputBox({ prompt: "Code contribution title" });
  if (!title) return;

  const patchPick = await vscode.window.showOpenDialog({
    canSelectMany: false,
    canSelectFiles: true,
    canSelectFolders: false,
    openLabel: "Select patch/diff file"
  });
  if (!patchPick || patchPick.length === 0) return;
  const patchPath = patchPick[0].fsPath;

  const citationsRaw = await vscode.window.showInputBox({ prompt: "Citations (optional, comma-separated submitId/contentId hex)" });
  const citations = normalizeCitations(citationsRaw);

  const patch = fs.readFileSync(patchPath, "utf8");
  if (!patch || patch.length === 0) {
    vscode.window.showErrorMessage("Patch file is empty");
    return;
  }

  const params = { title, patch, auto_finalize: true };
  if (citations.length > 0) params.citations = citations;

  const res = await rpcCall("poe.submit_code", params);
  output.appendLine(JSON.stringify(res, null, 2));
  vscode.window.showInformationMessage("Submitted: " + (res.submitId || ""));
}

async function cmdQuestConfigure(output) {
  const cfg = getConfig();
  const repoInput = await vscode.window.showInputBox({
    prompt: "GitHub repo for quests (owner/name)",
    value: cfg.questRepo,
    ignoreFocusOut: true
  });
  if (repoInput === undefined) return;
  const repo = normalizeRepoName(repoInput);
  if (repo && !isValidRepoName(repo)) {
    throw new Error("repo must be owner/name");
  }

  const tokenInput = await vscode.window.showInputBox({
    prompt: "GitHub token (optional). Leave blank to keep current, use '-' to clear",
    password: true,
    ignoreFocusOut: true
  });
  if (tokenInput === undefined) return;
  const tokenAction = String(tokenInput || "").trim();

  const minPowInput = await vscode.window.showInputBox({
    prompt: "Minimum submitId PoW leading-zero bits",
    value: String(cfg.questMinSubmitPowBits),
    ignoreFocusOut: true
  });
  if (minPowInput === undefined) return;
  const minPow = Math.trunc(Number(minPowInput));
  if (!Number.isFinite(minPow) || minPow < 1 || minPow > 256) {
    throw new Error("questMinSubmitPowBits must be between 1 and 256");
  }

  const wsCfg = vscode.workspace.getConfiguration("synapsenet");
  await wsCfg.update("questRepo", repo, vscode.ConfigurationTarget.Global);
  if (tokenAction === "-") {
    await wsCfg.update("questToken", "", vscode.ConfigurationTarget.Global);
  } else if (tokenAction !== "") {
    await wsCfg.update("questToken", tokenAction, vscode.ConfigurationTarget.Global);
  }
  await wsCfg.update("questMinSubmitPowBits", minPow, vscode.ConfigurationTarget.Global);

  output.appendLine(JSON.stringify({
    questRepo: repo,
    questMinSubmitPowBits: minPow,
    tokenUpdated: tokenAction !== ""
  }, null, 2));
  vscode.window.showInformationMessage("GitHub Quests settings updated");
}

async function cmdQuestSelect(context, output, questState, questStatusBar) {
  let cfg = getConfig();
  if (!cfg.questRepo) {
    const action = await vscode.window.showInformationMessage("GitHub quest repo is not configured", "Configure", "Cancel");
    if (action !== "Configure") return;
    await cmdQuestConfigure(output);
    cfg = getConfig();
  }

  const repo = normalizeRepoName(cfg.questRepo);
  if (!repo || !isValidRepoName(repo)) throw new Error("repo must be owner/name");
  const issues = await githubFetchOpenIssues(repo, cfg.questToken);
  output.appendLine(JSON.stringify({ repo, openIssues: issues.length }, null, 2));

  if (issues.length === 0) {
    vscode.window.showInformationMessage("No open issues found for " + repo);
    return;
  }

  const dup = detectDuplicateIssueMap(issues);
  const picks = [{
    label: "Clear active quest",
    description: "",
    detail: "",
    _clear: true
  }];
  for (const it of issues) {
    const dupTag = dup.has(it.number) ? " (dup)" : "";
    picks.push({
      label: "#" + String(it.number) + " " + it.title + dupTag,
      description: it.html_url || "",
      detail: repo,
      _issue: it
    });
  }

  const pick = await vscode.window.showQuickPick(picks, {
    placeHolder: "Select active quest issue",
    matchOnDescription: true,
    matchOnDetail: true
  });
  if (!pick) return;
  if (pick._clear) {
    await persistQuestActive(context, questState, questStatusBar, null);
    vscode.window.showInformationMessage("Active quest cleared");
    return;
  }
  if (!pick._issue) return;

  const it = pick._issue;
  await persistQuestActive(context, questState, questStatusBar, {
    issue: it.number,
    title: it.title,
    url: it.html_url
  });
  vscode.window.showInformationMessage("Active quest: #" + String(it.number));
}

async function cmdQuestClear(context, questState, questStatusBar) {
  await persistQuestActive(context, questState, questStatusBar, null);
  vscode.window.showInformationMessage("Active quest cleared");
}

async function cmdQuestActive(context, output, questState, questStatusBar) {
  const q = normalizeQuestActive(questState.active);
  if (!q) {
    const action = await vscode.window.showInformationMessage("No active quest", "Select Quest");
    if (action === "Select Quest") {
      await cmdQuestSelect(context, output, questState, questStatusBar);
    }
    return;
  }

  const lines = ["Active quest: #" + String(q.issue)];
  if (q.title) lines.push(q.title);
  if (q.url) lines.push(q.url);
  const options = ["Clear"];
  if (q.url) {
    options.unshift("Copy URL");
    options.unshift("Open URL");
  }
  const action = await vscode.window.showInformationMessage(lines.join(" • "), ...options);
  if (action === "Open URL" && q.url) {
    await vscode.env.openExternal(vscode.Uri.parse(q.url));
    return;
  }
  if (action === "Copy URL" && q.url) {
    await vscode.env.clipboard.writeText(q.url);
    return;
  }
  if (action === "Clear") {
    await persistQuestActive(context, questState, questStatusBar, null);
  }
}

async function cmdQuestCheckoutBranch(output, questState) {
  const q = normalizeQuestActive(questState.active);
  if (!q) throw new Error("no active quest selected");
  const root = getWorkspaceRoot();
  if (!root) throw new Error("No workspace folder");

  await runGit(["rev-parse", "--is-inside-work-tree"], root, 8000);

  const cfg = getConfig();
  let warn = "";
  try {
    const remoteOut = await runGit(["remote", "get-url", "origin"], root, 8000);
    const remoteRepo = githubRepoFromRemote(remoteOut.stdout);
    if (cfg.questRepo && remoteRepo && !equalFold(cfg.questRepo, remoteRepo)) {
      warn = "repo mismatch: " + cfg.questRepo + " (config) vs " + remoteRepo + " (origin)";
    }
  } catch (_) {}

  const branch = "quest-" + String(q.issue);
  let exists = true;
  try {
    await runGit(["show-ref", "--verify", "--quiet", "refs/heads/" + branch], root, 8000);
  } catch (_) {
    exists = false;
  }
  if (exists) {
    await runGit(["checkout", branch], root, 12000);
  } else {
    await runGit(["checkout", "-b", branch], root, 12000);
  }

  if (warn) {
    vscode.window.showWarningMessage("Checked out " + branch + " • " + warn);
  } else {
    vscode.window.showInformationMessage("Checked out " + branch + " for #" + String(q.issue));
  }
  output.appendLine(JSON.stringify({ branch, questIssue: q.issue, warning: warn }, null, 2));
}

async function cmdQuestForkRepo(context, output) {
  const cfg = getConfig();
  const repo = normalizeRepoName(cfg.questRepo);
  const token = String(cfg.questToken || "").trim();
  if (!repo || !isValidRepoName(repo)) throw new Error("repo must be owner/name");
  if (!token) throw new Error("token required");

  const nowSec = Math.floor(Date.now() / 1000);
  const wait = questRateLimitRemaining(context, "fork", nowSec);
  if (wait > 0) throw new Error("rate limited: wait " + String(wait) + "s");

  const fork = await githubCreateFork(repo, token);
  await questRecordAction(context, "fork", nowSec);

  const root = getWorkspaceRoot();
  let addedRemote = false;
  let remoteWarn = "";
  if (root) {
    try {
      await runGit(["rev-parse", "--is-inside-work-tree"], root, 8000);
      let remoteURL = fork.clone_url;
      try {
        const originOut = await runGit(["remote", "get-url", "origin"], root, 8000);
        const origin = String(originOut.stdout || "").trim();
        if ((origin.startsWith("git@") || origin.startsWith("ssh://")) && fork.ssh_url) {
          remoteURL = fork.ssh_url;
        }
      } catch (_) {}

      let hasForkRemote = false;
      try {
        const existing = await runGit(["remote", "get-url", "fork"], root, 8000);
        if (String(existing.stdout || "").trim()) hasForkRemote = true;
      } catch (_) {}
      if (!hasForkRemote && remoteURL) {
        await runGit(["remote", "add", "fork", remoteURL], root, 12000);
        addedRemote = true;
      }
    } catch (e) {
      remoteWarn = String(e && e.message ? e.message : e);
    }
  }

  const msgParts = ["Fork created: " + fork.full_name];
  if (fork.html_url) msgParts.push(fork.html_url);
  if (addedRemote) msgParts.push("added git remote 'fork'");
  if (remoteWarn) msgParts.push("failed to add fork remote: " + remoteWarn);
  const msg = msgParts.join(" • ");
  if (remoteWarn) {
    vscode.window.showWarningMessage(msg);
  } else {
    vscode.window.showInformationMessage(msg);
  }

  output.appendLine(JSON.stringify({ repo, fork, addedRemote, remoteWarn }, null, 2));
}

async function cmdQuestCommit(output, questState) {
  const q = normalizeQuestActive(questState.active);
  if (!q) throw new Error("no active quest selected");
  const root = getWorkspaceRoot();
  if (!root) throw new Error("No workspace folder");

  await runGit(["rev-parse", "--is-inside-work-tree"], root, 8000);

  const defaultSubject = q.title ? ("Quest #" + String(q.issue) + ": " + q.title) : ("Quest #" + String(q.issue));
  const subjectInput = await vscode.window.showInputBox({
    prompt: "Commit subject",
    value: defaultSubject,
    ignoreFocusOut: true
  });
  if (subjectInput === undefined) return;
  const subject = String(subjectInput || "").trim() || defaultSubject;

  const submitIdInput = await vscode.window.showInputBox({
    prompt: "PoE CODE submitId (optional, included in commit body)",
    ignoreFocusOut: true
  });
  if (submitIdInput === undefined) return;
  const submitId = String(submitIdInput || "").trim();

  const status = await runGit(["status", "--porcelain"], root, 8000);
  if (!String(status.stdout || "").trim()) throw new Error("no changes to commit");

  await runGit(["add", "-A"], root, 12000);

  const bodyLines = ["Quest: #" + String(q.issue)];
  if (q.url) bodyLines.push("Quest URL: " + q.url);
  if (submitId) bodyLines.push("PoE CODE submitId: " + submitId);
  const commitArgs = ["commit", "-m", subject];
  const body = bodyLines.join("\n");
  if (body) commitArgs.push("-m", body);
  await runGit(commitArgs, root, 25000);

  vscode.window.showInformationMessage("Committed: " + subject);
  output.appendLine(JSON.stringify({ subject, questIssue: q.issue, submitIdIncluded: !!submitId }, null, 2));
}

async function cmdQuestCreatePr(context, output, questState) {
  const cfg = getConfig();
  const repo = normalizeRepoName(cfg.questRepo);
  const token = String(cfg.questToken || "").trim();
  const q = normalizeQuestActive(questState.active);

  if (!repo || !isValidRepoName(repo)) throw new Error("repo must be owner/name");
  if (!token) throw new Error("token required");
  if (!q) throw new Error("no active quest selected");

  const nowSec = Math.floor(Date.now() / 1000);
  const wait = questRateLimitRemaining(context, "pr", nowSec);
  if (wait > 0) throw new Error("rate limited: wait " + String(wait) + "s");

  const submitIdInput = await vscode.window.showInputBox({
    prompt: "PoE CODE submitId",
    ignoreFocusOut: true
  });
  if (!submitIdInput) return;
  const submitId = String(submitIdInput || "").trim();

  const draftPick = await vscode.window.showQuickPick([
    { label: "No", _draft: false },
    { label: "Yes", _draft: true }
  ], { placeHolder: "Create as draft PR?" });
  if (!draftPick) return;
  const draft = !!draftPick._draft;

  const poeRes = await validateQuestSubmitId(submitId, cfg.questMinSubmitPowBits);

  const root = getWorkspaceRoot();
  if (!root) throw new Error("No workspace folder");
  await runGit(["rev-parse", "--is-inside-work-tree"], root, 8000);

  const branchOut = await runGit(["branch", "--show-current"], root, 8000);
  const branch = String(branchOut.stdout || "").trim();
  if (!branch) throw new Error("no current git branch");

  const remoteHeads = await runGit(["ls-remote", "--heads", "origin", branch], root, 12000);
  if (!String(remoteHeads.stdout || "").trim()) throw new Error("branch not found on origin: " + branch + " (push first)");

  let head = branch;
  const warnParts = [];
  try {
    const remoteOut = await runGit(["remote", "get-url", "origin"], root, 8000);
    const remoteRepo = githubRepoFromRemote(remoteOut.stdout);
    if (remoteRepo && !equalFold(remoteRepo, repo)) {
      warnParts.push("repo mismatch: " + repo + " (config) vs " + remoteRepo + " (origin)");
      const owner = remoteRepo.split("/")[0] || "";
      if (owner) head = owner + ":" + branch;
    }
  } catch (_) {}
  if (!poeRes.finalized) warnParts.push("PoE entry not finalized");

  const base = await githubFetchDefaultBranch(repo, token);
  const prTitle = q.title ? ("Quest #" + String(q.issue) + ": " + q.title) : ("Quest #" + String(q.issue));
  const bodyLines = [
    "Quest: #" + String(q.issue),
    "PoE CODE submitId: " + submitId
  ];
  if (q.url) bodyLines.push("Quest URL: " + q.url);
  const body = bodyLines.join("\n") + "\n";

  const pr = await githubCreatePullRequest(repo, token, {
    title: prTitle,
    head,
    base,
    body,
    draft
  });
  await questRecordAction(context, "pr", nowSec);

  const msg = warnParts.length > 0
    ? warnParts.join(" • ") + " • PR created: " + pr.html_url
    : "PR created: " + pr.html_url;
  if (warnParts.length > 0) {
    vscode.window.showWarningMessage(msg);
  } else {
    vscode.window.showInformationMessage(msg);
  }

  output.appendLine(JSON.stringify({
    repo,
    branch,
    head,
    base,
    draft,
    submitId,
    finalized: !!poeRes.finalized,
    prUrl: pr.html_url,
    warnings: warnParts
  }, null, 2));
}

async function cmdSuggestPatch(output, previewState) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) {
    vscode.window.showErrorMessage("No active editor");
    return;
  }

  const relPath = getWorkspaceRelPath(editor.document.uri);
  if (!relPath) {
    vscode.window.showErrorMessage("File must be inside the workspace");
    return;
  }

  const instruction = await vscode.window.showInputBox({ prompt: "Describe the change (returns unified diff patch)" });
  if (!instruction) return;

  const cfg = getConfig();
  const fileText = editor.document.getText();
  if (fileText.length > 200000) {
    vscode.window.showErrorMessage("File too large for patch mode (select a smaller file)");
    return;
  }

  const params = {
    prompt:
      "You are a code editing engine.\n" +
      "Output a unified diff patch that applies cleanly.\n" +
      "No markdown. Only the diff.\n" +
      "Target file: " + relPath + "\n" +
      "Instruction: " + instruction + "\n" +
      "-----\n" +
      fileText +
      "\n-----\n" +
      "Return: diff --git a/" + relPath + " b/" + relPath + " ...",
    maxTokens: cfg.patchMaxTokens,
    temperature: cfg.patchTemperature
  };

  let res;
  try {
    res = await rpcCall("ai.complete", params);
  } catch (e) {
    vscode.window.showErrorMessage("ai.complete failed: " + String(e));
    return;
  }

  const raw = cleanModelText(res && res.text ? String(res.text) : "");
  if (!raw) {
    vscode.window.showErrorMessage("Empty patch");
    return;
  }

  let edits;
  try {
    edits = await computePatchEdits(raw);
  } catch (e) {
    output.appendLine(raw);
    vscode.window.showErrorMessage("Patch parse/apply failed: " + String(e));
    return;
  }

  previewState.lastPatchText = raw;
  previewState.lastEdits = edits;

  if (edits.length === 0) {
    vscode.window.showInformationMessage("No edits in patch");
    return;
  }

  const pick = await vscode.window.showQuickPick(
    edits.map((e) => ({ label: e.relPath })),
    { placeHolder: "Preview which file?" }
  );
  if (!pick) return;
  const chosen = edits.find((e) => e.relPath === pick.label);
  if (!chosen) return;

  const baseUri = vscode.Uri.from({ scheme: "synapsenet-preview", path: "/orig/" + chosen.relPath });
  const newUri = vscode.Uri.from({ scheme: "synapsenet-preview", path: "/new/" + chosen.relPath });
  previewState.previewContent.set(baseUri.toString(), chosen.originalText);
  previewState.previewContent.set(newUri.toString(), chosen.newText);

  await vscode.commands.executeCommand("vscode.diff", baseUri, newUri, "SynapseNet Patch: " + chosen.relPath);

  const choice = await vscode.window.showInformationMessage("Apply patch to workspace?", "Apply", "Cancel");
  if (choice !== "Apply") return;

  const we = new vscode.WorkspaceEdit();
  for (const e of edits) {
    if (!e.exists) {
      we.createFile(e.uri, { overwrite: false, ignoreIfExists: true });
      we.insert(e.uri, new vscode.Position(0, 0), e.newText);
      continue;
    }
    const doc = await vscode.workspace.openTextDocument(e.uri);
    const end = doc.lineCount > 0 ? doc.lineAt(doc.lineCount - 1).range.end : new vscode.Position(0, 0);
    we.replace(e.uri, new vscode.Range(new vscode.Position(0, 0), end), e.newText);
  }
  const ok = await vscode.workspace.applyEdit(we);
  if (!ok) {
    vscode.window.showErrorMessage("Failed to apply workspace edit");
    return;
  }
  await vscode.workspace.saveAll(false);

  const submitChoice = await vscode.window.showInformationMessage("Submit this patch as PoE code contribution?", "Submit", "Skip");
  if (submitChoice !== "Submit") return;

  const title = await vscode.window.showInputBox({ prompt: "Code contribution title", value: instruction.slice(0, 120) });
  if (!title) return;
  const citationsRaw = await vscode.window.showInputBox({ prompt: "Citations (optional, comma-separated submitId/contentId hex)" });
  const citations = normalizeCitations(citationsRaw);

  const submitParams = { title, patch: raw, auto_finalize: true };
  if (citations.length > 0) submitParams.citations = citations;
  const submitRes = await rpcCall("poe.submit_code", submitParams);
  output.appendLine(JSON.stringify(submitRes, null, 2));
  vscode.window.showInformationMessage("Submitted: " + (submitRes.submitId || ""));
}

async function cmdToggleInline() {
  const cfg = vscode.workspace.getConfiguration("synapsenet");
  const cur = cfg.get("inlineEnabled", true);
  await cfg.update("inlineEnabled", !cur, vscode.ConfigurationTarget.Global);
  vscode.window.showInformationMessage("SynapseNet inline completions: " + (!cur ? "ON" : "OFF"));
}

function buildChatPrompt(messages) {
  const maxMsgs = 16;
  const slice = messages.length > maxMsgs ? messages.slice(messages.length - maxMsgs) : messages;
  let p =
    "You are a local AI agent running inside SynapseNet.\n" +
    "Return only the assistant answer (no markdown unless the user asks).\n" +
    "If the user asks for code, output code only.\n" +
    "Conversation:\n";
  for (const m of slice) {
    const role = m.role === "user" ? "User" : "Assistant";
    p += role + ": " + String(m.content || "") + "\n";
  }
  p += "Assistant:";
  return p;
}

function chatHtml(initialState) {
  const s = JSON.stringify(initialState || {});
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>SynapseNet Chat</title>
  <style>
    :root { color-scheme: dark; }
    html, body { height: 100%; margin: 0; padding: 0; background: #0b0d10; color: #e7e7e7; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace; }
    .wrap { display: flex; flex-direction: column; height: 100%; }
    .top { display: flex; gap: 12px; align-items: center; padding: 10px 12px; border-bottom: 1px solid #1b222d; background: #0b0d10; }
    .top .pill { padding: 4px 8px; border: 1px solid #1b222d; border-radius: 999px; font-size: 12px; color: #9fb3c8; }
    .top label { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; color: #cbd5e1; }
    .top input[type="checkbox"] { transform: translateY(1px); }
    .top input[type="number"] { width: 84px; background: #0f141a; border: 1px solid #1b222d; color: #e7e7e7; border-radius: 8px; padding: 4px 8px; }
    .msgs { flex: 1; overflow: auto; padding: 14px 12px; }
    .msg { max-width: 960px; margin: 0 auto 12px auto; padding: 10px 12px; border: 1px solid #1b222d; border-radius: 12px; background: #0f141a; }
    .msg.user { border-color: #1f7a8c55; }
    .msg.assistant { border-color: #7f5af055; }
    .meta { display: flex; gap: 10px; font-size: 11px; color: #94a3b8; margin-bottom: 8px; }
    .content { white-space: pre-wrap; word-break: break-word; line-height: 1.35; }
    .bottom { padding: 10px 12px; border-top: 1px solid #1b222d; background: #0b0d10; }
    .row { max-width: 960px; margin: 0 auto; display: flex; gap: 8px; align-items: center; }
    textarea { flex: 1; resize: none; height: 60px; background: #0f141a; border: 1px solid #1b222d; color: #e7e7e7; border-radius: 12px; padding: 10px 12px; font-family: inherit; }
    button { background: #151b22; border: 1px solid #1b222d; color: #e7e7e7; border-radius: 12px; padding: 10px 12px; cursor: pointer; }
    button:hover { border-color: #2a3546; }
    button.primary { background: #1f7a8c; border-color: #1f7a8c; }
    button.primary:hover { background: #236c7a; border-color: #236c7a; }
    button.danger { background: #5b1f1f; border-color: #5b1f1f; }
    button.danger:hover { background: #6a2424; border-color: #6a2424; }
    .status { max-width: 960px; margin: 10px auto 0 auto; font-size: 12px; color: #9fb3c8; }
    .hint { color: #64748b; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="top">
      <span class="pill" id="modelPill">model: -</span>
      <span class="pill" id="webPill">web: off</span>
      <span class="pill" id="remotePill">remote: off</span>
      <label><input type="checkbox" id="remoteUse" /> Remote</label>
      <label><input type="checkbox" id="webInject" /> Web inject</label>
      <label><input type="checkbox" id="webOnion" /> Onion</label>
      <label><input type="checkbox" id="webTor" /> Tor clearnet</label>
      <label>maxTokens <input type="number" id="maxTokens" min="1" max="8192" /></label>
      <label>temp <input type="number" id="temperature" min="0" max="2" step="0.05" /></label>
    </div>
    <div class="msgs" id="msgs"></div>
    <div class="bottom">
      <div class="row">
        <textarea id="input" placeholder="Ask the local agent…" spellcheck="false"></textarea>
        <button class="primary" id="sendBtn">Send</button>
        <button class="danger" id="stopBtn">Stop</button>
        <button id="clearBtn">Clear</button>
      </div>
      <div class="status" id="statusLine"><span class="hint">Enter to send • Shift+Enter newline</span></div>
    </div>
  </div>

  <script>
    const vscode = acquireVsCodeApi();
    let state = ${s};

    const elMsgs = document.getElementById("msgs");
    const elInput = document.getElementById("input");
    const elSend = document.getElementById("sendBtn");
    const elStop = document.getElementById("stopBtn");
    const elClear = document.getElementById("clearBtn");
    const elModel = document.getElementById("modelPill");
    const elWeb = document.getElementById("webPill");
    const elRemote = document.getElementById("remotePill");
    const elRemoteUse = document.getElementById("remoteUse");
    const elWebInject = document.getElementById("webInject");
    const elWebOnion = document.getElementById("webOnion");
    const elWebTor = document.getElementById("webTor");
    const elMaxTokens = document.getElementById("maxTokens");
    const elTemp = document.getElementById("temperature");
    const elStatus = document.getElementById("statusLine");

    function safeBool(v) { return !!v; }
    function safeNum(v, def) { const n = Number(v); return Number.isFinite(n) ? n : def; }

    function setState(next) {
      state = next || {};
      render();
    }

    function render() {
      const msgs = Array.isArray(state.messages) ? state.messages : [];
      elMsgs.innerHTML = "";
      for (const m of msgs) {
        const wrap = document.createElement("div");
        wrap.className = "msg " + (m.role === "user" ? "user" : "assistant");
        const meta = document.createElement("div");
        meta.className = "meta";
        const who = document.createElement("span");
        who.textContent = m.role === "user" ? "You" : "AI";
        meta.appendChild(who);
        if (m.model) {
          const model = document.createElement("span");
          model.textContent = "model: " + m.model;
          meta.appendChild(model);
        }
        if (m.web && typeof m.web.lastResults === "number") {
          const w = document.createElement("span");
          w.textContent = "web: " + m.web.lastResults + " (clearnet " + (m.web.lastClearnetResults || 0) + ", onion " + (m.web.lastDarknetResults || 0) + ")";
          meta.appendChild(w);
        }
        if (m.remote && m.remote.sessionId) {
          const r = document.createElement("span");
          r.textContent = "remote: " + String(m.remote.sessionId).slice(0, 12);
          meta.appendChild(r);
        }
        wrap.appendChild(meta);
        const content = document.createElement("div");
        content.className = "content";
        content.textContent = String(m.content || "");
        wrap.appendChild(content);
        elMsgs.appendChild(wrap);
      }

      const opts = state.options || {};
      elRemoteUse.checked = safeBool(opts.remoteEnabled);
      elWebInject.checked = safeBool(opts.webInject);
      elWebOnion.checked = safeBool(opts.webOnion);
      elWebTor.checked = safeBool(opts.webTor);
      elMaxTokens.value = String(safeNum(opts.maxTokens, 256));
      elTemp.value = String(safeNum(opts.temperature, 0.2));

      const st = state.status || {};
      elModel.textContent = "model: " + (st.model || "-");
      elWeb.textContent = "web: " + (opts.webInject ? (opts.webOnion ? "clearnet+onion" : "clearnet") : "off");
      const remoteSessionId = st.remote && st.remote.sessionId ? String(st.remote.sessionId) : "";
      elRemote.textContent = "remote: " + (remoteSessionId ? remoteSessionId.slice(0, 12) : "off");

      if (st.generating) {
        elStatus.textContent = "Generating…";
      } else if (st.error) {
        elStatus.textContent = "Error: " + st.error;
      } else {
        elStatus.innerHTML = '<span class="hint">Enter to send • Shift+Enter newline</span>';
      }

      if (st.autoScroll !== false) {
        elMsgs.scrollTop = elMsgs.scrollHeight;
      }
    }

    function collectOptions() {
      return {
        remoteEnabled: elRemoteUse.checked,
        webInject: elWebInject.checked,
        webOnion: elWebOnion.checked,
        webTor: elWebTor.checked,
        maxTokens: safeNum(elMaxTokens.value, 256),
        temperature: safeNum(elTemp.value, 0.2)
      };
    }

    function send() {
      const text = String(elInput.value || "");
      const trimmed = text.trim();
      if (!trimmed) return;
      const options = collectOptions();
      elInput.value = "";
      vscode.postMessage({ type: "send", text: trimmed, options });
    }

    elSend.addEventListener("click", send);
    elStop.addEventListener("click", () => vscode.postMessage({ type: "stop" }));
    elClear.addEventListener("click", () => vscode.postMessage({ type: "clear" }));

    elInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault();
        send();
      }
    });

    window.addEventListener("message", (event) => {
      const msg = event.data;
      if (!msg || !msg.type) return;
      if (msg.type === "state") setState(msg.state);
    });

    vscode.postMessage({ type: "ready" });
    render();
  </script>
</body>
</html>`;
}

async function cmdOpenChat(context, output, chatState) {
  if (chatState.panel) {
    chatState.panel.reveal(vscode.ViewColumn.Beside);
    return;
  }

  const cfg = getConfig();
  chatState.options = chatState.options || {
    remoteEnabled: cfg.remoteUseByDefault && !!cfg.remoteSessionId,
    webInject: false,
    webOnion: false,
    webTor: false,
    maxTokens: cfg.maxTokens,
    temperature: cfg.temperature
  };

  const panel = vscode.window.createWebviewPanel(
    "synapsenetChat",
    "SynapseNet Chat",
    vscode.ViewColumn.Beside,
    { enableScripts: true, retainContextWhenHidden: true }
  );
  chatState.panel = panel;

  const state = {
    messages: chatState.messages || [],
    options: chatState.options,
    status: { model: "", generating: false, error: "", remote: chatState.remoteSession || null }
  };
  panel.webview.html = chatHtml(state);

  const postState = (s) => {
    try {
      panel.webview.postMessage({ type: "state", state: s });
    } catch (_) {}
  };

  const updateStatus = async (s, opts) => {
    let modelName = "";
    try {
      const st = await rpcCall("model.status", {});
      if (st && st.name) modelName = String(st.name);
    } catch (_) {}
    const next = { ...s, options: opts, status: { ...(s.status || {}), model: modelName, remote: chatState.remoteSession || null } };
    postState(next);
    return next;
  };

  let current = await updateStatus(state, chatState.options);

  panel.onDidDispose(() => {
    chatState.panel = null;
  }, null, context.subscriptions);

  panel.webview.onDidReceiveMessage(async (msg) => {
    if (!msg || !msg.type) return;

    if (msg.type === "ready") {
      postState(current);
      return;
    }

    if (msg.type === "clear") {
      chatState.messages = [];
      current = { ...current, messages: [], status: { ...(current.status || {}), generating: false, error: "" } };
      postState(current);
      return;
    }

    if (msg.type === "stop") {
      try {
        await rpcCall("ai.stop", {});
      } catch (_) {}
      return;
    }

    if (msg.type !== "send") return;
    const text = String(msg.text || "").trim();
    if (!text) return;

    const opts = msg.options || {};
    chatState.options = {
      remoteEnabled: !!opts.remoteEnabled,
      webInject: !!opts.webInject,
      webOnion: !!opts.webOnion,
      webTor: !!opts.webTor,
      maxTokens: Math.max(1, Math.min(8192, Number(opts.maxTokens) || cfg.maxTokens)),
      temperature: Math.max(0, Math.min(2, Number(opts.temperature) || cfg.temperature))
    };

    chatState.messages = chatState.messages || [];
    chatState.messages.push({ role: "user", content: text });

    current = { ...current, messages: chatState.messages, options: chatState.options, status: { ...(current.status || {}), generating: true, error: "" } };
    current = await updateStatus(current, chatState.options);

    const prompt = buildChatPrompt(chatState.messages);
    const params = {
      prompt,
      maxTokens: chatState.options.maxTokens,
      temperature: chatState.options.temperature,
      topP: 0.9,
      topK: 40,
      stopSequences: ["\nUser:", "\nAssistant:", "\nYou:"]
    };
    if (chatState.options.webInject) {
      params.webInject = true;
      params.webOnion = chatState.options.webOnion;
      params.webTor = chatState.options.webTor;
      params.webQuery = text;
    }
    if (chatState.options.remoteEnabled) {
      const cfgNow = getConfig();
      const sessionId = chatState.remoteSession && chatState.remoteSession.sessionId
        ? String(chatState.remoteSession.sessionId)
        : String(cfgNow.remoteSessionId || "");
      if (!sessionId) {
        current = { ...current, status: { ...(current.status || {}), generating: false, error: "Remote session is not set" } };
        current = await updateStatus(current, chatState.options);
        return;
      }
      params.remote = true;
      params.remoteSessionId = sessionId;
    }

    try {
      const res = await rpcCall("ai.complete", params);
      const raw = res && res.text ? String(res.text) : "";
      const out = stripLeadingRole(raw).trim();
      chatState.messages.push({
        role: "assistant",
        content: out || "[empty response]",
        model: res && res.model ? String(res.model) : "",
        web: res && res.web ? res.web : null,
        remote: res && res.remote ? res.remote : null
      });
      current = { ...current, messages: chatState.messages, status: { ...(current.status || {}), generating: false, error: "" } };
      current = await updateStatus(current, chatState.options);
    } catch (e) {
      current = { ...current, status: { ...(current.status || {}), generating: false, error: String(e) } };
      current = await updateStatus(current, chatState.options);
    }
  }, null, context.subscriptions);
}

function activate(context) {
  const output = vscode.window.createOutputChannel("SynapseNet");
  output.appendLine("SynapseNet extension activated");

  const previewState = {
    previewContent: new Map(),
    lastPatchText: "",
    lastEdits: []
  };

  const cfg = getConfig();
  const persistedRemote = context.globalState.get("synapsenet.remoteSession");
  const startupRemote = persistedRemote && persistedRemote.sessionId
    ? persistedRemote
    : (cfg.remoteSessionId ? { sessionId: cfg.remoteSessionId } : null);

  const chatState = {
    panel: null,
    messages: [],
    options: null,
    remoteSession: startupRemote
  };

  const questState = {
    active: normalizeQuestActive(context.globalState.get("synapsenet.quest.active"))
  };
  const questStatusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 95);
  questStatusBar.command = "synapsenet.questActive";
  updateQuestStatusBar(questStatusBar, questState.active);

  const inlineCache = new Map();
  let inlineInFlight = false;

  const previewProvider = {
    provideTextDocumentContent: (uri) => {
      return previewState.previewContent.get(uri.toString()) || "";
    }
  };

  function cachePut(key, value) {
    inlineCache.set(key, { value, ts: Date.now() });
    if (inlineCache.size <= 64) return;
    let oldestKey = null;
    let oldestTs = Infinity;
    for (const [k, v] of inlineCache.entries()) {
      if (v.ts < oldestTs) {
        oldestTs = v.ts;
        oldestKey = k;
      }
    }
    if (oldestKey) inlineCache.delete(oldestKey);
  }

  const inlineProvider = {
    provideInlineCompletionItems: async (document, position, _context, token) => {
      const cfg = getConfig();
      if (!cfg.inlineEnabled) return [];
      if (token && token.isCancellationRequested) return [];
      if (inlineInFlight) return [];

      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.uri.toString() !== document.uri.toString()) return [];
      if (editor.selection && !editor.selection.isEmpty) return [];

      await sleepMs(cfg.inlineDebounceMs, token);
      if (token && token.isCancellationRequested) return [];

      const key = document.uri.toString() + "@" + document.version + ":" + document.offsetAt(position);
      const cached = inlineCache.get(key);
      if (cached && cached.value) {
        return [new vscode.InlineCompletionItem(cached.value, new vscode.Range(position, position))];
      }

      const params = buildInlineCompletionParams(document, position);

      inlineInFlight = true;
      try {
        const res = await rpcCall("ai.complete", params);
        const text = cleanModelText(res && res.text ? res.text : "");
        if (!text) return [];

        const clipped = text.length > 4000 ? text.slice(0, 4000) : text;
        cachePut(key, clipped);
        return [new vscode.InlineCompletionItem(clipped, new vscode.Range(position, position))];
      } catch (_) {
        return [];
      } finally {
        inlineInFlight = false;
      }
    }
  };

  context.subscriptions.push(
    output,
    questStatusBar,
    vscode.commands.registerCommand("synapsenet.modelStatus", () => cmdModelStatus(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.modelList", () => cmdModelList(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.modelLoad", () => cmdModelLoad(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.modelUnload", () => cmdModelUnload(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.remoteList", () => cmdRemoteList(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.remoteRent", () => cmdRemoteRent(context, output, chatState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.remoteEnd", () => cmdRemoteEnd(context, output, chatState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.aiComplete", () => cmdAiComplete(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.aiStop", () => cmdAiStop(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.openChat", () => cmdOpenChat(context, output, chatState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.toggleInline", () => cmdToggleInline().catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questConfigure", () => cmdQuestConfigure(output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questSelect", () => cmdQuestSelect(context, output, questState, questStatusBar).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questActive", () => cmdQuestActive(context, output, questState, questStatusBar).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questClear", () => cmdQuestClear(context, questState, questStatusBar).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questCheckoutBranch", () => cmdQuestCheckoutBranch(output, questState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questForkRepo", () => cmdQuestForkRepo(context, output).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questCommit", () => cmdQuestCommit(output, questState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.questCreatePr", () => cmdQuestCreatePr(context, output, questState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.commands.registerCommand("synapsenet.suggestPatch", () => cmdSuggestPatch(output, previewState).catch((e) => vscode.window.showErrorMessage(String(e)))),
    vscode.workspace.registerTextDocumentContentProvider("synapsenet-preview", previewProvider),
    vscode.languages.registerInlineCompletionItemProvider({ pattern: "**" }, inlineProvider),
    vscode.commands.registerCommand("synapsenet.poeSubmitCode", () => cmdPoeSubmitCode(output).catch((e) => vscode.window.showErrorMessage(String(e))))
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
