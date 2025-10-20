<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { _ } from 'svelte-i18n';
  import Button from './ui/Button.svelte';
  import Card from './ui/Card.svelte';

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
<Card>
  <div slot="header" class="space-y-1">
    <h2 class="text-xl font-semibold text-emerald-100">{$_('connect_title')}</h2>
    <p class="text-sm text-emerald-200/90">
      {$_('connect_description')}
    </p>
  </div>

  <div slot="content" class="space-y-4">
    <label class="grid gap-2">
      <span class="text-sm font-medium text-emerald-200/90">{$_('field_username')}</span>
      <input
        class="w-full rounded-xl border border-emerald-400/50 bg-emerald-950/40 px-3 py-2 text-sm text-emerald-100 placeholder-emerald-300/70 outline-none transition focus:border-emerald-200 focus:ring-2 focus:ring-emerald-200/60"
        type="text"
        autocomplete="username"
        placeholder={$_('placeholder_username')}
        value={username}
        on:input={handleInput('username')}
      />
    </label>

    <label class="grid gap-2">
      <span class="text-sm font-medium text-emerald-200/90">{$_('field_password')}</span>
      <input
        class="w-full rounded-xl border border-emerald-400/50 bg-emerald-950/40 px-3 py-2 text-sm text-emerald-100 placeholder-emerald-300/70 outline-none transition focus:border-emerald-200 focus:ring-2 focus:ring-emerald-200/60"
        type="password"
        autocomplete="current-password"
        placeholder={$_('placeholder_password')}
        value={password}
        on:input={handleInput('password')}
      />
    </label>

    <label class="flex items-center gap-3 text-sm text-emerald-200/80">
      <input
        type="checkbox"
        class="h-4 w-4 rounded border border-emerald-400/70 bg-transparent text-emerald-200 focus:ring-emerald-300"
        checked={remember}
        on:change={handleRememberChange}
      />
      {$_('remember_label')}
    </label>

    {#if formError}
      <div class="rounded-xl border border-rose-400/40 bg-rose-500/20 px-4 py-3 text-sm text-rose-100">
        {formError}
      </div>
    {/if}

    {#if connectionError}
      <div class="rounded-xl border border-rose-400/40 bg-rose-500/20 px-4 py-3 text-sm text-rose-100">
        {connectionError}
      </div>
    {/if}
  </div>

  <div slot="footer">
    <Button variant="solid" size="md" fullWidth on:click={handleSubmit} disabled={isConnecting}>
      {isConnecting ? $_('button_connecting') : $_('button_continue')}
    </Button>
  </div>
</Card>



