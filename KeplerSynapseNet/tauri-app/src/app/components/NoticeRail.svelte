<script lang="ts">
  // Plaques slide in from the right. They sit above every tab.
  import { notices, dismissNotice, type NoticeKind } from "../../lib/notices";

  const kicker: Record<NoticeKind, string> = {
    fault: "FIX",
    security: "SECURITY",
    naan: "NAAN",
  };
</script>

{#if $notices.length > 0}
  <aside class="notice-rail" aria-live="polite">
    {#each $notices as notice (notice.id)}
      <article class="notice notice-{notice.kind}">
        <div class="notice-top">
          <span class="notice-kicker">{kicker[notice.kind]}</span>
          <button class="notice-x" type="button" on:click={() => dismissNotice(notice.id)} aria-label="Dismiss">×</button>
        </div>
        <div class="notice-title">{notice.title}</div>
        <p class="notice-body">{notice.body}</p>
      </article>
    {/each}
  </aside>
{/if}

<style>
  .notice-rail {
    position: fixed;
    top: calc(var(--topbar-h) + 10px);
    right: 12px;
    width: min(320px, calc(100vw - 24px));
    z-index: 40;
    display: flex;
    flex-direction: column;
    gap: 8px;
    pointer-events: none;
  }

  .notice {
    pointer-events: auto;
    border: 1px solid var(--border);
    background: #000000;
    color: var(--text-primary);
    padding: 10px 12px 12px;
    animation: notice-in 220ms var(--ease-menu) both;
  }

  .notice-fault {
    border-color: var(--err);
  }

  .notice-security {
    border-color: var(--warn);
  }

  .notice-naan {
    border-color: var(--ok);
  }

  .notice-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
  }

  .notice-kicker {
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
    color: var(--text-faint);
  }

  .notice-fault .notice-kicker { color: var(--err); }
  .notice-security .notice-kicker { color: var(--warn); }
  .notice-naan .notice-kicker { color: var(--ok); }

  .notice-x {
    border: none;
    background: none;
    color: var(--text-secondary);
    font-size: 14px;
    line-height: 1;
    padding: 0 2px;
  }

  .notice-title {
    font-family: var(--font);
    font-size: 11px;
    margin-top: 6px;
  }

  .notice-body {
    margin: 6px 0 0;
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.45;
    color: var(--text-secondary);
  }

  @keyframes notice-in {
    from { transform: translateX(110%); }
    to { transform: translateX(0); }
  }

  @media (prefers-reduced-motion: reduce) {
    .notice { animation: none; }
  }
</style>
