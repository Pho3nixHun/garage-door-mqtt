<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { _ } from 'svelte-i18n';

  type Field = 'username' | 'password';

  const dispatch = createEventDispatcher<{
    submit: void;
    update: { field: Field; value: string };
    rememberChange: boolean;
  }>();

  export let username = '';
  export let password = '';
  export let remember = true;
  export let formError: string | null = null;
  export let connectionError: string | undefined;
  export let isConnecting = false;

  const handleInput = (field: Field) => (event: Event) => {
    const value = (event.target as HTMLInputElement).value;
    dispatch('update', { field, value });
  };

  const handleRememberChange = (event: Event) => {
    const value = (event.target as HTMLInputElement).checked;
    dispatch('rememberChange', value);
  };

  const handleSubmit = () => {
    dispatch('submit');
  };
</script>

<div class="rounded-2xl border border-slate-800/80 bg-slate-900/80 p-6 shadow-lg shadow-black/30 backdrop-blur">
  <h2 class="text-xl font-semibold text-slate-50">{$_('connect_title')}</h2>
  <p class="mt-1 text-sm text-slate-400">
    {$_('connect_description')}
  </p>

  <div class="mt-5 space-y-4">
    <label class="grid gap-2">
      <span class="text-sm font-medium text-slate-200">{$_('field_username')}</span>
      <input
        class="w-full rounded-lg border border-slate-700 bg-slate-800 px-3 py-2 text-sm text-slate-100 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-500/40"
        type="text"
        autocomplete="username"
        placeholder={$_('placeholder_username')}
        value={username}
        on:input={handleInput('username')}
      />
    </label>

    <label class="grid gap-2">
      <span class="text-sm font-medium text-slate-200">{$_('field_password')}</span>
      <input
        class="w-full rounded-lg border border-slate-700 bg-slate-800 px-3 py-2 text-sm text-slate-100 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-500/40"
        type="password"
        autocomplete="current-password"
        placeholder={$_('placeholder_password')}
        value={password}
        on:input={handleInput('password')}
      />
    </label>

    <label class="flex items-center gap-3 text-sm text-slate-300">
      <input
        type="checkbox"
        class="h-4 w-4 rounded border border-slate-600 bg-slate-800 text-sky-400 focus:ring-sky-500"
        checked={remember}
        on:change={handleRememberChange}
      />
      {$_('remember_label')}
    </label>

    {#if formError}
      <div class="rounded-lg border border-rose-500/40 bg-rose-500/10 px-3 py-2 text-sm text-rose-200">
        {formError}
      </div>
    {/if}

    {#if connectionError}
      <div class="rounded-lg border border-rose-500/40 bg-rose-500/10 px-3 py-2 text-sm text-rose-200">
        {connectionError}
      </div>
    {/if}

    <button
      class="w-full rounded-xl bg-sky-500 px-4 py-3 text-base font-semibold text-slate-950 shadow-lg shadow-sky-500/30 transition hover:bg-sky-400 disabled:cursor-not-allowed disabled:bg-slate-700/70 disabled:text-slate-400"
      type="button"
      on:click={handleSubmit}
      disabled={isConnecting}
    >
      {isConnecting ? $_('button_connecting') : $_('button_continue')}
    </button>
  </div>
</div>
