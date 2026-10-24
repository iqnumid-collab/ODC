# odc-root — API Reference

## IPC Methods (JSON-RPC 2.0 over Unix socket)

### Registry IPC (`/run/odc-root/odc-ipc.sock`)

#### `ping`
Health check.
```
→ {"jsonrpc":"2.0","method":"ping","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"pong":true,"timestamp":...},"id":1}
```

#### `status`
Get registry manager status.
```
→ {"jsonrpc":"2.0","method":"status","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"service":"odc-registry-manager","version":"0.1.0","ws_connections":0,"services":0},"id":1}
```

#### `services.list`
List registered services, optionally filtered by type.
```
→ {"jsonrpc":"2.0","method":"services.list","params":{"type":"app"},"id":1}
← {"jsonrpc":"2.0","result":{"services":[...],"total":42},"id":1}
```

#### `services.register`
Register a new service.
```
→ {"jsonrpc":"2.0","method":"services.register","params":{"id":"svc-1","name":"My Service","type":"app","metadata":{}},"id":1}
← {"jsonrpc":"2.0","result":{"ok":true,"service":{...}},"id":1}
```

#### `services.unregister`
Unregister a service.
```
→ {"jsonrpc":"2.0","method":"services.unregister","params":{"id":"svc-1"},"id":1}
← {"jsonrpc":"2.0","result":{"ok":true},"id":1}
```

### Tunnel Manager IPC (`/run/odc-root/tunnel-ipc.sock`)

#### `tunnel.status`
Get VPN server status.
```
→ {"jsonrpc":"2.0","method":"tunnel.status","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"ok":true,"servers":[{"id":"wg-main","type":"wireguard","running":true,"peer_count":3},{"id":"ovpn-main","type":"openvpn","running":true,"peer_count":1}]},"id":1}
```

#### `tunnel.peers.list`
List all VPN peers (WG + OpenVPN).
```
→ {"jsonrpc":"2.0","method":"tunnel.peers.list","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"peers":[
    {"tunnel_type":"wireguard","public_key":"...","allowed_ip":"10.0.0.2","last_handshake_sec":45,"bytes_rx":1024,"bytes_tx":512,"endpoint":"192.168.1.100:51820"},
    {"tunnel_type":"openvpn","common_name":"client1","assigned_ip":"10.8.0.2","remote_addr":"192.168.1.101:1194","connected_at":...,"bytes_rx":...}
],"count":2},"id":1}
```

#### `tunnel.peers.add`
Add a WireGuard peer with auto-assigned IP.
```
→ {"jsonrpc":"2.0","method":"tunnel.peers.add","params":{"public_key":"..."},"id":1}
← {"jsonrpc":"2.0","result":{"ok":true,"assigned_ip":"10.0.0.3"},"id":1}
```

#### `tunnel.peers.remove`
Remove a WireGuard peer.
```
→ {"jsonrpc":"2.0","method":"tunnel.peers.remove","params":{"public_key":"..."},"id":1}
← {"jsonrpc":"2.0","result":{"ok":true},"id":1}
```

#### `tunnel.health`
Get health stats per server.
```
→ {"jsonrpc":"2.0","method":"tunnel.health","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"ok":true,"servers":[
    {"id":"wg-main","type":"wireguard","running":true,"peers_total":5,"peers_connected":3},
    {"id":"ovpn-main","type":"openvpn","running":true,"peers_total":1,"peers_connected":1}
]},"id":1}
```

### Hook IPC (`/run/odc-root/hook.sock`)

#### `hook.status`
Get hook client status.
```
→ {"jsonrpc":"2.0","method":"hook.status","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"connected":true,"server":"127.0.0.1:9443","events_received":42,"events_processed":42,"thread_pool_active":0},"id":1}
```

#### `hook.ping`
Simple ping check.
```
→ {"jsonrpc":"2.0","method":"hook.ping","params":{},"id":1}
← {"jsonrpc":"2.0","result":{"pong":true},"id":1}
```

### Tunnel Notifications (tunnel-manager → registry-manager)

```
→ {"jsonrpc":"2.0","method":"tunnel.server.started","params":{"server_id":"wg-main","type":"wireguard","running":true}}
→ {"jsonrpc":"2.0","method":"tunnel.server.stopped","params":{"server_id":"wg-main","type":"wireguard"}}
→ {"jsonrpc":"2.0","method":"tunnel.client.connected","params":{"client_id":"...","server_id":"wg-main","tunnel_type":"wireguard","assigned_ip":"10.0.0.2","public_key":"..."}}
→ {"jsonrpc":"2.0","method":"tunnel.client.disconnected","params":{"client_id":"...","server_id":"wg-main","tunnel_type":"wireguard"}}
→ {"jsonrpc":"2.0","method":"tunnel.cert.issued","params":{...}}
→ {"jsonrpc":"2.0","method":"tunnel.error","params":{"server_id":"ovpn-main","error":"..."}}
```

## WebSocket API

WebSocket endpoint: `wss://<host>:9443/ws`

All messages are JSON with a `type` field. Subscriptions are managed automatically by the server — subscribe/unsubscribe messages are intercepted at the WsServer layer.

### Subscribe / Unsubscribe
```
→ {"type":"subscribe","topic":"tunnel.events"}
← {"type":"subscribed","topic":"tunnel.events"}

→ {"type":"unsubscribe","topic":"tunnel.events"}
← {"type":"unsubscribed","topic":"tunnel.events"}
```

### Service CRUD
```
→ {"type":"service.register","service":{"id":"svc-1","name":"My Service","type":"app","metadata":{}}}
← {"type":"service.registered","service":{...}}

→ {"type":"service.unregister","service_id":"svc-1"}
← {"type":"service.unregistered","service_id":"svc-1"}

→ {"type":"query","filter":{"type":"app"},"limit":50}
← {"type":"query_result","services":[...],"total":42}
```

### Event Stream
```
← {"type":"event","topic":"tunnel.events","data":{"event":"tunnel.client.connected","data":{...},"timestamp":...}}
```

## Webhook Delivery

HTTP POST with HMAC-SHA256 signing and exponential backoff retry.

```
POST https://webhook.example.com/path
Content-Type: application/json
User-Agent: odc-registry-manager/0.1.0
X-ODC-Signature: <base64(HMAC-SHA256(secret, body))>

{"event":"tunnel.client.connected","timestamp":...,"payload":{...}}
```

## CLI Commands

```
# Registry management
odc-cli status
odc-cli services list [--type <type>]
odc-cli services register --id <id> --name <name> --type <type>
odc-cli services unregister --id <id>

# Tunnel management
odc-cli tunnel status [-s <socket>]
odc-cli tunnel peers list [-s <socket>]
odc-cli tunnel peers add [-s <socket>] --public-key <key>
odc-cli tunnel peers remove [-s <socket>] --public-key <key>
odc-cli tunnel health [-s <socket>]

# Certificate lifecycle
odc-cli cert generate-ca --cert <path> --key <path> [--org <org>] [--cn <cn>]
odc-cli cert issue --cn <name> --ca-cert <path> --ca-key <path> --cert-out <path> --key-out <path>
odc-cli cert verify --cert <path> --ca-cert <path>
odc-cli cert fingerprint --cert <path>
odc-cli cert revoke --cert <path> --ca-cert <path> --ca-key <path> --crl <path>
odc-cli cert keypair-wg [--key-out <path>]

# Event streaming
odc-cli events watch [--url <wss://>] [--ca-cert <path>] [--client-cert <path>] [--client-key <path>] [--topic <topic>] [--timeout <sec>]

# Hook status
odc-cli hook status [-s <socket>]
```

## Error Codes

| Code   | Meaning                  |
|--------|--------------------------|
| -32700 | Parse error              |
| -32601 | Method not found         |
| -32000 | Internal / handler error |
