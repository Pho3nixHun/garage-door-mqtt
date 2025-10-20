# ESP32-C6 Smart Garage Opener — Managed MQTT Architecture Plan

> Target hardware: **Seeed Studio XIAO ESP32-C6**  
> Connectivity: **Wi‑Fi client + managed MQTT broker (HiveMQ Cloud / EMQX Cloud / AWS IoT Core, etc.)**  
> Client UI: **Static Web App (e.g., GitHub Pages) using MQTT over WebSocket**

---

## 1. Goals & Non-goals

**Goals**

- Allow a user to open the garage via a web page or automation while the ESP32-C6 remains outbound-only.
- Use an off-the-shelf managed MQTT broker (no self-hosting, no port forwarding).
- Keep the ESP32 firmware simple: connect, subscribe, act on commands, publish state.
- Enforce a 30 s debounce on relay activation.
- Stream live status updates (`LISTENING`, `TRIGGERING`, `THROTTLED`, etc.) back to clients.

**Non-goals**

- BLE pairing, invites, or auto-open based on BLE reconnect.
- Running a custom public WebSocket server or P2P signalling.
- Having the ESP issue tokens or manage user identities (broker handles auth).
- Providing a full mobile app (web client covers manual control).
  - Providing a full mobile app (web client covers manual control).

---

## 2. High-level Architecture

```
[ Browser Client ]
    |  MQTT over WebSocket (TLS)
[ Managed MQTT Broker ]
    |  MQTT over TLS (device credentials)
[ ESP32-C6 Garage Opener ]
```

1. Browser loads the web page (static hosting) and prompts the user for broker credentials (or retrieves them from local storage).
2. Browser connects to the broker via MQTT over WebSocket (TLS) and subscribes to status topics.
3. ESP32-C6 boots, connects to the same broker via TLS (device credentials).
4. Client publishes `open` commands; broker delivers them to the ESP.
5. ESP validates, applies debounce, triggers relay, and publishes state updates.
6. UI reflects state in real time (button disabled while throttled, etc.).

---

## 3. Hardware & Wiring

**Core**

- Seeed Studio XIAO ESP32-C6 (USB or 5 V supply).
- Opto-isolated relay module (IN ← `RELAY_PIN`, VCC ← 5 V/3.3 V, GND ← GND). Dry contacts wired across the wall-button terminals.

**Optional**

- Status LED (e.g., ON when throttled).
- Manual pushbutton input for local override (debounced in firmware).

**Suggested pins**

| Function    | Pin |
|-------------|-----|
| Relay IN    | D2  |
| Status LED  | D1  |

---

## 4. System States & Timing

- `LISTENING`: ready for commands.
- `TRIGGERING`: relay pulse active (`RELAY_PULSE_MS` default 500 ms).
- `THROTTLED`: within 30 s debounce window; reject new open requests.

Constants:

- `RELAY_PULSE_MS = 500`
- `DEBOUNCE_MS = 30000`

State transitions are published over MQTT so all clients see the same timeline.

---

## 5. Message Bus & Authentication

- **Broker**: Managed MQTT service (HiveMQ Cloud, EMQX Cloud, AWS IoT, Azure IoT Hub, etc.).
- **Topics** (example):
  - `garage/<device-id>/command` — clients publish commands (`{"type":"open"}`).
  - `garage/<device-id>/state` — device publishes state updates (`{"state":"THROTTLED","cooldownMs":12000}`).
- **Auth**:
  - ESP32-C6 uses device credentials provisioned in the broker (username/password or client certificate).
  - Browser clients use credentials issued via the broker control plane (or a simple admin UI) to connect over MQTT/WebSocket.
  - Broker handles TLS termination; no inbound ports on the ESP.
- **Message format**: JSON with explicit `type`, `timestamp`, and any metadata the UI needs (no extra token handling in firmware).

---

## 6. ESP32-C6 Firmware

1. **Wi-Fi client**: connect using stored credentials; re-connect on drop.
2. **MQTT client**:
   - Connect to broker via TLS.
   - Subscribe to `command` topic.
   - Publish heartbeat/state on `state` topic (on connect, on state change, on interval).
3. **Command handling**:
   - Check debounce timer (`DEBOUNCE_MS`).
   - If OK: trigger relay, update internal state (`TRIGGERING` → `THROTTLED` → `LISTENING`), publish successes.
   - If blocked: publish `THROTTLED` state with remaining cooldown.
4. **Persistence**:
   - NVS for Wi-Fi credentials, MQTT credentials, last state if needed.
   - No BLE/Invite data required.
5. **Diagnostics**:
   - Optional HTTP endpoint for local health checks (Wi-Fi strength, broker connection status).

---

## 7. Web Companion (Client)

- Static single-page app (HTML + JS).
- Features:
  - Connect button (starts MQTT/WebSocket session with stored credentials).
  - `Open Door` button (disabled when state ≠ `LISTENING`).
  - Live state indicator, log of recent messages/errors.
- Workflow:
  1. Provide broker credentials (from admin UI, stored locally, or typed by user).
  2. Connect to broker using MQTT over WebSocket.
  3. Subscribe to `state`.
  4. Publish `{ "type": "open" }` to `command` when button pressed.
  5. Update UI based on `state` messages (`LISTENING`, `TRIGGERING`, `THROTTLED`, etc.).
- Optional: integrate web push or notifications for status changes.

---

## 8. Admin & Credential Management

- Admin UI/CLI to:
  - Manage device credentials (for ESPs) in the broker dashboard.
  - Create/revoke client credentials (and optionally map them to ACLs).
  - View logs, connection status, last commands.
- Storage of credentials can be managed via broker APIs (most managed services provide control plane for users/certs).

---

## 9. Security Considerations

- All traffic over TLS.
- Rotate credentials periodically; leverage broker ACLs to scope topic access.
- Throttle commands in firmware to prevent abuse (`DEBOUNCE_MS`).
- Log and audit command usage (either in the broker or via device log).
- Optionally restrict MQTT topics at the broker level (per-client ACLs).

---

## 10. Future Enhancements

- Integrate with Home Assistant / Node-RED by subscribing to the same MQTT topics.
- Add sensor feedback (door open/closed) using additional GPIO, publish on state topic.
- Implement OTA updates triggered via MQTT (e.g., command `{"type":"ota","url":"..."}`).
- Provide fallback manual trigger (HTTP endpoint or physical button) if MQTT unavailable.

---

## 11. Summary

- ESP32-C6 joins home Wi-Fi and maintains a secure outbound MQTT connection.
- Browser client (or automation) connects via WebSocket to the same broker.
- Commands flow through the broker; the ESP enforces debounce and pulses the relay.
- Managed MQTT service removes the need for port forwarding or custom servers.
- Entire BLE invite workflow is removed; focus is on reliable, authenticated remote control with live status feedback.




