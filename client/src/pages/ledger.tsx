import { useQuery } from "@tanstack/react-query";
import { Link } from "wouter";
import { ArrowLeft, Hash, CheckCircle, Clock, Lock } from "lucide-react";

export default function LedgerPage() {
  const { data, isLoading } = useQuery({
    queryKey: ["ledger"],
    queryFn: async () => {
      const res = await fetch("/api/network/ledger");
      if (!res.ok) return { entries: [], enabled: false };
      return res.json();
    },
    refetchInterval: 30000,
  });

  const entries = data?.entries ?? [];
  const enabled = data?.enabled ?? false;
  const algorithm = data?.hash_algorithm ?? "SHA-256";
  const totalProofs = data?.total_proofs ?? entries.length;

  return (
    <div className="max-w-2xl mx-auto px-4 py-6">
      <div className="flex items-center gap-3 mb-6">
        <Link href="/"><span className="text-white/30 hover:text-white/60 cursor-pointer transition-colors"><ArrowLeft className="w-4 h-4" /></span></Link>
        <Hash className="w-4 h-4 text-white/40" />
        <h1 className="text-white/60 uppercase tracking-widest font-medium" style={{ fontSize: "10px" }}>Message Ledger</h1>
        <span className={"ml-auto text-xs px-1.5 py-0.5 rounded " + (enabled ? "bg-green-500/10 text-green-400/60" : "bg-white/5 text-white/20")} style={{ fontSize: "7px" }}>
          {enabled ? "ACTIVE" : "DISABLED"}
        </span>
      </div>

      <div className="grid grid-cols-3 gap-3 mb-6">
        <div className="border border-white/5 rounded p-3 text-center">
          <div className="text-white/50 font-mono text-sm">{isLoading ? "..." : totalProofs}</div>
          <div className="text-white/15 uppercase" style={{ fontSize: "6px" }}>Proofs</div>
        </div>
        <div className="border border-white/5 rounded p-3 text-center">
          <div className="text-white/50 font-mono text-sm">{algorithm}</div>
          <div className="text-white/15 uppercase" style={{ fontSize: "6px" }}>Algorithm</div>
        </div>
        <div className="border border-white/5 rounded p-3 text-center">
          <div className="text-white/50 font-mono text-sm">{isLoading ? "..." : data?.confirmation_depth ?? 3}</div>
          <div className="text-white/15 uppercase" style={{ fontSize: "6px" }}>Depth</div>
        </div>
      </div>

      <div className="border border-white/5 rounded p-4 mb-4">
        <div className="flex items-center gap-2 mb-3">
          <Lock className="w-3.5 h-3.5 text-blue-400/60" />
          <span className="text-white/30 uppercase tracking-widest font-medium" style={{ fontSize: "8px" }}>How It Works</span>
        </div>
        <div className="text-white/25 space-y-1" style={{ fontSize: "9px", lineHeight: "1.6" }}>
          <p>Every message sent through SynapseNet is hashed and timestamped in the distributed ledger.</p>
          <p>This provides tamper-proof delivery confirmation without revealing message content.</p>
          <p>Only the hash + timestamp are stored — the actual message remains end-to-end encrypted between sender and recipient.</p>
        </div>
      </div>

      {entries.length > 0 && (
        <div className="space-y-1">
          <div className="text-white/20 uppercase mb-2" style={{ fontSize: "7px" }}>Recent Proofs</div>
          {entries.slice(0, 20).map((entry: any, i: number) => (
            <div key={i} className="flex items-center gap-2 px-2 py-1.5 border border-white/5 rounded hover:border-white/10 transition-colors">
              <CheckCircle className="w-3 h-3 text-green-400/40 flex-shrink-0" />
              <span className="text-white/40 font-mono truncate" style={{ fontSize: "7px" }}>{entry.hash?.slice(0, 24) || "proof-" + i}...</span>
              <Clock className="w-2.5 h-2.5 text-white/15 flex-shrink-0 ml-auto" />
              <span className="text-white/15 font-mono flex-shrink-0" style={{ fontSize: "6px" }}>{entry.timestamp || "pending"}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
