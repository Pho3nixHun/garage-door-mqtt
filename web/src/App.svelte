<script lang="ts">
  import { onMount } from 'svelte';
  import SetupStage from './components/SetupStage.svelte';
  import ControlStage from './components/ControlStage.svelte';
  import LanguageSwitcher from './components/LanguageSwitcher.svelte';
  import { mqttStore } from './lib/stores';
  import type { ConnectionParams } from './lib/mqtt';
  import { _ } from 'svelte-i18n';

  type Stage = 'configure' | 'control';

  const STORAGE_KEY = 'garage-door-web::connection';

  type StoredConnection = ConnectionParams & { remember: boolean };

  const DEFAULTS: ConnectionParams = {
    url: 'wss://b406d4a112e343469636dde3b36c432d.s1.eu.hivemq.cloud:8884/mqtt',
    username: 'esp32',
    password: '',
    deviceId: 'garage-esp32c6'
  };

  let stage: Stage = 'configure';
  let remember = true;
  let formError: string | null = null;
  let credentials: ConnectionParams = { ...DEFAULTS };

  $: connection = $mqttStore;

  const loadStoredCredentials = (): StoredConnection | null => {
    if (typeof window === 'undefined') {
      return null;
    }
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) {
        return null;
      }
      const parsed = JSON.parse(raw);
      if (!parsed.url || !parsed.deviceId) {
        return null;
      }
      return {
        url: parsed.url,
        username: parsed.username ?? '',
        password: parsed.password ?? '',
        deviceId: parsed.deviceId,
        commandTopic: parsed.commandTopic,
        stateTopic: parsed.stateTopic,
        remember: parsed.remember ?? true
      };
    } catch (error) {
      console.warn('Unable to read stored credentials', error);
      return null;
    }
  };

  const persistCredentials = () => {
    if (!remember || typeof window === 'undefined') {
      if (typeof window !== 'undefined') {
        localStorage.removeItem(STORAGE_KEY);
      }
      return;
    }

    const payload: StoredConnection = {
      ...credentials,
      remember
    };

    localStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
  };

  const connectWithCredentials = () => {
    try {
      mqttStore.connect(credentials);
    } catch (error) {
      formError = error instanceof Error ? error.message : $_('error_connection_start');
    }
  };

  const goToControlStage = () => {
    stage = 'control';
    persistCredentials();
    connectWithCredentials();
  };

  onMount(() => {
    const stored = loadStoredCredentials();
    if (stored) {
      credentials = {
        url: stored.url ?? DEFAULTS.url,
        username: stored.username ?? '',
        password: stored.password ?? '',
        deviceId: stored.deviceId ?? DEFAULTS.deviceId,
        commandTopic: stored.commandTopic,
        stateTopic: stored.stateTopic
      };
      remember = stored.remember;
      stage = 'control';
      connectWithCredentials();
    }
  });

  const startConnection = () => {
    formError = null;

    if (!credentials.username.trim() || !credentials.password.trim()) {
      formError = $_('error_missing_details');
      return;
    }

    goToControlStage();
  };

  const handleCredentialsUpdate = (event: CustomEvent<{ field: 'username' | 'password'; value: string }>) => {
    const { field, value } = event.detail;
    credentials = {
      ...credentials,
      [field]: value
    };
  };

  const handleRememberChange = (event: CustomEvent<boolean>) => {
    remember = event.detail;
    if (!remember && typeof window !== 'undefined') {
      localStorage.removeItem(STORAGE_KEY);
    } else if (remember) {
      persistCredentials();
    }
  };

  const resetToConfigure = () => {
    mqttStore.disconnect();
    stage = 'configure';
    formError = null;
  };

  const handleReconnect = () => {
    formError = null;
    connectWithCredentials();
  };

  const handleTrigger = () => {
    formError = null;
    try {
      mqttStore.openDoor();
    } catch (error) {
      formError = error instanceof Error ? error.message : $_('error_open_signal');
    }
  };
</script>

<main class="flex min-h-screen flex-col bg-slate-950 text-slate-100">
  <div class="flex justify-end px-5 pt-6">
    <LanguageSwitcher />
  </div>

  <header class="px-5 pb-8 pt-4 text-center">
    <h1 class="text-3xl font-semibold tracking-tight sm:text-4xl">{$_('app_title')}</h1>
    <p class="mt-2 text-sm text-slate-400">
      {$_('app_subtitle')}
    </p>
  </header>

  <section class="mx-auto mb-10 w-full max-w-xl flex-1 px-5">
    {#if stage === 'configure'}
      <SetupStage
        username={credentials.username}
        password={credentials.password}
        remember={remember}
        formError={formError}
        connectionError={connection.status === 'error' ? connection.error : undefined}
        isConnecting={connection.status === 'connecting'}
        on:update={handleCredentialsUpdate}
        on:rememberChange={handleRememberChange}
        on:submit={startConnection}
      />
    {:else}
      <ControlStage
        deviceId={credentials.deviceId}
        connection={connection}
        actionError={formError}
        on:changeDetails={resetToConfigure}
        on:reconnect={handleReconnect}
        on:trigger={handleTrigger}
      />
    {/if}
  </section>

</main>
