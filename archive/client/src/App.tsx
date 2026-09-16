import { Switch, Route, Router } from "wouter";
import { useHashLocation } from "wouter/use-hash-location";
import { queryClient } from "./lib/queryClient";
import { QueryClientProvider } from "@tanstack/react-query";
import { Toaster } from "@/components/ui/toaster";
import { TooltipProvider } from "@/components/ui/tooltip";
import { AuthProvider, useAuth } from "@/lib/auth";
import AuthPage from "@/pages/auth";
import HomePage from "@/pages/home";
import ProfilePage from "@/pages/profile";
import SettingsPage from "@/pages/settings";
import DiscussionPage from "@/pages/discussion";
import MessagesPage from "@/pages/messages";
import SearchPage from "@/pages/search";
import NotificationsPage from "@/pages/notifications";
import NotFound from "@/pages/not-found";
import NetworkPage from "@/pages/network";
import HarvestPage from "@/pages/harvest";
import ExploitsPage from "@/pages/exploits";
import LedgerPage from "@/pages/ledger";
import { useEffect } from "react";
import { ErrorBoundary } from "@/components/ErrorBoundary";

function useRemoveInitialLoader(ready: boolean) {
  useEffect(() => {
    if (!ready) return;
    const el = document.getElementById("initial-loader");
    if (el) {
      el.style.transition = "opacity 0.2s";
      el.style.opacity = "0";
      setTimeout(() => el.remove(), 200);
    }
  }, [ready]);
}

function ProtectedRoute({ component: Component }: { component: React.ComponentType }) {
  const { user, isLoading } = useAuth();

  useRemoveInitialLoader(!isLoading);

  if (isLoading) return null;

  if (!user) return <AuthPage />;

  return <Component />;
}

function AppRouter() {
  return (
    <Switch>
      <Route path="/" component={() => <ProtectedRoute component={HomePage} />} />
      <Route path="/u/:username" component={() => <ProtectedRoute component={ProfilePage} />} />
      <Route path="/d/:id" component={() => <ProtectedRoute component={DiscussionPage} />} />
      <Route path="/settings" component={() => <ProtectedRoute component={SettingsPage} />} />
      <Route path="/messages" component={() => <ProtectedRoute component={MessagesPage} />} />
      <Route path="/messages/:username" component={() => <ProtectedRoute component={MessagesPage} />} />
      <Route path="/search" component={() => <ProtectedRoute component={SearchPage} />} />
      <Route path="/search/:q" component={() => <ProtectedRoute component={SearchPage} />} />
      <Route path="/notifications" component={() => <ProtectedRoute component={NotificationsPage} />} />
      <Route path="/network" component={() => <ProtectedRoute component={NetworkPage} />} />
      <Route path="/harvest" component={() => <ProtectedRoute component={HarvestPage} />} />
      <Route path="/exploits" component={() => <ProtectedRoute component={ExploitsPage} />} />
      <Route path="/ledger" component={() => <ProtectedRoute component={LedgerPage} />} />
      <Route component={NotFound} />
    </Switch>
  );
}

function App() {
  return (
    <ErrorBoundary>
      <QueryClientProvider client={queryClient}>
        <TooltipProvider>
          <Toaster />
          <AuthProvider>
            <Router hook={useHashLocation}>
              <AppRouter />
            </Router>
          </AuthProvider>
        </TooltipProvider>
      </QueryClientProvider>
    </ErrorBoundary>
  );
}

export default App;
