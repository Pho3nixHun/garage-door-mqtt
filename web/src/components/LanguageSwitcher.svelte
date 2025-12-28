<script lang="ts">
  import { locale, _ } from 'svelte-i18n';
  import { availableLanguages, setLocale } from '../lib/i18n';
  import type { Language } from '../lib/i18n';
  import Dropdown from './ui/Dropdown.svelte';
  import Button from './ui/Button.svelte';

  const DEFAULT_LOCALE: Language = 'en';

  let currentLocale: Language = DEFAULT_LOCALE;

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

  const handleChange = async (event: Event, close: () => void) => {
    const value = (event.target as HTMLSelectElement).value as Language;
    await setLocale(value);
    close();
  };
</script>

<Dropdown let:toggle let:open let:close align="right" className="language-dropdown">
  <Button
    slot="trigger"
    variant="outline"
    size="sm"
    className="language-trigger"
    aria-haspopup="listbox"
    aria-expanded={open}
    aria-label={currentLocale.toUpperCase()}
  >
    <span>🌐 {currentLocale.toUpperCase()}</span>
  </Button>

  <div class="language-menu" role="dialog" on:click|stopPropagation>
    <label class="sr-only" for="language-select">{$_('language_label')}</label>
    <select
      id="language-select"
      class="language-select"
      bind:value={currentLocale}
      on:change={(event) => handleChange(event, close)}
      size={availableLanguages.length}
    >
      {#each availableLanguages as langOption}
        <option value={langOption.code}>{langOption.label}</option>
      {/each}
    </select>
  </div>
</Dropdown>

<style>
  .language-menu {
    border-radius: 0.75rem;
    padding: 0.35rem 0.25rem;
    min-width: 12rem;
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
    backdrop-filter: blur(16px);
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

