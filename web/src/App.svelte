<script lang="ts">
  import { onMount } from 'svelte';
  import SetupStage from './components/SetupStage.svelte';
  import ControlStage from './components/ControlStage.svelte';
  import LanguageSwitcher from './components/LanguageSwitcher.svelte';
  import Button from './components/ui/Button.svelte';
  import { mqttStore } from './lib/stores';
  import type { ConnectionParams, MqttStoreValue } from './lib/mqtt';
  import { _ } from 'svelte-i18n';
  import logoUrl from './assets/oasis-logo.svg?url';

  interface BeforeInstallPromptEvent extends Event {
    prompt: () => Promise<void>;
    userChoice: Promise<{ outcome: 'accepted' | 'dismissed'; platform: string }>;
  }

  type Stage = 'configure' | 'control';

  const STORAGE_KEY = 'garage-door-web::connection';
  const AUTO_OPEN_STORAGE_KEY = 'garage-door-web::auto-open';

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
  let autoTrigger = false;
  let autoTriggerHandled = false;
  let lastKnownState: MqttStoreValue = { status: 'disconnected', garageState: 'UNKNOWN' };
  let autoOpenEnabled = false;
  let deferredInstallPrompt: BeforeInstallPromptEvent | null = null;
  let showInstallPrompt = false;
  let installPromptDismissed = false;

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

  const loadAutoOpenPreference = () => {
    if (typeof window === 'undefined') {
      return false;
    }
    return window.localStorage.getItem(AUTO_OPEN_STORAGE_KEY) === '1';
  };

  const persistAutoOpenPreference = (value: boolean) => {
    if (typeof window === 'undefined') {
      return;
    }
    if (value) {
      window.localStorage.setItem(AUTO_OPEN_STORAGE_KEY, '1');
    } else {
      window.localStorage.removeItem(AUTO_OPEN_STORAGE_KEY);
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

  const clearAutoTriggerParam = () => {
    if (typeof window === 'undefined') {
      return;
    }
    const url = new URL(window.location.href);
    url.searchParams.delete('autotrigger');
    url.searchParams.delete('action');
    window.history.replaceState({}, '', url.toString());
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
    const unsubscribe = mqttStore.subscribe((state) => {
      lastKnownState = state;
    });

    const handleVisibilityChange = () => {
      if (typeof document === 'undefined') {
        return;
      }
      if (document.visibilityState === 'visible') {
        if (stage === 'control' && lastKnownState.status !== 'connected' && lastKnownState.status !== 'connecting') {
          if (lastKnownState.connection) {
            mqttStore.connect({
              url: lastKnownState.connection.url,
              deviceId: lastKnownState.connection.deviceId,
              username: credentials.username,
              password: credentials.password,
              commandTopic: lastKnownState.connection.commandTopic,
              stateTopic: lastKnownState.connection.stateTopic
            });
          } else {
            connectWithCredentials();
          }
        }
      }
    };

    const handleBeforeInstallPrompt = (event: Event) => {
      if (installPromptDismissed) {
        return;
      }
      event.preventDefault();
      deferredInstallPrompt = event as BeforeInstallPromptEvent;
      showInstallPrompt = true;
    };

    const handleAppInstalled = () => {
      deferredInstallPrompt = null;
      showInstallPrompt = false;
      installPromptDismissed = true;
    };

    if (typeof window !== 'undefined') {
      const params = new URLSearchParams(window.location.search);
      autoTrigger = params.get('autotrigger') === '1' || params.get('action') === 'open';
      autoTriggerHandled = false;
      autoOpenEnabled = loadAutoOpenPreference();

      window.addEventListener('beforeinstallprompt', handleBeforeInstallPrompt as EventListener);
      window.addEventListener('appinstalled', handleAppInstalled);
    }

    if (typeof document !== 'undefined') {
      document.addEventListener('visibilitychange', handleVisibilityChange);
    }

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

    return () => {
      unsubscribe();
      if (typeof document !== 'undefined') {
        document.removeEventListener('visibilitychange', handleVisibilityChange);
      }
      if (typeof window !== 'undefined') {
        window.removeEventListener('beforeinstallprompt', handleBeforeInstallPrompt as EventListener);
        window.removeEventListener('appinstalled', handleAppInstalled);
      }
    };
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

  const clearStoredCredentials = () => {
    if (typeof window !== 'undefined') {
      localStorage.removeItem(STORAGE_KEY);
    }
    remember = false;
    credentials = {
      ...DEFAULTS,
      username: '',
      password: ''
    };
  };

  const resetToConfigure = () => {
    clearStoredCredentials();
    mqttStore.disconnect();
    stage = 'configure';
    formError = null;
    autoTrigger = false;
    autoTriggerHandled = false;
    clearAutoTriggerParam();
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

  const handleAutoTriggered = () => {
    autoTrigger = false;
    autoTriggerHandled = true;
    clearAutoTriggerParam();
  };

  const handleAutoOpenChange = (event: CustomEvent<boolean>) => {
    autoOpenEnabled = event.detail;
    persistAutoOpenPreference(autoOpenEnabled);
  };

  const handleInstallClick = async () => {
    if (!deferredInstallPrompt) {
      return;
    }
    try {
      await deferredInstallPrompt.prompt();
      const choice = await deferredInstallPrompt.userChoice;
      if (choice.outcome === 'dismissed') {
        installPromptDismissed = true;
      }
    } catch (error) {
      console.warn('[pwa] install prompt failed', error);
    } finally {
      deferredInstallPrompt = null;
      showInstallPrompt = false;
    }
  };
</script>

<main class="oasis-app text-slate-100">
  <div class="oasis-overlay">
    <div class="oasis-header">
      <img src={logoUrl} alt="Oasis Residence" class="oasis-logo" />
      <div class="oasis-header-controls">
        <LanguageSwitcher />
        {#if showInstallPrompt}
          <Button variant="outline" size="sm" on:click={handleInstallClick}>
            Install App
          </Button>
        {/if}
        {#if stage === 'control'}
          <Button variant="outline" size="sm" on:click={resetToConfigure}>
            {$_('button_logout')}
          </Button>
        {/if}
      </div>
    </div>

    <section class="oasis-content">
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
          autoTrigger={autoTrigger && !autoTriggerHandled}
          autoOpenPreference={autoOpenEnabled}
          on:reconnect={handleReconnect}
          on:trigger={handleTrigger}
          on:autoTriggered={handleAutoTriggered}
          on:autoOpenChange={handleAutoOpenChange}
        />
      {/if}
    </section>
  </div>
</main>
