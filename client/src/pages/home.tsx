import { useState, useEffect } from "react";
import { useAuth } from "@/lib/auth";
import { useQuery, useMutation } from "@tanstack/react-query";
import { getResponseErrorMessage, queryClient } from "@/lib/queryClient";
import { Link, useLocation } from "wouter";
import { Heart, MessageSquare, Plus, LogOut, User, Bell, Mail, Search, X, Repeat2, GitBranch, Trash2, Shield, Download, Zap, Hash } from "lucide-react";
import { formatDistanceToNow } from "date-fns";
import { ReputationPill } from "@/components/reputation";
import { MobileMenu } from "@/components/mobile-menu";
import { AttachmentPicker } from "@/components/attachment-picker";
import { InspiredBy } from "@/components/inspired-by";
import { AttachmentGrid } from "@/components/attachment-grid";
import { ATTACHMENT_MAX_PER_DISCUSSION, type Attachment } from "@shared/schema";

function parseAttachments(raw: unknown): Attachment[] {
  if (!raw) return [];
  if (Array.isArray(raw)) return raw as Attachment[];
  if (typeof raw === "string") {
    try {
      const parsed = JSON.parse(raw);
      return Array.isArray(parsed) ? (parsed as Attachment[]) : [];
    } catch {
      return [];
    }
  }
  return [];
}

const API_BASE = "__PORT_5000__".startsWith("__") ? "" : "__PORT_5000__";

function ActiveMiningPeers() {
  const { data, isLoading } = useQuery({
    queryKey: ["mining-peers"],
    queryFn: async () => {
      const res = await fetch(API_BASE + "/api/mining/peers");
      if (!res.ok) throw new Error("Failed to fetch peers");
      return res.json();
    },
    refetchInterval: 30000,
    staleTime: 15000,
  });

  const peers = data?.peers ?? [];
  const totalPeers = data?.totalPeers ?? 0;
  const validators = data?.validators ?? 0;

  return (
    <div className="border-t border-white/5 pt-4 pb-2 mb-4">
      <div className="flex items-center justify-center gap-2 mb-2">
        <div className="w-1.5 h-1.5 rounded-full bg-green-500 animate-pulse" />
        <span className="text-white/25 uppercase tracking-widest" style={{ fontSize: "6px" }}>
          Network
        </span>
      </div>

      <div className="flex items-center justify-center gap-8 mb-3">
        <div className="text-center">
          <span className="text-white/50 font-mono block" style={{ fontSize: "9px" }}>
            {isLoading ? "..." : totalPeers}
          </span>
          <span className="text-white/15 uppercase" style={{ fontSize: "5px" }}>Peers</span>
        </div>
        <div className="text-center">
          <span className="text-white/50 font-mono block" style={{ fontSize: "9px" }}>
            {isLoading ? "..." : validators}
          </span>
          <span className="text-white/15 uppercase" style={{ fontSize: "5px" }}>Validators</span>
        </div>
      </div>

      {peers.length > 0 && (
        <div className="max-w-sm mx-auto space-y-0.5">
          {peers.map((peer: { onion: string; lastSeen: number; type: string }) => (
            <div key={peer.onion} className="flex items-center gap-2 px-2 py-0.5">
              <div
                className="w-1 h-1 rounded-full flex-shrink-0"
                style={{ backgroundColor: peer.lastSeen < 120 ? "#22c55e" : peer.lastSeen < 600 ? "#eab308" : "#6b7280" }}
              />
              <span className="text-white/30 truncate font-mono flex-1" style={{ fontSize: "5px" }}>
                {peer.onion.slice(0, 12)}...{peer.onion.slice(-10)}
              </span>
              <span className="text-white/15 flex-shrink-0 uppercase" style={{ fontSize: "4px" }}>
                {peer.type}
              </span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}



interface DiscussionItem {
  id: number;
  title: string;
  content: string;
  authorId: number;
  likes: number;
  reposts: number;
  repostCount: number;
  repostedByMe: boolean;
  attachments?: string | Attachment[];
  createdAt: string;
  editedAt: string | null;
  authorUsername: string;
  authorAvatar: string;
  authorReputation: number;
  commentCount: number;
  tags: string[];
}

type SortKey = "trending" | "new" | "liked" | "discussed";

const SORT_LABELS: { key: SortKey; label: string }[] = [
  { key: "trending", label: "Trending" },
  { key: "new", label: "New" },
  { key: "liked", label: "Liked" },
  { key: "discussed", label: "Discussed" },
];

interface TagItem { id: number; name: string; count: number; }

function Avatar({ src, username, size = 20 }: { src: string; username: string; size?: number }) {
  const initial = username?.[0]?.toUpperCase() || "?";
  if (src) {
    return (
      <img
        src={src.startsWith("/") ? `${API_BASE}${src}` : src}
        alt=""
        style={{ width: size, height: size }}
        className="object-cover border border-white/10 flex-shrink-0"
      />
    );
  }
  return (
    <div
      style={{ width: size, height: size, fontSize: size * 0.4 }}
      className="border border-white/10 flex items-center justify-center text-white/30 bg-white/5 flex-shrink-0"
    >
      {initial}
    </div>
  );
}

function HeaderIcons() {
  const { user, token, logout } = useAuth();
  const [, navigate] = useLocation();
  const [showNotif, setShowNotif] = useState(false);
  const iconButtonClass = "relative inline-flex h-4 w-4 items-center justify-center text-white/50 hover:text-white cursor-pointer align-middle";

  const { data: unreadNotif } = useQuery<{ count: number }>({
    queryKey: ["/api/notifications/unread-count"],
    queryFn: async () => {
      const res = await fetch(`${API_BASE}/api/notifications/unread-count`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) return { count: 0 };
      return res.json();
    },
    refetchInterval: 15000,
    enabled: !!token,
  });

  const { data: unreadMsg } = useQuery<{ count: number }>({
    queryKey: ["/api/conversations/unread-count"],
    queryFn: async () => {
      const res = await fetch(`${API_BASE}/api/conversations/unread-count`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) return { count: 0 };
      return res.json();
    },
    refetchInterval: 15000,
    enabled: !!token,
  });

  const { data: notifs = [] } = useQuery<any[]>({
    queryKey: ["/api/notifications"],
    queryFn: async () => {
      const res = await fetch(`${API_BASE}/api/notifications`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) return [];
      return res.json();
    },
    refetchInterval: 15000,
    enabled: !!token && showNotif,
  });

  const markReadMutation = useMutation({
    mutationFn: async () => {
      await fetch(`${API_BASE}/api/notifications/read`, {
        method: "POST",
        headers: { Authorization: `Bearer ${token}` },
      });
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["/api/notifications/unread-count"] });
      queryClient.invalidateQueries({ queryKey: ["/api/notifications"] });
    },
  });

  const toggleNotif = () => {
    const next = !showNotif;
    setShowNotif(next);
    if (next && (unreadNotif?.count || 0) > 0) {
      markReadMutation.mutate();
    }
  };

  const notifLink = (n: any): string => {
    if (n.type === "follow") return `/u/${n.actorUsername}`;
    if (n.type === "wall") return `/u/${user?.username}`;
    if (n.type === "message") return `/messages/${n.actorUsername}`;
    if (n.discussionId) return `/d/${n.discussionId}`;
    return "/";
  };

  const notifText = (n: any): string => {
    const t = n.discussionTitle ? (n.discussionTitle.length > 40 ? n.discussionTitle.slice(0, 40) + "..." : n.discussionTitle) : "";
    if (n.type === "like") return `@${n.actorUsername} liked your post: ${t}`;
    if (n.type === "follow") return `@${n.actorUsername} started following you`;
    if (n.type === "wall") return `@${n.actorUsername} wrote on your wall`;
    if (n.type === "comment") return `@${n.actorUsername} commented on: ${t}`;
    if (n.type === "message") return `@${n.actorUsername} sent you a message`;
    return `@${n.actorUsername}`;
  };

  const unreadCount = unreadNotif?.count || 0;
  const unreadM = unreadMsg?.count || 0;

  return (
    <div className="flex items-center gap-4 relative">
      <Link href="/messages">
        <span className={iconButtonClass} data-testid="link-messages">
          <Mail size={14} className="block -translate-y-[2px]" />
          {unreadM > 0 && (
            <span className="absolute -top-1 -right-2 bg-red-500 text-white rounded-full px-1" style={{ fontSize: "6px", minWidth: 8, textAlign: "center" }}>
              {unreadM}
            </span>
          )}
        </span>
      </Link>
      <button type="button" onClick={toggleNotif} className={iconButtonClass} data-testid="button-notifications">
        <Bell size={14} className="block -translate-y-[1px]" />
        {unreadCount > 0 && (
          <span className="absolute -top-1 -right-2 bg-red-500 text-white rounded-full px-1" style={{ fontSize: "6px", minWidth: 8, textAlign: "center" }}>
            {unreadCount}
          </span>
        )}
      </button>
      <Link href={`/u/${user?.username}`}>
        <span className={iconButtonClass} data-testid="link-profile">
          <User size={14} className="block -translate-y-[2px]" />
        </span>
      </Link>
      <button
        type="button"
        onClick={() => { logout(); navigate("/"); }}
        className={iconButtonClass}
        data-testid="button-logout"
      >
        <LogOut size={14} className="block -translate-y-[1px]" />
      </button>

      {showNotif && (
        <div className="absolute right-0 top-6 w-80 bg-black border border-white/20 z-50 max-h-96 flex flex-col" data-testid="panel-notifications">
          <div className="border-b border-white/10 p-2 flex items-center justify-between flex-shrink-0">
            <span className="text-white/60" style={{ fontSize: "8px" }}>Notifications</span>
            <div className="flex items-center gap-2">
              {notifs.length > 0 && (
                <button
                  onClick={() => {
                    fetch(`${API_BASE}/api/notifications`, { method: "DELETE", headers: { Authorization: `Bearer ${token}` } })
                      .then(() => {
                        queryClient.invalidateQueries({ queryKey: ["/api/notifications"] });
                        queryClient.invalidateQueries({ queryKey: ["/api/notifications/unread-count"] });
                      });
                  }}
                  className="text-white/20 hover:text-red-400 transition-colors"
                  title="Clear all"
                >
                  <Trash2 size={10} />
                </button>
              )}
              <button onClick={() => setShowNotif(false)} className="text-white/40 hover:text-white">
                <X size={10} />
              </button>
            </div>
          </div>
          <div className="overflow-y-auto flex-1" style={{ scrollbarWidth: "thin", scrollbarColor: "rgba(255,255,255,0.1) transparent" }}>
          {notifs.length === 0 ? (
            <p className="text-white/20 text-center py-6" style={{ fontSize: "8px" }}>No notifications yet.</p>
          ) : (
            <div className="flex flex-col">
              {notifs.map((n: any) => (
                <Link key={n.id} href={notifLink(n)}>
                  <div
                    onClick={() => setShowNotif(false)}
                    className={`p-2 border-b border-white/5 hover:bg-white/5 cursor-pointer flex items-start gap-2 ${n.read ? "" : "bg-white/5"}`}
                    data-testid={`notif-${n.id}`}
                  >
                    <Avatar src={n.actorAvatar} username={n.actorUsername} size={14} />
                    <div className="flex-1 min-w-0">
                      <p className="text-white/70 break-words" style={{ fontSize: "7px", lineHeight: "1.8" }}>
                        {notifText(n)}
                      </p>
                      <p className="text-white/20 mt-1" style={{ fontSize: "6px" }}>
                        {formatDistanceToNow(new Date(n.createdAt), { addSuffix: true })}
                      </p>
                    </div>
                  </div>
                </Link>
              ))}
            </div>
          )}
          </div>
        </div>
      )}
    </div>
  );
}

export default function HomePage() {
  const { user, token } = useAuth();
  const [, navigate] = useLocation();
  const [showNew, setShowNew] = useState(false);
  const [title, setTitle] = useState("");
  const [content, setContent] = useState("");
  const [tagsInput, setTagsInput] = useState("");
  const [newAttachments, setNewAttachments] = useState<Attachment[]>([]);
  const [searchQ, setSearchQ] = useState("");
  const [activeTag, setActiveTag] = useState<string | null>(null);
  const [sort, setSort] = useState<SortKey>("trending");
  const [createError, setCreateError] = useState("");

  useEffect(() => {
    queryClient.invalidateQueries({ queryKey: ["/api/discussions"] });
    queryClient.invalidateQueries({ queryKey: ["/api/likes"] });
  }, []);

  const { data: discussions = [], isLoading } = useQuery<DiscussionItem[]>({
    queryKey: ["/api/discussions", activeTag || "", sort],
    queryFn: async () => {
      const params = new URLSearchParams();
      if (activeTag) params.set("tag", activeTag);
      params.set("sort", sort);
      const url = `${API_BASE}/api/discussions?${params.toString()}`;
      const res = await fetch(url, {
        headers: token ? { Authorization: `Bearer ${token}` } : {},
      });
      if (!res.ok) return [];
      return res.json();
    },
  });

  const { data: allTags = [] } = useQuery<TagItem[]>({
    queryKey: ["/api/tags"],
    queryFn: async () => {
      const res = await fetch(`${API_BASE}/api/tags`);
      if (!res.ok) return [];
      return res.json();
    },
  });

  const { data: likedIds = [] } = useQuery<number[]>({
    queryKey: ["/api/likes"],
    queryFn: async () => {
      const res = await fetch(`${API_BASE}/api/likes`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) return [];
      return res.json();
    },
  });

  const createMutation = useMutation({
    mutationFn: async (data: { title: string; content: string; tags: string[]; attachments: Attachment[] }) => {
      const res = await fetch(`${API_BASE}/api/discussions`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`,
        },
        body: JSON.stringify(data),
      });
      if (!res.ok) throw new Error(await getResponseErrorMessage(res, "Failed to create post"));
      return res.json();
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["/api/discussions"] });
      queryClient.invalidateQueries({ queryKey: ["/api/tags"] });
      setCreateError("");
      setShowNew(false);
      setTitle("");
      setContent("");
      setTagsInput("");
      setNewAttachments([]);
    },
    onError: (err: Error) => {
      setCreateError(err.message);
    },
  });

  const likeMutation = useMutation({
    mutationFn: async (id: number) => {
      const res = await fetch(`${API_BASE}/api/discussions/${id}/like`, {
        method: "POST",
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) throw new Error("Failed");
      return res.json();
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["/api/discussions"] });
      queryClient.invalidateQueries({ queryKey: ["/api/likes"] });
    },
  });

const repostMutation = useMutation({
    mutationFn: async (id: number) => {
      const res = await fetch(`${API_BASE}/api/discussions/${id}/repost`, {
        method: "POST",
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) throw new Error("Failed");
      return res.json();
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["/api/discussions"] });
    },
  });

  const parseTags = (s: string): string[] => {
    return s.split(/[\s,]+/).map(x => x.trim()).filter(Boolean);
  };

  const handleCreate = (e: React.FormEvent) => {
    e.preventDefault();
    if (!title.trim() || !content.trim()) return;
    setCreateError("");
    createMutation.mutate({ title, content, tags: parseTags(tagsInput), attachments: newAttachments });
  };

  const submitSearch = (e: React.FormEvent) => {
    e.preventDefault();
    const q = searchQ.trim();
    if (!q) return;
    navigate(`/search/${encodeURIComponent(q)}`);
  };

  return (
    <div className="min-h-screen bg-black">
      <header className="border-b border-white/10 px-4 py-3">
        <div className="max-w-2xl mx-auto flex items-center justify-between gap-3">
          <Link href="/">
            <span className="text-white pixel-glow cursor-pointer flex-shrink-0" style={{ fontSize: "12px" }} data-testid="text-logo">
              Synapsenetai.org
            </span>
          </Link>
          <a href="/git/kepler/Synapsenetai" className="border border-white/20 px-2 py-1 text-white/60 hover:text-white hover:border-white/40 transition-colors flex items-center gap-1 flex-shrink-0" style={{ fontSize: "9px" }} title="Git">
            <GitBranch size={12} />
            Git
          </a>
          <a href="/#/network" className="border border-white/20 px-2 py-1 text-white/60 hover:text-white hover:border-white/40 transition-colors flex items-center gap-1 flex-shrink-0" style={{ fontSize: "9px" }} title="Network Intelligence">
            <Shield size={12} />
            Network
          </a>
          <a href="/#/harvest" className="border border-white/20 px-2 py-1 text-white/60 hover:text-white hover:border-white/40 transition-colors flex items-center gap-1 flex-shrink-0" style={{ fontSize: "9px" }} title="Knowledge Harvest">
            <Download size={12} />
            Harvest
          </a>
          <a href="/#/exploits" className="border border-white/20 px-2 py-1 text-white/60 hover:text-white hover:border-white/40 transition-colors flex items-center gap-1 flex-shrink-0" style={{ fontSize: "9px" }} title="Exploit Intelligence">
            <Zap size={12} />
            Exploits
          </a>
          <a href="/#/ledger" className="border border-white/20 px-2 py-1 text-white/60 hover:text-white hover:border-white/40 transition-colors flex items-center gap-1 flex-shrink-0" style={{ fontSize: "9px" }} title="Message Ledger">
            <Hash size={12} />
            Ledger
          </a>
          <form onSubmit={submitSearch} className="flex-1 max-w-xs relative">
            <Search size={10} className="absolute left-2 top-1/2 -translate-y-1/2 text-white/30" />
            <input
              value={searchQ}
              onChange={(e) => setSearchQ(e.target.value)}
              placeholder="Search..."
              className="w-full bg-black border border-white/10 text-white pl-6 pr-2 py-1 focus:outline-none focus:border-white/30"
              style={{ fontSize: "8px" }}
              data-testid="input-search"
            />
          </form>
          <div className="hidden md:flex">
            <HeaderIcons />
          </div>
          <MobileMenu />
        </div>
      </header>

      <main className="max-w-2xl mx-auto px-4 py-6">
        <div className="flex flex-wrap items-center gap-2 mb-4" data-testid="row-sort">
          {SORT_LABELS.map(({ key, label }) => (
            <button
              key={key}
              onClick={() => setSort(key)}
              className={`border px-2 py-1 transition-colors ${sort === key ? "border-white text-white" : "border-white/10 text-white/40 hover:text-white/70 hover:border-white/25"}`}
              style={{ fontSize: "7px" }}
              data-testid={`button-sort-${key}`}
            >
              {label}
            </button>
          ))}
        </div>

        {allTags.length > 0 && (
          <div className="flex flex-wrap items-center gap-2 mb-5" data-testid="row-tags">
            {activeTag && (
              <button
                onClick={() => setActiveTag(null)}
                className="border border-white/20 text-white/60 hover:text-white px-2 py-0.5"
                style={{ fontSize: "7px" }}
                data-testid="button-clear-tag"
              >
                clear
              </button>
            )}
            {allTags.slice(0, 20).map(t => (
              <button
                key={t.id}
                onClick={() => setActiveTag(activeTag === t.name ? null : t.name)}
                className={`border px-2 py-0.5 transition-colors ${activeTag === t.name ? "border-white text-white" : "border-white/10 text-white/40 hover:text-white/70 hover:border-white/25"}`}
                style={{ fontSize: "7px" }}
                data-testid={`tag-${t.name}`}
              >
                #{t.name}{t.count > 0 ? ` (${t.count})` : ""}
              </button>
            ))}
          </div>
        )}

        <button
          onClick={() => {
            setCreateError("");
            setShowNew(!showNew);
          }}
          className="w-full border border-white/15 text-white/60 hover:text-white hover:border-white/30 py-3 px-4 flex items-center gap-3 transition-colors mb-6"
          style={{ fontSize: "8px" }}
          data-testid="button-new-discussion"
        >
          <Plus size={12} />
          Start a new discussion
        </button>

        {showNew && (
          <form onSubmit={handleCreate} className="border border-white/15 p-4 mb-6">
            <input
              type="text"
              placeholder="Title"
              value={title}
              onChange={(e) => setTitle(e.target.value)}
              className="w-full bg-black border border-white/10 text-white px-3 py-2 mb-3 focus:outline-none focus:border-white/30"
              style={{ fontSize: "7px" }}
              data-testid="input-title"
            />
            <textarea
              placeholder="What's on your mind?"
              value={content}
              onChange={(e) => setContent(e.target.value)}
              className="w-full bg-black border border-white/10 text-white px-3 py-2 mb-3 focus:outline-none focus:border-white/30 min-h-[100px] resize-y"
              style={{ fontSize: "7px" }}
              data-testid="input-content"
            />
            <AttachmentPicker
              value={newAttachments}
              onChange={setNewAttachments}
              max={ATTACHMENT_MAX_PER_DISCUSSION}
              token={token}
              disabled={createMutation.isPending}
              className="mb-3"
            />
            <input
              type="text"
              placeholder="Tags (comma or space separated, max 6)"
              value={tagsInput}
              onChange={(e) => setTagsInput(e.target.value)}
              className="w-full bg-black border border-white/10 text-white px-3 py-2 mb-3 focus:outline-none focus:border-white/30"
              style={{ fontSize: "8px" }}
              data-testid="input-tags"
            />
            <div className="flex justify-end gap-3">
              <button
                type="button"
                onClick={() => {
                  setCreateError("");
                  setShowNew(false);
                  setNewAttachments([]);
                }}
                className="text-white/40 hover:text-white py-2 px-4"
                style={{ fontSize: "8px" }}
                data-testid="button-cancel"
              >
                Cancel
              </button>
              <button
                type="submit"
                disabled={createMutation.isPending}
                className="bg-white text-black py-2 px-4 hover:bg-white/90 disabled:opacity-50"
                style={{ fontSize: "8px" }}
                data-testid="button-post"
              >
                {createMutation.isPending ? "..." : "Post"}
              </button>
            </div>
            {createError && (
              <p className="mt-3 text-red-400" style={{ fontSize: "7px" }} data-testid="text-create-error">
                {createError}
              </p>
            )}
          </form>
        )}

        {isLoading ? (
          <div className="text-white/30 text-center py-12" style={{ fontSize: "8px" }}>Loading...</div>
        ) : discussions.length === 0 ? (
          <div className="text-white/20 text-center py-12" style={{ fontSize: "8px" }}>
            No discussions yet. Be the first to post.
          </div>
        ) : (
          <div className="flex flex-col gap-1">
            {discussions.map((d) => {
              const isLiked = likedIds.includes(d.id);
              return (
                <div
                  key={d.id}
                  className="border border-white/8 hover:border-white/15 p-4 transition-colors"
                  data-testid={`card-discussion-${d.id}`}
                >
                  {d.repostedByMe && (
                    <p className="text-green-400/70 mb-2 flex items-center gap-1" style={{ fontSize: "6px" }}>
                      <Repeat2 size={8} /> Reposted by you
                    </p>
                  )}
                  <Link href={`/d/${d.id}`}>
                    <div className="cursor-pointer">
                      <h3 className="text-white mb-2 break-words" style={{ fontSize: "9px" }} data-testid={`text-title-${d.id}`}>
                        {d.title}
                      </h3>
                      <p className="text-white/50 mb-3 break-words" style={{ fontSize: "8px", lineHeight: "2" }}>
                        {d.content.length > 300 ? d.content.slice(0, 300) + "..." : d.content}
                      </p>
                    </div>
                  </Link>
                  {(() => {
                    const atts = parseAttachments(d.attachments);
                    return atts.length > 0 ? (
                      <AttachmentGrid attachments={atts} singleMaxHeight={360} multiHeight={220} className="mb-3" />
                    ) : null;
                  })()}
                  {d.tags && d.tags.length > 0 && (
                    <div className="flex flex-wrap gap-1 mb-2">
                      {d.tags.map(t => (
                        <button
                          key={t}
                          onClick={(e) => { e.stopPropagation(); setActiveTag(t); }}
                          className="text-white/30 hover:text-white border border-white/5 hover:border-white/20 px-1.5 py-0.5"
                          style={{ fontSize: "6px" }}
                          data-testid={`chip-${d.id}-${t}`}
                        >
                          #{t}
                        </button>
                      ))}
                    </div>
                  )}
                  <div className="flex items-center justify-between">
                    <div className="flex items-center gap-4 text-white/25" style={{ fontSize: "7px" }}>
                      <Link href={`/u/${d.authorUsername}`}>
                        <span className="hover:text-white/50 cursor-pointer flex items-center gap-1.5" data-testid={`link-author-${d.id}`}>
                          <Avatar src={d.authorAvatar} username={d.authorUsername} size={14} />
                          @{d.authorUsername}
                        </span>
                      </Link>
                      <ReputationPill value={d.authorReputation ?? 0} />
                      <span>{formatDistanceToNow(new Date(d.createdAt), { addSuffix: true })}</span>
                    </div>
                    <div className="flex items-center gap-3">
                      <Link href={`/d/${d.id}`}>
                        <span className="flex items-center gap-1 text-white/20 hover:text-white/40 cursor-pointer" style={{ fontSize: "7px" }}>
                          <MessageSquare size={11} />
                          {d.commentCount}
                        </span>
                      </Link>
                      <button
                        onClick={(e) => { e.stopPropagation(); repostMutation.mutate(d.id); }}
                        className={`flex items-center gap-1 transition-colors ${d.repostedByMe ? "text-green-400" : "text-white/20 hover:text-white/40"}`}
                        style={{ fontSize: "7px" }}
                        data-testid={`button-repost-${d.id}`}
                      >
                        <Repeat2 size={11} />
                        {d.repostCount ?? d.reposts ?? 0}
                      </button>
                      <button
                        onClick={(e) => { e.stopPropagation(); likeMutation.mutate(d.id); }}
                        className={`flex items-center gap-1 transition-colors ${isLiked ? "text-white" : "text-white/20 hover:text-white/40"}`}
                        style={{ fontSize: "7px" }}
                        data-testid={`button-like-${d.id}`}
                      >
                        <Heart size={11} fill={isLiked ? "white" : "none"} />
                        {d.likes}
                      </button>
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </main>

      <footer className="border-t border-white/5 px-4 py-6 mt-12">
        <div className="max-w-2xl mx-auto">
          {/* Active Mining Peers */}
          <ActiveMiningPeers />

          <p className="text-white/25 mb-2 text-center" style={{ fontSize: "6px" }}>
            Community Support
          </p>
          <div className="flex items-center justify-center gap-3 mb-4">
            <svg width="18" height="18" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg" className="flex-shrink-0">
              <circle cx="16" cy="16" r="16" fill="#F7931A"/>
              <path d="M22.5 14.2c.3-2-1.2-3.1-3.3-3.8l.7-2.7-1.6-.4-.6 2.6c-.4-.1-.9-.2-1.3-.3l.7-2.6-1.7-.4-.7 2.7c-.4-.1-.7-.2-1-.2l-2.3-.6-.4 1.7s1.2.3 1.2.3c.7.2.8.6.8 1l-.8 3.2c0 0 .1 0 .1 0l-.1 0-1.1 4.5c-.1.2-.3.5-.7.4 0 0-1.2-.3-1.2-.3l-.8 1.9 2.2.5c.4.1.8.2 1.2.3l-.7 2.8 1.6.4.7-2.7c.5.1.9.2 1.3.3l-.7 2.7 1.7.4.7-2.8c2.8.5 4.9.3 5.8-2.2.7-2-.1-3.2-1.5-3.9 1.1-.3 1.9-1 2.1-2.5zm-3.7 5.2c-.5 2-3.9.9-5 .7l.9-3.6c1.1.3 4.7.8 4.1 2.9zm.5-5.3c-.5 1.8-3.3.9-4.2.7l.8-3.3c.9.2 3.9.7 3.4 2.6z" fill="white"/>
            </svg>
            <div className="min-w-0">
              <p className="text-white/25 mb-0.5" style={{ fontSize: "5px" }}>BTC Wallet</p>
              <span className="text-white/50 break-all select-all" style={{ fontSize: "6px" }}>bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4</span>
            </div>
          </div>
          <div className="flex justify-center mb-4">
            <img
              src="/fm21ni2noi2n3.gif"
              alt=""
              draggable={false}
              className="block select-none"
              style={{
                width: "72px",
                height: "72px",
                imageRendering: "pixelated",
                filter: "grayscale(1) contrast(1.05)",
              }}
            />
          </div>
          <div className="text-center text-white/20" style={{ fontSize: "6px", lineHeight: "2.2" }}>
            <p>
              <a href="https://github.com/anakrypt-kepler/Synapsenetai" target="_blank" rel="noopener noreferrer" className="text-white/30 hover:text-white/50 underline">
                Main Repository
              </a>
            </p>
            <InspiredBy />
            <p className="mt-1">
              Created by <a href="/#/u/kepler" className="text-white/30 hover:text-white/50 underline">kepler</a> & <a href="/#/u/dexter" className="text-white/30 hover:text-white/50 underline">dexter</a>
            </p>

            <div className="mt-3 pt-3 border-t border-white/5">
              <p className="text-white/25 mb-1" style={{ fontSize: "5px" }}>Access</p>
              <p className="text-white/40 select-all" style={{ fontSize: "6px" }}>
                https://synapsenetai.org
              </p>
              <p className="text-white/40 select-all mt-0.5" style={{ fontSize: "5px", wordBreak: "break-all" }}>
                http://4wt2yuebbtjosbptzje4qqicq6do4cegeoo63mdffommq7pe65movwqd.onion
              </p>
            </div>
          </div>
        </div>
      </footer>
    </div>
  );
}

export { HeaderIcons };
