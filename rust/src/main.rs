use std::env;
use std::net::TcpStream;
use std::time::Instant;
use native_tls::TlsConnector;
use tungstenite::{client_tls_with_config, Connector, Message};

const DEFAULT_ADDR: &str = "192.168.1.100:5000";

fn main() {
    let arg = env::args().nth(1).unwrap_or_else(|| DEFAULT_ADDR.to_string());
    let addr = if arg.starts_with("wss://") {
        arg.trim_start_matches("wss://").trim_end_matches("/ws").to_string()
    } else {
        arg.clone()
    };
    let url = if arg.starts_with("wss://") {
        arg
    } else {
        format!("wss://{arg}/ws")
    };

    // Accept self-signed certificates from the ESP32
    let tls_connector = TlsConnector::builder()
        .danger_accept_invalid_certs(true)
        .build()
        .expect("Failed to build TLS connector");

    println!("Connecting to {url}...");
    let tcp_stream = TcpStream::connect(&addr).expect("Failed to connect TCP");
    let (mut socket, _response) = client_tls_with_config(
        &url,
        tcp_stream,
        None,
        Some(Connector::NativeTls(tls_connector)),
    )
    .expect("Failed to connect");
    println!("Connected!");

    // Send trigger message to start the blast
    socket.send(Message::Text("start".into())).expect("Failed to send trigger");

    let mut bytes_this_second: u64 = 0;
    let mut interval_start = Instant::now();

    loop {
        match socket.read() {
            Ok(msg) => {
                let n = match &msg {
                    Message::Binary(data) => data.len(),
                    Message::Text(data) => data.len(),
                    Message::Close(_) => {
                        println!("Connection closed by server");
                        break;
                    }
                    _ => continue,
                };
                bytes_this_second += n as u64;
                let elapsed = interval_start.elapsed();
                if elapsed.as_secs_f64() >= 1.0 {
                    let mbps = (bytes_this_second as f64 * 8.0) / (elapsed.as_secs_f64() * 1_000_000.0);
                    let mb_s = bytes_this_second as f64 / (elapsed.as_secs_f64() * 1_000_000.0);
                    println!("{mb_s:.2} MB/s ({mbps:.2} Mbps)");
                    bytes_this_second = 0;
                    interval_start = Instant::now();
                }
            }
            Err(e) => {
                eprintln!("Read error: {e}");
                break;
            }
        }
    }
}
