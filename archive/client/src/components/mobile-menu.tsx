import { useState } from "react";
import { Link, useLocation } from "wouter";
import { Menu, Home, Search, Bell, Mail, User, Settings, LogOut, X, Shield, Download, Zap, Hash } from "lucide-react";
import { useAuth } from "@/lib/auth";

interface MobileMenuProps {
  username?: string;
}

export function MobileMenu({ username }: MobileMenuProps) {
  const { user, logout } = useAuth();
  const [, navigate] = useLocation();
  const [open, setOpen] = useState(false);

  const close = () => setOpen(false);
  const uname = username || user?.username || "";

  const links: { key: string; href: string; label: string; Icon: React.ComponentType<{ size?: number | string }> }[] = [
    { key: "home", href: "/", label: "Home", Icon: Home },
    { key: "search", href: "/search", label: "Search", Icon: Search },
    { key: "notifications", href: "/notifications", label: "Notifications", Icon: Bell },
    { key: "messages", href: "/messages", label: "Messages", Icon: Mail },
    { key: "profile", href: uname ? `/u/${uname}` : "/", label: "Profile", Icon: User },
    { key: "settings", href: "/settings", label: "Settings", Icon: Settings },
    { key: "network", href: "/network", label: "Network", Icon: Shield },
    { key: "harvest", href: "/harvest", label: "Harvest", Icon: Download },
    { key: "exploits", href: "/exploits", label: "Exploits", Icon: Zap },
    { key: "ledger", href: "/ledger", label: "Ledger", Icon: Hash },
  ];

  return (
    <>
      <button
        onClick={() => setOpen(true)}
        className="text-white/60 hover:text-white md:hidden"
        data-testid="button-mobile-menu"
        aria-label="Open menu"
      >
        <Menu size={16} />
      </button>

      {open && (
        <div className="fixed inset-0 z-50 md:hidden" role="dialog" aria-modal="true">
          <div
            className="absolute inset-0 bg-black/80"
            onClick={close}
            data-testid="overlay-mobile-menu"
          />
          <div className="absolute top-0 right-0 h-full w-64 bg-black border-l border-white/20 p-4 flex flex-col gap-3">
            <div className="flex items-center justify-between mb-3">
              <span className="text-white" style={{ fontSize: "10px" }}>Menu</span>
              <button
                onClick={close}
                className="text-white/50 hover:text-white"
                data-testid="button-mobile-menu-close"
                aria-label="Close menu"
              >
                <X size={14} />
              </button>
            </div>
            {links.map(({ key, href, label, Icon }) => (
              <Link key={key} href={href}>
                <span
                  onClick={close}
                  className="flex items-center gap-3 text-white/70 hover:text-white py-2 border-b border-white/5 cursor-pointer"
                  style={{ fontSize: "9px" }}
                  data-testid={`link-mobile-${key}`}
                >
                  <Icon size={12} />
                  {label}
                </span>
              </Link>
            ))}
            <button
              onClick={() => { close(); logout(); navigate("/"); }}
              className="flex items-center gap-3 text-white/70 hover:text-white py-2 cursor-pointer text-left"
              style={{ fontSize: "9px" }}
              data-testid="link-mobile-logout"
            >
              <LogOut size={12} />
              Sign out
            </button>
          </div>
        </div>
      )}
    </>
  );
}
