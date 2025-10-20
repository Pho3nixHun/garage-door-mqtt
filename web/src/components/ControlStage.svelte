<script lang="ts">
  import { createEventDispatcher, onDestroy, onMount } from 'svelte';
  import type { MqttStoreValue } from '../lib/mqtt';
  import { _ } from 'svelte-i18n';

  const dispatch = createEventDispatcher<{
    trigger: void;
    reconnect: void;
    changeDetails: void;
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
  const handleChangeDetails = () => dispatch('changeDetails');

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
      <span class="text-sm text-slate-400">{isConnected ? t('connection_online') : t('connection_offline')}</span>
    </div>
    <button
      class="rounded-full border border-slate-600/70 px-3 py-1 text-xs font-semibold text-slate-300 transition hover:border-slate-500 hover:text-slate-100"
      type="button"
      on:click={handleChangeDetails}
    >
      {t('change_details')}
    </button>
  </div>

  <div class="rounded-2xl border border-slate-800/80 bg-slate-900/80 p-6 shadow-lg shadow-black/30 backdrop-blur">
    <h2 class="text-xl font-semibold text-slate-50">{t('panel_title')}</h2>
    <p class="mt-1 text-sm text-slate-400">
      {t('door_summary', { values: { id: deviceId } })}
    </p>

    <div class="mt-6 grid gap-4 rounded-xl border border-slate-800 bg-slate-950/60 p-4 sm:grid-cols-2">
      <div>
        <p class="text-xs uppercase tracking-wide text-slate-400">{t('section_status')}</p>
        <p class="mt-2 text-2xl font-semibold text-slate-50">{statusLabel}</p>
        <p class="mt-2 text-sm text-slate-400">{cooldownMessage}</p>
      </div>

      <div>
        <p class="text-xs uppercase tracking-wide text-slate-400">{t('section_connection')}</p>
        <p class="mt-2 text-sm text-slate-300">
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

        <div class="mt-3 flex gap-2">
          <button
            class="rounded-lg border border-slate-700 px-3 py-2 text-xs font-semibold text-slate-200 transition hover:border-slate-600 hover:text-slate-50 disabled:cursor-not-allowed disabled:border-slate-700/70 disabled:text-slate-500"
            type="button"
            on:click={handleReconnect}
            disabled={isConnecting}
          >
            {isConnecting ? t('button_connecting') : t('button_try_again')}
          </button>
        </div>
      </div>
    </div>

    {#if connection.error}
      <div class="mt-4 rounded-lg border border-rose-500/40 bg-rose-500/10 px-3 py-2 text-sm text-rose-200">
        {connection.error}
      </div>
    {/if}

    {#if actionError}
      <div class="mt-4 rounded-lg border border-rose-500/40 bg-rose-500/10 px-3 py-2 text-sm text-rose-200">
        {actionError}
      </div>
    {/if}

    <button
      class="mt-6 w-full rounded-2xl bg-emerald-500 px-6 py-4 text-lg font-semibold text-emerald-950 shadow-lg shadow-emerald-500/30 transition hover:bg-emerald-400 disabled:cursor-not-allowed disabled:bg-slate-700/70 disabled:text-slate-400"
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
