import mqtt from 'mqtt';
import type { IClientOptions, MqttClient } from 'mqtt';
import { writable } from 'svelte/store';

export type GarageState = 'LISTENING' | 'TRIGGERING' | 'THROTTLED' | 'UNKNOWN';

export interface ConnectionParams {
  url: string;
  username?: string;
  password?: string;
  deviceId: string;
  commandTopic?: string;
  stateTopic?: string;
}

interface ResolvedConnection {
  url: string;
  username?: string;
  password?: string;
  deviceId: string;
  commandTopic: string;
  stateTopic: string;
}

export interface MqttStoreValue {
  status: 'disconnected' | 'connecting' | 'connected' | 'error';
  error?: string;
  garageState: GarageState;
  cooldownMs?: number;
  lastUpdate?: number;
  connection?: Pick<ResolvedConnection, 'url' | 'deviceId' | 'stateTopic' | 'commandTopic'>;
}

const initialState: MqttStoreValue = {
  status: 'disconnected',
  garageState: 'UNKNOWN'
};

const parseStatePayload = (payload: Buffer | Uint8Array): Partial<MqttStoreValue> => {
  try {
    const text = payload.toString();
    const data = JSON.parse(text);
    const garageState = (data.state ?? 'UNKNOWN') as GarageState;

    return {
      garageState,
      cooldownMs: typeof data.cooldownMs === 'number' ? data.cooldownMs : undefined,
      lastUpdate: typeof data.timestamp === 'number' ? data.timestamp : Date.now()
    };
  } catch (error) {
    console.warn('[mqtt] Failed to parse state payload', error);
    return {};
  }
};

function resolveTopics(params: ConnectionParams): ResolvedConnection {
  const deviceId = params.deviceId.trim();
  if (!deviceId) {
    throw new Error('Device ID is required');
  }

  const url = params.url.trim();
  if (!url) {
    throw new Error('Broker URL is required');
  }

  return {
    url,
    username: params.username?.trim() || undefined,
    password: params.password || undefined,
    deviceId,
    commandTopic: params.commandTopic?.trim() || `garage/${deviceId}/command`,
    stateTopic: params.stateTopic?.trim() || `garage/${deviceId}/state`
  };
}

export const createMqttStore = () => {
  const { subscribe, set, update } = writable<MqttStoreValue>({ ...initialState });
  let client: MqttClient | undefined;
  let activeConnection: ResolvedConnection | undefined;

  const cleanupClient = () => {
    if (client) {
      client.removeAllListeners();
      client.end(true);
      client = undefined;
    }
  };

  const connectToBroker = (params: ConnectionParams) => {
    const resolved = resolveTopics(params);

    cleanupClient();

    const options: IClientOptions = {
      protocolVersion: 5,
      clean: true,
      reconnectPeriod: 0,
      keepalive: 60,
      username: resolved.username,
      password: resolved.password
    };

    set({
      status: 'connecting',
      garageState: 'UNKNOWN',
      connection: {
        url: resolved.url,
        deviceId: resolved.deviceId,
        stateTopic: resolved.stateTopic,
        commandTopic: resolved.commandTopic
      }
    });

    try {
      client = mqtt.connect(resolved.url, options);
    } catch (error) {
      update((state) => ({
        ...state,
        status: 'error',
        error: error instanceof Error ? error.message : 'Failed to create MQTT client'
      }));
      return;
    }

    activeConnection = resolved;

    client.on('connect', () => {
      client?.subscribe(resolved.stateTopic, { qos: 1 }, (err) => {
        if (err) {
          update((state) => ({
            ...state,
            status: 'error',
            error: `Failed to subscribe to ${resolved.stateTopic}`
          }));
          return;
        }

        update((state) => ({
          ...state,
          status: 'connected',
          error: undefined
        }));
      });
    });

    client.on('message', (topic, payload) => {
      if (topic === resolved.stateTopic) {
        const partial = parseStatePayload(payload);
        update((state) => ({
          ...state,
          ...partial
        }));
      }
    });

    client.on('error', (err) => {
      console.error('[mqtt] client error', err);
      update((state) => ({
        ...state,
        status: 'error',
        error: err?.message ?? 'MQTT connection error'
      }));
    });

    client.on('close', () => {
      update((state) => ({
        ...state,
        status: 'disconnected'
      }));
    });
  };

  const disconnect = () => {
    cleanupClient();
    activeConnection = undefined;
    set({ ...initialState });
  };

  const publishOpenCommand = () => {
    if (!client || !activeConnection) {
      throw new Error('MQTT client is not connected');
    }

    const payload = {
      type: 'open',
      source: 'web-app',
      timestamp: Date.now()
    };

    client.publish(activeConnection.commandTopic, JSON.stringify(payload), { qos: 1 }, (err) => {
      if (err) {
        update((state) => ({
          ...state,
          status: 'error',
          error: `Failed to publish command: ${err.message}`
        }));
      }
    });
  };

  return {
    subscribe,
    connect: connectToBroker,
    disconnect,
    openDoor: publishOpenCommand
  };
};

export type MqttStore = ReturnType<typeof createMqttStore>;
