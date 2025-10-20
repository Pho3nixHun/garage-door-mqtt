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
    logout: void;
  }>();

  export let deviceId = "";
  export let connection: MqttStoreValue;
  export let actionError: string | null = null;

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
  });

  $: isConnecting = connection.status === "connecting";
  $: isConnected = connection.status === "connected";
  $: canOpen = isConnected && garageState === "LISTENING";

  const handleTrigger = () => dispatch("trigger");
  const handleReconnect = () => dispatch("reconnect");

  $: {
    if (garageState !== "THROTTLED") {
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
  <div class="flex items-center justify-between gap-3">
    <div class="flex items-center gap-3">
      <Tag tone={statusTone}>
        {statusLabel}
      </Tag>
      <span class="text-sm text-emerald-200/90">{isConnected ? t('connection_online') : t('connection_offline')}</span>
    </div>
  </div>

  <Card>
    <div slot="header" class="space-y-1">
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

      {#if actionError}
        <div class="rounded-xl border border-rose-400/40 bg-rose-500/15 px-4 py-3 text-sm text-rose-100">
          {actionError}
        </div>
      {/if}

      <p class="text-xs text-emerald-200/70">
        {t('open_note')}
      </p>
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


