import type { Express, Request, Response, NextFunction } from "express";
import { createServer, type Server } from "http";
import { storage, type DiscussionSort } from "./storage";
import { insertUserSchema, loginSchema, insertDiscussionSchema, updateDiscussionSchema, insertCommentSchema, updateCommentSchema, updateProfileSchema, updateKeysSchema, postMessageSchema, createGroupSchema, postGroupMessageSchema, type User } from "@shared/schema";
import { computeBadges } from "@shared/badges";
import {
  normalizeSnakeCaptchaMoves,
  replaySnakeCaptcha,
  SNAKE_CAPTCHA_GRID_SIZE,
  SNAKE_CAPTCHA_MAX_MOVES,
  SNAKE_CAPTCHA_TARGET_SCORE,
  SNAKE_CAPTCHA_TICK_MS,
} from "@shared/snakeCaptcha";
import crypto from "crypto";
import path from "path";
import fs from "fs";
import rateLimit from "express-rate-limit";
import { z } from "zod";
import { registerUploadRoutes, deleteUploadedFiles } from "./upload";
import { type Attachment } from "@shared/schema";

function parseAttachmentsJson(raw: string | null | undefined): Attachment[] {
  if (!raw) return [];
  try {
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? (parsed as Attachment[]) : [];
  } catch {
    return [];
  }
}

async function computeUserBadges(userId: number, hasPublicKey: boolean, reputation: number): Promise<string[]> {
  const [discussionCount, commentCount, encryptedMessagesSent] = await Promise.all([
    storage.getUserDiscussionCount(userId),
    storage.getUserCommentCount(userId),
    storage.getUserEncryptedMessageCount(userId),
  ]);
  return computeBadges({
    userId,
    reputation,
    discussionCount,
    commentCount,
    hasPublicKey,
    encryptedMessagesSent,
  });
}

const rateLimitDisabled = process.env.NODE_ENV === "test" || process.env.NODE_ENV === "development" || process.env.DISABLE_RATE_LIMIT === "1";
const DAILY_POST_LIMIT = 2;
const EDIT_WINDOW_MS = 60 * 60 * 1000;

function tooManyHandler(_req: Request, res: Response) {
  res.status(429).json({ error: "Too many attempts. Please wait a moment and try again." });
}

function noopLimiter(_req: Request, _res: Response, next: NextFunction) {
  next();
}

const globalLimiter = rateLimitDisabled ? noopLimiter : rateLimit({
  windowMs: 15 * 60 * 1000,
  max: 300,
  standardHeaders: true,
  legacyHeaders: false,
  handler: tooManyHandler,
});

const authLimiter = rateLimitDisabled ? noopLimiter : rateLimit({
  windowMs: 15 * 60 * 1000,
  max: 50,
  standardHeaders: true,
  legacyHeaders: false,
  skipSuccessfulRequests: true,
  handler: tooManyHandler,
});

const writeLimiter = rateLimitDisabled ? noopLimiter : rateLimit({
  windowMs: 5 * 60 * 1000,
  max: 30,
  standardHeaders: true,
  legacyHeaders: false,
  handler: tooManyHandler,
});

const fileUploadLimiter = rateLimitDisabled ? noopLimiter : rateLimit({
  windowMs: 5 * 60 * 1000,
  max: 10,
  standardHeaders: true,
  legacyHeaders: false,
  handler: (_req: Request, res: Response) => {
    res.status(429).json({ message: "Too many file uploads. Please wait and try again." });
  },
});

// Per-user conversation upload quota: 200MB
const CONVERSATION_UPLOAD_QUOTA_BYTES = 200 * 1024 * 1024;

function hashPassword(password: string, salt?: string): string {
  const s = salt || crypto.randomBytes(16).toString("hex");
  const hash = crypto.scryptSync(password, s, 64).toString("hex");
  return `${s}:${hash}`;
}

function verifyPassword(password: string, stored: string): boolean {
  if (!stored.includes(":")) {
    return stored === crypto.createHash("sha256").update(password).digest("hex");
  }
  const [salt, hash] = stored.split(":");
  const derived = crypto.scryptSync(password, salt, 64).toString("hex");
  return crypto.timingSafeEqual(Buffer.from(hash, "hex"), Buffer.from(derived, "hex"));
}

function sanitizeText(raw: unknown, maxLen = 10000): string {
  let s = String(raw ?? "").slice(0, maxLen);
  // Strip all HTML tags
  s = s.replace(/<[^>]*>/g, "");
  // Strip any remaining event handlers (defense in depth)
  s = s.replace(/on\w+\s*=/gi, "");
  // Decode and re-strip (catch entity-encoded tags)
  s = s.replace(/&#x([0-9a-fA-F]+);/g, (_: string, hex: string) => String.fromCharCode(parseInt(hex, 16)))
       .replace(/&#(\d+);/g, (_: string, dec: string) => String.fromCharCode(parseInt(dec, 10)));
  s = s.replace(/<[^>]*>/g, "");
  s = s.replace(/on\w+\s*=/gi, "");
  return s.trim();
}

function getStartOfCurrentLocalDayIso(): string {
  const now = new Date();
  now.setHours(0, 0, 0, 0);
  return now.toISOString();
}

function isWithinEditWindow(createdAt: string): boolean {
  const createdAtMs = Date.parse(createdAt);
  if (Number.isNaN(createdAtMs)) return false;
  return Date.now() - createdAtMs <= EDIT_WINDOW_MS;
}

function generatePixelAvatar(userId: number, username: string): string {
  let seed = 0;
  for (let i = 0; i < username.length; i++) seed = ((seed << 5) - seed + username.charCodeAt(i)) | 0;
  const rng = (function(s) { return function() { s = (s * 1103515245 + 12345) & 0x7fffffff; return s / 0x7fffffff; }; })(Math.abs(seed));

  const colors = ['#00ff41','#f7931a','#a855f7','#ef4444','#3b82f6','#22d3ee','#facc15','#f472b6','#34d399','#fb923c'];
  const color = colors[Math.floor(rng() * colors.length)];
  const size = 128;
  const grid = 8;
  const cell = size / grid;

  let rects = '';
  for (let y = 0; y < grid; y++) {
    for (let x = 0; x <= Math.floor(grid / 2); x++) {
      if (rng() > 0.45) {
        rects += `<rect x="${x*cell}" y="${y*cell}" width="${cell}" height="${cell}" fill="${color}"/>`;
        const mx = grid - 1 - x;
        if (mx !== x) {
          rects += `<rect x="${mx*cell}" y="${y*cell}" width="${cell}" height="${cell}" fill="${color}"/>`;
        }
      }
    }
  }

  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}"><rect width="${size}" height="${size}" fill="#0a0a0a"/>${rects}</svg>`;

  const filename = `avatar_${userId}_${Date.now()}.svg`;
  const filePath = path.join(uploadsDir, filename);
  fs.writeFileSync(filePath, svg);
  return `/uploads/${filename}`;
}

const SESSION_TTL_MS = 7 * 24 * 60 * 60 * 1000;
const SESSION_FILE = path.join(process.cwd(), "sessions.json");

function loadSessionsFromDisk(): Map<string, { userId: number; createdAt: number }> {
  try {
    if (fs.existsSync(SESSION_FILE)) {
      const raw = JSON.parse(fs.readFileSync(SESSION_FILE, "utf-8"));
      const map = new Map<string, { userId: number; createdAt: number }>();
      const now = Date.now();
      for (const [k, v] of Object.entries(raw)) {
        const sess = v as { userId: number; createdAt: number };
        if (now - sess.createdAt < SESSION_TTL_MS) {
          map.set(k, sess);
        }
      }
      return map;
    }
  } catch {}
  return new Map();
}

function saveSessionsToDisk() {
  try {
    const obj: Record<string, { userId: number; createdAt: number }> = {};
    for (const [k, v] of sessions.entries()) {
      obj[k] = v;
    }
    const tmpFile = SESSION_FILE + ".tmp";
    fs.writeFileSync(tmpFile, JSON.stringify(obj), { encoding: "utf-8", mode: 0o600 });
    fs.renameSync(tmpFile, SESSION_FILE);
  } catch {}
}

const sessions = loadSessionsFromDisk();

function purgeExpiredSessions() {
  const now = Date.now();
  let changed = false;
  for (const [token, session] of Array.from(sessions.entries())) {
    if (now - session.createdAt > SESSION_TTL_MS) {
      sessions.delete(token);
      changed = true;
    }
  }
  if (changed) saveSessionsToDisk();
}
const githubOauthStates = new Map<string, number>();
const githubStateTtlMs = 10 * 60 * 1000;
const snakeCaptchaChallenges = new Map<string, {
  seed: string;
  createdAt: number;
  ip: string;
  userAgent: string;
}>();
const snakeCaptchaVerifications = new Map<string, {
  challengeId: string;
  createdAt: number;
  ip: string;
  userAgent: string;
  mode: "register" | "login";
  email: string;
  username: string;
}>();
const snakeCaptchaChallengeTtlMs = 120 * 1000; // 30 second expiry for security
const snakeCaptchaAttempts = new Map<string, { count: number; windowStart: number }>();
const CAPTCHA_MAX_ATTEMPTS_PER_MINUTE = 5;

function checkCaptchaRateLimit(ip: string): boolean {
  const now = Date.now();
  const entry = snakeCaptchaAttempts.get(ip);
  if (!entry || now - entry.windowStart > 60000) {
    snakeCaptchaAttempts.set(ip, { count: 1, windowStart: now });
    return true;
  }
  if (entry.count >= CAPTCHA_MAX_ATTEMPTS_PER_MINUTE) {
    return false;
  }
  entry.count++;
  return true;
}

const snakeCaptchaVerificationTtlMs = 10 * 60 * 1000;
const snakeCaptchaMinSolveMs = process.env.NODE_ENV === "test" ? 0 : 4000;

function generateToken(): string {
  return crypto.randomBytes(32).toString("hex");
}

function getRequestIp(req: Request): string {
  return req.ip || req.socket.remoteAddress || "";
}

function getRequestUserAgent(req: Request): string {
  return req.get("user-agent") || "";
}

function purgeExpiredSnakeCaptchaEntries() {
  const now = Date.now();

  for (const [challengeId, challenge] of Array.from(snakeCaptchaChallenges.entries())) {
    if (now - challenge.createdAt > snakeCaptchaChallengeTtlMs) {
      snakeCaptchaChallenges.delete(challengeId);
    }
  }

  for (const [token, verification] of Array.from(snakeCaptchaVerifications.entries())) {
    if (now - verification.createdAt > snakeCaptchaVerificationTtlMs) {
      snakeCaptchaVerifications.delete(token);
    }
  }
}

function createSnakeCaptchaChallenge(req: Request) {
  purgeExpiredSnakeCaptchaEntries();

  const challengeId = generateToken();
  const seed = crypto.randomBytes(12).toString("hex");

  snakeCaptchaChallenges.set(challengeId, {
    seed,
    createdAt: Date.now(),
    ip: getRequestIp(req),
    userAgent: getRequestUserAgent(req),
  });

  return {
    challengeId,
    seed,
    gridSize: SNAKE_CAPTCHA_GRID_SIZE,
    targetScore: SNAKE_CAPTCHA_TARGET_SCORE,
    tickMs: SNAKE_CAPTCHA_TICK_MS,
  };
}

function getSnakeCaptchaVerification(req: Request, token: string) {
  purgeExpiredSnakeCaptchaEntries();

  const verification = snakeCaptchaVerifications.get(token);
  if (!verification) return null;
  if (verification.ip !== getRequestIp(req)) return null;
  if (verification.userAgent !== getRequestUserAgent(req)) return null;
  return verification;
}

function getGithubOAuthEnv() {
  return {
    clientId: process.env.GITHUB_CLIENT_ID?.trim() ?? "",
    clientSecret: process.env.GITHUB_CLIENT_SECRET?.trim() ?? "",
  };
}

function isGithubOAuthEnabled(): boolean {
  const { clientId, clientSecret } = getGithubOAuthEnv();
  return !!clientId && !!clientSecret;
}

function purgeExpiredGithubStates() {
  const now = Date.now();
  for (const [state, createdAt] of Array.from(githubOauthStates.entries())) {
    if (now - createdAt > githubStateTtlMs) {
      githubOauthStates.delete(state);
    }
  }
}

function createGithubOAuthState(): string {
  purgeExpiredGithubStates();
  const state = generateToken();
  githubOauthStates.set(state, Date.now());
  return state;
}

function consumeGithubOAuthState(state: string): boolean {
  purgeExpiredGithubStates();
  const createdAt = githubOauthStates.get(state);
  if (!createdAt) return false;
  githubOauthStates.delete(state);
  return Date.now() - createdAt <= githubStateTtlMs;
}

function getGithubRedirectUri(req: Request): string {
  const host = req.get("host") || `localhost:${process.env.PORT || "5000"}`;
  return `${req.protocol}://${host}/api/auth/github/callback`;
}

function buildGithubAuthorizeUrl(req: Request, state: string): string {
  const { clientId } = getGithubOAuthEnv();
  const params = new URLSearchParams({
    client_id: clientId,
    redirect_uri: getGithubRedirectUri(req),
    scope: "read:user user:email",
    state,
  });
  return `https://github.com/login/oauth/authorize?${params.toString()}`;
}

function normalizeUsernameCandidate(raw: string): string {
  const cleaned = raw
    .trim()
    .replace(/\s+/g, "_")
    .replace(/[^a-zA-Z0-9_-]/g, "")
    .slice(0, 24);
  return cleaned || "github_user";
}

async function getAvailableUsername(rawBase: string): Promise<string> {
  const base = normalizeUsernameCandidate(rawBase);
  let candidate = base;
  let suffix = 1;

  while (await storage.getUserByUsername(candidate)) {
    const nextSuffix = `_${suffix}`;
    candidate = `${base.slice(0, Math.max(1, 24 - nextSuffix.length))}${nextSuffix}`;
    suffix += 1;
  }

  return candidate;
}

async function buildAuthUserPayload(user: User, avatarUrlOverride?: string) {
  const reputation = user.reputation ?? 0;
  const badges = await computeUserBadges(user.id, !!user.publicKeyJwk, reputation);
  return {
    id: user.id,
    email: user.email,
    username: user.username,
    bio: user.bio,
    avatarUrl: avatarUrlOverride ?? user.avatarUrl,
    reputation,
    badges,
  };
}

function sendPopupAuthResult(res: Response, payload: Record<string, unknown>) {
  const serialized = JSON.stringify(payload)
    .replace(/</g, "\\u003c")
    .replace(/\u2028/g, "\\u2028")
    .replace(/\u2029/g, "\\u2029");

  res
    .status(200)
    .type("html")
    .send(`<!doctype html>
<html lang="en">
  <body style="background:#000;color:#fff;font-family:monospace;padding:24px">
    <p>Completing authentication...</p>
    <script>
      const payload = ${serialized};
      if (window.opener && !window.opener.closed) {
        window.opener.postMessage(payload, window.location.origin);
        window.close();
      }
      document.body.innerHTML = "<p>" + (payload.message || "Authentication finished. You can close this window.") + "</p>";
    </script>
  </body>
</html>`);
}

type GithubTokenResponse = {
  access_token?: string;
  error?: string;
  error_description?: string;
};

type GithubProfile = {
  id: number;
  login: string;
  email: string | null;
  avatar_url: string;
  bio: string | null;
};

type GithubEmail = {
  email: string;
  primary: boolean;
  verified: boolean;
};

const snakeCaptchaVerifySchema = z.object({
  challengeId: z.string().min(1, "Challenge is required"),
  mode: z.enum(["register", "login"]),
  email: z.preprocess(
    (value) => (typeof value === "string" ? value.trim() : value),
    z.string().email("Invalid email address"),
  ).transform((value) => value.trim().toLowerCase()),
  username: z.preprocess(
    (value) => (typeof value === "string" ? value.trim() : value),
    z.string().optional().default(""),
  ).transform((value) => value?.trim() ?? ""),
  moves: z.unknown(),
});

async function exchangeGithubCodeForToken(req: Request, code: string): Promise<string> {
  const { clientId, clientSecret } = getGithubOAuthEnv();
  const tokenRes = await fetch("https://github.com/login/oauth/access_token", {
    method: "POST",
    headers: {
      Accept: "application/json",
      "Content-Type": "application/json",
      "User-Agent": "Synapsenetai",
    },
    body: JSON.stringify({
      client_id: clientId,
      client_secret: clientSecret,
      code,
      redirect_uri: getGithubRedirectUri(req),
    }),
  });

  const tokenData = await tokenRes.json() as GithubTokenResponse;
  if (!tokenRes.ok || !tokenData.access_token) {
    throw new Error(tokenData.error_description || tokenData.error || "GitHub token exchange failed");
  }

  return tokenData.access_token;
}

async function fetchGithubProfile(accessToken: string): Promise<GithubProfile> {
  const profileRes = await fetch("https://api.github.com/user", {
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: `Bearer ${accessToken}`,
      "User-Agent": "Synapsenetai",
    },
  });

  if (!profileRes.ok) {
    throw new Error("Failed to load GitHub profile");
  }

  return await profileRes.json() as GithubProfile;
}

async function fetchGithubEmail(accessToken: string, fallbackEmail: string | null): Promise<string | null> {
  if (fallbackEmail) {
    return fallbackEmail.trim().toLowerCase();
  }

  const emailRes = await fetch("https://api.github.com/user/emails", {
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: `Bearer ${accessToken}`,
      "User-Agent": "Synapsenetai",
    },
  });

  if (!emailRes.ok) {
    return null;
  }

  const emails = await emailRes.json() as GithubEmail[];
  const selected =
    emails.find((entry) => entry.primary && entry.verified)
    || emails.find((entry) => entry.verified)
    || emails[0];

  return selected?.email?.trim().toLowerCase() ?? null;
}

function getUserIdFromToken(req: any): number | null {
  // __SYNAPSE_SSO_PATCH__: accept token from either Authorization header OR
  // the `synapse_token` cookie (used by nginx auth_request to enable SSO
  // between the main site and the self-hosted Gitea at /git/).
  let token: string | null = null;
  const auth = req.headers?.authorization;
  if (auth && typeof auth === "string" && auth.startsWith("Bearer ")) {
    token = auth.slice(7);
  } else {
    const cookieHeader: string =
      (req.headers?.cookie as string | undefined) || "";
    const match = cookieHeader.match(/(?:^|;\s*)synapse_token=([^;]+)/);
    if (match) {
      try { token = decodeURIComponent(match[1]); } catch { token = match[1]; }
    }
  }
  if (!token) return null;
  purgeExpiredSessions();
  const session = sessions.get(token);
  if (!session) return null;
  if (Date.now() - session.createdAt > SESSION_TTL_MS) {
    sessions.delete(token);
    return null;
  }
  return session.userId;
}

const uploadsDir = path.join(process.cwd(), "uploads");
if (!fs.existsSync(uploadsDir)) {
  fs.mkdirSync(uploadsDir, { recursive: true });
}

export async function registerRoutes(
  httpServer: Server,
  app: Express
): Promise<Server> {
  app.set("trust proxy", 1);

  app.use("/uploads", (req, res, next) => {
    const basename = path.basename(req.path);
    if (!/^[A-Za-z0-9_.-]+$/.test(basename) || basename.startsWith(".") || basename.includes("..")) {
      return res.status(400).send("Invalid filename");
    }
    const filePath = path.join(uploadsDir, basename);
    if (!filePath.startsWith(uploadsDir)) {
      return res.status(403).send("Forbidden");
    }
    if (!fs.existsSync(filePath)) {
      return res.status(404).send("Not found");
    }
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.setHeader("Cache-Control", "public, max-age=31536000, immutable");

    const ext = path.extname(basename).toLowerCase();
    // Only safe image and video types may render inline
    const safeInlineExts = [".webp", ".gif", ".jpg", ".jpeg", ".png", ".mp4", ".webm"];
    // Dangerous types that must NEVER render inline
    const dangerousExts = [".svg", ".html", ".htm", ".js", ".mjs", ".php", ".exe", ".bat", ".cmd", ".msi", ".ps1", ".vbs", ".scr"];

    if (dangerousExts.includes(ext)) {
      // Force download for dangerous file types - prevents XSS via SVG, HTML, JS, etc.
      res.setHeader("Content-Disposition", `attachment; filename="${basename}"`);
      res.setHeader("Content-Type", "application/octet-stream");
    } else if (safeInlineExts.includes(ext)) {
      res.setHeader("Content-Disposition", "inline");
    } else {
      res.setHeader("Content-Disposition", `attachment; filename="${basename}"`);
    }
    res.sendFile(filePath);
  });

  app.get("/uploads/:filename/download", (req, res) => {
    const basename = path.basename(req.params.filename);
    if (!/^[A-Za-z0-9_.-]+$/.test(basename) || basename.startsWith(".")) {
      return res.status(400).send("Invalid filename");
    }
    const filePath = path.join(uploadsDir, basename);
    if (!filePath.startsWith(uploadsDir) || !fs.existsSync(filePath)) {
      return res.status(404).send("Not found");
    }
    res.setHeader("Content-Disposition", `attachment; filename="${basename}"`);
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.sendFile(filePath);
  });

  app.use("/api", globalLimiter);

  app.use("/api", async (req, _res, next) => {
    try { const uid = getUserIdFromToken(req); if (uid) await storage.updateLastSeen(uid); } catch {}
    next();
  });

  registerUploadRoutes(app, getUserIdFromToken);

  app.get("/api/auth/github/config", (_req, res) => {
    if (!isGithubOAuthEnabled()) {
      return res.json({
        enabled: false,
        startUrl: null,
        message: "GitHub sign-in is disabled on this server. Set GITHUB_CLIENT_ID and GITHUB_CLIENT_SECRET to enable it.",
      });
    }

    res.json({
      enabled: true,
      startUrl: "/api/auth/github/start",
      message: "",
    });
  });

  app.get("/api/auth/snake-captcha/challenge", authLimiter, (req, res) => {
    const ip = getRequestIp(req);
    if (!checkCaptchaRateLimit(ip)) {
      return res.status(429).json({ message: "Too many captcha attempts. Please wait a minute." });
    }
    res.json(createSnakeCaptchaChallenge(req));
  });

  app.post("/api/auth/snake-captcha/verify", authLimiter, (req, res) => {
    const ip = getRequestIp(req);
    if (!checkCaptchaRateLimit(ip)) {
      return res.status(429).json({ message: "Too many captcha attempts. Please wait a minute." });
    }
    const parsed = snakeCaptchaVerifySchema.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ message: parsed.error.errors[0].message });
    }

    purgeExpiredSnakeCaptchaEntries();
    const challenge = snakeCaptchaChallenges.get(parsed.data.challengeId);
    if (!challenge) {
      return res.status(400).json({ message: "Captcha expired. Start a new one." });
    }
    if (challenge.ip !== getRequestIp(req) || challenge.userAgent !== getRequestUserAgent(req)) {
      return res.status(400).json({ message: "Captcha does not match this browser." });
    }

    if (process.env.NODE_ENV !== "test") {
      const moves = normalizeSnakeCaptchaMoves(parsed.data.moves);
      if (!moves || moves.length === 0) {
        return res.status(400).json({ message: "Captcha moves are missing." });
      }
      if (moves.length > SNAKE_CAPTCHA_MAX_MOVES) {
        return res.status(400).json({ message: "Captcha payload is too large." });
      }

      const replay = replaySnakeCaptcha(challenge.seed, moves, SNAKE_CAPTCHA_TARGET_SCORE);
      if (!replay.success) {
        return res.status(400).json({ message: replay.reason || "Captcha failed." });
      }

      const elapsedMs = Date.now() - challenge.createdAt;
      const expectedMinimumMs = Math.max(
        snakeCaptchaMinSolveMs,
        Math.floor(replay.ticks * (SNAKE_CAPTCHA_TICK_MS * 0.65)),
      );
      if (elapsedMs < expectedMinimumMs) {
        return res.status(400).json({ message: "Captcha completed too quickly. Please try again." });
      }
    }

    snakeCaptchaChallenges.delete(parsed.data.challengeId);

    const verificationToken = generateToken();
    snakeCaptchaVerifications.set(verificationToken, {
      challengeId: parsed.data.challengeId,
      createdAt: Date.now(),
      ip: getRequestIp(req),
      userAgent: getRequestUserAgent(req),
      mode: parsed.data.mode,
      email: parsed.data.email,
      username: parsed.data.username,
    });

    res.json({ verificationToken });
  });

  app.get("/api/auth/github/start", authLimiter, (req, res) => {
    if (!isGithubOAuthEnabled()) {
      return res.status(503).json({
        message: "GitHub sign-in is disabled on this server. Set GITHUB_CLIENT_ID and GITHUB_CLIENT_SECRET to enable it.",
      });
    }

    const state = createGithubOAuthState();
    res.redirect(buildGithubAuthorizeUrl(req, state));
  });

  app.get("/api/auth/github/callback", authLimiter, async (req, res) => {
    try {
      if (!isGithubOAuthEnabled()) {
        return sendPopupAuthResult(res, {
          type: "github-auth-error",
          message: "GitHub sign-in is disabled on this server.",
        });
      }

      const code = typeof req.query.code === "string" ? req.query.code : "";
      const state = typeof req.query.state === "string" ? req.query.state : "";
      const oauthError = typeof req.query.error === "string" ? req.query.error : "";

      if (oauthError) {
        return sendPopupAuthResult(res, {
          type: "github-auth-error",
          message: "GitHub sign-in was cancelled or denied.",
        });
      }

      if (!code || !state || !consumeGithubOAuthState(state)) {
        return sendPopupAuthResult(res, {
          type: "github-auth-error",
          message: "GitHub sign-in failed because the OAuth state was invalid or expired.",
        });
      }

      const accessToken = await exchangeGithubCodeForToken(req, code);
      const githubProfile = await fetchGithubProfile(accessToken);
      const email = await fetchGithubEmail(accessToken, githubProfile.email);

      if (!email) {
        return sendPopupAuthResult(res, {
          type: "github-auth-error",
          message: "GitHub account does not expose a verified email address.",
        });
      }

      let user = await storage.getUserByEmail(email);
      if (!user) {
        const username = await getAvailableUsername(githubProfile.login || email.split("@")[0] || "github_user");
        user = await storage.createUser({
          email,
          username,
          password: hashPassword(generateToken()),
        });
      }

      const profileUpdate: { avatarUrl?: string; bio?: string } = {};
      if (githubProfile.avatar_url && githubProfile.avatar_url !== user.avatarUrl) {
        profileUpdate.avatarUrl = githubProfile.avatar_url;
      }
      if (githubProfile.bio && githubProfile.bio !== user.bio) {
        profileUpdate.bio = githubProfile.bio;
      }
      if (Object.keys(profileUpdate).length > 0) {
        user = (await storage.updateUser(user.id, profileUpdate)) || user;
      }

      const token = generateToken();
      sessions.set(token, { userId: user.id, createdAt: Date.now() });
      saveSessionsToDisk();
      const authUser = await buildAuthUserPayload(user);

      return sendPopupAuthResult(res, {
        type: "github-auth-success",
        token,
        user: authUser,
        message: "GitHub sign-in complete. You can close this window.",
      });
    } catch (err: any) {
      return sendPopupAuthResult(res, {
        type: "github-auth-error",
        message: err?.message || "GitHub sign-in failed.",
      });
    }
  });

  app.post("/api/auth/register", authLimiter, async (req, res) => {
    try {
      const parsed = insertUserSchema.safeParse(req.body);
      if (!parsed.success) {
        return res.status(400).json({ message: parsed.error.errors[0].message });
      }

      const snakeCaptchaToken =
        typeof req.body?.snakeCaptchaToken === "string" ? req.body.snakeCaptchaToken.trim() : "";
      if (!snakeCaptchaToken) {
        return res.status(400).json({ message: "Complete the captcha to create your account." });
      }

      const captchaVerification = getSnakeCaptchaVerification(req, snakeCaptchaToken);
      if (!captchaVerification) {
        return res.status(400).json({ message: "Captcha expired. Please solve it again." });
      }
      if (captchaVerification.mode !== "register") {
        return res.status(400).json({ message: "Captcha no longer matches this signup. Please solve it again." });
      }

      const email = parsed.data.email.trim().toLowerCase();
      const username = parsed.data.username.trim();

      if (captchaVerification.email !== email || captchaVerification.username !== username) {
        return res.status(400).json({ message: "Captcha no longer matches this signup. Please solve it again." });
      }

      const existing = await storage.getUserByEmail(email);
      if (existing) {
        return res.status(400).json({ message: "Email already registered" });
      }

      const existingUsername = await storage.getUserByUsername(username);
      if (existingUsername) {
        return res.status(400).json({ message: "Username already taken" });
      }

      let user;
      try {
        user = await storage.createUser({
          ...parsed.data,
          email,
          username,
          password: hashPassword(parsed.data.password),
        });
      } catch (dbErr: any) {
        const msg = String(dbErr?.message || "");
        if (msg.includes("UNIQUE") && msg.toLowerCase().includes("email")) {
          return res.status(400).json({ message: "Email already registered" });
        }
        if (msg.includes("UNIQUE") && msg.toLowerCase().includes("username")) {
          return res.status(400).json({ message: "Username already taken" });
        }
        throw dbErr;
      }

      const avatarUrl = generatePixelAvatar(user.id, parsed.data.username);
      await storage.updateUser(user.id, { avatarUrl });

      const token = generateToken();
      sessions.set(token, { userId: user.id, createdAt: Date.now() });
      saveSessionsToDisk();
      snakeCaptchaVerifications.delete(snakeCaptchaToken);

      const authUser = await buildAuthUserPayload(user, avatarUrl);
      try {
        const isSecure = req.secure || req.headers["x-forwarded-proto"] === "https";
        res.setHeader("Set-Cookie", `synapse_token=${encodeURIComponent(token)}; Path=/; Max-Age=86400; SameSite=Lax${isSecure ? "; Secure" : ""}; HttpOnly`);
      } catch {}
      res.json({ token, user: authUser });
    } catch (err: any) {
      console.error("Register error:", err);
      res.status(500).json({ message: "Server error" });
    }
  });

  app.post("/api/auth/login", authLimiter, async (req, res) => {
    try {
      const parsed = loginSchema.safeParse(req.body);
      if (!parsed.success) {
        return res.status(400).json({ message: parsed.error.errors[0].message });
      }

      const snakeCaptchaToken =
        typeof req.body?.snakeCaptchaToken === "string" ? req.body.snakeCaptchaToken.trim() : "";
      if (!snakeCaptchaToken) {
        return res.status(400).json({ message: "Complete the captcha to log in." });
      }

      const captchaVerification = getSnakeCaptchaVerification(req, snakeCaptchaToken);
      if (!captchaVerification) {
        return res.status(400).json({ message: "Captcha expired. Please solve it again." });
      }
      if (captchaVerification.mode !== "login") {
        return res.status(400).json({ message: "Captcha no longer matches this login. Please solve it again." });
      }

      const email = parsed.data.email.trim().toLowerCase();
      if (captchaVerification.email !== email) {
        return res.status(400).json({ message: "Captcha no longer matches this login. Please solve it again." });
      }

      const user = await storage.getUserByEmail(email);
      if (!user || !verifyPassword(parsed.data.password, user.password)) {
        return res.status(401).json({ message: "Invalid email or password" });
      }
      if (!user.password.includes(":")) {
        const upgraded = hashPassword(parsed.data.password);
        await storage.updateUser(user.id, { password: upgraded } as any);
      }

      const token = generateToken();
      sessions.set(token, { userId: user.id, createdAt: Date.now() });
      saveSessionsToDisk();
      snakeCaptchaVerifications.delete(snakeCaptchaToken);

      const authUser = await buildAuthUserPayload(user);
      // __SYNAPSE_SSO_PATCH__: also persist the token in a cookie so
      // nginx auth_request can transparently authenticate requests to /git/.
      try {
        res.setHeader(
          "Set-Cookie",
          `synapse_token=${encodeURIComponent(token)}; Path=/; Max-Age=${Math.floor(SESSION_TTL_MS / 1000)}; SameSite=Lax${(req.secure || req.headers["x-forwarded-proto"] === "https") ? "; Secure" : ""}; HttpOnly`,
        );
      } catch {}
      res.json({ token, user: authUser });
    } catch (err: any) {
      console.error("Login error:", err);
      res.status(500).json({ message: "Server error" });
    }
  });


  app.get("/api/internal/auth-check", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const user = await storage.getUser(userId);
    if (!user) return res.status(401).json({ message: "Not found" });
    res.setHeader("X-Auth-User", user.username);
    res.setHeader("X-Auth-Email", user.email);
    res.json({ ok: true });
  });

  app.get("/api/auth/me", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const user = await storage.getUser(userId);
    if (!user) return res.status(404).json({ message: "User not found" });

    res.setHeader("X-Auth-User", user.username);
    res.setHeader("X-Auth-Email", user.email);
    const reputation = user.reputation ?? 0;
    const badges = await computeUserBadges(user.id, !!user.publicKeyJwk, reputation);
    res.json({
      id: user.id,
      email: user.email,
      username: user.username,
      bio: user.bio,
      avatarUrl: user.avatarUrl,
      hasPublicKey: !!user.publicKeyJwk,
      publicKeyJwk: user.publicKeyJwk || null,
      reputation,
      badges,
    });
  });

  app.post("/api/auth/avatar", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const contentType = req.headers["content-type"] || "";
    if (!contentType.startsWith("image/")) {
      return res.status(400).json({ message: "Must be an image file" });
    }

    const chunks: Buffer[] = [];
    req.on("data", (chunk: Buffer) => chunks.push(chunk));
    req.on("end", async () => {
      try {
        const buffer = Buffer.concat(chunks);
        if (buffer.length > 5 * 1024 * 1024) {
          return res.status(400).json({ message: "File too large (max 5MB)" });
        }
        const sharp = (await import("sharp")).default;
        const isGif = buffer[0] === 0x47 && buffer[1] === 0x49 && buffer[2] === 0x46;
        let outBuf: Buffer;
        let ext: string;
        if (isGif) {
          outBuf = buffer;
          ext = "gif";
        } else {
          try {
            outBuf = await sharp(buffer, { failOn: "truncated" })
              .rotate()
              .resize({ width: 256, height: 256, fit: "cover" })
              .webp({ quality: 80 })
              .toBuffer();
          } catch {
            return res.status(400).json({ message: "Invalid or corrupt image file" });
          }
          ext = "webp";
        }
        const filename = `avatar_${userId}_${Date.now()}.${ext}`;
        const filePath = path.join(uploadsDir, filename);
        fs.writeFileSync(filePath, outBuf);
        const avatarUrl = `/uploads/${filename}`;
        await storage.updateUser(userId, { avatarUrl });
        res.json({ avatarUrl });
      } catch (err) {
        res.status(500).json({ message: "Avatar upload failed" });
      }
    });
  });

  app.patch("/api/auth/profile", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const parsed = updateProfileSchema.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ message: parsed.error.errors[0].message });
    }

    const profileData = { ...parsed.data };
    if (profileData.bio) {
      profileData.bio = sanitizeText(profileData.bio, 500);
    }
    const user = await storage.updateUser(userId, profileData);
    if (!user) return res.status(404).json({ message: "User not found" });

    const reputation = user.reputation ?? 0;
    const badges = await computeUserBadges(user.id, !!user.publicKeyJwk, reputation);
    res.json({ id: user.id, email: user.email, username: user.username, bio: user.bio, avatarUrl: user.avatarUrl, reputation, badges });
  });

  app.patch("/api/auth/keys", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const parsed = updateKeysSchema.safeParse(req.body);
    if (!parsed.success) return res.status(400).json({ message: parsed.error.errors[0].message });
    if (parsed.data.publicKeyJwk) await storage.updateUserPublicKey(userId, parsed.data.publicKeyJwk);
    if (parsed.data.privateKeyJwk) await storage.updateUserPrivateKey(userId, parsed.data.privateKeyJwk);
    res.json({ success: true });
  });

  app.get("/api/auth/keys", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const privateKeyJwk = await storage.getUserPrivateKey(userId);
    res.json({ privateKeyJwk });
  });

  app.get("/api/users/:username/public-key", async (req, res) => {
    const user = await storage.getUserByUsername(req.params.username);
    if (!user) return res.status(404).json({ message: "User not found" });
    if (!user.publicKeyJwk) return res.status(404).json({ message: "No public key" });
    res.json({ publicKeyJwk: user.publicKeyJwk });
  });

  app.get("/api/users/:username", async (req, res) => {
    const user = await storage.getUserByUsername(req.params.username);
    if (!user) return res.status(404).json({ message: "User not found" });

    const userId = getUserIdFromToken(req);
    const reputation = user.reputation ?? 0;
    const [disc, followers, following, isFollowing, badges, repostedList] = await Promise.all([
      storage.getUserDiscussions(user.id),
      storage.getFollowerCount(user.id),
      storage.getFollowingCount(user.id),
      userId ? storage.isFollowing(userId, user.id) : Promise.resolve(false),
      computeUserBadges(user.id, !!user.publicKeyJwk, reputation),
      storage.getUserRepostedDiscussions(user.id),
    ]);
    const lastSeenAt = await storage.getLastSeen(user.id);
    const isOnline = lastSeenAt ? (Date.now() - new Date(lastSeenAt).getTime()) < 5 * 60 * 1000 : false;
    res.json({
      id: user.id,
      username: user.username,
      bio: user.bio,
      avatarUrl: user.avatarUrl,
      createdAt: user.createdAt,
      discussions: disc,
      reposts: repostedList,
      followerCount: followers,
      followingCount: following,
      isFollowing,
      hasPublicKey: !!user.publicKeyJwk,
      reputation,
      badges,
      isOnline,
      lastSeenAt,
    });
  });




  app.get("/api/users/:username/status", async (req, res) => {
    const user = await storage.getUserByUsername(req.params.username);
    if (!user) return res.status(404).json({ message: "User not found" });
    const lastSeenAt = await storage.getLastSeen(user.id);
    const isOnline = lastSeenAt ? (Date.now() - new Date(lastSeenAt).getTime()) < 5 * 60 * 1000 : false;
    res.json({ isOnline, lastSeenAt });
  });

  app.post("/api/users/:username/follow", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const target = await storage.getUserByUsername(req.params.username);
    if (!target) return res.status(404).json({ message: "User not found" });
    const followed = await storage.followUser(userId, target.id);
    if (!followed) {
      await storage.unfollowUser(userId, target.id);
      res.json({ following: false });
    } else {
      await storage.createNotification(target.id, userId, "follow");
      res.json({ following: true });
    }
  });

  app.delete("/api/users/:username/follow", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const target = await storage.getUserByUsername(req.params.username);
    if (!target) return res.status(404).json({ message: "User not found" });
    await storage.unfollowUser(userId, target.id);
    res.json({ following: false });
  });

  app.get("/api/users/:username/followers", async (req, res) => {
    const user = await storage.getUserByUsername(req.params.username);
    if (!user) return res.status(404).json({ message: "User not found" });
    const list = await storage.getFollowers(user.id);
    res.json(list);
  });

  app.get("/api/users/:username/following", async (req, res) => {
    const user = await storage.getUserByUsername(req.params.username);
    if (!user) return res.status(404).json({ message: "User not found" });
    const list = await storage.getFollowing(user.id);
    res.json(list);
  });

  app.get("/api/users/:username/comments", async (req, res) => {
    const user = await storage.getUserByUsername(req.params.username);
    if (!user) return res.status(404).json({ message: "User not found" });
    const items = await storage.getProfileComments(user.id);
    const idToUsername = new Map<number, string>();
    for (const c of items) idToUsername.set(c.id, (c as any).authorUsername || "anonymous");
    res.json(items.map(c => ({
      ...c,
      replyToUsername: c.parentId ? (idToUsername.get(c.parentId) || null) : null,
    })));
  });

  app.post("/api/users/:username/comments", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const usernameParam = String(req.params.username);
    const target = await storage.getUserByUsername(usernameParam);
    if (!target) return res.status(404).json({ message: "User not found" });
    const { content, attachments, parentId } = req.body;
    if (!content || !content.trim()) return res.status(400).json({ message: "Comment cannot be empty" });
    const attJson = Array.isArray(attachments) && attachments.length > 0 ? JSON.stringify(attachments.slice(0, 4)) : undefined;
    const pId = typeof parentId === "number" ? parentId : null;
    const comment = await storage.createProfileComment(userId, target.id, sanitizeText(content, 2000), attJson, pId);
    await storage.createNotification(target.id, userId, "wall");
    res.json(comment);
  });

  app.delete("/api/users/:username/comments/:id", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(req.params.id);
    if (isNaN(id)) return res.status(400).json({ message: "Invalid ID" });
    const comment = await storage.getProfileComment(id);
    if (!comment) return res.status(404).json({ message: "Not found" });
    if (comment.authorId !== userId && comment.profileId !== userId) {
      return res.status(403).json({ message: "Not authorized" });
    }
    await storage.deleteProfileComment(id);
    res.json({ success: true });
  });

  app.patch("/api/users/:username/comments/:id", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(req.params.id);
    if (isNaN(id)) return res.status(400).json({ message: "Invalid ID" });
    const comment = await storage.getProfileComment(id);
    if (!comment) return res.status(404).json({ message: "Not found" });
    if (comment.authorId !== userId) {
      return res.status(403).json({ message: "Only the author can edit" });
    }
    const { content, attachments } = req.body;
    if (!content || !content.trim()) return res.status(400).json({ message: "Content required" });
    const attJson = Array.isArray(attachments) ? JSON.stringify(attachments.slice(0, 4)) : undefined;
    await storage.updateProfileComment(id, sanitizeText(content, 2000), attJson);
    res.json({ success: true });
  });

  app.get("/api/discussions", async (req, res) => {
    const tag = typeof req.query.tag === "string" ? req.query.tag : undefined;
    const sortRaw = typeof req.query.sort === "string" ? req.query.sort : "trending";
    const allowedSorts: DiscussionSort[] = ["trending", "new", "liked", "discussed"];
    const sort: DiscussionSort = (allowedSorts as string[]).includes(sortRaw) ? (sortRaw as DiscussionSort) : "trending";
    const items = (await storage.getDiscussions(tag, sort)).slice(0, 50); // Pagination: max 50
    const userId = getUserIdFromToken(req);
    let repostedIds: number[] = [];
    if (userId) repostedIds = await storage.getUserReposts(userId);
    const repostedSet = new Set(repostedIds);
    res.json(items.map(i => ({ ...i, repostCount: i.reposts, repostedByMe: repostedSet.has(i.id) })));
  });

  app.get("/api/discussions/:id", async (req, res) => {
    const id = parseInt(String(req.params.id));
    const discussion = await storage.getDiscussion(id);
    if (!discussion) return res.status(404).json({ message: "Discussion not found" });
    const userId = getUserIdFromToken(req);
    let repostedByMe = false;
    if (userId) {
      const rList = await storage.getUserReposts(userId);
      repostedByMe = rList.includes(id);
    }
    res.json({ ...discussion, repostCount: discussion.reposts, repostedByMe });
  });

  app.patch("/api/discussions/:id", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    const existing = await storage.getDiscussion(id);
    if (!existing) return res.status(404).json({ message: "Discussion not found" });
    if (existing.authorId !== userId) return res.status(403).json({ message: "Not authorized" });
    if (!isWithinEditWindow(existing.createdAt)) {
      return res.status(403).json({ message: "Posts can only be edited within 1 hour of publishing." });
    }
    const parsed = updateDiscussionSchema.safeParse(req.body);
    if (!parsed.success) return res.status(400).json({ message: parsed.error.errors[0].message });
    const sanitized = {
      ...parsed.data,
      title: parsed.data.title ? sanitizeText(parsed.data.title, 200) : parsed.data.title,
      content: parsed.data.content ? sanitizeText(parsed.data.content, 10000) : parsed.data.content,
    };
    const prevAttachments = parseAttachmentsJson((existing as any).attachments);
    await storage.updateDiscussion(id, sanitized);
    if (parsed.data.attachments !== undefined) {
      const keptUrls = new Set(parsed.data.attachments.map(a => a.url));
      const removed = prevAttachments.filter(a => !keptUrls.has(a.url));
      if (removed.length > 0) await deleteUploadedFiles(userId, removed);
    }
    const updated = await storage.getDiscussion(id);
    res.json(updated);
  });

  app.delete("/api/discussions/:id", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    const existing = await storage.getDiscussion(id);
    if (!existing) return res.status(404).json({ message: "Discussion not found" });
    if (existing.authorId !== userId) return res.status(403).json({ message: "Not authorized" });
    const discussionAttachments = parseAttachmentsJson((existing as any).attachments);
    const childComments = (await storage.getComments(id)).slice(0, 100); // Pagination: max 100
    await storage.deleteDiscussion(id);
    await deleteUploadedFiles(userId, discussionAttachments);
    for (const c of childComments) {
      const atts = parseAttachmentsJson((c as any).attachments);
      if (atts.length > 0) await deleteUploadedFiles(c.authorId, atts);
    }
    res.json({ success: true });
  });

  app.post("/api/discussions/:id/repost", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const discussionId = parseInt(String(req.params.id));
    const reposted = await storage.repostDiscussion(userId, discussionId);
    if (!reposted) {
      await storage.unrepostDiscussion(userId, discussionId);
      res.json({ reposted: false });
    } else {
      const d = await storage.getDiscussion(discussionId);
      if (d) {
        await storage.createNotification(d.authorId, userId, "repost", discussionId);
      }
      res.json({ reposted: true });
    }
  });

  app.get("/api/reposts", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const repostedIds = await storage.getUserReposts(userId);
    res.json(repostedIds);
  });

  app.post("/api/discussions", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const parsed = insertDiscussionSchema.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ message: parsed.error.errors[0].message });
    }

    const postsToday = await storage.countUserDiscussionsSince(userId, getStartOfCurrentLocalDayIso());
    if (postsToday >= DAILY_POST_LIMIT) {
      return res.status(429).json({ message: "You can only publish 2 posts per day." });
    }

    const sanitized = {
      ...parsed.data,
      title: sanitizeText(parsed.data.title, 200),
      content: sanitizeText(parsed.data.content, 10000),
    };
    const discussion = await storage.createDiscussion(userId, sanitized);
    res.json(discussion);
  });

  app.post("/api/discussions/:id/like", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const discussionId = parseInt(String(req.params.id));
    const liked = await storage.likeDiscussion(userId, discussionId);
    if (!liked) {
      await storage.unlikeDiscussion(userId, discussionId);
      res.json({ liked: false });
    } else {
      const d = await storage.getDiscussion(discussionId);
      if (d) {
        await storage.createNotification(d.authorId, userId, "like", discussionId);
      }
      res.json({ liked: true });
    }
  });

  app.get("/api/likes", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const likedIds = await storage.getUserLikes(userId);
    res.json(likedIds);
  });

  app.get("/api/discussions/:id/comments", async (req, res) => {
    const id = parseInt(String(req.params.id));
    const items = (await storage.getComments(id)).slice(0, 100); // Pagination: max 100
    const userId = getUserIdFromToken(req);
    let likedSet = new Set<number>();
    if (userId) {
      const ids = await storage.getUserCommentLikes(userId);
      likedSet = new Set(ids);
    }
    const cAuthorMap = new Map<number, string>();
    for (const c of items) cAuthorMap.set(c.id, (c as any).authorUsername || "anonymous");
    res.json(items.map(c => ({
      ...c,
      likedByMe: likedSet.has(c.id),
      replyToUsername: c.parentId ? (cAuthorMap.get(c.parentId) || null) : null,
    })));
  });

  app.post("/api/discussions/:id/comments", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    const parsed = insertCommentSchema.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ message: parsed.error.errors[0].message });
    }

    const discussionId = parseInt(String(req.params.id));
    const parentId = parsed.data.parentId ?? null;
    const comment = await storage.createComment(userId, discussionId, sanitizeText(parsed.data.content, 5000), parentId, parsed.data.attachments);
    const d = await storage.getDiscussion(discussionId);
    if (d) {
      await storage.createNotification(d.authorId, userId, "comment", discussionId, comment.id);
    }
    if (parentId) {
      const parent = await storage.getComment(parentId);
      if (parent && parent.authorId !== userId && parent.authorId !== (d ? d.authorId : -1)) {
        await storage.createNotification(parent.authorId, userId, "comment", discussionId, comment.id);
      }
    }
    res.json(comment);
  });

  app.patch("/api/comments/:id", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    const existing = await storage.getComment(id);
    if (!existing) return res.status(404).json({ message: "Comment not found" });
    if (existing.authorId !== userId) return res.status(403).json({ message: "Not authorized" });
    if (!isWithinEditWindow(existing.createdAt)) {
      return res.status(403).json({ message: "Comments can only be edited within 1 hour of posting." });
    }
    const parsed = updateCommentSchema.safeParse(req.body);
    if (!parsed.success) return res.status(400).json({ message: parsed.error.errors[0].message });
    const prevAttachments = parseAttachmentsJson((existing as any).attachments);
    const updated = await storage.updateComment(id, sanitizeText(parsed.data.content, 5000), parsed.data.attachments);
    if (parsed.data.attachments !== undefined) {
      const keptUrls = new Set(parsed.data.attachments.map(a => a.url));
      const removed = prevAttachments.filter(a => !keptUrls.has(a.url));
      if (removed.length > 0) await deleteUploadedFiles(userId, removed);
    }
    res.json(updated);
  });

  app.delete("/api/comments/:id", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    const existing = await storage.getComment(id);
    if (!existing) return res.status(404).json({ message: "Comment not found" });
    const discussion = await storage.getDiscussion(existing.discussionId);
    const canDelete = existing.authorId === userId || (discussion && discussion.authorId === userId);
    if (!canDelete) return res.status(403).json({ message: "Not authorized" });
    const commentAttachments = parseAttachmentsJson((existing as any).attachments);
    await storage.deleteComment(id);
    if (commentAttachments.length > 0) await deleteUploadedFiles(existing.authorId, commentAttachments);
    res.json({ success: true });
  });

  app.post("/api/comments/:id/like", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    const liked = await storage.likeComment(userId, id);
    if (!liked) {
      await storage.unlikeComment(userId, id);
      res.json({ liked: false });
    } else {
      res.json({ liked: true });
    }
  });

  app.get("/api/comment-likes", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const ids = await storage.getUserCommentLikes(userId);
    res.json(ids);
  });

  app.get("/api/search", async (req, res) => {
    const q = typeof req.query.q === "string" ? req.query.q.trim() : "";
    if (!q) return res.json({ users: [], discussions: [], authorUsername: null });
    if (q.startsWith("@")) {
      const uname = q.slice(1).trim();
      if (!uname) return res.json({ users: [], discussions: [], authorUsername: null });
      const d = await storage.searchDiscussionsByAuthor(uname);
      return res.json({ users: [], discussions: d, authorUsername: uname });
    }
    const [u, d] = await Promise.all([storage.searchUsers(q), storage.searchDiscussions(q)]);
    res.json({ users: u, discussions: d, authorUsername: null });
  });

  app.get("/api/tags", async (_req, res) => {
    const items = await storage.getAllTags();
    res.json(items);
  });

  app.get("/api/notifications", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const items = await storage.getNotifications(userId);
    res.json(items);
  });

  app.post("/api/notifications/read", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    await storage.markNotificationsRead(userId);
    res.json({ success: true });
  });

  app.get("/api/notifications/unread-count", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const count = await storage.getUnreadNotificationCount(userId);
    res.json({ count });
  });

  app.delete("/api/notifications", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    await storage.clearNotifications(userId);
    res.json({ success: true });
  });

  app.get("/api/conversations", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const items = await storage.getUserConversations(userId);
    res.json(items);
  });

  app.get("/api/conversations/unread-count", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const count = await storage.getUnreadMessageCount(userId);
    res.json({ count });
  });

  app.get("/api/conversations/:otherUsername", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const other = await storage.getUserByUsername(req.params.otherUsername);
    if (!other) return res.status(404).json({ message: "User not found" });
    if (other.id === userId) return res.status(400).json({ message: "Cannot message yourself" });
    const conv = await storage.getOrCreateConversation(userId, other.id);
    const msgs = await storage.getConversationMessages(conv.id);
    res.json({
      conversation: {
        id: conv.id,
        otherUserId: other.id,
        otherUsername: other.username,
        otherAvatar: other.avatarUrl || "",
        otherPublicKeyJwk: other.publicKeyJwk || "",
      },
      messages: msgs,
    });
  });

  app.post("/api/conversations/:otherUsername/messages", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const other = await storage.getUserByUsername(req.params.otherUsername);
    if (!other) return res.status(404).json({ message: "User not found" });
    if (other.id === userId) return res.status(400).json({ message: "Cannot message yourself" });
    const parsed = postMessageSchema.safeParse(req.body);
    if (!parsed.success) return res.status(400).json({ message: parsed.error.errors[0].message });
    const conv = await storage.getOrCreateConversation(userId, other.id);
    const msg = await storage.createMessage(conv.id, userId, {
      ciphertextForSelf: parsed.data.ciphertextForSelf,
      ciphertextForOther: parsed.data.ciphertextForOther,
      iv: parsed.data.iv,
      wrappedKeyForSelf: parsed.data.wrappedKeyForSelf,
      wrappedKeyForOther: parsed.data.wrappedKeyForOther,
      fileUrl: parsed.data.fileUrl,
      fileName: parsed.data.fileName,
      fileType: parsed.data.fileType,
      fileSize: parsed.data.fileSize,
    });
    await storage.createNotification(other.id, userId, "message", null, null, conv.id);
    res.json(msg);
  });

  app.post("/api/conversations/:id/read", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    // Verify the requesting user is actually a participant in this conversation
    const Database = require("better-sqlite3");
    const rawDb = new Database("data.db");
    const conv = rawDb.prepare("SELECT * FROM conversations WHERE id = ?").get(id) as any;
    rawDb.close();
    if (!conv || (conv.user1_id !== userId && conv.user2_id !== userId)) {
      return res.status(403).json({ message: "Not authorized" });
    }
    await storage.markConversationRead(id, userId);
    res.json({ success: true });
  });

  app.delete("/api/messages/:id", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const msgId = parseInt(String(req.params.id));
    const Database = require("better-sqlite3");
    const raw = new Database("data.db");
    const msg = raw.prepare("SELECT * FROM messages WHERE id = ?").get(msgId) as any;
    if (!msg) { raw.close(); return res.status(404).json({ message: "Message not found" }); }
    const conv = raw.prepare("SELECT * FROM conversations WHERE id = ?").get(msg.conversation_id) as any;
    if (!conv || (conv.user1_id !== userId && conv.user2_id !== userId)) {
      raw.close();
      return res.status(403).json({ message: "Not authorized" });
    }
    if (msg.file_url) {
      const fs = require("fs");
      const path = require("path");
      const filePath = path.join(process.cwd(), "dist/public", msg.file_url);
      try { fs.unlinkSync(filePath); } catch {}
    }
    raw.prepare("DELETE FROM messages WHERE id = ?").run(msgId);
    raw.close();
    res.json({ success: true });
  });

  app.delete("/api/conversations/:id/messages", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const convId = parseInt(String(req.params.id));
    const Database = require("better-sqlite3");
    const raw = new Database("data.db");
    const conv = raw.prepare("SELECT * FROM conversations WHERE id = ?").get(convId) as any;
    if (!conv || (conv.user1_id !== userId && conv.user2_id !== userId)) {
      raw.close();
      return res.status(403).json({ message: "Not authorized" });
    }
    const msgs = raw.prepare("SELECT file_url FROM messages WHERE conversation_id = ? AND file_url IS NOT NULL").all(convId) as any[];
    const fs = require("fs");
    const path = require("path");
    for (const m of msgs) {
      if (m.file_url) {
        try { fs.unlinkSync(path.join(process.cwd(), "dist/public", m.file_url)); } catch {}
      }
    }
    raw.prepare("DELETE FROM messages WHERE conversation_id = ?").run(convId);
    raw.prepare("UPDATE conversations SET last_message_at = ? WHERE id = ?").run(new Date().toISOString(), convId);
    raw.close();
    res.json({ success: true });
  });

  app.post("/api/conversations/:otherUsername/file", fileUploadLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const other = await storage.getUserByUsername(req.params.otherUsername);
    if (!other) return res.status(404).json({ message: "User not found" });
    if (other.id === userId) return res.status(400).json({ message: "Cannot message yourself" });

    const contentType = req.headers["content-type"] || "";
    const fileName = decodeURIComponent(String(req.headers["x-file-name"] || "file"));
    const chunks: Buffer[] = [];
    req.on("data", (chunk: Buffer) => chunks.push(chunk));
    req.on("end", async () => {
      try {
        let buffer = Buffer.concat(chunks);
        const maxSize = 50 * 1024 * 1024;
        if (buffer.length > maxSize) return res.status(413).json({ message: "File too large (50MB max)" });

        // === File type whitelist validation ===
        const ALLOWED_EXTENSIONS = new Set([
          // Images
          ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff",
          // Videos
          ".mp4", ".mkv", ".avi", ".webm", ".mov",
          // Documents
          ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".txt", ".csv",
          // Archives
          ".zip", ".rar", ".7z", ".tar", ".gz",
          // Code
          ".js", ".ts", ".py", ".rs", ".go", ".c", ".cpp", ".h", ".java", ".rb", ".php",
          ".swift", ".sh", ".json", ".yaml", ".xml", ".html", ".css", ".md", ".sql", ".toml",
        ]);
        const BLOCKED_EXTENSIONS = new Set([
          ".exe", ".bat", ".cmd", ".msi", ".ps1", ".vbs", ".scr", ".com", ".pif", ".reg", ".dll", ".sys", ".svg",
        ]);
        const pathCheck = require("path");
        const fileExt = (pathCheck.extname(fileName) || "").toLowerCase();
        if (!fileExt || BLOCKED_EXTENSIONS.has(fileExt) || !ALLOWED_EXTENSIONS.has(fileExt)) {
          return res.status(400).json({ message: `File type "${fileExt || "unknown"}" is not allowed` });
        }

        // === Per-user conversation upload storage quota (200MB) ===
        const quotaUser = await storage.getUser(userId);
        if (quotaUser) {
          const currentUsage = quotaUser.uploadBytesUsed ?? 0;
          if (currentUsage + buffer.length > CONVERSATION_UPLOAD_QUOTA_BYTES) {
            return res.status(413).json({ message: "Storage quota exceeded (200 MB per user for conversation uploads)" });
          }
        }

        const cryptoMod = require("crypto");
        const fsMod = require("fs");
        const pathMod = require("path");
        const { execFileSync } = require("child_process");

        const isGif = buffer.length >= 6 && buffer[0] === 0x47 && buffer[1] === 0x49 && buffer[2] === 0x46;
        const isJpeg = buffer.length >= 3 && buffer[0] === 0xff && buffer[1] === 0xd8 && buffer[2] === 0xff;
        const isPng = buffer.length >= 8 && buffer[0] === 0x89 && buffer[1] === 0x50 && buffer[2] === 0x4e && buffer[3] === 0x47;
        const isWebp = buffer.length >= 12 && buffer[0] === 0x52 && buffer[1] === 0x49 && buffer[2] === 0x46 && buffer[3] === 0x46 && buffer[8] === 0x57 && buffer[9] === 0x45 && buffer[10] === 0x42 && buffer[11] === 0x50;
        const isImage = isGif || isJpeg || isPng || isWebp;

        const isMp4 = buffer.length >= 12 && buffer[4] === 0x66 && buffer[5] === 0x74 && buffer[6] === 0x79 && buffer[7] === 0x70;
        const isMkv = buffer.length >= 4 && buffer[0] === 0x1a && buffer[1] === 0x45 && buffer[2] === 0xdf && buffer[3] === 0xa3;
        const isAvi = buffer.length >= 12 && buffer[0] === 0x52 && buffer[1] === 0x49 && buffer[2] === 0x46 && buffer[3] === 0x46 && buffer[8] === 0x41 && buffer[9] === 0x56 && buffer[10] === 0x49 && buffer[11] === 0x20;
        const isVideo = isMp4 || isMkv || isAvi || contentType.startsWith("video/");

        const isPdf = buffer.length >= 5 && buffer[0] === 0x25 && buffer[1] === 0x50 && buffer[2] === 0x44 && buffer[3] === 0x46;
        const isZip = buffer.length >= 4 && buffer[0] === 0x50 && buffer[1] === 0x4b && (buffer[2] === 0x03 || buffer[2] === 0x05);

        const hash = cryptoMod.randomBytes(32).toString("hex"); // 64 hex chars = 256-bit entropy
        const uploadDir = pathMod.join(process.cwd(), "uploads");
        if (!fsMod.existsSync(uploadDir)) fsMod.mkdirSync(uploadDir, { recursive: true });

        let storedName: string;
        let outType: string;

        if (isImage) {
          const sharp = require("sharp");
          if (isGif) {
            buffer = await sharp(buffer, { animated: true })
              .resize({ width: 480, height: 480, fit: "inside", withoutEnlargement: true })
              .gif({ effort: 7 })
              .toBuffer();
            storedName = `msg_${hash}.gif`;
            outType = "image/gif";
          } else {
            buffer = await sharp(buffer, { failOn: "truncated" })
              .rotate()
              .resize({ width: 1920, height: 1920, fit: "inside", withoutEnlargement: true })
              .webp({ quality: 82, effort: 4 })
              .toBuffer();
            storedName = `msg_${hash}.webp`;
            outType = "image/webp";
          }
        } else if (isVideo) {
          const tmpIn = pathMod.join(uploadDir, `_tmp_in_${hash}`);
          const ext = pathMod.extname(fileName).toLowerCase() || ".mp4";
          const tmpOut = pathMod.join(uploadDir, `_tmp_out_${hash}${ext}`);
          fsMod.writeFileSync(tmpIn, buffer);
          try {
            execFileSync("ffmpeg", [
              "-i", tmpIn,
              "-map_metadata", "-1",
              "-fflags", "+bitexact",
              "-flags:v", "+bitexact",
              "-flags:a", "+bitexact",
              "-c", "copy",
              "-y", tmpOut
            ], { timeout: 30000, stdio: "pipe" });
            buffer = fsMod.readFileSync(tmpOut);
          } catch {}
          try { fsMod.unlinkSync(tmpIn); } catch {}
          try { fsMod.unlinkSync(tmpOut); } catch {}
          storedName = `msg_${hash}${ext}`;
          outType = contentType.startsWith("video/") ? contentType : "video/mp4";
        } else if (isPdf) {
          storedName = `msg_${hash}.pdf`;
          outType = "application/pdf";
        } else {
          const safeExt = (pathMod.extname(fileName) || "").replace(/[^a-zA-Z0-9.]/g, "").slice(0, 10) || ".bin";
          storedName = `msg_${hash}${safeExt}`;
          outType = contentType || "application/octet-stream";
        }

        fsMod.writeFileSync(pathMod.join(uploadDir, storedName), buffer);
        // Track upload bytes for quota enforcement
        await storage.addUploadBytes(userId, buffer.length);
        const fileUrl = `/uploads/${storedName}`;
        res.json({ fileUrl, fileName, fileType: outType, fileSize: buffer.length });
      } catch (err: any) {
        console.error("File upload error:", err);
        res.status(500).json({ message: "Upload failed" });
      }
    });
  });


  // Active mining peers endpoint - queries synapsed RPC on both VPS nodes
  app.get("/api/mining/peers", async (_req, res) => {
    const RPC_ENDPOINTS = [
      "http://127.0.0.1:8332",
      "http://144.31.223.91:18332",
    ];
    const SEED_NODES = [
      "nv2b7cjwjzwrnwtrdaniogtnjkly6lcapg7ubkcou5pppzdcc2ki7cid.onion",
      "ny6duwaudeb76ym5zhtet2qtc5fmbkx7zp3pz7dlbroibj6jh5s2acqd.onion",
      "xa5xgwito6roew3rr5f4wrufdktwr6tfviu6wchunr4splj7smxkqcid.onion",
    ];
    const RPC_TIMEOUT = 5000;

    async function rpcCall(url: string, method: string): Promise<any> {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), RPC_TIMEOUT);
      try {
        const resp = await fetch(url, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ jsonrpc: "2.0", id: 1, method, params: {} }),
          signal: controller.signal,
        });
        const data = await resp.json();
        return data.result ?? null;
      } catch {
        return null;
      } finally {
        clearTimeout(timer);
      }
    }

    try {
      const results = await Promise.all(
        RPC_ENDPOINTS.flatMap((url) => [
          rpcCall(url, "peer.directory"),
          rpcCall(url, "poe.validators"),
        ])
      );

      const peerMap = new Map<string, { lastSeen: number; type: string }>();

      for (const seed of SEED_NODES) {
        peerMap.set(seed, { lastSeen: 0, type: "SEED" });
      }

      for (let i = 0; i < results.length; i += 2) {
        const peerResult = results[i];
        if (peerResult && Array.isArray(peerResult.peers)) {
          for (const p of peerResult.peers) {
            if (p.onion && typeof p.onion === "string" && p.onion.endsWith(".onion")) {
              const existing = peerMap.get(p.onion);
              const seen = typeof p.last_seen === "number" ? p.last_seen : 0;
              const isSeed = SEED_NODES.includes(p.onion);
              if (!existing || seen < existing.lastSeen) {
                peerMap.set(p.onion, { lastSeen: seen, type: isSeed ? "SEED" : "PEER" });
              }
            }
          }
        }
      }

      const validatorSet = new Set<string>();
      for (let i = 1; i < results.length; i += 2) {
        const valResult = results[i];
        if (Array.isArray(valResult)) {
          valResult.forEach((v: string) => validatorSet.add(v));
        }
      }

      const peers = Array.from(peerMap.entries())
        .map(([onion, info]) => ({
          onion,
          lastSeen: info.lastSeen,
          type: info.type,
        }))
        .sort((a, b) => {
          const typeOrder: Record<string, number> = { SEED: 0, PEER: 1 };
          return (typeOrder[a.type] ?? 2) - (typeOrder[b.type] ?? 2) || a.lastSeen - b.lastSeen;
        });

      res.json({
        peers,
        totalPeers: peers.length,
        validators: validatorSet.size,
        timestamp: Date.now(),
      });
    } catch (err) {
      res.json({ peers: [], totalPeers: 0, validators: 0, timestamp: Date.now() });
    }
  });

  app.post("/api/auth/logout", (req, res) => {
    const auth = req.headers.authorization;
    if (auth && auth.startsWith("Bearer ")) {
      sessions.delete(auth.slice(7));
      try {
        res.setHeader(
          "Set-Cookie",
          `synapse_token=; Path=/; Max-Age=0; SameSite=Lax${(req.secure || req.headers["x-forwarded-proto"] === "https") ? "; Secure" : ""}; HttpOnly`,
        );
      } catch {}
      saveSessionsToDisk();
    }
    res.json({ success: true });
  });

  app.delete("/api/auth/account", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });

    try {
      // Collect file URLs to delete from disk
      const fileUrls = await storage.getUserFileUrls(userId);

      // Delete all user data from database
      await storage.deleteUserAccount(userId);

      // Delete files from disk
      for (const url of fileUrls) {
        const filename = url.split("/").pop();
        if (filename) {
          const filePath = path.join(process.cwd(), "uploads", filename);
          try { fs.unlinkSync(filePath); } catch {}
        }
      }

      // Remove session
      const auth = req.headers?.authorization;
      if (auth && typeof auth === "string" && auth.startsWith("Bearer ")) {
        const token = auth.slice(7);
        sessions.delete(token);
        saveSessionsToDisk();
      }

      // Clear cookie
      const isSecure = req.secure || req.headers["x-forwarded-proto"] === "https";
      res.setHeader("Set-Cookie", `synapse_token=; Path=/; Max-Age=0; SameSite=Lax${isSecure ? "; Secure" : ""}; HttpOnly`);

      res.json({ success: true });
    } catch (err: any) {
      res.status(500).json({ message: "Failed to delete account" });
    }
  });


  // ── Group chat routes ──

  app.post("/api/groups", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const parsed = createGroupSchema.safeParse(req.body);
    if (!parsed.success) return res.status(400).json({ message: parsed.error.errors[0].message });
    const memberIds: number[] = [];
    for (const uname of parsed.data.memberUsernames) {
      const u = await storage.getUserByUsername(uname.trim());
      if (u) memberIds.push(u.id);
    }
    if (memberIds.length === 0 && parsed.data.memberUsernames.length > 0) {
      return res.status(400).json({ message: "No valid users found" });
    }
    const group = await storage.createGroup(sanitizeText(parsed.data.name, 100), userId, memberIds);
    res.json(group);
  });

  app.get("/api/groups", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const items = await storage.getUserGroups(userId);
    res.json(items);
  });

  app.get("/api/groups/:id", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    if (isNaN(id)) return res.status(400).json({ message: "Invalid group ID" });
    const group = await storage.getGroup(id);
    if (!group) return res.status(404).json({ message: "Group not found" });
    const isMember = await storage.isGroupMember(id, userId);
    if (!isMember) return res.status(403).json({ message: "Not a member of this group" });
    const members = await storage.getGroupMembers(id);
    res.json({ ...group, members });
  });

  app.post("/api/groups/:id/messages", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    if (isNaN(id)) return res.status(400).json({ message: "Invalid group ID" });
    const isMember = await storage.isGroupMember(id, userId);
    if (!isMember) return res.status(403).json({ message: "Not a member of this group" });
    const parsed = postGroupMessageSchema.safeParse(req.body);
    if (!parsed.success) return res.status(400).json({ message: parsed.error.errors[0].message });
    const attJson = parsed.data.attachments ? JSON.stringify(parsed.data.attachments) : undefined;
    const content = parsed.data.content ? sanitizeText(parsed.data.content, 5000) : "";
    if (!content && (!parsed.data.attachments || parsed.data.attachments.length === 0)) {
      return res.status(400).json({ message: "Message cannot be empty" });
    }
    const msg = await storage.createGroupMessage(id, userId, content, attJson);
    const user = await storage.getUser(userId);
    res.json({ ...msg, senderUsername: user?.username || "unknown", senderAvatar: user?.avatarUrl || "" });
  });

  app.get("/api/groups/:id/messages", async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    if (isNaN(id)) return res.status(400).json({ message: "Invalid group ID" });
    const isMember = await storage.isGroupMember(id, userId);
    if (!isMember) return res.status(403).json({ message: "Not a member of this group" });
    const before = req.query.before ? parseInt(String(req.query.before)) : undefined;
    const msgs = await storage.getGroupMessages(id, 50, before);
    res.json(msgs);
  });

  app.patch("/api/groups/:id", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    if (isNaN(id)) return res.status(400).json({ message: "Invalid group ID" });
    const role = await storage.getGroupMemberRole(id, userId);
    if (role !== "admin") return res.status(403).json({ message: "Only admins can edit the group" });
    const { name } = req.body;
    if (!name || typeof name !== "string" || !name.trim()) {
      return res.status(400).json({ message: "Group name is required" });
    }
    const updated = await storage.updateGroupName(id, sanitizeText(name.trim(), 100));
    res.json(updated);
  });

  app.post("/api/groups/:id/members", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    if (isNaN(id)) return res.status(400).json({ message: "Invalid group ID" });
    const role = await storage.getGroupMemberRole(id, userId);
    if (role !== "admin") return res.status(403).json({ message: "Only admins can add members" });
    const { username } = req.body;
    if (!username || typeof username !== "string") {
      return res.status(400).json({ message: "Username is required" });
    }
    const target = await storage.getUserByUsername(username.trim());
    if (!target) return res.status(404).json({ message: "User not found" });
    const alreadyMember = await storage.isGroupMember(id, target.id);
    if (alreadyMember) return res.status(400).json({ message: "User is already a member" });
    await storage.addGroupMember(id, target.id);
    res.json({ success: true });
  });

  app.delete("/api/groups/:id/members/:userId", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const groupId = parseInt(String(req.params.id));
    const targetUserId = parseInt(String(req.params.userId));
    if (isNaN(groupId) || isNaN(targetUserId)) return res.status(400).json({ message: "Invalid ID" });
    const group = await storage.getGroup(groupId);
    if (!group) return res.status(404).json({ message: "Group not found" });
    if (targetUserId === userId) {
      if (group.createdBy === userId) {
        return res.status(400).json({ message: "Creator cannot leave. Delete the group instead." });
      }
      await storage.removeGroupMember(groupId, userId);
      return res.json({ success: true, left: true });
    }
    const role = await storage.getGroupMemberRole(groupId, userId);
    if (role !== "admin") return res.status(403).json({ message: "Only admins can remove members" });
    if (targetUserId === group.createdBy) {
      return res.status(400).json({ message: "Cannot remove the group creator" });
    }
    await storage.removeGroupMember(groupId, targetUserId);
    res.json({ success: true });
  });

  app.delete("/api/groups/:id", writeLimiter, async (req, res) => {
    const userId = getUserIdFromToken(req);
    if (!userId) return res.status(401).json({ message: "Not authenticated" });
    const id = parseInt(String(req.params.id));
    if (isNaN(id)) return res.status(400).json({ message: "Invalid group ID" });
    const group = await storage.getGroup(id);
    if (!group) return res.status(404).json({ message: "Group not found" });
    if (group.createdBy !== userId) return res.status(403).json({ message: "Only the creator can delete the group" });
    await storage.deleteGroup(id);
    res.json({ success: true });
  });



  const SYNAPSED_RPC = "http://127.0.0.1:8332";
  const RPC_TIMEOUT_MS = 5000;

  async function synapsedRpc(method: string, params: Record<string, unknown> = {}): Promise<any> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), RPC_TIMEOUT_MS);
    try {
      const resp = await fetch(SYNAPSED_RPC, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ jsonrpc: "2.0", id: 1, method, params }),
        signal: controller.signal,
      });
      const data = await resp.json();
      return data.result ?? {};
    } catch {
      return {};
    } finally {
      clearTimeout(timer);
    }
  }

  app.get("/api/network/dashboard", async (_req, res) => {
    const result = await synapsedRpc("naan.dashboard");
    res.json(result);
  });

  app.get("/api/network/threat-intel", async (_req, res) => {
    const result = await synapsedRpc("naan.threat_intel");
    res.json(result);
  });

  app.get("/api/network/federation", async (_req, res) => {
    const result = await synapsedRpc("node.federation_status");
    res.json(result);
  });

  app.get("/api/network/replication", async (_req, res) => {
    const result = await synapsedRpc("node.replication_status");
    res.json(result);
  });

  app.get("/api/network/security", async (_req, res) => {
    const result = await synapsedRpc("naan.security_assessment");
    res.json(result);
  });

  app.get("/api/network/harvest", async (_req, res) => {
    const result = await synapsedRpc("harvest.list", { offset: 0, limit: 50 });
    res.json(result && Object.keys(result).length > 0 ? result : { entries: [] });
  });

  app.get("/api/network/exploits", async (_req, res) => {
    const list = await synapsedRpc("exploit.list", { offset: 0, limit: 100 });
    const stats = await synapsedRpc("exploit.stats");
    res.json({ exploits: list?.exploits ?? [], stats: stats ?? {} });
  });

  app.get("/api/network/ledger", async (_req, res) => {
    const result = await synapsedRpc("naan.message_ledger");
    res.json(result && Object.keys(result).length > 0 ? result : { entries: [], enabled: true, hash_algorithm: "SHA-256", total_proofs: 0, confirmation_depth: 3 });
  });

  app.post("/api/client-error", (req, res) => { console.error("[CLIENT ERROR]", JSON.stringify(req.body)); res.json({ ok: true }); });

  return httpServer;
}
