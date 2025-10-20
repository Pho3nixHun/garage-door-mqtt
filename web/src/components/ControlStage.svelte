<script lang="ts">
  import { createEventDispatcher, onDestroy, onMount } from 'svelte';
  import type { MqttStoreValue } from '../lib/mqtt';
  import { _ } from 'svelte-i18n';

  const dispatch = createEventDispatcher<{
    trigger: void;
    reconnect: void;
    logout: void;
  }>();

  export let deviceId = '';
  export let connection: MqttStoreValue;
  export let actionError: string | null = null;

  let t: (key: string, vars?: Record<string, string | number>) => string = (key) => key;
  $: t = $_;

  const getStatusColor = (state: string) => {
    switch (state) {
      case 'LISTENING':
        return 'bg-emerald-500/90 text-emerald-950';
      case 'TRIGGERING':
        return 'bg-sky-500/90 text-sky-950';
      case 'THROTTLED':
        return 'bg-amber-400/90 text-amber-950';
      default:
        return 'bg-slate-400/80 text-slate-900';
    }
  };

  const formatStatusLabel = (state: string, translator: typeof t) => {
    switch (state) {
      case 'LISTENING':
        return translator('status_ready');
      case 'TRIGGERING':
        return translator('status_opening');
      case 'THROTTLED':
        return translator('status_cooling');
      default:
        return translator('status_waiting');
    }
  };

  const formatCooldownMessage = (secondsRemaining: number, translator: typeof t) => {
    if (secondsRemaining <= 0) {
      return translator('cooldown_ready');
    }
    return translator('cooldown_wait', { values: { seconds: secondsRemaining } });
  };

  $: garageState = connection.garageState ?? 'UNKNOWN';
  $: statusClass = getStatusColor(garageState);
  $: statusLabel = formatStatusLabel(garageState, t);

  let countdown = 0;

  $: {
    const remainingMs = connection.cooldownMs ?? 0;
    countdown = remainingMs > 0 ? Math.max(0, Math.ceil(remainingMs / 1000)) : 0;
  }

  let interval: number | undefined;

  onMount(() => {
    interval = window.setInterval(() => {
      if (garageState === 'THROTTLED' && countdown > 0) {
        countdown = Math.max(0, countdown - 1);
      }
    }, 1000);
  });

  $: cooldownMessage = formatCooldownMessage(countdown, t);
  $: isConnecting = connection.status === 'connecting';
  $: isConnected = connection.status === 'connected';
  $: canOpen = isConnected && garageState === 'LISTENING';

  const handleTrigger = () => dispatch('trigger');
  const handleReconnect = () => dispatch('reconnect');
  const handleLogout = () => dispatch('logout');

  $: {
    if (garageState !== 'THROTTLED') {
      countdown = 0;
    }
  }

  onDestroy(() => {
    if (interval !== undefined) {
      clearInterval(interval);
    }
  });
</script>

<div class="space-y-5">
  <div class="flex items-center justify-between">
    <div class="flex items-center gap-3">
      <span class={`rounded-full px-3 py-1 text-xs font-semibold uppercase tracking-wide ${statusClass}`}>
        {statusLabel}
      </span>
      <span class="text-sm text-emerald-200/90">{isConnected ? t('connection_online') : t('connection_offline')}</span>
    </div>
    <button
      class="rounded-full border border-emerald-300/60 px-3 py-1 text-xs font-semibold text-emerald-100 transition hover:border-emerald-200 hover:text-emerald-50"
      type="button"
      on:click={handleLogout}
    >
      {t('button_logout')}
    </button>
  </div>

  <div class="rounded-3xl border border-emerald-300/50 bg-emerald-950/20 p-6 shadow-xl shadow-emerald-900/30 backdrop-blur">
    <h2 class="text-xl font-semibold text-emerald-100">{t('panel_title')}</h2>
    <p class="mt-1 text-sm text-emerald-200/90">
      {t('door_summary', { values: { id: deviceId } })}
    </p>

    <div class="mt-6 grid gap-4 rounded-xl border border-emerald-400/40 bg-emerald-950/30 p-4 sm:grid-cols-2">
      <div>
        <p class="text-xs uppercase tracking-wide text-emerald-200/80">{t('section_status')}</p>
        <p class="mt-2 text-2xl font-semibold text-emerald-100">{statusLabel}</p>
        <p class="mt-2 text-sm text-emerald-200/80">{cooldownMessage}</p>
      </div>

      <div>
        <p class="text-xs uppercase tracking-wide text-emerald-200/80">{t('section_connection')}</p>
        <p class="mt-2 text-sm text-emerald-200/90">
          {#if connection.status === 'connected'}
            {t('connection_connected')}
          {:else if connection.status === 'connecting'}
            {t('connection_connecting')}
          {:else if connection.status === 'error'}
            {t('connection_error')}
          {:else}
            {t('connection_disconnected')}
          {/if}
        </p>

        {#if connection.status === 'error'}
          <div class="mt-3 flex gap-2">
            <button
              class="rounded-lg border border-emerald-300/60 bg-emerald-600/80 px-3 py-2 text-xs font-semibold text-emerald-50 transition hover:bg-emerald-500 disabled:cursor-not-allowed disabled:border-emerald-400/40 disabled:bg-emerald-700/40 disabled:text-emerald-200/50"
              type="button"
              on:click={handleReconnect}
              disabled={isConnecting}
            >
              {isConnecting ? t('button_connecting') : t('button_try_again')}
            </button>
          </div>
        {/if}
      </div>
    </div>

    {#if connection.error}
      <div class="mt-4 rounded-lg border border-rose-400/40 bg-rose-500/20 px-3 py-2 text-sm text-rose-100">
        {connection.error}
      </div>
    {/if}

    {#if actionError}
      <div class="mt-4 rounded-lg border border-rose-400/40 bg-rose-500/20 px-3 py-2 text-sm text-rose-100">
        {actionError}
      </div>
    {/if}

    <button
      class="mt-6 w-full rounded-2xl bg-gradient-to-r from-emerald-500 via-teal-500 to-emerald-400 px-6 py-4 text-lg font-semibold text-emerald-950 shadow-lg shadow-emerald-500/40 transition hover:from-emerald-400 hover:via-teal-400 hover:to-emerald-300 disabled:cursor-not-allowed disabled:from-emerald-700/60 disabled:to-emerald-700/60 disabled:text-emerald-200/60"
      type="button"
      disabled={!canOpen}
      on:click={handleTrigger}
    >
      {#if garageState === 'THROTTLED'}
        {t('button_cooling')}
      {:else if garageState === 'TRIGGERING'}
        {t('button_opening')}
      {:else}
        {t('button_open')}
      {/if}
    </button>
  </div>
</div>
