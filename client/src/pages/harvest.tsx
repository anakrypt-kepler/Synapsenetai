import { useQuery } from "@tanstack/react-query";
import { Link } from "wouter";
import { ArrowLeft, FileText, Image, File, Download, Shield } from "lucide-react";

export default function HarvestPage() {
  const { data, isLoading } = useQuery({
    queryKey: ["harvest"],
    queryFn: async () => {
      const res = await fetch("/api/network/harvest");
      if (!res.ok) return { entries: [] };
      return res.json();
    },
    refetchInterval: 30000,
  });

  const entries = data?.entries ?? [];

  return (
    <div className="max-w-2xl mx-auto px-4 py-6">
      <div className="flex items-center gap-3 mb-6">
        <Link href="/"><span className="text-white/30 hover:text-white/60 cursor-pointer transition-colors"><ArrowLeft className="w-4 h-4" /></span></Link>
        <Download className="w-4 h-4 text-white/40" />
        <h1 className="text-white/60 uppercase tracking-widest font-medium" style={{ fontSize: "10px" }}>Knowledge Harvest</h1>
        <span className="ml-auto text-white/20 font-mono" style={{ fontSize: "8px" }}>{entries.length} entries</span>
      </div>

      {isLoading ? (
        <div className="text-white/20 text-center py-8" style={{ fontSize: "8px" }}>LOADING...</div>
      ) : entries.length === 0 ? (
        <div className="border border-white/5 rounded p-8 text-center">
          <Shield className="w-6 h-6 text-white/10 mx-auto mb-3" />
          <p className="text-white/20" style={{ fontSize: "9px" }}>No harvested entries yet. NAAN agent will populate this as it mines knowledge.</p>
        </div>
      ) : (
        <div className="space-y-1">
          {entries.map((entry: any, i: number) => (
            <div key={i} className="border border-white/5 rounded px-3 py-2 flex items-center gap-3 hover:border-white/10 transition-colors">
              <div className="flex-shrink-0">
                {entry.type === "image" ? <Image className="w-3.5 h-3.5 text-blue-400/50" /> :
                 entry.type === "text" ? <FileText className="w-3.5 h-3.5 text-green-400/50" /> :
                 <File className="w-3.5 h-3.5 text-white/30" />}
              </div>
              <div className="flex-1 min-w-0">
                <div className="text-white/50 font-mono truncate" style={{ fontSize: "8px" }}>{entry.sha256?.slice(0, 16) || entry.id || "entry-" + i}...</div>
                <div className="text-white/20 truncate" style={{ fontSize: "7px" }}>{entry.source || "unknown source"}</div>
              </div>
              <div className="text-white/15 font-mono flex-shrink-0" style={{ fontSize: "7px" }}>{entry.size ? (entry.size / 1024).toFixed(1) + "KB" : ""}</div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
