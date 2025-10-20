<script lang="ts">
  import { onMount } from 'svelte';
  import { locale } from 'svelte-i18n';
  import { availableLanguages, setLocale } from '../lib/i18n';
  import type { Language } from '../lib/i18n';

  const DEFAULT_LOCALE: Language = 'en';

  let currentLocale: Language = DEFAULT_LOCALE;
  let selectorOpen = false;
  let rootElement: HTMLElement | null = null;

  const normalizeLocale = (value?: string): Language => {
    if (value && value.length > 0) {
      const base = value.split('-')[0].toLowerCase();
      if (availableLanguages.some((entry) => entry.code === base)) {
        return base as Language;
      }
    }
    return DEFAULT_LOCALE;
  };

  $: currentLocale = normalizeLocale($locale as string | undefined);

  const handleChange = async (event: Event) => {
    const value = (event.target as HTMLSelectElement).value as Language;
    selectorOpen = false;
    await setLocale(value);
  };

  const toggleSelector = () => {
    selectorOpen = !selectorOpen;
  };

  onMount(() => {
    const onDocumentClick = (event: MouseEvent) => {
      if (!selectorOpen) {
        return;
      }
      const target = event.target as Node;
      if (rootElement && !rootElement.contains(target)) {
        selectorOpen = false;
      }
    };

    document.addEventListener('click', onDocumentClick);
    return () => {
      document.removeEventListener('click', onDocumentClick);
    };
  });
</script>

<div class="language-switcher" bind:this={rootElement}>
  <button
    type="button"
    class="language-toggle"
    on:click|stopPropagation={toggleSelector}
    aria-haspopup="listbox"
    aria-expanded={selectorOpen}
    aria-label={currentLocale.toUpperCase()}
  >
    <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
      <path d="M12 3C7.03 3 3 7.03 3 12c0 4.82 3.82 8.74 8.62 8.99.9-4.11.29-7.84-1.55-10.99h5.86c-1.04 1.99-1.64 4.46-1.64 7 0 1.45.16 2.86.46 4.19 4.04-.97 7.05-4.63 7.05-9.19C22 7.03 17.97 3 12 3Zm-4.88 5c.46-1.2 1.08-2.23 1.82-3.05 1.03.85 1.87 2.05 2.47 3.47H7.12Zm10.76 0A7.1 7.1 0 0 0 16.05 5c.63-.85 1.41-1.56 2.29-2.09A8.03 8.03 0 0 1 19.88 8h-2Zm-5.43 11.97c-1.05-.87-1.89-2.1-2.46-3.57h4.93c-.47 1.34-1.14 2.5-1.87 3.57h-.6ZM7.12 13c-.08-.65-.12-1.32-.12-2 0-.68.04-1.35.12-2h3.76c-.4-.8-.87-1.53-1.41-2.2.62-1.19 1.36-2.19 2.2-3 2.06 2.06 3.33 5.05 3.33 8.2 0 .68-.04 1.35-.12 2H7.12Zm10.76 0h2.05a8.03 8.03 0 0 1-1.54 3.09 7.1 7.1 0 0 0-1.83-3.09Zm-9.82 0c.63 1.73 1.52 3.2 2.58 4.31A6.02 6.02 0 0 1 7.12 13Z" fill="currentColor"/>
    </svg>
    <span>{currentLocale.toUpperCase()}</span>
  </button>

  {#if selectorOpen}
    <div class="language-menu" role="dialog" on:click|stopPropagation>
      <label class="sr-only" for="language-select">Select language</label>
      <select
        id="language-select"
        class="language-select"
        bind:value={currentLocale}
        on:change={handleChange}
        size={availableLanguages.length}
      >
        {#each availableLanguages as langOption}
          <option value={langOption.code}>{langOption.label}</option>
        {/each}
      </select>
    </div>
  {/if}
</div>

<style>
  .language-switcher {
    position: relative;
    display: inline-block;
  }

  .language-toggle {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    border: none;
    background: transparent;
    color: #ecfdf5;
    border-radius: 9999px;
    padding: 0.25rem 0.45rem;
    font-size: 0.7rem;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    cursor: pointer;
    transition: color 0.2s ease;
  }

  .language-toggle:hover,
  .language-toggle:focus-visible {
    color: rgba(236, 253, 245, 0.68);
    outline: none;
  }

  .language-toggle svg {
    width: 1rem;
    height: 1rem;
  }

  .language-menu {
    position: absolute;
    top: calc(100% + 0.4rem);
    right: 0;
    width: max-content;
    border: 1px solid rgba(15, 118, 110, 0.45);
    border-radius: 0.75rem;
    padding: 0.35rem 0.25rem;
    box-shadow: 0 12px 30px rgba(3, 7, 18, 0.38);
    backdrop-filter: blur(16px);
    z-index: 30;
  }

  .language-select {
    display: block;
    width: 100%;
    max-height: 80vh;
    border: none;
    background: transparent;
    color: #ecfdf5;
    font-size: 0.85rem;
    line-height: 1.5;
    outline: none;
    overflow: auto;
  }

  .language-select option {
    color: inherit;
    padding: 0.4rem 0.75rem;
  }

  .language-select option:checked {
    background: rgba(15, 118, 110, 0.6);
  }

  .sr-only {
    position: absolute;
    width: 1px;
    height: 1px;
    padding: 0;
    margin: -1px;
    overflow: hidden;
    clip: rect(0, 0, 0, 0);
    white-space: nowrap;
    border: 0;
  }
</style>
