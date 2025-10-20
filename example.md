# GarageDoor MQTT Command Examples

All commands are published to:

```
garage/<device-id>/command
```

Use the `deviceId` from your flashed configuration (e.g. `garage-esp32c6`).

---

## Trigger the Garage Door

```json
{
  "type": "open"
}
```

---

## Update Heartbeat Interval

Set periodic heartbeat publishes to every 45 seconds:

```json
{
  "type": "config_update",
  "heartbeatIntervalS": 45
}
```

---

## Update Debounce Window

Allow another open command after 20 seconds:

```json
{
  "type": "config_update",
  "debounceMs": 20000
}
```

---

## Update Relay Pulse Duration

Pulse relay for 750 ms:

```json
{
  "type": "config_update",
  "relayPulseMs": 750
}
```

---

## Update Multiple Settings at Once

```json
{
  "type": "config_update",
  "heartbeatIntervalS": 30,
  "debounceMs": 25000,
  "relayPulseMs": 600
}
```

Only supplied fields are changed; others remain in place. All values are validated (non-negative for heartbeat, non-negative for debounce, positive for relay pulse). Each successful update is persisted to NVS automatically.
