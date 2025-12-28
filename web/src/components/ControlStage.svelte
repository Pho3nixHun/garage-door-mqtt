<script lang="ts">
  import { createEventDispatcher, onDestroy, onMount } from "svelte";
  import type { MqttStoreValue } from "../lib/mqtt";
  import { _ } from "svelte-i18n";
  import Button from "./ui/Button.svelte";
  import Card from "./ui/Card.svelte";
  import Tag from "./ui/Tag.svelte";

  const dispatch = createEventDispatcher<{
    trigger: void;
    reconnect: void;
    autoTriggered: void;
    autoOpenChange: boolean;
  }>();

  const AUTO_OPEN_STORAGE_KEY = 'garage-door-web::auto-open';

  export let deviceId = "";
  export let connection: MqttStoreValue;
  export let actionError: string | null = null;
  export let autoTrigger = false;
  export let autoOpenPreference = false;

  let t: (key: string, vars?: Record<string, string | number>) => string = (key) => key;
  $: t = $_;

  const formatStatusLabel = (state: string, translator: typeof t) => {
    switch (state) {
      case "LISTENING":
        return translator("status_ready");
      case "TRIGGERING":
        return translator("status_opening");
      case "THROTTLED":
        return translator("status_cooling");
      default:
        return translator("status_waiting");
    }
  };

  const getStatusTone = (state: string): "success" | "warning" | "info" => {
    switch (state) {
      case "LISTENING":
        return "success";
      case "THROTTLED":
        return "warning";
      default:
        return "info";
    }
  };

  $: garageState = connection.garageState ?? "UNKNOWN";
  $: statusLabel = formatStatusLabel(garageState, t);
  $: statusTone = getStatusTone(garageState);

  let countdown = 0;
  let autoTriggerFired = false;
  let autoOpenChecked = autoOpenPreference;

  $: {
    const remainingMs = connection.cooldownMs ?? 0;
    countdown = remainingMs > 0 ? Math.max(0, Math.ceil(remainingMs / 1000)) : 0;
  }

  let interval: number | undefined;

  onMount(() => {
    interval = window.setInterval(() => {
      if (garageState === "THROTTLED" && countdown > 0) {
        countdown = Math.max(0, countdown - 1);
      }
    }, 1000);

    if (typeof window !== 'undefined') {
      const stored = window.localStorage.getItem(AUTO_OPEN_STORAGE_KEY);
      if (stored !== null) {
        const value = stored === '1';
        if (value !== autoOpenChecked) {
          autoOpenChecked = value;
          dispatch('autoOpenChange', value);
        }
      }
    }
  });

  $: isConnecting = connection.status === "connecting";
  $: isConnected = connection.status === "connected";
  $: canOpen = isConnected && garageState === "LISTENING";
  $: if (autoOpenPreference !== autoOpenChecked) {
    autoOpenChecked = autoOpenPreference;
  }

  const handleTrigger = () => dispatch("trigger");
  const handleReconnect = () => dispatch("reconnect");

  $: {
    if (garageState !== "THROTTLED") {
      countdown = 0;
    }
  }

  $: {
    if (!autoTrigger && !autoOpenChecked) {
      autoTriggerFired = false;
    }
  }

  $: {
    if ((autoTrigger || autoOpenChecked) && !autoTriggerFired && canOpen) {
      handleTrigger();
      autoTriggerFired = true;
      dispatch("autoTriggered");
    }
  }

  const handleAutoOpenToggle = (event: Event) => {
    const target = event.currentTarget as HTMLInputElement;
    autoOpenChecked = target.checked;
    if (typeof window !== 'undefined') {
      window.localStorage.setItem(
        AUTO_OPEN_STORAGE_KEY,
        autoOpenChecked ? '1' : '0'
      );
    }
    dispatch('autoOpenChange', autoOpenChecked);
  };

  onDestroy(() => {
    if (interval !== undefined) {
      clearInterval(interval);
    }
  });
</script>

<div class="space-y-5">
  <Card>
    <div slot="header" class="space-y-1">
      <div class="flex items-center justify-between gap-3">
        <Tag tone={statusTone}>
          {statusLabel}
        </Tag>
        <span class="text-sm text-emerald-200/90">{isConnected ? t('connection_online') : t('connection_offline')}</span>
      </div>
      <h2 class="text-xl font-semibold text-emerald-100">{t('panel_title')}</h2>
      <p class="text-sm text-emerald-200/90">
        {t('door_summary', { values: { id: deviceId } })}
      </p>
    </div>

    <div slot="content" class="space-y-4">
      {#if connection.error}
        <div class="space-y-3 rounded-xl border border-rose-400/40 bg-rose-500/15 px-4 py-3 text-sm text-rose-100">
          <p>{connection.error}</p>
          <Button variant="outline" size="sm" on:click={handleReconnect} disabled={isConnecting}>
            {isConnecting ? t('button_connecting') : t('button_try_again')}
          </Button>
        </div>
      {/if}

      <label class="flex items-center gap-3 text-sm text-emerald-200/80">
        <span class="relative inline-flex h-6 w-11 items-center">
          <input
            type="checkbox"
            class="peer absolute inset-0 h-full w-full cursor-pointer opacity-0"
            checked={autoOpenChecked}
            on:change={handleAutoOpenToggle}
            aria-label={t('auto_open_label')}
          />
          <span class="h-5 w-9 rounded-full border border-emerald-400/70 bg-transparent transition-colors duration-200 peer-checked:border-emerald-500 peer-checked:bg-emerald-500 peer-focus-visible:outline peer-focus-visible:outline-2 peer-focus-visible:outline-offset-2 peer-focus-visible:outline-emerald-300"></span>
          <span class="absolute left-0.5 top-0.5 h-4 w-4 rounded-full bg-emerald-200 transition-transform duration-200 peer-checked:translate-x-[18px]"></span>
        </span>
        <span>{t('auto_open_label')}</span>
      </label>

      {#if actionError}
        <div class="rounded-xl border border-rose-400/40 bg-rose-500/15 px-4 py-3 text-sm text-rose-100">
          {actionError}
        </div>
      {/if}
    </div>

    <div slot="footer">
      <Button variant="solid" size="lg" fullWidth disabled={!canOpen} on:click={handleTrigger}>
        {#if garageState === 'THROTTLED'}
          {t('button_cooling')} ({countdown}s)
        {:else if garageState === 'TRIGGERING'}
          {t('button_opening')}
        {:else}
          {t('button_open')}
        {/if}
      </Button>
    </div>
  </Card>
</div>
