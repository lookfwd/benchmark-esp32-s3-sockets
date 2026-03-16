use std::env;
use std::time::Instant;
use tungstenite::{connect, Message};

const DEFAULT_ADDR: &str = "192.168.1.100:5000";

fn main() {
    let arg = env::args().nth(1).unwrap_or_else(|| DEFAULT_ADDR.to_string());
    let url = if arg.starts_with("ws://") {
        arg
    } else {
        format!("ws://{arg}/ws")
    };

    println!("Connecting to {url}...");
    let (mut socket, _response) = connect(&url).expect("Failed to connect");
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
