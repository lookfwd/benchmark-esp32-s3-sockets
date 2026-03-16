# Readme

## Initial Performance

```
Connecting to 192.168.86.40:5000...
Connected!
0.04 MB/s (0.29 Mbps)
0.09 MB/s (0.69 Mbps)
0.02 MB/s (0.13 Mbps)
0.07 MB/s (0.57 Mbps)
0.09 MB/s (0.69 Mbps)
0.02 MB/s (0.18 Mbps)
0.06 MB/s (0.47 Mbps)
0.08 MB/s (0.63 Mbps)
0.08 MB/s (0.63 Mbps)
```

## Claims Improvements

Here's what was changed and why:

**sdkconfig (the big win):**
- `TCP_SND_BUF_DEFAULT`: 5760 → 65534 — TCP send buffer was only ~4 MSS segments, now ~45
- `TCP_WND_DEFAULT`: 5760 → 65534 — TCP receive window was tiny, limiting how much unacknowledged data can be in flight
- `TCP_RECVMBOX_SIZE`: 6 → 64 — lwIP internal mailbox was too small
- `TCP_OOSEQ_MAX_PBUFS`: 4 → 8 — more out-of-order buffering to handle WiFi packet reordering
- `WIFI_TX_BA_WIN` / `WIFI_RX_BA_WIN`: 6 → 16 — AMPDU block-ack window, allows more WiFi frames in flight

**Code:**
- Send buffer bumped from 16KB to 32KB
- Socket SO_SNDBUF bumped to 65534
- Removed `TCP_NODELAY` — Nagle is actually helpful for bulk streaming (batches partial segments)

Rebuild and reflash. You should see a significant improvement — expect ~5-15 Mbps depending on WiFi conditions.

## Improved Configuration

```
Connecting to 192.168.86.40:5000...
Connected!
3.35 MB/s (26.79 Mbps)
3.26 MB/s (26.06 Mbps)
3.60 MB/s (28.83 Mbps)
3.11 MB/s (24.89 Mbps)
3.52 MB/s (28.14 Mbps)
3.50 MB/s (27.98 Mbps)
3.77 MB/s (30.19 Mbps)
```

## Comment

Nice! ~28 Mbps is excellent for ESP32-S3 over WiFi — that's close to the practical maximum for TCP over 802.11n. The TCP buffer tuning made a ~40x improvement.



# Websockets

## Initial build

```
% cargo run -- "ws://192.168.86.40:5000/ws"
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.10s
     Running `target/debug/socket-bandwidth 'ws://192.168.86.40:5000/ws'`
Connecting to ws://192.168.86.40:5000/ws...
Connected!
2.31 MB/s (18.47 Mbps)
2.35 MB/s (18.81 Mbps)
2.33 MB/s (18.65 Mbps)
2.23 MB/s (17.88 Mbps)
1.65 MB/s (13.21 Mbps)
1.99 MB/s (15.88 Mbps)
```

## Improvements

Increased `SEND_BUF_SIZE` from 4096 to 16384. With 4KB frames, the WebSocket framing overhead (2-14 bytes per frame + masking) is proportionally large. At 16KB per frame, it's much less significant.

That said, ~18 Mbps is roughly what to expect with WebSocket on ESP32-S3 over WiFi. The raw TCP version likely got more because:
- No per-message framing overhead (WebSocket adds header bytes per frame)
- No HTTP server layer in the middle (the `esp_http_server` adds processing)
- Raw TCP can use larger send buffers (32KB) and the kernel handles segmentation

WebSocket will always be slower than raw TCP — it's the cost of the protocol. The 16KB buffer should help get a few more Mbps though.

## Final build

```
% cargo run -- "ws://192.168.86.40:5000/ws"
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.08s
     Running `target/debug/socket-bandwidth 'ws://192.168.86.40:5000/ws'`
Connecting to ws://192.168.86.40:5000/ws...
Connected!
3.14 MB/s (25.09 Mbps)
3.33 MB/s (26.68 Mbps)
3.31 MB/s (26.52 Mbps)
2.58 MB/s (20.63 Mbps)
2.85 MB/s (22.79 Mbps)
3.38 MB/s (27.01 Mbps)
3.38 MB/s (27.03 Mbps)
3.16 MB/s (25.26 Mbps)
```

# Secure Websockets

## Initial

```
% cargo run -- "wss://192.168.86.40:5000/ws"
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.12s
     Running `target/debug/socket-bandwidth 'wss://192.168.86.40:5000/ws'`
Connecting to wss://192.168.86.40:5000/ws...
Connected!
0.70 MB/s (5.57 Mbps)
0.71 MB/s (5.64 Mbps)
0.70 MB/s (5.61 Mbps)
0.72 MB/s (5.77 Mbps)
0.70 MB/s (5.61 Mbps)
0.72 MB/s (5.80 Mbps)
0.71 MB/s (5.66 Mbps)
0.71 MB/s (5.70 Mbps)
0.71 MB/s (5.68 Mbps)
```

## Improvements

Increase mbedTLS output buffer — the sdkconfig has CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096. Increasing this to 16384 would allow larger TLS records per send.
Want me to bump the mbedTLS output buffer size? That's likely the biggest bottleneck — each send is being fragmented into 4KB TLS records.

## Final Build

```
Connected!
1.02 MB/s (8.16 Mbps)
1.00 MB/s (8.03 Mbps)
1.05 MB/s (8.37 Mbps)
1.04 MB/s (8.35 Mbps)
1.05 MB/s (8.44 Mbps)
1.05 MB/s (8.38 Mbps)
1.09 MB/s (8.73 Mbps)
1.04 MB/s (8.34 Mbps)
```

