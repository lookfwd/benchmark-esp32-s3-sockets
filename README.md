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


