<script lang="ts">
  import { locale, _ } from 'svelte-i18n';
  import { availableLanguages, setLocale } from '../lib/i18n';
  import type { Language } from '../lib/i18n';

  let currentLocale: Language = 'en';

  $: currentLocale = (($locale as string)?.split('-')[0] as Language) ?? 'en';

  const handleChange = async (event: Event) => {
    const value = (event.target as HTMLSelectElement).value as Language;
    await setLocale(value);
  };
</script>

<label class="sr-only" for="language-select">{$_('language_label')}</label>
<select
  id="language-select"
  class="rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-sm text-slate-100 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-500/40"
  bind:value={currentLocale}
  on:change={handleChange}
>
  {#each availableLanguages as langOption}
    <option value={langOption.code}>{langOption.label}</option>
  {/each}
</select>
