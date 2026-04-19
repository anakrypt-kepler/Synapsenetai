<script lang="ts">
  import { theme } from "../../lib/theme";

  function toggle() {
    theme.toggle();
  }
</script>

<button class="theme-toggle" on:click={toggle} title="Toggle theme">
  {#if $theme === "dark"}
    <span class="toggle-dot dark-active"></span>
    <span class="toggle-label">DARK</span>
  {:else}
    <span class="toggle-dot light-active"></span>
    <span class="toggle-label">LIGHT</span>
  {/if}
</button>

<style>
  .theme-toggle {
    display: flex;
    align-items: center;
    gap: 6px;
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 3px 10px;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 0.5px;
    color: var(--text-secondary);
    background: none;
    transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
  }

  .theme-toggle:hover {
    color: var(--text-primary);
    border-color: var(--text-faint);
    background: var(--surface-alt);
  }

  .toggle-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    flex-shrink: 0;
  }

  .toggle-dot.dark-active {
    background: var(--accent);
    box-shadow: 0 0 4px var(--accent);
  }

  .toggle-dot.light-active {
    background: var(--accent);
  }

  .toggle-label {
    font-size: 10px;
    letter-spacing: 0.5px;
  }
</style>
