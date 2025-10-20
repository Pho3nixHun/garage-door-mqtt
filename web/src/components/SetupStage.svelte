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

<div class="rounded-3xl border border-emerald-300/40 bg-white/85 p-6 text-emerald-900 shadow-xl shadow-emerald-900/20 backdrop-blur">
  <h2 class="text-xl font-semibold text-emerald-900">{$_('connect_title')}</h2>
  <p class="mt-1 text-sm text-emerald-700/80">
    {$_('connect_description')}
  </p>

  <div class="mt-5 space-y-4">
    <label class="grid gap-2">
      <span class="text-sm font-medium text-emerald-800">{$_('field_username')}</span>
      <input
        class="w-full rounded-xl border border-emerald-300 bg-white/70 px-3 py-2 text-sm text-emerald-900 placeholder-emerald-500 outline-none focus:border-amber-300 focus:ring-2 focus:ring-amber-200/60"
        type="text"
        autocomplete="username"
        placeholder={$_('placeholder_username')}
        value={username}
        on:input={handleInput('username')}
      />
    </label>

    <label class="grid gap-2">
      <span class="text-sm font-medium text-emerald-800">{$_('field_password')}</span>
      <input
        class="w-full rounded-xl border border-emerald-300 bg-white/70 px-3 py-2 text-sm text-emerald-900 placeholder-emerald-500 outline-none focus:border-amber-300 focus:ring-2 focus:ring-amber-200/60"
        type="password"
        autocomplete="current-password"
        placeholder={$_('placeholder_password')}
        value={password}
        on:input={handleInput('password')}
      />
    </label>

    <label class="flex items-center gap-3 text-sm text-emerald-700/90">
      <input
        type="checkbox"
        class="h-4 w-4 rounded border border-emerald-400 bg-white text-emerald-600 focus:ring-emerald-400"
        checked={remember}
        on:change={handleRememberChange}
      />
      {$_('remember_label')}
    </label>

    {#if formError}
      <div class="rounded-lg border border-rose-400/40 bg-rose-500/10 px-3 py-2 text-sm text-rose-700">
        {formError}
      </div>
    {/if}

    {#if connectionError}
      <div class="rounded-lg border border-rose-400/40 bg-rose-500/10 px-3 py-2 text-sm text-rose-700">
        {connectionError}
      </div>
    {/if}

    <button
      class="w-full rounded-xl bg-gradient-to-r from-emerald-500 via-teal-500 to-emerald-400 px-4 py-3 text-base font-semibold text-emerald-950 shadow-lg shadow-emerald-500/40 transition hover:from-emerald-400 hover:via-teal-400 hover:to-emerald-300 disabled:cursor-not-allowed disabled:from-emerald-700/60 disabled:to-emerald-700/60 disabled:text-emerald-200/60"
      type="button"
      on:click={handleSubmit}
      disabled={isConnecting}
    >
      {isConnecting ? $_('button_connecting') : $_('button_continue')}
    </button>
  </div>
</div>
